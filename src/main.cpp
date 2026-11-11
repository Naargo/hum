#include "engine.h"
#include "util.h"

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QLocalSocket>
#include <cstdio>

// Chromium flags that keep the hidden player small. One renderer is plenty
// for one page, and there's nothing to draw, so no GPU process either.
static void setChromiumFlags() {
  if (qEnvironmentVariableIsSet("QTWEBENGINE_CHROMIUM_FLAGS")) return;
  QStringList flags{
    "--disable-gpu",
    "--renderer-process-limit=1",
    "--disable-features=BackForwardCache,MediaRouter,OptimizationHints,Translate",
    "--disable-background-networking",
    "--js-flags=--max-old-space-size=96",
  };
  // Spotify's audio is Widevine-protected. Brave keeps a copy of Google's
  // Widevine module; Qt WebEngine can use it when told where it is.
  QDirIterator it(QDir::homePath() + "/.config/BraveSoftware/Brave-Browser/WidevineCdm",
                  {"libwidevinecdm.so"}, QDir::Files, QDirIterator::Subdirectories);
  QString newest;
  while (it.hasNext()) { QString p = it.next(); if (p > newest) newest = p; }
  if (!newest.isEmpty()) flags << "--widevine-path=" + newest;
  qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags.join(' ').toUtf8());
}

int main(int argc, char **argv) {
  // One engine per user: if one answers, there's nothing to do.
  {
    QLocalSocket probe;
    probe.connectToServer(util::socketPath());
    if (probe.waitForConnected(300)) return 0;
  }

  setChromiumFlags();
  QApplication app(argc, argv);
  app.setApplicationName("hum");
  app.setDesktopFileName("hum");
  // The Suno sign-in window closing must not end the engine.
  app.setQuitOnLastWindowClosed(false);

  Engine engine;
  QString error;
  if (!engine.listen(&error)) {
    fprintf(stderr, "hum: can't listen on %s: %s\n", qPrintable(util::socketPath()), qPrintable(error));
    return 1;
  }
  return app.exec();
}
