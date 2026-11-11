#pragma once

#include <QHash>
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

  bool loggedIn() const { return !m_client.isEmpty() && !m_signedOut; }
  bool loggingIn() const { return !m_window.isNull(); }

  void login();
  void logout();
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

  QNetworkAccessManager *m_net;
  QWebEngineProfile *m_profile;
  QString m_client;        // the __client cookie value
  QString m_authHost;      // where that cookie lives (auth.suno.com today)
  QString m_jwt;
  qint64 m_jwtExpires = 0;
  bool m_signedOut = false;
  QPointer<QWebEngineView> m_window;
  QList<TokenReply> m_waiting;
};
