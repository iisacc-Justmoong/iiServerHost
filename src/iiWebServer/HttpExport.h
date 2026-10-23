#pragma once

#if defined(IIWEBSERVER_HTTP_STATIC)
#  define IIWEBSERVER_HTTP_EXPORT
#elif defined(_WIN32)
#  if defined(IIWEBSERVER_HTTP_BUILDING_LIBRARY)
#    define IIWEBSERVER_HTTP_EXPORT __declspec(dllexport)
#  else
#    define IIWEBSERVER_HTTP_EXPORT __declspec(dllimport)
#  endif
#else
#  define IIWEBSERVER_HTTP_EXPORT __attribute__((visibility("default")))
#endif
