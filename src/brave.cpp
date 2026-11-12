#include "brave.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QPasswordDigestor>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <openssl/evp.h>

namespace {

// The keyring password Brave encrypts cookies with. Chromium on Linux turns
// it into an AES-128 key with one round of PBKDF2 over a fixed salt.
QByteArray cookieKey(QString *error) {
  QProcess p;
  p.start("secret-tool", {"lookup", "application", "brave"});
  if (!p.waitForFinished(5000) || p.exitCode() != 0) {
    *error = "Couldn't read Brave's key from the keyring";
    return {};
  }
  QByteArray password = p.readAllStandardOutput().trimmed();
  if (password.isEmpty()) { *error = "Brave's keyring entry is empty"; return {}; }
  return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha1, password, "saltysalt", 1, 16);
}

// "v11" + AES-128-CBC(PKCS#7) with an IV of 16 spaces.
QByteArray decrypt(const QByteArray &blob, const QByteArray &key) {
  if (!blob.startsWith("v11") && !blob.startsWith("v10")) return {};
  QByteArray data = blob.mid(3);
  QByteArray iv(16, ' ');
  QByteArray out(data.size() + 16, '\0');
  int len = 0, tail = 0;
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  bool ok = ctx
    && EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr,
                          reinterpret_cast<const unsigned char *>(key.constData()),
                          reinterpret_cast<const unsigned char *>(iv.constData())) == 1
    && EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()), &len,
                         reinterpret_cast<const unsigned char *>(data.constData()), int(data.size())) == 1
    && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(out.data()) + len, &tail) == 1;
  EVP_CIPHER_CTX_free(ctx);
  if (!ok) return {};
  out.truncate(len + tail);
  return out;
}

// Chrome's cookie times count microseconds from 1601.
QDateTime fromChromeTime(qint64 us) {
  if (us <= 0) return {};
  return QDateTime::fromSecsSinceEpoch(us / 1000000 - 11644473600LL);
}

}  // namespace

namespace brave {

QList<QNetworkCookie> cookies(const QString &site, QString *error) {
  QString source = QDir::homePath() + "/.config/BraveSoftware/Brave-Browser/Default/Cookies";
  if (!QFile::exists(source)) { *error = "Brave isn't installed, or has no profile yet"; return {}; }

  QByteArray key = cookieKey(error);
  if (key.isEmpty()) return {};

  // Brave keeps its database locked while it runs, so read a copy.
  QTemporaryDir tmp;
  QString copy = tmp.path() + "/Cookies";
  if (!tmp.isValid() || !QFile::copy(source, copy)) { *error = "Couldn't copy Brave's cookies"; return {}; }
  QFile::copy(source + "-journal", copy + "-journal");

  QList<QNetworkCookie> out;
  const QString conn = "hum-brave-cookies";
  {
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
    db.setDatabaseName(copy);
    if (!db.open()) { *error = "Couldn't open Brave's cookies: " + db.lastError().text(); }
    else {
      // From database version 24 on, Chromium prefixes each value with a
      // SHA-256 of its host before encrypting it.
      int version = 0;
      QSqlQuery meta("SELECT value FROM meta WHERE key = 'version'", db);
      if (meta.next()) version = meta.value(0).toInt();

      QSqlQuery q(db);
      q.prepare("SELECT host_key, name, path, encrypted_value, expires_utc, is_secure, is_httponly "
                "FROM cookies WHERE host_key = :site OR host_key LIKE :sub");
      q.bindValue(":site", site);
      q.bindValue(":sub", "%." + site);
      if (!q.exec()) *error = "Couldn't read Brave's cookies: " + q.lastError().text();
      while (q.next()) {
        QByteArray value = decrypt(q.value(3).toByteArray(), key);
        if (value.isEmpty()) continue;
        if (version >= 24 && value.size() >= 32) value = value.mid(32);
        QNetworkCookie c(q.value(1).toByteArray(), value);
        c.setDomain(q.value(0).toString());
        c.setPath(q.value(2).toString());
        c.setExpirationDate(fromChromeTime(q.value(4).toLongLong()));
        c.setSecure(q.value(5).toBool());
        c.setHttpOnly(q.value(6).toBool());
        out << c;
      }
      db.close();
    }
  }
  QSqlDatabase::removeDatabase(conn);
  if (out.isEmpty() && error->isEmpty()) *error = "Brave isn't signed in to " + site;
  return out;
}

}  // namespace brave
