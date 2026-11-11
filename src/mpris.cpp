#include "mpris.h"

#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>

static const char *objectPath = "/org/mpris/MediaPlayer2";

Mpris::Mpris(QObject *parent) : QObject(parent) {
  new MprisRoot(this);
  new MprisPlayer(this);
  QDBusConnection bus = QDBusConnection::sessionBus();
  bus.registerObject(objectPath, this);
  bus.registerService("org.mpris.MediaPlayer2.hum");
}

qint64 Mpris::positionMs() const {
  qint64 pos = m_state.positionMs;
  if (m_state.playing) pos += QDateTime::currentMSecsSinceEpoch() - m_positionAt;
  return m_state.durationMs > 0 ? qMin(pos, m_state.durationMs) : pos;
}

QString MprisPlayer::playbackStatus() const {
  if (!m->m_state.hasTrack) return "Stopped";
  return m->m_state.playing ? "Playing" : "Paused";
}

QVariantMap MprisPlayer::metadata() const {
  const Mpris::State &s = m->m_state;
  if (!s.hasTrack) return {};
  QString hash = QString::fromLatin1(QCryptographicHash::hash(s.trackId.toUtf8(), QCryptographicHash::Md5).toHex());
  QVariantMap md;
  md["mpris:trackid"] = QVariant::fromValue(QDBusObjectPath("/hum/track/t" + hash));
  md["mpris:length"] = qlonglong(s.durationMs * 1000);
  md["xesam:title"] = s.title;
  md["xesam:artist"] = QStringList{s.artist};
  md["xesam:album"] = s.album;
  if (!s.art.isEmpty()) md["mpris:artUrl"] = s.art;
  return md;
}

void Mpris::update(const State &s) {
  State old = m_state;
  qint64 oldAt = m_positionAt;
  m_state = s;
  m_positionAt = QDateTime::currentMSecsSinceEpoch();

  QVariantMap props;
  MprisPlayer *p = findChild<MprisPlayer *>();
  if (old.playing != s.playing || old.hasTrack != s.hasTrack) props["PlaybackStatus"] = p->playbackStatus();
  if (old.trackId != s.trackId || old.title != s.title || old.art != s.art || old.durationMs != s.durationMs || old.hasTrack != s.hasTrack)
    props["Metadata"] = p->metadata();
  if (old.canNext != s.canNext) props["CanGoNext"] = s.canNext;
  if (old.canPrev != s.canPrev) props["CanGoPrevious"] = s.canPrev;
  if (old.hasTrack != s.hasTrack) {
    props["CanPlay"] = s.hasTrack;
    props["CanPause"] = s.hasTrack;
    props["CanSeek"] = s.hasTrack;
  }
  if (!props.isEmpty()) changed(props);
  // A jump in position (a seek, or a new track) is announced; normal playback
  // is extrapolated by whoever is listening.
  qint64 expected = old.positionMs + (old.playing ? m_positionAt - oldAt : 0);
  if (old.trackId == s.trackId && qAbs(s.positionMs - expected) > 3000) emit p->Seeked(s.positionMs * 1000);
}

void Mpris::changed(const QVariantMap &props) {
  QDBusMessage msg = QDBusMessage::createSignal(objectPath, "org.freedesktop.DBus.Properties", "PropertiesChanged");
  msg << QString("org.mpris.MediaPlayer2.Player") << props << QStringList();
  QDBusConnection::sessionBus().send(msg);
}
