#pragma once

#include <iiWebServer/HttpServer.h>

#include <QtCore/QString>
#include "iiWebServerExport.h"

namespace iiWebServer {

/// Compatibility greeting for existing consumers.
[[nodiscard]] IIWEBSERVER_EXPORT QString helloWorld();

} // namespace iiWebServer

#include "WebServer.h"
#include "PairingLink.h"
#include "LanPeer.h"
#include "Transfer.h"
#include "StorageBridge.h"
