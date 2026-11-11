#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QVariantMap>

// MPRIS, so media keys, Omarchy's media widget and SUPER+SHIFT+M
// (media.playPause) work the same as with any other player.
class Mpris : public QObject {
  Q_OBJECT
public:
  explicit Mpris(QObject *parent = nullptr);

  struct State {
    bool playing = false;
    bool hasTrack = false;
    QString trackId;  // any stable string; turned into an object path
    QString title, artist, album, art;
    qint64 durationMs = 0;
    qint64 positionMs = 0;
    bool canNext = false, canPrev = false;
  };
  void update(const State &s);
  qint64 positionMs() const;
  const State &state() const { return m_state; }

signals:
  void playPause();
  void play();
  void pause();
  void next();
  void previous();
  void seekTo(qint64 ms);

private:
  void changed(const QVariantMap &props);
  State m_state;
  qint64 m_positionAt = 0;
  friend class MprisPlayer;
};

class MprisRoot : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
  Q_PROPERTY(bool CanQuit READ no)
  Q_PROPERTY(bool CanRaise READ no)
  Q_PROPERTY(bool HasTrackList READ no)
  Q_PROPERTY(QString Identity READ identity)
  Q_PROPERTY(QString DesktopEntry READ desktopEntry)
  Q_PROPERTY(QStringList SupportedUriSchemes READ empty)
  Q_PROPERTY(QStringList SupportedMimeTypes READ empty)
public:
  explicit MprisRoot(QObject *parent) : QDBusAbstractAdaptor(parent) {}
  bool no() const { return false; }
  QString identity() const { return "Hum"; }
  QString desktopEntry() const { return "hum"; }
  QStringList empty() const { return {}; }
public slots:
  void Raise() {}
  void Quit() {}
};

class MprisPlayer : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
  Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
  Q_PROPERTY(double Rate READ one)
  Q_PROPERTY(double MinimumRate READ one)
  Q_PROPERTY(double MaximumRate READ one)
  Q_PROPERTY(double Volume READ one)
  Q_PROPERTY(QVariantMap Metadata READ metadata)
  Q_PROPERTY(qlonglong Position READ position)
  Q_PROPERTY(bool CanGoNext READ canNext)
  Q_PROPERTY(bool CanGoPrevious READ canPrev)
  Q_PROPERTY(bool CanPlay READ hasTrack)
  Q_PROPERTY(bool CanPause READ hasTrack)
  Q_PROPERTY(bool CanSeek READ hasTrack)
  Q_PROPERTY(bool CanControl READ yes)
public:
  MprisPlayer(Mpris *m) : QDBusAbstractAdaptor(m), m(m) {}
  QString playbackStatus() const;
  double one() const { return 1.0; }
  bool yes() const { return true; }
  QVariantMap metadata() const;
  qlonglong position() const { return m->positionMs() * 1000; }
  bool canNext() const { return m->m_state.canNext; }
  bool canPrev() const { return m->m_state.canPrev; }
  bool hasTrack() const { return m->m_state.hasTrack; }
public slots:
  void Next() { emit m->next(); }
  void Previous() { emit m->previous(); }
  void Pause() { emit m->pause(); }
  void PlayPause() { emit m->playPause(); }
  void Stop() { emit m->pause(); }
  void Play() { emit m->play(); }
  void Seek(qlonglong offsetUs) { emit m->seekTo(qMax<qint64>(0, m->positionMs() + offsetUs / 1000)); }
  void SetPosition(const QDBusObjectPath &, qlonglong us) { emit m->seekTo(us / 1000); }
  void OpenUri(const QString &) {}
signals:
  void Seeked(qlonglong position);
private:
  Mpris *m;
};
