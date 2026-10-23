#pragma once
#include "NetworkTypes.h"
#include <QStringList>
namespace iiWebServer {
TlsConfiguration createLanIdentity(const QStringList &addresses);
}
