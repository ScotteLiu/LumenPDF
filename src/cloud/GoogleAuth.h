#pragma once

#include <QObject>
#include <QString>

class QTcpServer;
class QTimer;
class QNetworkAccessManager;

namespace lumen {

// Google sign-in for a desktop application.
//
// Uses the authorization-code flow with PKCE and a loopback redirect, which is
// what Google specifies for installed apps. Two consequences worth stating:
//
//   * The "client secret" is not treated as a secret. It cannot be one in a
//     binary anyone can open, and Google's own guidance says so. Security comes
//     from the PKCE verifier, which is generated fresh for every sign-in and
//     never leaves the process.
//
//   * Sign-in happens in the user's real browser, not an embedded one. Asking
//     somebody to type their Google password into a window we control would be
//     teaching them the exact habit that phishing relies on.
//
// Credentials come from the environment or from a config file the user writes;
// nothing is compiled in. See docs/GOOGLE-DRIVE-SETUP.md.
class GoogleAuth : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool configured READ isConfigured NOTIFY configuredChanged)
    Q_PROPERTY(bool signedIn READ isSignedIn NOTIFY signedInChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(QString account READ account NOTIFY signedInChanged)

public:
    explicit GoogleAuth(QNetworkAccessManager *network, QObject *parent = nullptr);
    ~GoogleAuth() override;

    // True when a client ID is available. Without one the UI hides Drive
    // entirely rather than offering an action that can only fail.
    bool isConfigured() const { return !m_clientId.isEmpty(); }

    bool isSignedIn() const { return !m_refreshToken.isEmpty(); }
    bool isBusy() const { return m_busy; }
    QString account() const { return m_account; }

    // A valid access token, refreshing it first if it has expired. Empty when
    // not signed in. Asynchronous: connect to tokenReady.
    void requestAccessToken();

    Q_INVOKABLE void signIn();
    Q_INVOKABLE void signOut();

    // Abandons a sign-in in progress: closes the loopback listener and clears
    // the busy flag. Without a way out, closing the consent tab left the port
    // bound and busy stuck true for the rest of the process.
    Q_INVOKABLE void cancel();

signals:
    void configuredChanged();
    void signedInChanged();
    void busyChanged();

    void tokenReady(const QString &accessToken);
    void failed(const QString &reason);

private:
    void loadCredentials();
    void loadRefreshToken();
    void saveRefreshToken();
    void clearRefreshToken();

    void startLoopbackServer();

    // Closes and destroys the listener and its timeout, if either exists.
    void stopLoopbackServer();
    void exchangeCode(const QString &code, quint16 port);
    void refreshAccessToken();
    void fetchAccountEmail();

    void setBusy(bool busy);

    static QString configDirectory();

    QNetworkAccessManager *m_network = nullptr;

    QString m_clientId;
    QString m_clientSecret;

    QString m_refreshToken;
    QString m_accessToken;
    qint64 m_accessTokenExpiry = 0;   // ms since epoch
    QString m_account;

    // Regenerated for every sign-in; never stored.
    QString m_codeVerifier;

    // Correlation value for the redirect. PKCE is what protects the token;
    // this is what stops an unrelated request to 127.0.0.1 from aborting a
    // sign-in that is in progress.
    QString m_state;

    QTcpServer *m_loopback = nullptr;
    QTimer *m_authTimeout = nullptr;
    bool m_busy = false;
};

} // namespace lumen
