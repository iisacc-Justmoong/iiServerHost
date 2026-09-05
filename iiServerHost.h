#pragma once

#include <QtCore/QString>
#include <QtCore/qglobal.h>

#if defined(IISERVERHOST_BUILDING_LIBRARY)
#  define IISERVERHOST_EXPORT Q_DECL_EXPORT
#else
#  define IISERVERHOST_EXPORT Q_DECL_IMPORT
#endif

namespace iiServerHost {

/// Returns the placeholder greeting; no domain functionality is implemented.
[[nodiscard]] IISERVERHOST_EXPORT QString helloWorld();

} // namespace iiServerHost
