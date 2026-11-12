#pragma once

#include <QList>
#include <QNetworkCookie>
#include <QString>

// Reads cookies for one site out of Brave's cookie store, so a sign-in done
// in the browser can be reused here. Brave (like Chrome) encrypts cookie
// values with a key it keeps in the desktop keyring as "Brave Safe Storage".
// Returns the cookies whose host ends with `site` (e.g. "suno.com"); on
// failure returns none and says why in *error.
namespace brave {
QList<QNetworkCookie> cookies(const QString &site, QString *error);
}
