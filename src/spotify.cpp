#include "spotify.h"
#include "util.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTcpServer>
#include <QTcpSocket>
#include <QProcess>
#include <QTimer>
#include <QUrlQuery>

static const char *scopes =
  "streaming user-read-email user-read-private user-read-playback-state "
  "user-modify-playback-state playlist-read-private playlist-read-collaborative "
  "user-library-read user-read-recently-played";

static QString redirectUri() {
  return QString("http://127.0.0.1:%1/callback").arg(Spotify::callbackPort);
}

Spotify::Spotify(QNetworkAccessManager *net, QObject *parent)
  : QObject(parent), m_net(net), m_path(util::dataDir() + "/spotify.json") {
  QJsonObject saved = util::readJson(m_path);
  m_clientId = saved.value("client_id").toString();
  m_refreshToken = saved.value("refresh_token").toString();
}

void Spotify::save() {
  util::writeJson(m_path, {{"client_id", m_clientId}, {"refresh_token", m_refreshToken}}, true);
}

void Spotify::setClientId(const QString &id) {
  QString clean = id.trimmed();
  if (clean == m_clientId) return;
  // Tokens belong to the app that issued them.
  m_clientId = clean;
  m_refreshToken.clear();
  m_accessToken.clear();
  save();
  emit authChanged();
}

void Spotify::logout() {
  m_refreshToken.clear();
  m_accessToken.clear();
  save();
  emit authChanged();
}

void Spotify::login() {
  if (m_clientId.isEmpty()) { emit loginFailed("No Spotify client ID yet"); return; }
  if (m_server) m_server->deleteLater();
  m_server = new QTcpServer(this);
  if (!m_server->listen(QHostAddress::LocalHost, callbackPort)) {
    QString err = "Port 8898 is busy: " + m_server->errorString();
    m_server->deleteLater();
    m_server = nullptr;
    emit loginFailed(err);
    return;
  }
  // Give up after ten minutes rather than holding the port forever.
  QTimer::singleShot(10 * 60 * 1000, m_server, [this] {
    if (!m_server) return;
    m_server->deleteLater();
    m_server = nullptr;
    emit authChanged();
  });

  connect(m_server, &QTcpServer::newConnection, this, [this] {
    while (m_server && m_server->hasPendingConnections()) {
      QTcpSocket *sock = m_server->nextPendingConnection();
      connect(sock, &QTcpSocket::readyRead, sock, [this, sock] {
        QByteArray line = sock->readLine();
        // "GET /callback?code=...&state=... HTTP/1.1"
        QList<QByteArray> parts = line.split(' ');
        QUrl url(parts.size() > 1 ? QString::fromUtf8(parts[1]) : QString());
        QUrlQuery q(url);
        bool ours = url.path() == "/callback" && q.queryItemValue("state") == m_state && !m_state.isEmpty();
        QString code = q.queryItemValue("code");
        QString error = q.queryItemValue("error");
        QByteArray body = ours && !code.isEmpty()
          ? "<!doctype html><title>Hum</title><body style='background:#111;color:#ddd;font:16px sans-serif;padding:3em'>Spotify is connected. You can close this tab."
          : "<!doctype html><title>Hum</title><body style='background:#111;color:#ddd;font:16px sans-serif;padding:3em'>Spotify sign-in didn't finish. Try again from the music panel.";
        sock->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\n\r\n" + body);
        sock->disconnectFromHost();
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
        if (!ours) return;
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
        m_state.clear();
        if (code.isEmpty()) emit loginFailed(error.isEmpty() ? "Sign-in was cancelled" : error);
        else finishLogin(code);
      });
    }
  });

  m_verifier = util::randomString(64);
  m_state = util::randomString(16);
  QByteArray challenge = util::base64Url(QCryptographicHash::hash(m_verifier.toLatin1(), QCryptographicHash::Sha256));
  QUrl url("https://accounts.spotify.com/authorize");
  QUrlQuery q;
  q.addQueryItem("client_id", m_clientId);
  q.addQueryItem("response_type", "code");
  q.addQueryItem("redirect_uri", redirectUri());
  q.addQueryItem("code_challenge_method", "S256");
  q.addQueryItem("code_challenge", QString::fromLatin1(challenge));
  q.addQueryItem("scope", scopes);
  q.addQueryItem("state", m_state);
  url.setQuery(q);
  QDesktopServices::openUrl(url);
  QProcess::startDetached("notify-send", {"-a", "Hum", "Spotify sign-in opened in your browser",
                                          "Click Agree there, then come back to the music panel."});
  emit authChanged();
}

void Spotify::finishLogin(const QString &code) {
  tokenRequest({{"grant_type", "authorization_code"}, {"code", code},
                {"redirect_uri", redirectUri()}, {"client_id", m_clientId},
                {"code_verifier", m_verifier}},
               [this](const QString &, const QString &error) {
                 if (!error.isEmpty()) emit loginFailed(error);
                 emit authChanged();
               });
}

void Spotify::tokenRequest(const QList<QPair<QString, QString>> &form, const TokenReply &cb) {
  QUrlQuery q;
  for (const auto &p : form) q.addQueryItem(p.first, QString::fromLatin1(QUrl::toPercentEncoding(p.second)));
  QNetworkRequest req(QUrl("https://accounts.spotify.com/api/token"));
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
  req.setTransferTimeout(15000);
  QNetworkReply *reply = m_net->post(req, q.query(QUrl::FullyEncoded).toLatin1());
  connect(reply, &QNetworkReply::finished, this, [this, reply, cb] {
    reply->deleteLater();
    QJsonObject obj = QJsonDocument::fromJson(reply->read(1 << 16)).object();
    QString token = obj.value("access_token").toString();
    if (token.isEmpty()) {
      QString err = obj.value("error_description").toString(obj.value("error").toString(reply->errorString()));
      // A refresh token Spotify no longer honours means signing in again.
      if (obj.value("error").toString() == "invalid_grant") {
        m_refreshToken.clear();
        save();
        emit authChanged();
      }
      cb(QString(), "Spotify: " + err);
      return;
    }
    m_accessToken = token;
    m_expiresAt = QDateTime::currentSecsSinceEpoch() + obj.value("expires_in").toInt(3600);
    // PKCE refresh tokens rotate; keep whichever one came back last.
    QString refresh = obj.value("refresh_token").toString();
    if (!refresh.isEmpty() && refresh != m_refreshToken) { m_refreshToken = refresh; save(); }
    cb(token, QString());
  });
}

void Spotify::withToken(const TokenReply &cb) {
  if (!m_accessToken.isEmpty() && QDateTime::currentSecsSinceEpoch() < m_expiresAt - 60) { cb(m_accessToken, QString()); return; }
  if (m_refreshToken.isEmpty()) { cb(QString(), "Spotify isn't connected"); return; }
  m_waiting << cb;
  if (m_waiting.size() > 1) return;  // a refresh is already on its way
  tokenRequest({{"grant_type", "refresh_token"}, {"refresh_token", m_refreshToken}, {"client_id", m_clientId}},
               [this](const QString &token, const QString &error) {
                 QList<TokenReply> waiting = m_waiting;
                 m_waiting.clear();
                 for (const auto &w : waiting) w(token, error);
               });
}

void Spotify::api(const QByteArray &method, const QString &path, const QJsonDocument &body, const Reply &cb) {
  send(method, path, body, cb, false);
}

void Spotify::send(const QByteArray &method, const QString &path, const QJsonDocument &body, const Reply &cb, bool retried) {
  withToken([=](const QString &token, const QString &error) {
    if (!error.isEmpty()) { cb({}, error); return; }
    QUrl url(path.startsWith("https://api.spotify.com/") ? path : "https://api.spotify.com/v1" + path);
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", "Bearer " + token.toLatin1());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(15000);
    QByteArray payload = body.isNull() ? QByteArray() : body.toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_net->sendCustomRequest(req, method, payload);
    connect(reply, &QNetworkReply::finished, this, [=] {
      reply->deleteLater();
      int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      // Playlists and search results are a few hundred KB at most.
      QByteArray data = reply->read(4 << 20);
      if (status == 401 && !retried) {
        m_accessToken.clear();
        send(method, path, body, cb, true);
        return;
      }
      if (status >= 400 || (status == 0 && reply->error() != QNetworkReply::NoError)) {
        QJsonObject err = QJsonDocument::fromJson(data).object().value("error").toObject();
        QString msg = err.value("message").toString();
        if (status == 429) msg = "Spotify says slow down; try again in a minute";
        if (msg.isEmpty()) msg = reply->errorString();
        cb({}, "Spotify: " + util::clip(msg, 200));
        return;
      }
      cb(QJsonDocument::fromJson(data), QString());
    });
  });
}
