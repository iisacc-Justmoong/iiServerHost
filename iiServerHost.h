#pragma once

#include <QtCore/QString>
#include "iiServerHostExport.h"

namespace iiServerHost {

/// Compatibility greeting for existing consumers.
[[nodiscard]] IISERVERHOST_EXPORT QString helloWorld();

} // namespace iiServerHost

#include "ServerHost.h"
#include "PairingLink.h"
