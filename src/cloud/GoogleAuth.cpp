#include "cloud/GoogleAuth.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTimer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dpapi.h>
#endif

Q_LOGGING_CATEGORY(lcAuth, "lumen.google")

namespace lumen {

namespace {

constexpr auto kAuthEndpoint = "https://accounts.google.com/o/oauth2/v2/auth";
constexpr auto kTokenEndpoint = "https://oauth2.googleapis.com/token";
constexpr auto kUserInfoEndpoint = "https://www.googleapis.com/oauth2/v3/userinfo";

// drive.file only: access is limited to files the user explicitly opens through
// Google's picker, plus files this app created. A PDF editor has no business
// being able to read the rest of someone's Drive, and the narrow scope is also
// what keeps Google's verification straightforward.
constexpr auto kScope = "https://www.googleapis.com/auth/drive.file "
                        "https://www.googleapis.com/auth/userinfo.email";

QString randomUrlSafe(int bytes)
{
    QByteArray raw(bytes, Qt::Uninitialized);
    QRandomGenerator::system()->generate(raw.begin(), raw.end());
    return QString::fromLatin1(raw.toBase64(QByteArray::Base64UrlEncoding
                                            | QByteArray::OmitTrailingEquals));
}

QString s256Challenge(const QString &verifier)
{
    const QByteArray digest =
        QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toBase64(QByteArray::Base64UrlEncoding
                                               | QByteArray::OmitTrailingEquals));
}

// The page the browser lands on after consent. It is the last thing the user
// sees of the flow, so it says what happened rather than showing a blank tab.
QByteArray completionPage(bool ok)
{
    const QByteArray body = ok
        ? "<h1>Signed in</h1><p>You can close this tab and return to LumenPDF.</p>"
        : "<h1>Sign-in failed</h1><p>Return to LumenPDF and try again.</p>";

    QByteArray html =
        "<!doctype html><meta charset=utf-8><title>LumenPDF</title>"
        "<style>body{font-family:system-ui,sans-serif;margin:18vh auto;max-width:32rem;"
        "text-align:center;color:#1d1d1f}h1{font-weight:600}p{color:#6e6e73}</style>";
    html += body;

    QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n";
    response += "Content-Length: " + QByteArray::number(html.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += html;
    return response;
}

#ifdef Q_OS_WIN
// The refresh token is a long-lived credential to someone's Drive. On disk it
// is encrypted against the Windows account, so another user on the machine
// cannot read it and copying the file elsewhere makes it useless.
QByteArray protect(const QByteArray &plain, bool encrypt)
{
    DATA_BLOB in {};
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()));
    in.cbData = DWORD(plain.size());

    DATA_BLOB out {};
    const BOOL ok = encrypt
        ? CryptProtectData(&in, L"LumenPDF", nullptr, nullptr, nullptr, 0, &out)
        : CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out);

    if (!ok)
        return {};

    const QByteArray result(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    return result;
}
#endif

} // namespace

GoogleAuth::GoogleAuth(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_network(network)
{
    loadCredentials();
    if (isConfigured())
        loadRefreshToken();
}

GoogleAuth::~GoogleAuth() = default;

QString GoogleAuth::configDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

void GoogleAuth::loadCredentials()
{
    // Environment first, so a developer can point at a test project without
    // touching the file a user configured.
    m_clientId = qEnvironmentVariable("LUMEN_GOOGLE_CLIENT_ID");
    m_clientSecret = qEnvironmentVariable("LUMEN_GOOGLE_CLIENT_SECRET");

    if (m_clientId.isEmpty()) {
        QFile file(QDir(configDirectory()).filePath(QStringLiteral("google.json")));
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonObject json =
                QJsonDocument::fromJson(file.readAll()).object();
            m_clientId = json.value(QStringLiteral("client_id")).toString();
            m_clientSecret = json.value(QStringLiteral("client_secret")).toString();
        }
    }

    if (isConfigured())
        qCInfo(lcAuth) << "Google credentials found";
    else
        qCInfo(lcAuth) << "no Google credentials -- Drive features stay hidden";

    emit configuredChanged();
}

void GoogleAuth::loadRefreshToken()
{
    QFile file(QDir(configDirectory()).filePath(QStringLiteral("google-token.dat")));
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QByteArray stored = file.readAll();
#ifdef Q_OS_WIN
    const QByteArray plain = protect(stored, false);
#else
    const QByteArray plain = stored;
#endif

    if (plain.isEmpty())
        return;

    const QJsonObject json = QJsonDocument::fromJson(plain).object();
    m_refreshToken = json.value(QStringLiteral("refresh_token")).toString();
    m_account = json.value(QStringLiteral("account")).toString();

    if (isSignedIn()) {
        qCInfo(lcAuth) << "restored sign-in for" << m_account;
        emit signedInChanged();
    }
}

void GoogleAuth::saveRefreshToken()
{
    QDir().mkpath(configDirectory());

    QJsonObject json;
    json[QStringLiteral("refresh_token")] = m_refreshToken;
    json[QStringLiteral("account")] = m_account;

    const QByteArray plain = QJsonDocument(json).toJson(QJsonDocument::Compact);
#ifdef Q_OS_WIN
    const QByteArray stored = protect(plain, true);
    if (stored.isEmpty()) {
        qCWarning(lcAuth) << "could not encrypt the token; refusing to store it in the clear";
        return;
    }
#else
    // Nothing here is written unencrypted: a plaintext refresh token on disk is
    // a standing credential to somebody's Drive.
    qCWarning(lcAuth) << "no platform keystore; the session will not persist";
    return;
#endif

    QFile file(QDir(configDirectory()).filePath(QStringLiteral("google-token.dat")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(stored);
}

void GoogleAuth::clearRefreshToken()
{
    QFile::remove(QDir(configDirectory()).filePath(QStringLiteral("google-token.dat")));
}

void GoogleAuth::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void GoogleAuth::signIn()
{
    if (!isConfigured()) {
        emit failed(tr("Google Drive is not set up. See docs/GOOGLE-DRIVE-SETUP.md."));
        return;
    }
    if (m_busy)
        return;

    setBusy(true);
    m_codeVerifier = randomUrlSafe(48);

    // A correlation value the redirect must echo back. PKCE already makes a
    // stolen code useless, so this is not what stops a token being taken --
    // what it stops is any local process, or any web page that can reach
    // 127.0.0.1, sending a bogus code or an error to a sign-in it did not
    // start. Required by RFC 8252 and the OAuth 2.0 Security BCP regardless.
    m_state = randomUrlSafe(24);

    startLoopbackServer();
}

void GoogleAuth::cancel()
{
    if (!m_busy)
        return;

    stopLoopbackServer();
    m_state.clear();
    setBusy(false);
    emit failed(tr("Sign-in was cancelled."));
}

void GoogleAuth::stopLoopbackServer()
{
    if (m_authTimeout) {
        m_authTimeout->stop();
        m_authTimeout->deleteLater();
        m_authTimeout = nullptr;
    }
    if (m_loopback) {
        m_loopback->close();
        m_loopback->deleteLater();
        m_loopback = nullptr;
    }
}

void GoogleAuth::startLoopbackServer()
{
    delete m_loopback;
    m_loopback = new QTcpServer(this);

    // Port 0: the OS picks a free one. Loopback only, so nothing outside this
    // machine can reach it, and it exists for the seconds the flow takes.
    if (!m_loopback->listen(QHostAddress::LocalHost, 0)) {
        setBusy(false);
        emit failed(tr("Could not open a local port for sign-in."));
        return;
    }

    const quint16 port = m_loopback->serverPort();

    connect(m_loopback, &QTcpServer::newConnection, this, [this, port] {
        QTcpSocket *socket = m_loopback->nextPendingConnection();
        if (!socket)
            return;

        connect(socket, &QTcpSocket::readyRead, this, [this, socket, port] {
            const QByteArray request = socket->readAll();
            const int start = request.indexOf(' ') + 1;
            const int end = request.indexOf(' ', start);
            const QString target = QString::fromLatin1(request.mid(start, end - start));

            const QUrlQuery query(QUrl(target).query());
            const QString code = query.queryItemValue(QStringLiteral("code"));
            const QString error = query.queryItemValue(QStringLiteral("error"));
            const QString state = query.queryItemValue(QStringLiteral("state"));

            // Anything that does not echo the state this sign-in issued is not
            // the redirect we are waiting for. Answered and dropped *without*
            // closing the listener -- closing it is what would let a stray
            // request from any local process kill the real sign-in, since the
            // genuine redirect would then hit a refused connection.
            if (m_state.isEmpty() || state != m_state) {
                qCWarning(lcAuth) << "ignoring loopback request with unexpected state";
                socket->write(completionPage(false));
                socket->disconnectFromHost();
                return;
            }

            socket->write(completionPage(!code.isEmpty()));
            socket->disconnectFromHost();

            m_state.clear();
            stopLoopbackServer();

            if (!code.isEmpty()) {
                exchangeCode(code, port);
            } else {
                setBusy(false);
                emit failed(error.isEmpty()
                            ? tr("Sign-in was cancelled.")
                            : tr("Google refused the sign-in: %1").arg(error));
            }
        });
    });

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), m_clientId);
    query.addQueryItem(QStringLiteral("redirect_uri"),
                       QStringLiteral("http://127.0.0.1:%1").arg(port));
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("scope"), QString::fromLatin1(kScope));
    query.addQueryItem(QStringLiteral("code_challenge"), s256Challenge(m_codeVerifier));
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("state"), m_state);
    // Without these Google returns no refresh token on repeat sign-ins, and the
    // user would have to authorise again every hour.
    query.addQueryItem(QStringLiteral("access_type"), QStringLiteral("offline"));
    query.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));

    QUrl url(QString::fromLatin1(kAuthEndpoint));
    url.setQuery(query);

    // Nothing else ever ends a sign-in that the user abandons. Close the
    // consent tab without this and the port stays bound and m_busy stays true
    // for the rest of the process, so signIn() early-returns forever -- Drive
    // is dead until restart, with no error and nothing to click.
    const int timeoutMs = qEnvironmentVariableIsSet("LUMEN_OAUTH_TIMEOUT_MS")
                              ? qEnvironmentVariableIntValue("LUMEN_OAUTH_TIMEOUT_MS")
                              : 3 * 60 * 1000;

    m_authTimeout = new QTimer(this);
    m_authTimeout->setSingleShot(true);
    connect(m_authTimeout, &QTimer::timeout, this, [this] {
        qCInfo(lcAuth) << "sign-in timed out; releasing the loopback listener";
        stopLoopbackServer();
        m_state.clear();
        setBusy(false);
        emit failed(tr("Sign-in timed out. Try again."));
    });
    m_authTimeout->start(timeoutMs);

    // The user's own browser, where they can see the address bar and Google's
    // real certificate.
    QDesktopServices::openUrl(url);
}

void GoogleAuth::exchangeCode(const QString &code, quint16 port)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), m_clientId);
    if (!m_clientSecret.isEmpty())
        form.addQueryItem(QStringLiteral("client_secret"), m_clientSecret);
    form.addQueryItem(QStringLiteral("code"), code);
    form.addQueryItem(QStringLiteral("code_verifier"), m_codeVerifier);
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    form.addQueryItem(QStringLiteral("redirect_uri"),
                      QStringLiteral("http://127.0.0.1:%1").arg(port));

    QNetworkRequest request { QUrl(QString::fromLatin1(kTokenEndpoint)) };
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    QNetworkReply *reply = m_network->post(
        request, form.toString(QUrl::FullyEncoded).toUtf8());

    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_codeVerifier.clear();

        const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();

        if (reply->error() != QNetworkReply::NoError
            || !json.contains(QStringLiteral("access_token"))) {
            setBusy(false);
            emit failed(tr("Google rejected the sign-in: %1")
                            .arg(json.value(QStringLiteral("error_description"))
                                     .toString(reply->errorString())));
            return;
        }

        m_accessToken = json.value(QStringLiteral("access_token")).toString();
        m_accessTokenExpiry = QDateTime::currentMSecsSinceEpoch()
            + json.value(QStringLiteral("expires_in")).toInt(3600) * 1000LL;

        const QString refresh = json.value(QStringLiteral("refresh_token")).toString();
        if (!refresh.isEmpty())
            m_refreshToken = refresh;

        fetchAccountEmail();
    });
}

void GoogleAuth::fetchAccountEmail()
{
    QNetworkRequest request { QUrl(QString::fromLatin1(kUserInfoEndpoint)) };
    request.setRawHeader("Authorization", ("Bearer " + m_accessToken).toUtf8());

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();

        const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        m_account = json.value(QStringLiteral("email")).toString();

        saveRefreshToken();
        setBusy(false);

        qCInfo(lcAuth) << "signed in as" << m_account;
        emit signedInChanged();
        emit tokenReady(m_accessToken);
    });
}

void GoogleAuth::requestAccessToken()
{
    if (!isSignedIn()) {
        emit failed(tr("Not signed in to Google Drive."));
        return;
    }

    // A minute of slack: a token that expires mid-upload is a failure the user
    // cannot understand or retry usefully.
    if (!m_accessToken.isEmpty()
        && QDateTime::currentMSecsSinceEpoch() < m_accessTokenExpiry - 60'000) {
        emit tokenReady(m_accessToken);
        return;
    }

    refreshAccessToken();
}

void GoogleAuth::refreshAccessToken()
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), m_clientId);
    if (!m_clientSecret.isEmpty())
        form.addQueryItem(QStringLiteral("client_secret"), m_clientSecret);
    form.addQueryItem(QStringLiteral("refresh_token"), m_refreshToken);
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));

    QNetworkRequest request { QUrl(QString::fromLatin1(kTokenEndpoint)) };
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    setBusy(true);
    QNetworkReply *reply = m_network->post(
        request, form.toString(QUrl::FullyEncoded).toUtf8());

    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        setBusy(false);

        const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();

        if (!json.contains(QStringLiteral("access_token"))) {
            // A refresh token is revoked when the user withdraws access, so
            // this is a sign-out rather than a transient error.
            qCWarning(lcAuth) << "refresh failed; signing out";
            signOut();
            emit failed(tr("Google sign-in expired. Sign in again."));
            return;
        }

        m_accessToken = json.value(QStringLiteral("access_token")).toString();
        m_accessTokenExpiry = QDateTime::currentMSecsSinceEpoch()
            + json.value(QStringLiteral("expires_in")).toInt(3600) * 1000LL;

        emit tokenReady(m_accessToken);
    });
}

void GoogleAuth::signOut()
{
    // Signing out during a sign-in has to release it too, or m_busy stays true
    // and the next signIn() early-returns.
    stopLoopbackServer();
    m_state.clear();
    m_codeVerifier.clear();
    setBusy(false);

    m_refreshToken.clear();
    m_accessToken.clear();
    m_accessTokenExpiry = 0;
    m_account.clear();
    clearRefreshToken();

    emit signedInChanged();
}

} // namespace lumen
