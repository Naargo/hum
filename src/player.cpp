#include "player.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QWebEngineSettings>

Player::Player(QWebEngineProfile *profile, QObject *parent) : QWebEnginePage(profile, parent) {
  QWebEngineSettings *s = settings();
  // Nothing on this page is shown, and it never needs a click to start sound.
  s->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
  s->setAttribute(QWebEngineSettings::AutoLoadImages, false);
  s->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
  s->setAttribute(QWebEngineSettings::PluginsEnabled, false);
  s->setAttribute(QWebEngineSettings::PdfViewerEnabled, false);
  s->setAttribute(QWebEngineSettings::WebGLEnabled, false);
  s->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, false);
  // Tests run silent.
  if (qEnvironmentVariableIsSet("HUM_MUTE")) setAudioMuted(true);

  QFile f(":/player.html");
  f.open(QIODevice::ReadOnly);
  // Widevine and the SDK both want a secure origin; nothing is fetched from it.
  setHtml(QString::fromUtf8(f.readAll()), QUrl("https://hum.localhost/"));
}

void Player::call(const QString &function, const QJsonArray &args) {
  QString json = QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
  runJavaScript(QString("hum.%1.apply(null, %2)").arg(function, json));
}

void Player::javaScriptConsoleMessage(JavaScriptConsoleMessageLevel, const QString &text, int, const QString &) {
  if (!text.startsWith("HUM ")) {
    if (qEnvironmentVariableIsSet("HUM_DEBUG")) qInfo("page: %s", qPrintable(text.left(400)));
    return;
  }
  QJsonObject msg = QJsonDocument::fromJson(text.mid(4).toUtf8()).object();
  if (!msg.isEmpty()) emit message(msg);
}
