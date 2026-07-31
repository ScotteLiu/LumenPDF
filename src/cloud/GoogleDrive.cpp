#include "cloud/GoogleDrive.h"

#include "cloud/GoogleAuth.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(lcDrive, "lumen.drive")

namespace lumen {

namespace {

constexpr auto kFilesEndpoint = "https://www.googleapis.com/drive/v3/files";
constexpr auto kUploadEndpoint = "https://www.googleapis.com/upload/drive/v3/files";

// Drive will happily hand over a multi-gigabyte file. Refusing early is kinder
// than filling someone's disk and then failing to render it.
constexpr qint64 kMaxDownloadBytes = 512LL * 1024 * 1024;

} // namespace

GoogleDrive::GoogleDrive(GoogleAuth *auth, QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_auth(auth)
    , m_network(network)
{
    if (m_auth) {
        connect(m_auth, &GoogleAuth::signedInChanged,
                this, &GoogleDrive::availabilityChanged);
        connect(m_auth, &GoogleAuth::configuredChanged,
                this, &GoogleDrive::availabilityChanged);
    }
}

bool GoogleDrive::isAvailable() const
{
    return m_auth && m_auth->isConfigured();
}

QString GoogleDrive::cacheDirectory()
{
    const QString path = QDir(QStandardPaths::writableLocation(
        QStandardPaths::CacheLocation)).filePath(QStringLiteral("drive"));
    QDir().mkpath(path);
    return path;
}

void GoogleDrive::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void GoogleDrive::setProgress(qreal progress)
{
    m_progress = qBound(0.0, progress, 1.0);
    emit progressChanged();
}

// Every Drive call needs a fresh access token, and getting one is asynchronous.
// This wraps that so callers read as a straight sequence rather than a chain of
// one-shot connections.
void GoogleDrive::withToken(std::function<void(const QString &)> action)
{
    if (!m_auth || !m_auth->isSignedIn()) {
        emit failed(tr("Sign in to Google Drive first."));
        return;
    }

    auto *context = new QObject(this);

    connect(m_auth, &GoogleAuth::tokenReady, context,
            [this, context, action](const QString &token) {
                context->deleteLater();
                action(token);
            });

    connect(m_auth, &GoogleAuth::failed, context,
            [this, context](const QString &reason) {
                context->deleteLater();
                setBusy(false);
                emit failed(reason);
            });

    m_auth->requestAccessToken();
}

void GoogleDrive::openById(const QString &fileId)
{
    if (fileId.isEmpty())
        return;

    setBusy(true);
    setProgress(0.0);

    withToken([this, fileId](const QString &token) {
        // Metadata first: the name is needed for the local copy, and the size
        // is what makes it possible to refuse before downloading.
        QUrl metaUrl(QString::fromLatin1(kFilesEndpoint) + "/" + fileId);
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id,name,size,mimeType"));
        metaUrl.setQuery(query);

        QNetworkRequest request(metaUrl);
        request.setRawHeader("Authorization", ("Bearer " + token).toUtf8());

        QNetworkReply *reply = m_network->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, fileId, token] {
            reply->deleteLater();

            const QJsonObject meta = QJsonDocument::fromJson(reply->readAll()).object();
            if (reply->error() != QNetworkReply::NoError) {
                setBusy(false);
                emit failed(tr("Could not read the file from Drive: %1")
                                .arg(reply->errorString()));
                return;
            }

            const QString name = meta.value(QStringLiteral("name")).toString();
            const qint64 size = meta.value(QStringLiteral("size")).toString().toLongLong();

            if (size > kMaxDownloadBytes) {
                setBusy(false);
                emit failed(tr("%1 is too large to open (%2 MB).")
                                .arg(name).arg(size / (1024 * 1024)));
                return;
            }

            QUrl mediaUrl(QString::fromLatin1(kFilesEndpoint) + "/" + fileId);
            QUrlQuery mediaQuery;
            mediaQuery.addQueryItem(QStringLiteral("alt"), QStringLiteral("media"));
            mediaUrl.setQuery(mediaQuery);

            QNetworkRequest mediaRequest(mediaUrl);
            mediaRequest.setRawHeader("Authorization", ("Bearer " + token).toUtf8());

            QNetworkReply *download = m_network->get(mediaRequest);

            connect(download, &QNetworkReply::downloadProgress, this,
                    [this](qint64 received, qint64 total) {
                        if (total > 0)
                            setProgress(qreal(received) / total);
                    });

            connect(download, &QNetworkReply::finished, this,
                    [this, download, fileId, name] {
                        download->deleteLater();
                        setBusy(false);

                        if (download->error() != QNetworkReply::NoError) {
                            emit failed(tr("Download failed: %1")
                                            .arg(download->errorString()));
                            return;
                        }

                        // Prefixed with the file ID so two Drive files with the
                        // same name cannot overwrite each other in the cache.
                        const QString safeName = QFileInfo(name).fileName();
                        const QString localPath = QDir(cacheDirectory())
                            .filePath(fileId.left(12) + "-" + safeName);

                        QFile out(localPath);
                        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                            emit failed(tr("Could not write the downloaded file."));
                            return;
                        }
                        out.write(download->readAll());
                        out.close();

                        m_fileId = fileId;
                        m_fileName = name;
                        emit linkedFileChanged();

                        qCInfo(lcDrive) << "downloaded" << name << "to" << localPath;
                        emit downloaded(localPath, name);
                    });
        });
    });
}

void GoogleDrive::saveToLinkedFile(const QString &localPath)
{
    if (m_fileId.isEmpty()) {
        emit failed(tr("This document did not come from Drive."));
        return;
    }
    if (!QFile::exists(localPath)) {
        emit failed(tr("The local file is missing."));
        return;
    }

    setBusy(true);
    setProgress(0.0);

    withToken([this, localPath](const QString &token) {
        auto *file = new QFile(localPath);
        if (!file->open(QIODevice::ReadOnly)) {
            delete file;
            setBusy(false);
            emit failed(tr("Could not read the local file."));
            return;
        }

        QUrl url(QString::fromLatin1(kUploadEndpoint) + "/" + m_fileId);
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("uploadType"), QStringLiteral("media"));
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/pdf"));

        // PATCH updates the existing file, keeping its ID, sharing and history.
        // A create-and-delete would break every link anyone had to it.
        QNetworkReply *reply = m_network->sendCustomRequest(request, "PATCH", file);
        file->setParent(reply);

        connect(reply, &QNetworkReply::uploadProgress, this,
                [this](qint64 sent, qint64 total) {
                    if (total > 0)
                        setProgress(qreal(sent) / total);
                });

        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            reply->deleteLater();
            setBusy(false);

            if (reply->error() != QNetworkReply::NoError) {
                emit failed(tr("Upload failed: %1").arg(reply->errorString()));
                return;
            }

            qCInfo(lcDrive) << "updated" << m_fileName << "in Drive";
            emit uploaded(m_fileId, m_fileName);
        });
    });
}

void GoogleDrive::uploadAsNew(const QString &localPath, const QString &name)
{
    if (!QFile::exists(localPath)) {
        emit failed(tr("The local file is missing."));
        return;
    }

    setBusy(true);
    setProgress(0.0);

    withToken([this, localPath, name](const QString &token) {
        auto *file = new QFile(localPath);
        if (!file->open(QIODevice::ReadOnly)) {
            delete file;
            setBusy(false);
            emit failed(tr("Could not read the local file."));
            return;
        }

        // Multipart: metadata and content in one request, so a new file cannot
        // end up in Drive with the wrong name because the second call failed.
        auto *multipart = new QHttpMultiPart(QHttpMultiPart::RelatedType);

        QJsonObject metadata;
        metadata[QStringLiteral("name")] =
            name.isEmpty() ? QFileInfo(localPath).fileName() : name;

        QHttpPart metaPart;
        metaPart.setHeader(QNetworkRequest::ContentTypeHeader,
                           QStringLiteral("application/json; charset=UTF-8"));
        metaPart.setBody(QJsonDocument(metadata).toJson(QJsonDocument::Compact));
        multipart->append(metaPart);

        QHttpPart filePart;
        filePart.setHeader(QNetworkRequest::ContentTypeHeader,
                           QStringLiteral("application/pdf"));
        filePart.setBodyDevice(file);
        file->setParent(multipart);
        multipart->append(filePart);

        QUrl url(QString::fromLatin1(kUploadEndpoint));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("uploadType"), QStringLiteral("multipart"));
        query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id,name"));
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setRawHeader("Authorization", ("Bearer " + token).toUtf8());

        QNetworkReply *reply = m_network->post(request, multipart);
        multipart->setParent(reply);

        connect(reply, &QNetworkReply::uploadProgress, this,
                [this](qint64 sent, qint64 total) {
                    if (total > 0)
                        setProgress(qreal(sent) / total);
                });

        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            reply->deleteLater();
            setBusy(false);

            const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
            if (reply->error() != QNetworkReply::NoError) {
                emit failed(tr("Upload failed: %1").arg(reply->errorString()));
                return;
            }

            // The new file becomes the linked one, so the next save updates it
            // rather than making a second copy.
            m_fileId = json.value(QStringLiteral("id")).toString();
            m_fileName = json.value(QStringLiteral("name")).toString();
            emit linkedFileChanged();

            qCInfo(lcDrive) << "uploaded" << m_fileName << "as" << m_fileId;
            emit uploaded(m_fileId, m_fileName);
        });
    });
}

void GoogleDrive::unlink()
{
    if (m_fileId.isEmpty())
        return;

    m_fileId.clear();
    m_fileName.clear();
    emit linkedFileChanged();
}

} // namespace lumen
