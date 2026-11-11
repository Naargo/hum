#pragma once

#include <QJsonDocument>
#include <QObject>
#include <QString>
#include <functional>

class QNetworkAccessManager;
class QTcpServer;

// Spotify's Web API: sign-in, tokens, and the calls the panel needs.
//
// Sign-in is OAuth with PKCE, so no client secret lives on the laptop. The
// consent page opens in the user's own browser, where they are usually
// already signed in to Spotify, and comes back to a one-shot listener on
// 127.0.0.1:8898. Only the refresh token is kept on disk.
class Spotify : public QObject {
  Q_OBJECT
public:
  using Reply = std::function<void(const QJsonDocument &doc, const QString &error)>;
  using TokenReply = std::function<void(const QString &token, const QString &error)>;

  static constexpr int callbackPort = 8898;

  Spotify(QNetworkAccessManager *net, QObject *parent = nullptr);

  QString clientId() const { return m_clientId; }
  bool loggedIn() const { return !m_refreshToken.isEmpty(); }
  bool loggingIn() const { return m_server != nullptr; }

  void setClientId(const QString &id);
  void login();
  void logout();

  // A fresh access token, refreshed when it's about to run out.
  void withToken(const TokenReply &cb);
  // method is GET/PUT/POST; path is relative to /v1 or a full "next" URL.
  void api(const QByteArray &method, const QString &path, const QJsonDocument &body, const Reply &cb);

signals:
  void authChanged();
  void loginFailed(const QString &error);

private:
  void finishLogin(const QString &code);
  void tokenRequest(const QList<QPair<QString, QString>> &form, const TokenReply &cb);
  void save();
  void send(const QByteArray &method, const QString &path, const QJsonDocument &body, const Reply &cb, bool retried);

  QNetworkAccessManager *m_net;
  QString m_path;
  QString m_clientId;
  QString m_refreshToken;
  QString m_accessToken;
  qint64 m_expiresAt = 0;
  QString m_verifier;
  QString m_state;
  QTcpServer *m_server = nullptr;
  QList<TokenReply> m_waiting;
};
