#pragma once
#include "iiServerHostExport.h"
#include <QUrl>
#include <QString>

namespace iiServerHost {
// An untrusted QR payload. Parsing never opens a URL or supplies credentials.
struct IISERVERHOST_EXPORT PairingLink {
    QUrl relayUrl;
    QString hostId;
    QString code;
    QString encode() const;
    static bool decode(const QString &text, PairingLink *result);
};
}
