#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

namespace lumen {

// Checks GitHub for a newer release, and can fetch it.
//
// What this deliberately does not do is update itself. Downloading a binary and
// running it unattended is the same shape as the attack it would be defending
// against, and a signature on the installer only helps someone who checks it.
// So: the check is automatic, the download is a button, the checksum is
// verified against the one published with the release, and starting the
// installer is a second, separate button that says what it is about to run.
class UpdateChecker : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY stateChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY stateChanged)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY stateChanged)
    Q_PROPERTY(QUrl releasePage READ releasePage NOTIFY stateChanged)
    Q_PROPERTY(bool checking READ isChecking NOTIFY stateChanged)
    Q_PROPERTY(bool downloading READ isDownloading NOTIFY downloadChanged)
    Q_PROPERTY(qreal downloadProgress READ downloadProgress NOTIFY downloadChanged)
    Q_PROPERTY(QString downloadedFile READ downloadedFile NOTIFY downloadChanged)

public:
    explicit UpdateChecker(QNetworkAccessManager *network, QObject *parent = nullptr);
    ~UpdateChecker() override;

    QString currentVersion() const;
    QString latestVersion() const { return m_latestVersion; }
    bool updateAvailable() const { return m_updateAvailable; }
    QString releaseNotes() const { return m_releaseNotes; }
    QUrl releasePage() const { return m_releasePage; }
    bool isChecking() const { return m_checking; }
    bool isDownloading() const { return m_downloading; }
    qreal downloadProgress() const { return m_progress; }
    QString downloadedFile() const { return m_downloadedFile; }

    Q_INVOKABLE void check();
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancelDownload();

    // Starts the verified installer and asks the application to quit, because
    // an installer cannot replace files a running process has open.
    Q_INVOKABLE bool runInstaller();

    Q_INVOKABLE void openReleasePage();

    // Exposed for tests and for anyone who wants to sanity-check the ordering.
    // Returns >0 when `a` is newer than `b`, <0 when older, 0 when equal.
    Q_INVOKABLE static int compareVersions(const QString &a, const QString &b);

signals:
    void stateChanged();
    void downloadChanged();
    void failed(const QString &reason);
    void downloadFinished(const QString &path);
    void quitRequested();

private:
    void handleReleaseReply(QNetworkReply *reply);
    void fetchChecksums();
    void startAssetDownload();
    void finishDownload();

    QNetworkAccessManager *m_network = nullptr;

    QString m_latestVersion;
    QString m_releaseNotes;
    QUrl m_releasePage;
    QUrl m_assetUrl;
    QUrl m_checksumUrl;
    QString m_assetName;
    QString m_expectedSha256;

    bool m_updateAvailable = false;
    bool m_checking = false;
    bool m_downloading = false;
    qreal m_progress = 0.0;
    QString m_downloadedFile;

    QNetworkReply *m_reply = nullptr;
    QFile *m_sink = nullptr;
};

} // namespace lumen
