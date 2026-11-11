#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <functional>

class QLocalServer;
class QLocalSocket;
class QWebEngineProfile;
class Mpris;
class Player;
class Spotify;
class Suno;

// Holds everything together and answers the bar panel.
//
// The panel speaks JSON lines over $XDG_RUNTIME_DIR/hum.sock:
//   -> {"id": 3, "cmd": "spotify.search", "args": {"q": "..."}}
//   <- {"id": 3, "ok": true, "data": {...}}  or  {"id": 3, "ok": false, "error": "..."}
//   <- {"event": "state", "data": {...}}     pushed whenever anything changes
//
// The engine quits by itself after a stretch with nothing playing, so it
// costs nothing while there's no music. The panel starts it again on demand.
class Engine : public QObject {
  Q_OBJECT
public:
  explicit Engine(QObject *parent = nullptr);
  bool listen(QString *error);

private:
  using Done = std::function<void(const QJsonValue &data, const QString &error)>;

  void handle(QLocalSocket *sock, const QJsonObject &req);
  void run(const QString &cmd, const QJsonObject &args, const Done &done);
  void stateChanged();
  QJsonObject state() const;
  void broadcast();
  void syncMpris();

  void pageMessage(const QJsonObject &msg);
  void ensureSpotifyPlayer();
  void whenSpotifyReady(const std::function<void(const QString &error)> &cb);

  // Spotify
  void spotifyPlaylists(const Done &done);
  void spotifyRecent(const Done &done);
  void spotifyAlbum(const QString &id, const Done &done);
  void spotifyTracks(const QString &id, const Done &done);
  void spotifySearch(const QString &q, const Done &done);
  void spotifyPlay(const QJsonObject &args, const Done &done);
  void collectPages(const QString &url, const QString &itemKey, int limit, QJsonArray acc,
                    const std::function<void(const QJsonArray &, const QString &)> &cb);

  // Suno
  void sunoPlaylists(const Done &done);
  void sunoTracks(const QString &id, const Done &done);
  void sunoPlayIndex(int index);
  void sunoPages(const QString &id, int page, QJsonArray acc,
                 const std::function<void(const QJsonArray &, const QString &)> &cb);

  // Transport, for whichever source is playing.
  void toggle();
  void play();
  void pause();
  void next();
  void previous();
  void seek(qint64 ms);

  void setError(const QString &source, const QString &message);
  void touch();

  QNetworkAccessManager m_net;
  QWebEngineProfile *m_profile;
  Spotify *m_spotify;
  Suno *m_suno;
  Player *m_player;
  Mpris *m_mpris;
  QLocalServer *m_server;
  QList<QLocalSocket *> m_clients;
  bool m_broadcastQueued = false;

  QString m_source;            // "spotify" or "suno": what's in the now-playing slot
  bool m_pageLoaded = false;
  QString m_device;            // Spotify Connect device id, once the SDK is ready
  QList<std::function<void(const QString &)>> m_readyWaiters;
  QString m_meId;
  QHash<QString, QString> m_errors;

  // What's playing, as one shape for both sources.
  QJsonObject m_now;           // title, artist, album, image, id
  bool m_playing = false;
  qint64 m_position = 0;
  qint64 m_positionAt = 0;
  qint64 m_duration = 0;
  bool m_canNext = false, m_canPrev = false;
  QString m_context;           // Spotify context uri, or "suno:<list>"

  QHash<QString, QJsonArray> m_sunoLists;
  QJsonArray m_sunoQueue;
  QString m_sunoQueueId;
  int m_sunoIndex = -1;

  QElapsedTimer m_idle;
};
