#pragma once
#include <QtCore/qglobal.h>
#if defined(IISERVERHOST_STATIC)
#  define IISERVERHOST_EXPORT
#elif defined(IISERVERHOST_BUILDING_LIBRARY)
#  define IISERVERHOST_EXPORT Q_DECL_EXPORT
#else
#  define IISERVERHOST_EXPORT Q_DECL_IMPORT
#endif
