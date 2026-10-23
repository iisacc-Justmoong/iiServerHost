#pragma once
#include "iiWebServerExport.h"
#include <iiNetwork/Tls.h>
#include <QtCore/QString>

namespace iiWebServer {
using Address = ii::network::IpAddress;
using TlsConfiguration = ii::network::TlsOptions;
IIWEBSERVER_EXPORT QString certificateFingerprint(const TlsConfiguration &);
IIWEBSERVER_EXPORT bool hasIdentity(const TlsConfiguration &);
}
