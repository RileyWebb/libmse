#ifndef LIBMSE_VERSION_H
#define LIBMSE_VERSION_H

#include "libmse_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LIBMSE_VERSION_MAJOR 0
#define LIBMSE_VERSION_MINOR 1
#define LIBMSE_VERSION_PATCH 0
#define LIBMSE_VERSION_STRING "0.1.0"
#ifdef DEBUG
#define LIBMSE_VERSION_STRING_LONG "libmse v" LIBMSE_VERSION_STRING "-debug"
#else
#define LIBMSE_VERSION_STRING_LONG "libmse v" LIBMSE_VERSION_STRING
#endif
#define LIBMSE_VERSION_BUILD_DATE __DATE__
#define LIBMSE_VERSION_BUILD_STRING LIBMSE_VERSION_STRING_LONG " (" LIBMSE_VERSION_BUILD_DATE ")"

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_VERSION_H