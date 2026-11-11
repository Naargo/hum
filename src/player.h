#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QWebEnginePage>

// The hidden page that makes the sound. Spotify only hands its audio to a
// browser engine with Widevine, so its Web Playback SDK runs here and shows
// up in Spotify Connect as a device. Suno songs are plain MP3s and play
// through an <audio> element on the same page, so one engine covers both.
//
// The page talks back through console messages starting with "HUM ", each a
// JSON object; the engine talks to it with runJavaScript. That keeps the
// bridge one file each way, without QWebChannel.
class Player : public QWebEnginePage {
  Q_OBJECT
public:
  Player(QWebEngineProfile *profile, QObject *parent = nullptr);

  void call(const QString &function, const QJsonArray &args = QJsonArray());

signals:
  void message(const QJsonObject &msg);

protected:
  void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &text,
                                int line, const QString &source) override;
};
