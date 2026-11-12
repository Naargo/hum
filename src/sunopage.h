#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QWebEnginePage>
#include <functional>

// A hidden suno.com page. Suno only lets its own site play its audio, so
// Suno songs play through Suno's own player on this page, signed in with
// the engine's shared web profile. It exists only while Suno is playing.
//
// Like the Spotify page, it talks back through console messages that start
// with "HUM ", each a JSON object.
class SunoPage : public QWebEnginePage {
  Q_OBJECT
public:
  explicit SunoPage(QWebEngineProfile *profile, QObject *parent = nullptr);

  // Open a song's page and start it with Suno's own Play button.
  void playSong(const QString &id);
  // pause, resume, seek(ms): see suno.js.
  void call(const QString &function, const QJsonArray &args = QJsonArray());

signals:
  void message(const QJsonObject &msg);

protected:
  void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &text,
                                int line, const QString &source) override;

private:
  QString m_script;  // suno.js
};
