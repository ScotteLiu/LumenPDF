#include "app/UpdateChecker.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcUpdate, "lumen.update")

namespace lumen {

namespace {

constexpr auto kReleaseApi =
    "https://api.github.com/repos/ScotteLiu/LumenPDF/releases/latest";

// The checksum file published alongside every release. Without it a download
// is not verified, and an unverified installer is not offered at all.
constexpr auto kChecksumAsset = "SHA256SUMS.txt";

// An installer is tens of megabytes. Anything an order of magnitude past that
// is not our release.
constexpr qint64 kMaxDownloadBytes = 300LL * 1024 * 1024;

QList<int> parseVersion(const QString &text)
{
    QString cleaned = text.trimmed();
    if (cleaned.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        cleaned.remove(0, 1);

    // Stop at the first pre-release or build suffix: 1.2.3-beta.1 sorts as
    // 1.2.3 here, which is close enough for "is there something newer" and far
    // simpler than implementing semver precedence.
    const qsizetype dash = cleaned.indexOf(QLatin1Char('-'));
    if (dash >= 0)
        cleaned.truncate(dash);

    QList<int> parts;
    const QStringList fields = cleaned.split(QLatin1Char('.'));
    for (const QString &field : fields) {
        bool ok = false;
        const int value = field.toInt(&ok);
        parts.append(ok ? value : 0);
    }
    return parts;
}

} // namespace

UpdateChecker::UpdateChecker(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_network(network)
{
}

UpdateChecker::~UpdateChecker()
{
    cancelDownload();
}

QString UpdateChecker::currentVersion() const
{
#ifdef LUMEN_VERSION
    return QStringLiteral(LUMEN_VERSION);
#else
    return QStringLiteral("0.0.0");
#endif
}

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
    const QList<int> left = parseVersion(a);
    const QList<int> right = parseVersion(b);
    const qsizetype fields = qMax(left.size(), right.size());

    for (qsizetype i = 0; i < fields; ++i) {
        const int l = i < left.size() ? left.at(i) : 0;
        const int r = i < right.size() ? right.at(i) : 0;
        if (l != r)
            return l > r ? 1 : -1;
    }
    return 0;
}

void UpdateChecker::check()
{
    if (m_checking || !m_network)
        return;

    m_checking = true;
    emit stateChanged();

    QNetworkRequest request { QUrl(QLatin1String(kReleaseApi)) };
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "LumenPDF");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        handleReleaseReply(reply);
        reply->deleteLater();
    });
}

void UpdateChecker::handleReleaseReply(QNetworkReply *reply)
{
    m_checking = false;

    if (reply->error() != QNetworkReply::NoError) {
        emit stateChanged();
        // A failed check is not worth interrupting anyone over; it is logged
        // and the UI simply shows nothing.
        qCInfo(lcUpdate) << "update check failed:" << reply->errorString();
        return;
    }

    const QJsonDocument json = QJsonDocument::fromJson(reply->readAll());
    if (!json.isObject()) {
        emit stateChanged();
        return;
    }

    const QJsonObject release = json.object();
    if (release.value(QStringLiteral("draft")).toBool()
        || release.value(QStringLiteral("prerelease")).toBool()) {
        emit stateChanged();
        return;
    }

    m_latestVersion = release.value(QStringLiteral("tag_name")).toString();
    m_releaseNotes = release.value(QStringLiteral("body")).toString();
    m_releasePage = QUrl(release.value(QStringLiteral("html_url")).toString());

    m_assetUrl.clear();
    m_checksumUrl.clear();
    m_assetName.clear();

    const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());

        if (name == QLatin1String(kChecksumAsset)) {
            m_checksumUrl = url;
        } else if (name.endsWith(QLatin1String("setup.exe"), Qt::CaseInsensitive)) {
            m_assetName = name;
            m_assetUrl = url;
        }
    }

    m_updateAvailable = !m_latestVersion.isEmpty()
                        && compareVersions(m_latestVersion, currentVersion()) > 0;

    qCInfo(lcUpdate) << "latest is" << m_latestVersion << "current is" << currentVersion()
                     << "update available:" << m_updateAvailable;

    emit stateChanged();
}

void UpdateChecker::download()
{
    if (m_downloading || !m_network)
        return;
    if (!m_updateAvailable || !m_assetUrl.isValid()) {
        emit failed(tr("There is no update to download."));
        return;
    }
    if (!m_checksumUrl.isValid()) {
        // Refusing here is the point. A release without published checksums
        // cannot be verified, and an unverifiable installer should not be
        // handed to anyone by an application that fetched it for them.
        emit failed(tr("This release did not publish checksums, so the download "
                       "cannot be verified. Download it from the release page instead."));
        return;
    }

    m_downloading = true;
    m_progress = 0.0;
    m_downloadedFile.clear();
    emit downloadChanged();

    fetchChecksums();
}

void UpdateChecker::fetchChecksums()
{
    QNetworkRequest request { m_checksumUrl };
    request.setRawHeader("User-Agent", "LumenPDF");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            m_downloading = false;
            emit downloadChanged();
            emit failed(tr("The checksum file could not be downloaded."));
            return;
        }

        // Lines look like: <64 hex digits>  <filename>
        static const QRegularExpression line(
            QStringLiteral("^([0-9a-fA-F]{64})\\s+\\*?(.+)$"));

        const QString text = QString::fromUtf8(reply->readAll());
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (const QString &entry : lines) {
            const auto match = line.match(entry.trimmed());
            if (match.hasMatch() && match.captured(2).trimmed() == m_assetName) {
                m_expectedSha256 = match.captured(1).toLower();
                break;
            }
        }

        if (m_expectedSha256.isEmpty()) {
            m_downloading = false;
            emit downloadChanged();
            emit failed(tr("No checksum was published for %1.").arg(m_assetName));
            return;
        }

        startAssetDownload();
    });
}

void UpdateChecker::startAssetDownload()
{
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (directory.isEmpty() || !QDir().mkpath(directory)) {
        m_downloading = false;
        emit downloadChanged();
        emit failed(tr("The downloads folder is not writable."));
        return;
    }

    // Written to a .part first so a cancelled or failed download never leaves
    // something that looks like a finished installer.
    const QString target = QDir(directory).filePath(m_assetName);
    m_sink = new QFile(target + QLatin1String(".part"), this);
    if (!m_sink->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete m_sink;
        m_sink = nullptr;
        m_downloading = false;
        emit downloadChanged();
        emit failed(tr("The download could not be written to disk."));
        return;
    }

    QNetworkRequest request { m_assetUrl };
    request.setRawHeader("User-Agent", "LumenPDF");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_network->get(request);

    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (!m_sink || !m_reply)
            return;
        if (m_sink->size() > kMaxDownloadBytes) {
            qCWarning(lcUpdate) << "download exceeded the size cap; aborting";
            m_reply->abort();
            return;
        }
        m_sink->write(m_reply->readAll());
    });

    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                m_progress = total > 0 ? static_cast<qreal>(received) / total : 0.0;
                emit downloadChanged();
            });

    connect(m_reply, &QNetworkReply::finished, this, &UpdateChecker::finishDownload);
}

void UpdateChecker::finishDownload()
{
    if (!m_reply || !m_sink)
        return;

    const bool ok = m_reply->error() == QNetworkReply::NoError;
    const QString errorString = m_reply->errorString();

    m_sink->write(m_reply->readAll());
    m_sink->flush();

    const QString partPath = m_sink->fileName();
    const QString finalPath = partPath.chopped(5); // ".part"

    m_reply->deleteLater();
    m_reply = nullptr;

    auto fail = [this, &partPath](const QString &reason) {
        m_sink->close();
        m_sink->remove();
        delete m_sink;
        m_sink = nullptr;
        m_downloading = false;
        m_progress = 0.0;
        emit downloadChanged();
        emit failed(reason);
    };

    if (!ok) {
        fail(tr("The download failed: %1").arg(errorString));
        return;
    }

    // Verify before the file is given a name that looks runnable.
    m_sink->seek(0);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(m_sink)) {
        fail(tr("The download could not be read back for verification."));
        return;
    }
    const QString actual = QString::fromLatin1(hash.result().toHex());

    if (actual != m_expectedSha256) {
        qCWarning(lcUpdate) << "checksum mismatch: expected" << m_expectedSha256
                            << "got" << actual;
        fail(tr("The downloaded file does not match its published checksum. "
                "It has been deleted."));
        return;
    }

    m_sink->close();
    QFile::remove(finalPath);
    const bool renamed = m_sink->rename(finalPath);
    delete m_sink;
    m_sink = nullptr;

    m_downloading = false;
    m_progress = 1.0;

    if (!renamed) {
        emit downloadChanged();
        emit failed(tr("The verified download could not be moved into place."));
        return;
    }

    m_downloadedFile = finalPath;
    qCInfo(lcUpdate) << "verified" << finalPath;
    emit downloadChanged();
    emit downloadFinished(finalPath);
}

void UpdateChecker::cancelDownload()
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_sink) {
        m_sink->close();
        m_sink->remove();
        delete m_sink;
        m_sink = nullptr;
    }
    if (m_downloading) {
        m_downloading = false;
        m_progress = 0.0;
        emit downloadChanged();
    }
}

bool UpdateChecker::runInstaller()
{
    if (m_downloadedFile.isEmpty() || !QFile::exists(m_downloadedFile))
        return false;

    if (!QProcess::startDetached(m_downloadedFile, {}))
        return false;

    emit quitRequested();
    return true;
}

void UpdateChecker::openReleasePage()
{
    if (m_releasePage.isValid())
        QDesktopServices::openUrl(m_releasePage);
}

} // namespace lumen
