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

// Reduces an asset name from the release feed to something safe to use as a
// filename, or returns empty to reject it.
//
// The name arrives as JSON from the network and ends up in QDir::filePath().
// That returns its argument unchanged when it is absolute, and otherwise just
// concatenates -- ".." segments included. So an asset called
// "..\..\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup\x-setup.exe"
// or "C:/Users/Public/x-setup.exe" would have the downloader create, truncate
// and rename a file wherever the feed said.
//
// Whitelist, not blacklist: the only names accepted are the ones this project's
// own packaging produces.
QString sanitiseAssetName(const QString &name)
{
    // Take the last path component under either separator, so a name that is
    // a path at all is reduced to its leaf before anything else looks at it.
    const QString leaf = QFileInfo(QString(name).replace(QLatin1Char('\\'),
                                                        QLatin1Char('/'))).fileName();

    if (leaf.isEmpty() || leaf.size() > 128)
        return {};

    // No control characters, including the NUL that truncates a Win32 path.
    for (const QChar c : leaf) {
        if (c.category() == QChar::Other_Control)
            return {};
    }

    static const QRegularExpression allowed(
        QStringLiteral("^LumenPDF-[0-9A-Za-z._-]+-win64-setup\\.exe$"));

    return allowed.match(leaf).hasMatch() ? leaf : QString();
}

// Release assets must come from GitHub over TLS. The URLs are read from the
// same JSON as everything else, so without this a compromised or spoofed feed
// could point the downloader anywhere.
bool isTrustedReleaseUrl(const QUrl &url)
{
    if (!url.isValid() || url.scheme() != QLatin1String("https"))
        return false;

    const QString host = url.host().toLower();
    return host == QLatin1String("github.com")
           || host == QLatin1String("objects.githubusercontent.com")
           || host == QLatin1String("release-assets.githubusercontent.com");
}

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

QString UpdateChecker::sanitiseAssetNameForTest(const QString &name)
{
    return sanitiseAssetName(name);
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

        if (!isTrustedReleaseUrl(url)) {
            qCWarning(lcUpdate) << "ignoring release asset with untrusted URL" << url;
            continue;
        }

        if (name == QLatin1String(kChecksumAsset)) {
            m_checksumUrl = url;
            continue;
        }

        if (!name.endsWith(QLatin1String("setup.exe"), Qt::CaseInsensitive))
            continue;

        // Sanitised before it is stored, not before it is used -- so there is
        // no window in which the raw name is reachable by anything.
        const QString safe = sanitiseAssetName(name);
        if (safe.isEmpty()) {
            qCWarning(lcUpdate) << "ignoring release asset with unacceptable name" << name;
            continue;
        }

        m_assetName = safe;
        m_assetUrl = url;
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
    //
    // m_assetName has already been through sanitiseAssetName, but the built
    // path is confirmed to be inside the downloads directory anyway -- this is
    // the step that decides where a downloaded executable lands, and one check
    // is not worth trusting on its own.
    const QString target = QDir(directory).filePath(m_assetName);
    const QString canonicalDir = QDir::cleanPath(QDir(directory).absolutePath());
    if (!QDir::cleanPath(QFileInfo(target).absolutePath()).compare(canonicalDir,
                                                                  Qt::CaseInsensitive) == 0) {
        m_downloading = false;
        emit downloadChanged();
        emit failed(tr("The download location is not valid."));
        return;
    }

    // ReadWrite, not WriteOnly: the file is hashed from this handle after the
    // transfer, and QCryptographicHash::addData(QIODevice*) returns false
    // immediately for a device that is not readable. Opening it write-only made
    // verification fail every single time, so no download ever completed.
    m_sink = new QFile(target + QLatin1String(".part"), this);
    if (!m_sink->open(QIODevice::ReadWrite | QIODevice::Truncate)) {
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

    m_sink->close();
    delete m_sink;
    m_sink = nullptr;

    QString reason;
    QString promoted;
    if (!verifyAndPromote(partPath, m_expectedSha256, &promoted, &reason)) {
        m_downloading = false;
        m_progress = 0.0;
        emit downloadChanged();
        emit failed(reason);
        return;
    }

    m_downloading = false;
    m_progress = 1.0;
    m_downloadedFile = promoted;

    qCInfo(lcUpdate) << "verified" << promoted;
    emit downloadChanged();
    emit downloadFinished(promoted);
}

bool UpdateChecker::verifyAndPromote(const QString &partPath,
                                     const QString &expectedHex,
                                     QString *outPath,
                                     QString *outError)
{
    // Split out of finishDownload so the branch that decides whether to hand
    // somebody a runnable .exe can be tested without a network. It was
    // previously unreachable -- the sink was opened WriteOnly, so hashing
    // always failed and no download ever got this far.
    QFile part(partPath);
    if (!part.open(QIODevice::ReadOnly)) {
        if (outError)
            *outError = tr("The download could not be read back for verification.");
        part.remove();
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    const bool hashed = hash.addData(&part);
    part.close();

    if (!hashed) {
        if (outError)
            *outError = tr("The download could not be read back for verification.");
        part.remove();
        return false;
    }

    const QString actual = QString::fromLatin1(hash.result().toHex()).toLower();
    if (actual != expectedHex.trimmed().toLower()) {
        qCWarning(lcUpdate) << "checksum mismatch: expected" << expectedHex
                            << "got" << actual;
        part.remove();
        if (outError)
            *outError = tr("The downloaded file does not match its published "
                           "checksum. It has been deleted.");
        return false;
    }

    // Only now does it get a name that looks runnable.
    //
    // An existing file is stepped around rather than overwritten: this path is
    // derived from a name that came off the network, and silently replacing
    // whatever is already there is not a downloader's decision to make.
    QString finalPath = partPath.chopped(5); // ".part"
    if (QFile::exists(finalPath)) {
        const QFileInfo info(finalPath);
        for (int n = 1; n < 100; ++n) {
            const QString candidate = info.dir().filePath(
                QStringLiteral("%1 (%2).%3").arg(info.completeBaseName())
                                            .arg(n)
                                            .arg(info.suffix()));
            if (!QFile::exists(candidate)) {
                finalPath = candidate;
                break;
            }
        }
    }

    if (!part.rename(finalPath)) {
        part.remove();
        if (outError)
            *outError = tr("The verified download could not be moved into place.");
        return false;
    }

    if (outPath)
        *outPath = finalPath;
    return true;
}

void UpdateChecker::cancelDownload()
{
    // Detach before aborting. QNetworkReply::abort() emits finished()
    // synchronously, so finishDownload() would otherwise run to completion
    // inside abort() -- nulling m_reply underneath this function, and emitting
    // failed("Operation canceled") so a deliberate cancel surfaces to the user
    // as a download error.
    if (QNetworkReply *reply = std::exchange(m_reply, nullptr)) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (QFile *sink = std::exchange(m_sink, nullptr)) {
        sink->close();
        sink->remove();
        delete sink;
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
