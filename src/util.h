#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace util {

// ~/.local/share/hum: tokens and the web profile (cookies, Widevine licences).
QString dataDir();
// $XDG_RUNTIME_DIR/hum.sock: where the bar panel finds the engine.
QString socketPath();

QJsonObject readJson(const QString &path);
// Writes beside the target and renames, so a crash never leaves half a token
// file. Private files are readable by the owner only.
bool writeJson(const QString &path, const QJsonObject &obj, bool privateFile);

QString randomString(int length);
QByteArray base64Url(const QByteArray &bytes);

// Cuts untrusted text from the network down to a sane length before it
// reaches the panel.
QString clip(const QString &text, int max = 300);

}
