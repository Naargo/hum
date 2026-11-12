#pragma once

#include <QHash>
#include <QSet>
#include <QJsonDocument>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>

class QNetworkAccessManager;
class QNetworkCookie;
class QWebEngineProfile;
class QWebEngineView;

// The user's own Suno library.
//
// Suno has no public API. Its website signs in through Clerk, which leaves a
// long-lived __client cookie on Suno's auth host; Clerk trades that cookie for
// a JWT good for about a minute, and the JWT opens Suno's studio API. So
// signing in happens once, in a small window that shares the engine's web
// profile, and afterwards the engine only needs the cookie the profile keeps.
// These are the same requests suno.com makes, and they can change without
// notice; errors say so plainly instead of hiding it.
class Suno : public QObject {
  Q_OBJECT
public:
  using Reply = std::function<void(const QJsonDocument &doc, const QString &error)>;
  using TokenReply = std::function<void(const QString &token, const QString &error)>;

  Suno(QNetworkAccessManager *net, QWebEngineProfile *profile, QObject *parent = nullptr);

  // Suno sets its __client cookie before anyone signs in, so having one
  // proves nothing. Signed in means Suno handed us a session token for it.
  bool loggedIn() const { return !m_client.isEmpty() && m_sessionOk && !m_signedOut; }
  bool loggingIn() const { return !m_window.isNull(); }

  void login();
  void logout();
  // Reuse a Suno sign-in from Brave instead of signing in here. Returns
  // an error, or "" when the cookies were handed over (the session check
  // that follows decides whether that counts as signed in).
  QString importFromBrave();
  // Read the saved sign-in once the web profile is up. Replaying cookies
  // straight from the constructor isn't reliable, so the engine calls this
  // when its first page has loaded; if nothing usable turns up, the current
  // sign-in is fetched from Brave.
  void start();
  void withToken(const TokenReply &cb);
  void api(const QString &path, const Reply &cb);

signals:
  void authChanged();
  void loginFailed(const QString &error);

private:
  void cookieAdded(const QNetworkCookie &cookie);
  void cookieRemoved(const QNetworkCookie &cookie);
  void checkLogin();
  void fetchToken(const TokenReply &cb);
  void verify();
  void tryCandidates(int from);

  QNetworkAccessManager *m_net;
  QWebEngineProfile *m_profile;
  QString m_client;        // the __client cookie value
  QString m_authHost;      // where that cookie lives (auth.suno.com today)
  QString m_jwt;
  qint64 m_jwtExpires = 0;
  bool m_signedOut = false;
  bool m_sessionOk = false;  // the cookie really opens a session
  // Every __client cookie the profile holds, newest last. There can be more
  // than one (a signed-out one from visiting suno.com, a signed-in one from
  // Brave), so each is tried until one opens a session.
  QList<QPair<QString, QString>> m_candidates;  // (value, auth host)
  bool m_trying = false;
  // Brave's __client values already copied in, so a stale one isn't copied
  // over and over.
  QSet<QString> m_fromBrave;
  // The sign-in that last worked, kept in suno.json. The web profile keeps
  // it too, but its cookie notifications can't be relied on to report it.
  QString m_path;
  void remember();
  void addCandidate(const QString &value, const QString &host);
  QPointer<QWebEngineView> m_window;
  QList<TokenReply> m_waiting;
};
