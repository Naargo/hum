#include "player.h"

#include <QFile>
#include <QTimer>
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

  reload();
}

void Player::reload() {
  m_unloaded = false;
  setLifecycleState(LifecycleState::Active);
  QFile f(":/player.html");
  f.open(QIODevice::ReadOnly);
  // Widevine and the SDK both want a secure origin; nothing is fetched from it.
  setHtml(QString::fromUtf8(f.readAll()), QUrl("https://hum.localhost/"));
}

void Player::unload() {
  m_unloaded = true;
  setHtml("<!doctype html>", QUrl("https://hum.localhost/"));
  // Discarded lets Chromium drop the page's process entirely until
  // reload() brings it back.
  QTimer::singleShot(500, this, [this] {
    if (!m_unloaded) return;
    setLifecycleState(LifecycleState::Discarded);
    if (qEnvironmentVariableIsSet("HUM_DEBUG"))
      qInfo("player: discard -> state %d (recommended %d)", int(lifecycleState()), int(recommendedState()));
  });
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
