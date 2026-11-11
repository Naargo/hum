#include "suno.h"
#include "util.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QProcess>
#include <QTimer>
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>
#include <QWebEngineView>

static const char *studioApi = "https://studio-api.prod.suno.com";
static const char *clerkVersion = "_clerk_js_version=5.117.0";

Suno::Suno(QNetworkAccessManager *net, QWebEngineProfile *profile, QObject *parent)
  : QObject(parent), m_net(net), m_profile(profile) {
  QWebEngineCookieStore *store = profile->cookieStore();
  connect(store, &QWebEngineCookieStore::cookieAdded, this, &Suno::cookieAdded);
  connect(store, &QWebEngineCookieStore::cookieRemoved, this, &Suno::cookieRemoved);
  // Replays every saved cookie through cookieAdded, which is how the
  // engine finds the session it kept from last time.
  store->loadAllCookies();
}

void Suno::cookieAdded(const QNetworkCookie &cookie) {
  QString domain = cookie.domain();
  if (cookie.name() != "__client" || !domain.endsWith("suno.com")) return;
  QString host = domain.startsWith('.') ? domain.mid(1) : domain;
  bool changed = m_client != QString::fromUtf8(cookie.value());
  m_client = QString::fromUtf8(cookie.value());
  m_authHost = host;
  if (changed) { m_jwt.clear(); m_signedOut = false; }
  if (!m_window.isNull()) QTimer::singleShot(800, this, &Suno::checkLogin);
  else if (changed) emit authChanged();
}

void Suno::cookieRemoved(const QNetworkCookie &cookie) {
  if (cookie.name() != "__client" || QString::fromUtf8(cookie.value()) != m_client) return;
  m_client.clear();
  m_jwt.clear();
  emit authChanged();
}

void Suno::login() {
  if (!m_window.isNull()) { m_window->raise(); m_window->activateWindow(); return; }
  QWebEngineView *view = new QWebEngineView(m_profile, nullptr);
  view->setAttribute(Qt::WA_DeleteOnClose);
  view->setWindowTitle("Sign in to Suno");
  view->resize(520, 760);
  view->load(QUrl("https://suno.com/sign-in"));
  connect(view, &QObject::destroyed, this, [this] { emit authChanged(); });
  m_window = view;
  m_signedOut = false;
  view->show();
  QProcess::startDetached("notify-send", {"-a", "Hum", "Suno sign-in is on workspace 5",
                                          "Sign in there; the window closes by itself when it worked."});
  emit authChanged();
}

void Suno::logout() {
  m_profile->cookieStore()->deleteAllCookies();
  m_client.clear();
  m_jwt.clear();
  m_signedOut = true;
  emit authChanged();
}

// The sign-in window closes itself once the cookie it set really opens a
// session, rather than on the first cookie it sees (Clerk sets __client
// before anyone has signed in).
void Suno::checkLogin() {
  if (m_window.isNull()) return;
  m_jwt.clear();
  fetchToken([this](const QString &token, const QString &) {
    if (token.isEmpty() || m_window.isNull()) return;
    m_window->close();
    emit authChanged();
  });
}

void Suno::withToken(const TokenReply &cb) {
  if (!m_jwt.isEmpty() && QDateTime::currentSecsSinceEpoch() < m_jwtExpires - 10) { cb(m_jwt, QString()); return; }
  if (!loggedIn()) { cb(QString(), "Suno isn't connected"); return; }
  m_waiting << cb;
  if (m_waiting.size() > 1) return;
  fetchToken([this](const QString &token, const QString &error) {
    QList<TokenReply> waiting = m_waiting;
    m_waiting.clear();
    for (const auto &w : waiting) w(token, error);
  });
}

static QNetworkRequest clerkRequest(const QString &url, const QString &client) {
  QNetworkRequest req{QUrl(url)};
  req.setRawHeader("Cookie", "__client=" + client.toUtf8());
  req.setRawHeader("Origin", "https://suno.com");
  req.setRawHeader("Referer", "https://suno.com/");
  req.setTransferTimeout(15000);
  return req;
}

void Suno::fetchToken(const TokenReply &cb) {
  if (m_client.isEmpty()) { cb(QString(), "Suno isn't connected"); return; }
  QString base = "https://" + m_authHost + "/v1/client";
  QNetworkReply *reply = m_net->get(clerkRequest(base + "?" + clerkVersion, m_client));
  connect(reply, &QNetworkReply::finished, this, [=] {
    reply->deleteLater();
    QJsonObject resp = QJsonDocument::fromJson(reply->read(1 << 20)).object().value("response").toObject();
    QString sid = resp.value("last_active_session_id").toString();
    if (sid.isEmpty()) {
      QJsonArray sessions = resp.value("sessions").toArray();
      if (!sessions.isEmpty()) sid = sessions.first().toObject().value("id").toString();
    }
    if (sid.isEmpty()) { cb(QString(), "Suno: not signed in"); return; }
    QNetworkRequest req = clerkRequest(base + "/sessions/" + sid + "/tokens?" + clerkVersion, m_client);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QNetworkReply *tok = m_net->post(req, QByteArray());
    connect(tok, &QNetworkReply::finished, this, [=] {
      tok->deleteLater();
      QString jwt = QJsonDocument::fromJson(tok->read(1 << 16)).object().value("jwt").toString();
      if (jwt.isEmpty()) { cb(QString(), "Suno: couldn't get a session token (" + tok->errorString() + ")"); return; }
      // The JWT's own expiry, from its payload.
      QList<QString> parts = jwt.split('.');
      qint64 exp = 0;
      if (parts.size() == 3) {
        QByteArray payload = QByteArray::fromBase64(parts[1].toLatin1(), QByteArray::Base64UrlEncoding);
        exp = QJsonDocument::fromJson(payload).object().value("exp").toInteger();
      }
      m_jwt = jwt;
      m_jwtExpires = exp > 0 ? exp : QDateTime::currentSecsSinceEpoch() + 50;
      cb(jwt, QString());
    });
  });
}

void Suno::api(const QString &path, const Reply &cb) {
  withToken([=](const QString &token, const QString &error) {
    if (!error.isEmpty()) { cb({}, error); return; }
    QNetworkRequest req{QUrl(QString(studioApi) + path)};
    req.setRawHeader("Authorization", "Bearer " + token.toLatin1());
    req.setRawHeader("Origin", "https://suno.com");
    req.setTransferTimeout(20000);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [=] {
      reply->deleteLater();
      int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      QByteArray data = reply->read(8 << 20);
      if (status >= 400 || status == 0) {
        cb({}, QString("Suno answered %1 for %2. Suno may have changed its site.").arg(status).arg(path.section('?', 0, 0)));
        return;
      }
      cb(QJsonDocument::fromJson(data), QString());
    });
  });
}
