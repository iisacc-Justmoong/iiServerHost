#pragma once
#include "iiWebServerExport.h"
#include <QUrl>
#include <QString>

namespace iiWebServer {
// An untrusted QR payload. Parsing never opens a URL or supplies credentials.
struct IIWEBSERVER_EXPORT PairingLink {
    QUrl relayUrl;
    QString hostId;
    QString code;
    QString encode() const;
    static bool decode(const QString &text, PairingLink *result);
};
}
