#include "suno.h"
#include "util.h"
#include "brave.h"

#include <QDateTime>
#include <QFile>
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
  store->loadAllCookies();

  m_path = util::dataDir() + "/suno.json";
  QJsonObject saved = util::readJson(m_path);
  if (!saved.value("client").toString().isEmpty())
    addCandidate(saved.value("client").toString(), saved.value("host").toString("auth.suno.com"));
}

void Suno::remember() {
  util::writeJson(m_path, {{"client", m_client}, {"host", m_authHost}}, true);
}

void Suno::addCandidate(const QString &value, const QString &host) {
  QPair<QString, QString> c{value, host};
  m_candidates.removeAll(c);
  m_candidates << c;
  if (m_sessionOk || !m_window.isNull()) return;
  if (!m_trying) { m_trying = true; QTimer::singleShot(300, this, [this] { tryCandidates(m_candidates.size() - 1); }); }
}

void Suno::start() {
  if (m_sessionOk) return;
  m_profile->cookieStore()->loadAllCookies();
  QTimer::singleShot(5000, this, [this] {
    if (m_sessionOk || m_trying || !m_window.isNull() || m_signedOut) return;
    QString error = importFromBrave();
    if (qEnvironmentVariableIsSet("HUM_DEBUG")) qInfo("suno: no saved sign-in worked; copying from Brave: %s", error.isEmpty() ? "ok" : qPrintable(error));
  });
}

void Suno::cookieAdded(const QNetworkCookie &cookie) {
  QString domain = cookie.domain();
  if (cookie.name() != "__client" || !domain.endsWith("suno.com")) return;
  QString host = domain.startsWith('.') ? domain.mid(1) : domain;
  QString value = QString::fromUtf8(cookie.value());
  m_signedOut = false;
  if (!m_window.isNull()) {
    // A sign-in in progress: the newest cookie is the one to check.
    m_candidates.removeAll({value, host});
    m_candidates << QPair<QString, QString>{value, host};
    m_client = value;
    m_authHost = host;
    m_jwt.clear();
    QTimer::singleShot(800, this, &Suno::checkLogin);
    return;
  }
  addCandidate(value, host);
}

// Try the cookies newest first; keep the first that opens a session.
static bool debugOn() { return qEnvironmentVariableIsSet("HUM_DEBUG"); }

void Suno::tryCandidates(int from) {
  if (debugOn()) qInfo("suno: trying sign-in %d of %d", from + 1, int(m_candidates.size()));
  if (from < 0 || from >= m_candidates.size()) {
    m_trying = false;
    if (m_sessionOk) return;
    // None of ours works. Suno refreshes the browser's sign-in now and then,
    // which leaves the copy here stale; fetch the current one from Brave.
    QString importError = importFromBrave();
    if (debugOn()) qInfo("suno: none worked; copying from Brave: %s", importError.isEmpty() ? "ok" : qPrintable(importError));
    if (importError.isEmpty()) return;  // its cookies come back through cookieAdded
    m_client = m_candidates.isEmpty() ? QString() : m_candidates.last().first;
    emit authChanged();
    return;
  }
  m_client = m_candidates[from].first;
  m_authHost = m_candidates[from].second;
  m_jwt.clear();
  fetchToken([this, from](const QString &token, const QString &error) {
    if (debugOn()) qInfo("suno: sign-in %d -> %s", from + 1, token.isEmpty() ? qPrintable(error) : "session ok");
    if (!token.isEmpty()) {
      m_trying = false;
      m_sessionOk = true;
      remember();
      emit authChanged();
      return;
    }
    tryCandidates(from - 1);
  });
}

// Removals are ignored: the store reports one whenever a cookie is
// replaced, even by the same value, so they say nothing about whether the
// sign-in still works. Asking Suno (tryCandidates, withToken) decides that.
void Suno::cookieRemoved(const QNetworkCookie &) {}

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

QString Suno::importFromBrave() {
  QString error;
  QList<QNetworkCookie> found = brave::cookies("suno.com", &error);
  bool hasClient = false;
  for (const QNetworkCookie &c : found) hasClient = hasClient || (c.name() == "__client" && c.domain().endsWith("suno.com"));
  if (!hasClient) return error.isEmpty() ? "Brave isn't signed in to Suno" : error;
  QString braveClient;
  for (const QNetworkCookie &c : found) if (c.name() == "__client" && c.domain().endsWith("suno.com")) braveClient = QString::fromUtf8(c.value());
  if (m_fromBrave.contains(braveClient)) return "Brave's Suno sign-in didn't work either; sign in to suno.com in Brave again";
  m_fromBrave.insert(braveClient);
  m_signedOut = false;
  QWebEngineCookieStore *store = m_profile->cookieStore();
  QString braveHost = "auth.suno.com";
  for (const QNetworkCookie &c : found) {
    QString host = c.domain().startsWith('.') ? c.domain().mid(1) : c.domain();
    // Into the web profile too, so the hidden suno.com page is signed in.
    store->setCookie(c, QUrl("https://" + host));
    if (c.name() == "__client") braveHost = host;
  }
  m_sessionOk = false;
  addCandidate(braveClient, braveHost);
  return QString();
}

void Suno::logout() {
  m_profile->cookieStore()->deleteAllCookies();
  m_candidates.clear();
  QFile::remove(m_path);
  m_client.clear();
  m_jwt.clear();
  m_sessionOk = false;
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
    m_sessionOk = true;
    remember();
    m_window->close();
    emit authChanged();
  });
}

void Suno::withToken(const TokenReply &cb) {
  if (!m_jwt.isEmpty() && QDateTime::currentSecsSinceEpoch() < m_jwtExpires - 10) { cb(m_jwt, QString()); return; }
  if (m_client.isEmpty() || m_signedOut) { cb(QString(), "Suno isn't connected"); return; }
  m_waiting << cb;
  if (m_waiting.size() > 1) return;
  fetchToken([this](const QString &token, const QString &error) {
    // A clear "no session" flips us to signed out; a network hiccup doesn't.
    bool ok = !token.isEmpty() ? true : error == "Suno: not signed in" ? false : m_sessionOk;
    if (ok != m_sessionOk) { m_sessionOk = ok; emit authChanged(); }
    QList<TokenReply> waiting = m_waiting;
    m_waiting.clear();
    for (const auto &w : waiting) w(token, error);
  });
}

// Ask Suno whether the saved cookie still opens a session; withToken
// updates m_sessionOk (and tells the panel) from the answer.
void Suno::verify() {
  withToken([](const QString &, const QString &) {});
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
