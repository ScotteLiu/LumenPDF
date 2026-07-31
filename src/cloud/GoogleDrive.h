#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace lumen {

class GoogleAuth;

// Opening and saving PDFs in Google Drive.
//
// Scoped to `drive.file`, so this can only touch files the user picked or that
// LumenPDF created. Listing or searching someone's whole Drive is deliberately
// out of reach -- a PDF editor does not need it, and asking for it would mean
// asking users to trust a promise instead of a boundary Google enforces.
//
// Downloads land in a local cache and are opened from there. A document being
// edited in place over the network would mean every save is a network failure
// waiting to happen; instead the local copy is authoritative until the user
// saves, and saving is an explicit upload with a result they can see.
class GoogleDrive : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool available READ isAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)

    // The Drive file the open document came from, empty when it is a local
    // file. This is what decides whether "save to Drive" updates or creates.
    Q_PROPERTY(QString linkedFileId READ linkedFileId NOTIFY linkedFileChanged)
    Q_PROPERTY(QString linkedFileName READ linkedFileName NOTIFY linkedFileChanged)

public:
    GoogleDrive(GoogleAuth *auth, QNetworkAccessManager *network,
                QObject *parent = nullptr);

    bool isAvailable() const;
    bool isBusy() const { return m_busy; }
    qreal progress() const { return m_progress; }

    QString linkedFileId() const { return m_fileId; }
    QString linkedFileName() const { return m_fileName; }

    // Downloads a file by ID into the cache and reports the local path.
    // The ID comes from Google's own picker, which is the only way `drive.file`
    // grants access to something the app did not create.
    Q_INVOKABLE void openById(const QString &fileId);

    // Uploads the local file back to the Drive file it came from.
    Q_INVOKABLE void saveToLinkedFile(const QString &localPath);

    // Uploads as a new file. `name` is what it will be called in Drive.
    Q_INVOKABLE void uploadAsNew(const QString &localPath, const QString &name);

    // Forgets the link, so a subsequent save creates a new file instead of
    // overwriting the one this document came from.
    Q_INVOKABLE void unlink();

signals:
    void availabilityChanged();
    void busyChanged();
    void progressChanged();
    void linkedFileChanged();

    // A Drive file has been downloaded and is ready to open locally.
    void downloaded(const QString &localPath, const QString &fileName);

    void uploaded(const QString &fileId, const QString &fileName);
    void failed(const QString &reason);

private:
    void withToken(std::function<void(const QString &)> action);
    void setBusy(bool busy);
    void setProgress(qreal progress);
    static QString cacheDirectory();

    GoogleAuth *m_auth = nullptr;
    QNetworkAccessManager *m_network = nullptr;

    QString m_fileId;
    QString m_fileName;

    bool m_busy = false;
    qreal m_progress = 0.0;
};

} // namespace lumen
