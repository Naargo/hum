#include "engine.h"
#include "mpris.h"
#include "player.h"
#include "spotify.h"
#include "suno.h"
#include "util.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QWebEngineProfile>

// Quit after this long with nothing playing and no requests.
static const qint64 idleQuitMs = 20 * 60 * 1000;

static qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

Engine::Engine(QObject *parent) : QObject(parent) {
  QString data = util::dataDir();
  m_profile = new QWebEngineProfile("hum", this);
  m_profile->setPersistentStoragePath(data + "/web");
  m_profile->setCachePath(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/hum");
  m_profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
  m_profile->setHttpCacheMaximumSize(32 << 20);
  m_profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
  // Sign-in pages (Google's especially) turn away anything that admits to
  // being an embedded engine, so the profile reads as plain Chrome.
  QString ua = m_profile->httpUserAgent();
  ua.replace(QRegularExpression(" QtWebEngine/[^ ]+"), "");
  m_profile->setHttpUserAgent(ua);

  m_spotify = new Spotify(&m_net, this);
  m_suno = new Suno(&m_net, m_profile, this);
  m_player = new Player(m_profile, this);
  m_mpris = new Mpris(this);
  m_server = new QLocalServer(this);

  connect(m_spotify, &Spotify::authChanged, this, [this] {
    if (m_spotify->loggedIn()) { m_errors.remove("spotify"); ensureSpotifyPlayer(); }
    stateChanged();
  });
  connect(m_spotify, &Spotify::loginFailed, this, [this](const QString &e) { setError("spotify", e); });
  connect(m_suno, &Suno::authChanged, this, [this] { if (m_suno->loggedIn()) m_errors.remove("suno"); stateChanged(); });
  connect(m_suno, &Suno::loginFailed, this, [this](const QString &e) { setError("suno", e); });
  connect(m_player, &Player::message, this, &Engine::pageMessage);
  connect(m_player, &QWebEnginePage::renderProcessTerminated, this, [this] {
    // The page is the speaker; if Chromium kills it, build a new one.
    m_pageLoaded = false;
    m_device.clear();
    m_playing = false;
    stateChanged();
    QTimer::singleShot(1000, this, [this] { m_player->triggerAction(QWebEnginePage::Reload); });
  });

  connect(m_mpris, &Mpris::playPause, this, &Engine::toggle);
  connect(m_mpris, &Mpris::play, this, &Engine::play);
  connect(m_mpris, &Mpris::pause, this, &Engine::pause);
  connect(m_mpris, &Mpris::next, this, &Engine::next);
  connect(m_mpris, &Mpris::previous, this, &Engine::previous);
  connect(m_mpris, &Mpris::seekTo, this, &Engine::seek);

  m_idle.start();
  QTimer *idle = new QTimer(this);
  idle->start(60 * 1000);
  connect(idle, &QTimer::timeout, this, [this] {
    if (m_playing || m_spotify->loggingIn() || m_suno->loggingIn()) { touch(); return; }
    if (m_idle.elapsed() > idleQuitMs && !qEnvironmentVariableIsSet("HUM_STAY")) QCoreApplication::quit();
  });
}

bool Engine::listen(QString *error) {
  QString path = util::socketPath();
  QLocalServer::removeServer(path);
  m_server->setSocketOptions(QLocalServer::UserAccessOption);
  if (!m_server->listen(path)) { *error = m_server->errorString(); return false; }
  connect(m_server, &QLocalServer::newConnection, this, [this] {
    while (QLocalSocket *sock = m_server->nextPendingConnection()) {
      m_clients << sock;
      connect(sock, &QLocalSocket::disconnected, this, [this, sock] {
        m_clients.removeAll(sock);
        sock->deleteLater();
      });
      connect(sock, &QLocalSocket::readyRead, this, [this, sock] {
        while (sock->canReadLine()) {
          QByteArray line = sock->readLine(1 << 16).trimmed();
          if (line.isEmpty()) continue;
          handle(sock, QJsonDocument::fromJson(line).object());
        }
        // A line that never ends is not a client we want.
        if (sock->bytesAvailable() > (1 << 16)) sock->abort();
      });
      QByteArray hello = QJsonDocument(QJsonObject{{"event", "state"}, {"data", state()}}).toJson(QJsonDocument::Compact);
      sock->write(hello + "\n");
    }
  });
  return true;
}

void Engine::touch() { m_idle.restart(); }

void Engine::handle(QLocalSocket *sock, const QJsonObject &req) {
  touch();
  QJsonValue id = req.value("id");
  QPointer<QLocalSocket> guard(sock);
  run(req.value("cmd").toString(), req.value("args").toObject(), [guard, id](const QJsonValue &data, const QString &error) {
    if (!guard || id.isUndefined()) return;
    QJsonObject reply{{"id", id}, {"ok", error.isEmpty()}};
    if (error.isEmpty()) reply["data"] = data;
    else reply["error"] = error;
    guard->write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + "\n");
  });
}

void Engine::run(const QString &cmd, const QJsonObject &args, const Done &done) {
  auto ok = [&] { done(QJsonValue(true), QString()); };

  if (cmd == "state") { done(state(), QString()); return; }
  if (cmd == "toggle") { toggle(); ok(); return; }
  if (cmd == "play") { play(); ok(); return; }
  if (cmd == "pause") { pause(); ok(); return; }
  if (cmd == "next") { next(); ok(); return; }
  if (cmd == "previous") { previous(); ok(); return; }
  if (cmd == "seek") { seek(qMax<qint64>(0, args.value("ms").toInteger())); ok(); return; }
  if (cmd == "quit") { ok(); QTimer::singleShot(0, qApp, &QCoreApplication::quit); return; }

  if (cmd == "spotify.setClientId") {
    QString idv = args.value("id").toString().trimmed();
    // Spotify client IDs are 32 hex characters.
    if (!QRegularExpression("^[0-9a-fA-F]{32}$").match(idv).hasMatch()) { done({}, "That doesn't look like a Spotify Client ID (32 letters and digits)"); return; }
    m_spotify->setClientId(idv);
    ok();
    return;
  }
  if (cmd == "spotify.login") { m_errors.remove("spotify"); m_spotify->login(); ok(); return; }
  if (cmd == "spotify.logout") { m_spotify->logout(); ok(); return; }
  if (cmd == "spotify.playlists") { spotifyPlaylists(done); return; }
  if (cmd == "spotify.tracks") { spotifyTracks(args.value("id").toString(), done); return; }
  if (cmd == "spotify.search") { spotifySearch(args.value("q").toString(), done); return; }
  if (cmd == "spotify.play") { spotifyPlay(args, done); return; }

  if (cmd == "suno.login") { m_errors.remove("suno"); m_suno->login(); ok(); return; }
  if (cmd == "suno.logout") { m_suno->logout(); ok(); return; }
  if (cmd == "suno.playlists") { sunoPlaylists(done); return; }
  if (cmd == "suno.tracks") { sunoTracks(args.value("id").toString(), done); return; }
  if (cmd == "suno.play") {
    QString list = args.value("list").toString();
    if (!m_sunoLists.contains(list)) { done({}, "Open the playlist first"); return; }
    m_sunoQueue = m_sunoLists.value(list);
    m_sunoQueueId = list;
    sunoPlayIndex(args.value("index").toInt());
    ok();
    return;
  }
  done({}, "Unknown command: " + util::clip(cmd, 60));
}

// ---------------------------------------------------------------- state

QJsonObject Engine::state() const {
  QJsonObject now = m_now;
  now["playing"] = m_playing;
  now["position"] = m_position;
  now["at"] = m_positionAt;
  now["duration"] = m_duration;
  now["canNext"] = m_canNext;
  now["canPrev"] = m_canPrev;
  now["context"] = m_context;
  return {
    {"source", m_source},
    {"now", m_now.isEmpty() ? QJsonValue() : QJsonValue(now)},
    {"spotify", QJsonObject{
      {"configured", !m_spotify->clientId().isEmpty()},
      {"loggedIn", m_spotify->loggedIn()},
      {"loggingIn", m_spotify->loggingIn()},
      {"ready", !m_device.isEmpty()},
      {"error", m_errors.value("spotify")},
    }},
    {"suno", QJsonObject{
      {"loggedIn", m_suno->loggedIn()},
      {"loggingIn", m_suno->loggingIn()},
      {"error", m_errors.value("suno")},
    }},
  };
}

void Engine::stateChanged() {
  syncMpris();
  if (m_broadcastQueued) return;
  m_broadcastQueued = true;
  QTimer::singleShot(0, this, &Engine::broadcast);
}

void Engine::broadcast() {
  m_broadcastQueued = false;
  QByteArray line = QJsonDocument(QJsonObject{{"event", "state"}, {"data", state()}}).toJson(QJsonDocument::Compact) + "\n";
  for (QLocalSocket *c : std::as_const(m_clients)) c->write(line);
}

void Engine::syncMpris() {
  Mpris::State s;
  s.hasTrack = !m_now.isEmpty();
  s.playing = m_playing;
  s.trackId = m_now.value("id").toString();
  s.title = m_now.value("title").toString();
  s.artist = m_now.value("artist").toString();
  s.album = m_now.value("album").toString();
  s.art = m_now.value("image").toString();
  s.durationMs = m_duration;
  s.positionMs = m_position + (m_playing ? nowMs() - m_positionAt : 0);
  s.canNext = m_canNext;
  s.canPrev = m_canPrev;
  m_mpris->update(s);
}

void Engine::setError(const QString &source, const QString &message) {
  m_errors[source] = util::clip(message, 240);
  stateChanged();
}

// ---------------------------------------------------------------- the page

void Engine::pageMessage(const QJsonObject &msg) {
  QString t = msg.value("t").toString();
  if (t == "loaded") {
    m_pageLoaded = true;
    if (m_spotify->loggedIn()) m_player->call("startSpotify");
  } else if (t == "needToken") {
    m_spotify->withToken([this](const QString &token, const QString &error) {
      if (!error.isEmpty()) { setError("spotify", error); return; }
      m_player->call("token", {token});
    });
  } else if (t == "ready") {
    m_device = msg.value("device").toString();
    m_errors.remove("spotify");
    auto waiters = m_readyWaiters;
    m_readyWaiters.clear();
    for (const auto &w : waiters) w(QString());
    stateChanged();
  } else if (t == "notReady") {
    m_device.clear();
    stateChanged();
  } else if (t == "spState") {
    if (!msg.value("active").toBool()) {
      // Playback moved to another device (the phone, say): let it go.
      if (m_source == "spotify") { m_playing = false; stateChanged(); }
      return;
    }
    QJsonObject tr = msg.value("track").toObject();
    if (m_source != "spotify" && !msg.value("paused").toBool()) {
      // Spotify started here from elsewhere; Suno steps aside.
      m_player->call("sunoPause");
    }
    if (m_source != "spotify" && msg.value("paused").toBool()) return;
    m_source = "spotify";
    m_now = QJsonObject{
      {"source", "spotify"}, {"id", tr.value("uri").toString()},
      {"title", util::clip(tr.value("name").toString())}, {"artist", util::clip(tr.value("artist").toString())},
      {"album", util::clip(tr.value("album").toString())}, {"image", tr.value("image").toString()},
    };
    m_playing = !msg.value("paused").toBool();
    m_position = msg.value("position").toInteger();
    m_positionAt = nowMs();
    m_duration = msg.value("duration").toInteger();
    m_canNext = msg.value("canNext").toBool();
    m_canPrev = msg.value("canPrev").toBool();
    m_context = msg.value("context").toString();
    stateChanged();
  } else if (t == "sunoState") {
    if (m_source != "suno") return;
    m_playing = !msg.value("paused").toBool();
    m_position = msg.value("position").toInteger();
    m_positionAt = nowMs();
    qint64 d = msg.value("duration").toInteger();
    if (d > 0) m_duration = d;
    stateChanged();
  } else if (t == "sunoEnded") {
    if (m_source == "suno" && m_sunoIndex + 1 < m_sunoQueue.size()) sunoPlayIndex(m_sunoIndex + 1);
    else { m_playing = false; stateChanged(); }
  } else if (t == "error") {
    QString source = msg.value("source").toString();
    QString message = msg.value("message").toString();
    if (source == "spotify" && msg.value("kind").toString() == "account")
      message = "Spotify says this account can't use the web player (it needs Premium)";
    setError(source == "suno" ? "suno" : "spotify", message);
    if (source == "spotify") {
      auto waiters = m_readyWaiters;
      m_readyWaiters.clear();
      for (const auto &w : waiters) w(message);
    }
  }
}

void Engine::ensureSpotifyPlayer() {
  if (m_pageLoaded && m_spotify->loggedIn()) m_player->call("startSpotify");
}

void Engine::whenSpotifyReady(const std::function<void(const QString &error)> &cb) {
  if (!m_device.isEmpty()) { cb(QString()); return; }
  if (!m_spotify->loggedIn()) { cb("Spotify isn't connected"); return; }
  m_readyWaiters << cb;
  ensureSpotifyPlayer();
  // Give the SDK a fair chance, then say so instead of hanging.
  QTimer::singleShot(20000, this, [this] {
    if (m_readyWaiters.isEmpty()) return;
    auto waiters = m_readyWaiters;
    m_readyWaiters.clear();
    for (const auto &w : waiters) w("Spotify's player didn't start (no reply in 20 seconds)");
  });
}

// ---------------------------------------------------------------- Spotify

static QString smallestImage(const QJsonArray &images) {
  QString url;
  int best = 1 << 30;
  for (const QJsonValue &v : images) {
    QJsonObject im = v.toObject();
    int w = im.value("width").toInt(640);
    if (w >= 60 && w < best) { best = w; url = im.value("url").toString(); }
    if (url.isEmpty()) url = im.value("url").toString();
  }
  return url;
}

static QJsonObject mapTrack(const QJsonObject &t) {
  QStringList artists;
  for (const QJsonValue &a : t.value("artists").toArray()) artists << a.toObject().value("name").toString();
  return {
    {"uri", t.value("uri").toString()},
    {"title", util::clip(t.value("name").toString())},
    {"artist", util::clip(artists.join(", "))},
    {"duration", t.value("duration_ms").toInt()},
    {"image", smallestImage(t.value("album").toObject().value("images").toArray())},
  };
}

void Engine::collectPages(const QString &url, const QString &itemKey, int limit, QJsonArray acc,
                          const std::function<void(const QJsonArray &, const QString &)> &cb) {
  m_spotify->api("GET", url, {}, [=](const QJsonDocument &doc, const QString &error) mutable {
    if (!error.isEmpty()) { cb(acc, error); return; }
    QJsonObject page = itemKey.isEmpty() ? doc.object() : doc.object().value(itemKey).toObject();
    for (const QJsonValue &v : page.value("items").toArray()) if (!v.isNull()) acc.append(v);
    QString nextUrl = page.value("next").toString();
    if (nextUrl.isEmpty() || acc.size() >= limit) { cb(acc, QString()); return; }
    collectPages(nextUrl, itemKey, limit, acc, cb);
  });
}

void Engine::spotifyPlaylists(const Done &done) {
  auto list = [=] {
    collectPages("/me/playlists?limit=50", QString(), 300, {}, [=](const QJsonArray &items, const QString &error) {
      if (!error.isEmpty() && items.isEmpty()) { done({}, error); return; }
      QJsonArray out;
      out.append(QJsonObject{{"id", "liked"}, {"name", "Liked Songs"}, {"own", true}});
      out.append(QJsonObject{{"id", "recent"}, {"name", "Recently Played"}, {"own", true}});
      for (const QJsonValue &v : items) {
        QJsonObject p = v.toObject();
        QJsonObject owner = p.value("owner").toObject();
        bool own = owner.value("id").toString() == m_meId || p.value("collaborative").toBool();
        out.append(QJsonObject{
          {"id", p.value("id").toString()}, {"uri", p.value("uri").toString()},
          {"name", util::clip(p.value("name").toString(), 120)},
          {"owner", util::clip(owner.value("display_name").toString(), 80)},
          {"image", smallestImage(p.value("images").toArray())},
          {"own", own},
        });
      }
      // Saved albums go after the playlists, under their own header.
      collectPages("/me/albums?limit=50", QString(), 300, {}, [=](const QJsonArray &albums, const QString &) mutable {
        for (const QJsonValue &v : albums) {
          QJsonObject a = v.toObject().value("album").toObject();
          if (a.isEmpty()) continue;
          QStringList artists;
          for (const QJsonValue &ar : a.value("artists").toArray()) artists << ar.toObject().value("name").toString();
          out.append(QJsonObject{
            {"id", "album:" + a.value("id").toString()}, {"uri", a.value("uri").toString()},
            {"name", util::clip(a.value("name").toString(), 120)},
            {"owner", util::clip(artists.join(", "), 80)},
            {"image", smallestImage(a.value("images").toArray())},
            {"own", false}, {"section", "Albums"},
          });
        }
        done(out, QString());
      });
    });
  };
  if (!m_meId.isEmpty()) { list(); return; }
  m_spotify->api("GET", "/me", {}, [=](const QJsonDocument &doc, const QString &error) {
    if (!error.isEmpty()) { done({}, error); return; }
    m_meId = doc.object().value("id").toString();
    list();
  });
}

void Engine::spotifyRecent(const Done &done) {
  m_spotify->api("GET", "/me/player/recently-played?limit=50", {}, [=](const QJsonDocument &doc, const QString &error) {
    if (!error.isEmpty()) {
      if (error.contains("403") || error.contains("orbidden") || error.contains("scope")) {
        done({}, "Reconnect Spotify to see what you played recently");
        return;
      }
      done({}, error);
      return;
    }
    // Newest first; a song played twice shows once, at its latest play.
    QJsonArray tracks;
    QSet<QString> seen;
    for (const QJsonValue &v : doc.object().value("items").toArray()) {
      QJsonObject t = v.toObject().value("track").toObject();
      QString uri = t.value("uri").toString();
      if (t.isEmpty() || t.value("is_local").toBool() || seen.contains(uri)) continue;
      seen.insert(uri);
      tracks.append(mapTrack(t));
    }
    done(QJsonObject{{"tracks", tracks}, {"restricted", false}}, QString());
  });
}

void Engine::spotifyAlbum(const QString &id, const Done &done) {
  m_spotify->api("GET", "/albums/" + id, {}, [=](const QJsonDocument &doc, const QString &error) {
    if (!error.isEmpty()) { done({}, error); return; }
    QJsonObject album = doc.object();
    // An album's tracks don't carry the album, so lend them its cover.
    QJsonObject cover{{"images", album.value("images")}};
    QJsonObject page = album.value("tracks").toObject();
    auto finish = [=](const QJsonArray &items) {
      QJsonArray tracks;
      for (const QJsonValue &v : items) {
        QJsonObject t = v.toObject();
        if (t.isEmpty() || t.value("is_local").toBool()) continue;
        t["album"] = cover;
        tracks.append(mapTrack(t));
      }
      done(QJsonObject{{"tracks", tracks}, {"restricted", false}}, QString());
    };
    QString next = page.value("next").toString();
    if (next.isEmpty()) { finish(page.value("items").toArray()); return; }
    collectPages(next, QString(), 500, page.value("items").toArray(),
                 [=](const QJsonArray &items, const QString &) { finish(items); });
  });
}

void Engine::spotifyTracks(const QString &id, const Done &done) {
  if (id == "recent") { spotifyRecent(done); return; }
  if (id.startsWith("album:")) {
    QString albumId = id.mid(6);
    if (!QRegularExpression("^[A-Za-z0-9]{1,64}$").match(albumId).hasMatch()) { done({}, "Bad album id"); return; }
    spotifyAlbum(albumId, done);
    return;
  }
  bool liked = id == "liked";
  if (!liked && !QRegularExpression("^[A-Za-z0-9]{1,64}$").match(id).hasMatch()) { done({}, "Bad playlist id"); return; }
  QString url = liked ? "/me/tracks?limit=50" : "/playlists/" + id + "/items?limit=100";
  collectPages(url, QString(), liked ? 500 : 500, {}, [=](const QJsonArray &items, const QString &error) {
    if (!error.isEmpty() && items.isEmpty()) {
      // Since 2026, apps in development mode only see the songs of playlists
      // the user owns or collaborates on. The playlist still plays.
      if (error.contains("403") || error.contains("orbidden")) { done(QJsonObject{{"tracks", QJsonArray()}, {"restricted", true}}, QString()); return; }
      done({}, error);
      return;
    }
    QJsonArray tracks;
    for (const QJsonValue &v : items) {
      QJsonObject wrap = v.toObject();
      // The 2026 API calls it "item"; older answers say "track".
      QJsonObject t = wrap.contains("item") ? wrap.value("item").toObject() : wrap.value("track").toObject();
      if (t.isEmpty() || t.value("is_local").toBool() || t.value("type").toString() == "episode") continue;
      tracks.append(mapTrack(t));
    }
    done(QJsonObject{{"tracks", tracks}, {"restricted", items.isEmpty() && !liked}}, QString());
  });
}

void Engine::spotifySearch(const QString &q, const Done &done) {
  QString query = q.trimmed().left(200);
  if (query.isEmpty()) { done(QJsonObject(), QString()); return; }
  QUrlQuery params;
  params.addQueryItem("q", QString::fromUtf8(QUrl::toPercentEncoding(query)));
  params.addQueryItem("type", "track,playlist");
  params.addQueryItem("limit", "10");
  m_spotify->api("GET", "/search?" + params.query(QUrl::FullyEncoded), {}, [=](const QJsonDocument &doc, const QString &error) {
    if (!error.isEmpty()) { done({}, error); return; }
    QJsonObject root = doc.object();
    QJsonArray tracks, playlists;
    for (const QJsonValue &v : root.value("tracks").toObject().value("items").toArray())
      if (v.isObject()) tracks.append(mapTrack(v.toObject()));
    for (const QJsonValue &v : root.value("playlists").toObject().value("items").toArray()) {
      if (!v.isObject()) continue;
      QJsonObject p = v.toObject();
      playlists.append(QJsonObject{
        {"id", p.value("id").toString()}, {"uri", p.value("uri").toString()},
        {"name", util::clip(p.value("name").toString(), 120)},
        {"owner", util::clip(p.value("owner").toObject().value("display_name").toString(), 80)},
        {"image", smallestImage(p.value("images").toArray())},
        {"own", p.value("owner").toObject().value("id").toString() == m_meId},
      });
    }
    done(QJsonObject{{"tracks", tracks}, {"playlists", playlists}}, QString());
  });
}

void Engine::spotifyPlay(const QJsonObject &args, const Done &done) {
  QJsonObject body;
  QString context = args.value("context").toString();
  QString uri = args.value("uri").toString();
  QJsonArray uris = args.value("uris").toArray();
  static const QRegularExpression spotifyUri("^spotify:(track|playlist|album|artist):[A-Za-z0-9]{1,64}$");
  if (!context.isEmpty()) {
    if (!spotifyUri.match(context).hasMatch()) { done({}, "Bad context"); return; }
    body["context_uri"] = context;
    if (!uri.isEmpty()) body["offset"] = QJsonObject{{"uri", uri}};
  } else if (!uris.isEmpty()) {
    // A list without a Spotify context (Liked Songs, search results): send a
    // window starting at the chosen song.
    int index = qBound(0, args.value("index").toInt(), int(uris.size()) - 1);
    QJsonArray window;
    for (int i = index; i < uris.size() && window.size() < 100; i++) {
      QString u = uris[i].toString();
      if (spotifyUri.match(u).hasMatch()) window.append(u);
    }
    body["uris"] = window;
  } else if (!uri.isEmpty() && spotifyUri.match(uri).hasMatch()) {
    body["uris"] = QJsonArray{uri};
  } else {
    done({}, "Nothing to play");
    return;
  }

  m_player->call("sunoPause");
  m_player->call("spActivate");
  whenSpotifyReady([=](const QString &error) {
    if (!error.isEmpty()) { done({}, error); return; }
    m_spotify->api("PUT", "/me/player/play?device_id=" + m_device, QJsonDocument(body), [=](const QJsonDocument &, const QString &err) {
      if (!err.isEmpty()) { setError("spotify", err); done({}, err); return; }
      m_errors.remove("spotify");
      m_source = "spotify";
      done(true, QString());
    });
  });
}

// ---------------------------------------------------------------- Suno

void Engine::sunoPlaylists(const Done &done) {
  m_suno->api("/api/playlist/me?page=1&show_trashed=false&show_sharelist=false", [=](const QJsonDocument &doc, const QString &error) {
    QJsonArray out;
    out.append(QJsonObject{{"id", "liked"}, {"name", "Liked"}});
    out.append(QJsonObject{{"id", "mine"}, {"name", "My songs"}});
    for (const QJsonValue &v : doc.object().value("playlists").toArray()) {
      QJsonObject p = v.toObject();
      QString id = p.value("id").toString();
      if (id.isEmpty()) continue;
      out.append(QJsonObject{
        {"id", id}, {"name", util::clip(p.value("name").toString(), 120)},
        {"image", p.value("image_url").toString()},
        {"count", p.value("num_total_results").toInt()},
      });
    }
    if (!error.isEmpty() && !m_suno->loggedIn()) { done({}, error); return; }
    // The two built-in lists still work if the playlist call broke.
    if (!error.isEmpty()) setError("suno", error);
    done(out, QString());
  });
}

static QJsonObject mapClip(const QJsonObject &c) {
  QString audio = c.value("audio_url").toString();
  if (!audio.startsWith("https://")) return {};
  QString status = c.value("status").toString();
  if (!status.isEmpty() && status != "complete" && status != "streaming") return {};
  return {
    {"id", c.value("id").toString()},
    {"title", util::clip(c.value("title").toString().isEmpty() ? QString("Untitled") : c.value("title").toString())},
    {"artist", util::clip(c.value("display_name").toString())},
    {"audio", audio},
    {"image", c.value("image_url").toString()},
    {"duration", qint64(c.value("metadata").toObject().value("duration").toDouble() * 1000)},
  };
}

void Engine::sunoPages(const QString &id, int page, QJsonArray acc,
                       const std::function<void(const QJsonArray &, const QString &)> &cb) {
  QString path;
  if (id == "liked") path = QString("/api/feed/v2?is_liked=true&page=%1").arg(page);
  else if (id == "mine") path = QString("/api/feed/v2?page=%1").arg(page);
  else path = QString("/api/playlist/%1/?page=%2").arg(id).arg(page + 1);
  m_suno->api(path, [=](const QJsonDocument &doc, const QString &error) mutable {
    if (!error.isEmpty()) { cb(acc, error); return; }
    QJsonObject root = doc.object();
    QJsonArray clips = root.contains("playlist_clips") ? root.value("playlist_clips").toArray() : root.value("clips").toArray();
    int before = acc.size();
    for (const QJsonValue &v : clips) {
      QJsonObject c = v.toObject();
      if (c.contains("clip")) c = c.value("clip").toObject();
      QJsonObject m = mapClip(c);
      if (!m.isEmpty()) acc.append(m);
    }
    // Ten pages is a lot of songs, and keeps a broken pager from looping.
    if (clips.isEmpty() || acc.size() == before || page >= 9 || acc.size() >= 500) { cb(acc, QString()); return; }
    sunoPages(id, page + 1, acc, cb);
  });
}

void Engine::sunoTracks(const QString &id, const Done &done) {
  if (!QRegularExpression("^[A-Za-z0-9-]{1,64}$").match(id).hasMatch()) { done({}, "Bad playlist id"); return; }
  sunoPages(id, 0, {}, [=](const QJsonArray &tracks, const QString &error) {
    if (!error.isEmpty() && tracks.isEmpty()) { done({}, error); return; }
    m_sunoLists[id] = tracks;
    done(QJsonObject{{"tracks", tracks}}, QString());
  });
}

void Engine::sunoPlayIndex(int index) {
  if (index < 0 || index >= m_sunoQueue.size()) return;
  QJsonObject t = m_sunoQueue[index].toObject();
  m_sunoIndex = index;
  if (m_source == "spotify" || !m_device.isEmpty()) m_player->call("spPause");
  m_source = "suno";
  m_now = QJsonObject{
    {"source", "suno"}, {"id", t.value("id").toString()},
    {"title", t.value("title").toString()}, {"artist", t.value("artist").toString()},
    {"album", QString()}, {"image", t.value("image").toString()},
  };
  m_duration = t.value("duration").toInteger();
  m_position = 0;
  m_positionAt = nowMs();
  m_playing = true;
  m_canNext = index + 1 < m_sunoQueue.size();
  m_canPrev = true;
  m_context = "suno:" + m_sunoQueueId;
  m_errors.remove("suno");
  m_player->call("sunoPlay", {t.value("audio").toString()});
  stateChanged();
}

// ---------------------------------------------------------------- transport

void Engine::toggle() { m_playing ? pause() : play(); }

void Engine::play() {
  if (m_source == "suno") m_player->call("sunoResume");
  else if (m_source == "spotify") m_player->call("spResume");
}

void Engine::pause() {
  if (m_source == "suno") m_player->call("sunoPause");
  else if (m_source == "spotify") m_player->call("spPause");
}

void Engine::next() {
  if (m_source == "suno") sunoPlayIndex(m_sunoIndex + 1);
  else if (m_source == "spotify") m_player->call("spNext");
}

void Engine::previous() {
  qint64 pos = m_position + (m_playing ? nowMs() - m_positionAt : 0);
  if (m_source == "suno") {
    if (pos > 3000 || m_sunoIndex == 0) seek(0);
    else sunoPlayIndex(m_sunoIndex - 1);
  } else if (m_source == "spotify") {
    m_player->call("spPrev");
  }
}

void Engine::seek(qint64 ms) {
  if (m_source == "suno") m_player->call("sunoSeek", {double(ms)});
  else if (m_source == "spotify") m_player->call("spSeek", {double(ms)});
  m_position = ms;
  m_positionAt = nowMs();
  stateChanged();
}
