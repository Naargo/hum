#include "util.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>

namespace util {

QString dataDir() {
  QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/hum";
  QDir().mkpath(dir);
  return dir;
}

QString socketPath() {
  QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
  if (runtime.isEmpty()) runtime = QDir::tempPath();
  return runtime + "/hum.sock";
}

QJsonObject readJson(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return {};
  return QJsonDocument::fromJson(f.readAll()).object();
}

bool writeJson(const QString &path, const QJsonObject &obj, bool privateFile) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return false;
  if (privateFile) f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
  return f.commit();
}

QString randomString(int length) {
  static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
  QString out;
  out.reserve(length);
  for (int i = 0; i < length; i++)
    out += QChar(chars[QRandomGenerator::system()->bounded(int(sizeof(chars) - 1))]);
  return out;
}

QByteArray base64Url(const QByteArray &bytes) {
  return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QString clip(const QString &text, int max) {
  QString t = text;
  t.replace('\n', ' ');
  return t.length() > max ? t.left(max) : t;
}

}
