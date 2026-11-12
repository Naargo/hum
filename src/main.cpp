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
    // No --renderer-process-limit=1: with Spotify's and Suno's pages taking
    // turns, it made Spotify's page move into Suno's leftover process and
    // keep all of Suno's memory.
    // SpareRendererForSitePerProcess and site isolation each start extra
    // page processes (a spare kept warm; suno.com's sign-in and payment
    // frames in their own). For two known pages that's memory for nothing.
    "--disable-features=BackForwardCache,MediaRouter,OptimizationHints,Translate,"
      "SpareRendererForSitePerProcess,IsolateOrigins,site-per-process",
    "--disable-site-isolation-trials",
    "--disable-background-networking",
    // Chromium's settings for low-memory phones: smaller caches and budgets
    // everywhere. V8's lite mode skips the optimizing compiler, which costs
    // speed these pages don't need (they only play music) and saves memory.
    "--enable-low-end-device-mode",
    "--js-flags=--max-old-space-size=64,--lite-mode,--optimize-for-size",
  };
  // Spotify's audio is Widevine-protected. Brave, Chrome and Chromium each
  // keep a copy of Google's Widevine module; Qt WebEngine can use it when
  // told where it is. The first browser with a copy wins, newest version.
  const QStringList dirs = {
    "/.config/BraveSoftware/Brave-Browser/WidevineCdm",
    "/.config/google-chrome/WidevineCdm",
    "/.config/chromium/WidevineCdm",
  };
  QString newest;
  for (const QString &dir : dirs) {
    QDirIterator it(QDir::homePath() + dir, {"libwidevinecdm.so"}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) { QString p = it.next(); if (p > newest) newest = p; }
    if (!newest.isEmpty()) break;
  }
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
