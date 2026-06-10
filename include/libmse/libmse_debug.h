#ifndef MSE_DEBUG_H
#define MSE_DEBUG_H

#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

#include "libmse_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DEBUG_LOG_SOURCE
#define DEBUG_LOG_SOURCE "backend"
#endif

#define DEBUG_USE_COLOR
//#define DEBUG_ASSERT_EXITS

#define DEBUG_INFO(...) \
    libmse_debug_write_log(DEBUG_LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG_WARN(...) \
    libmse_debug_write_log(DEBUG_LOG_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG_ERROR(...) \
    libmse_debug_write_log(DEBUG_LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG_FATAL(...) \
    libmse_debug_write_log(DEBUG_LOG_LEVEL_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#ifndef NDEBUG
#define DEBUG
#endif

#ifdef DEBUG
#define DEBUG_DEBUG(...) \
    libmse_debug_write_log(DEBUG_LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)

#ifdef DEBUG_ASSERT_EXITS
#define DEBUG_ASSERT(x) \
    do { \
        if (!(x)) { \
            libmse_debug_write_log(DEBUG_LOG_LEVEL_ASSERT, __FILE__, __LINE__, "ASSERTION FAILED: %s", #x); \
            libmse_debug_flush_all(); \
            exit(-1); \
        } \
    } while (0)
#else
#define DEBUG_ASSERT(x) \
    do { \
        if (!(x)) { \
            libmse_debug_write_log(DEBUG_LOG_LEVEL_ASSERT, __FILE__, __LINE__, "ASSERTION FAILED: %s", #x); \
        } \
    } while (0)
#endif

#define DEBUG_TRACE() \
    libmse_debug_write_log(DEBUG_LOG_LEVEL_TRACE, __FILE__, __LINE__, "FUNCTION: %s()", __func__)
#else
#define DEBUG_DEBUG(...)
#define DEBUG_ASSERT(x)
#define DEBUG_TRACE()
#endif

typedef enum DEBUG_LOG_LEVEL {
    DEBUG_LOG_LEVEL_TRACE = 0,
    DEBUG_LOG_LEVEL_DEBUG = 1,
    DEBUG_LOG_LEVEL_INFO = 2,
    DEBUG_LOG_LEVEL_WARN = 3,
    DEBUG_LOG_LEVEL_ERROR = 4,
    DEBUG_LOG_LEVEL_FATAL = 5,
    DEBUG_LOG_LEVEL_ASSERT = 6,
} DEBUG_LOG_LEVEL;

typedef struct debug_log_s {
    DEBUG_LOG_LEVEL level;
    const char *origin;
    char message[512];
    char file[128];
    struct {
        time_t tv_sec;
        long tv_usec;
    } tp;
    int line;
} debug_log;

typedef void (*debug_log_callback)(const char *message);

LIBMSE_API void libmse_debug_write_log(int level, const char *file, int line, const char *fmt, ...);
LIBMSE_API int libmse_debug_register_buffer(FILE *buffer);
LIBMSE_API int libmse_debug_register_callback(debug_log_callback callback);
LIBMSE_API void libmse_debug_flush_all(void);

LIBMSE_API void libmse_debug_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif // MSE_DEBUG_H