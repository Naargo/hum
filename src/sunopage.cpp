#include "sunopage.h"

#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>

namespace {
// Nobody sees this page, so it skips what only matters on screen (images,
// fonts, video) and the analytics and tracking that ride along with the
// site. Scripts, data requests and audio from Suno itself all pass.
class Trim : public QWebEngineUrlRequestInterceptor {
public:
  using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;
  void interceptRequest(QWebEngineUrlRequestInfo &info) override {
    switch (info.resourceType()) {
    case QWebEngineUrlRequestInfo::ResourceTypeImage:
    case QWebEngineUrlRequestInfo::ResourceTypeFavicon:
    case QWebEngineUrlRequestInfo::ResourceTypeFontResource:
    case QWebEngineUrlRequestInfo::ResourceTypePing:
    case QWebEngineUrlRequestInfo::ResourceTypeCspReport:
    // suno.com's service worker runs in a page process of its own (~36 MB)
    // and does nothing for playback.
    case QWebEngineUrlRequestInfo::ResourceTypeServiceWorker:
      info.block(true);
      return;
    default:
      break;
    }
    static const QStringList trackers = {
      "statsig", "sentry", "google-analytics", "googletagmanager", "doubleclick",
      "segment.", "datadoghq", "hotjar", "facebook.", "tiktok", "intercom",
      "amplitude", "mixpanel", "clarity.ms", "posthog", "rudderstack", "hs-scripts",
    };
    QString host = info.requestUrl().host();
    for (const QString &t : trackers) if (host.contains(t)) { info.block(true); return; }
  }
};
}  // namespace

SunoPage::SunoPage(QWebEngineProfile *profile, QObject *parent) : QWebEnginePage(profile, parent) {
  QWebEngineSettings *s = settings();
  // Never shown, and it has to start sound without a click.
  s->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
  s->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
  s->setAttribute(QWebEngineSettings::PluginsEnabled, false);
  s->setAttribute(QWebEngineSettings::PdfViewerEnabled, false);
  s->setAttribute(QWebEngineSettings::WebGLEnabled, false);
  s->setAttribute(QWebEngineSettings::AutoLoadImages, false);
  s->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, false);
  if (qEnvironmentVariableIsSet("HUM_MUTE")) setAudioMuted(true);
  setUrlRequestInterceptor(new Trim(this));

  QFile f(":/suno.js");
  f.open(QIODevice::ReadOnly);
  m_script = QString::fromUtf8(f.readAll());
  // Each song is its own page load; drive it once it's there.
  connect(this, &QWebEnginePage::loadFinished, this, [this](bool ok) {
    if (!ok) { emit message({{"t", "error"}, {"source", "suno"}, {"message", "Couldn't open the song on suno.com"}}); return; }
    runJavaScript(m_script);
  });
}

void SunoPage::playSong(const QString &id) {
  static const QRegularExpression ok("^[A-Za-z0-9-]{1,64}$");
  if (!ok.match(id).hasMatch()) return;
  load(QUrl("https://suno.com/song/" + id));
}

void SunoPage::call(const QString &function, const QJsonArray &args) {
  QString json = QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
  runJavaScript(QString("window.hum && hum.%1.apply(null, %2)").arg(function, json));
}

void SunoPage::javaScriptConsoleMessage(JavaScriptConsoleMessageLevel, const QString &text, int, const QString &) {
  if (!text.startsWith("HUM ")) {
    if (qEnvironmentVariableIsSet("HUM_DEBUG")) qInfo("suno page: %s", qPrintable(text.left(400)));
    return;
  }
  QJsonObject msg = QJsonDocument::fromJson(text.mid(4).toUtf8()).object();
  if (!msg.isEmpty()) emit message(msg);
}
