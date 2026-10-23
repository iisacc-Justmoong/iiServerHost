#pragma once
#include <QtCore/qglobal.h>
#if defined(IIWEBSERVER_STATIC)
#  define IIWEBSERVER_EXPORT
#elif defined(IIWEBSERVER_BUILDING_LIBRARY)
#  define IIWEBSERVER_EXPORT Q_DECL_EXPORT
#else
#  define IIWEBSERVER_EXPORT Q_DECL_IMPORT
#endif
