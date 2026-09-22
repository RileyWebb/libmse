#ifndef LIBMSE_DEBUG_H
#define LIBMSE_DEBUG_H

#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

#include "libmse_api.h"
#include "libmse_log.h"

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(NDEBUG) || defined(DEBUG) || defined(_DEBUG)
#ifndef DEBUG
#define DEBUG
#endif
#endif

#ifdef DEBUG
#define DEBUG_DEBUG(...) \
    libmse_debug_write_log(DEBUG_LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG_INFO(...) \
    libmse_debug_write_log(DEBUG_LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG_TRACE() \
    libmse_debug_write_log(DEBUG_LOG_LEVEL_TRACE, __FILE__, __LINE__, "FUNCTION: %s()", __func__)
#else
#define DEBUG_DEBUG(...)
#define DEBUG_INFO(...)
#define DEBUG_TRACE()
#endif

#define DEBUG_WARN(...) \
    libmse_debug_write_log(LIBMSE_DEBUG_LOG_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG_ERROR(...) \
    libmse_debug_write_log(LIBMSE_DEBUG_LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define DEBUG_FATAL(...) \
    libmse_debug_write_log(LIBMSE_DEBUG_LOG_LEVEL_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#ifdef DEBUG_ASSERT_EXITS
#define DEBUG_ASSERT(x) \
    do { \
        if (!(x)) { \
            libmse_debug_write_log(LIBMSE_DEBUG_LOG_LEVEL_ASSERT, __FILE__, __LINE__, "ASSERTION FAILED: %s", #x); \
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

typedef enum libmse_debug_log_level_e {
    LIBMSE_DEBUG_LOG_LEVEL_TRACE,
    LIBMSE_DEBUG_LOG_LEVEL_DEBUG,
    LIBMSE_DEBUG_LOG_LEVEL_INFO,
    LIBMSE_DEBUG_LOG_LEVEL_WARN,
    LIBMSE_DEBUG_LOG_LEVEL_ERROR,
    LIBMSE_DEBUG_LOG_LEVEL_FATAL,
    LIBMSE_DEBUG_LOG_LEVEL_ASSERT
} libmse_debug_log_level_t;

static const char *libmse_debug_level_strings[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "ASSRT"
};

// ANSI Color Sequences
static const char *libmse_debug_level_esc_colors[] = {
    "\x1b[95m",   // Bright Magenta (TRACE)
    "\x1b[36m",   // Cyan (DEBUG)
    "\x1b[32m",   // Green (INFO)
    "\x1b[33m",   // Yellow (WARN)
    "\x1b[31m",   // Red (ERROR)
    "\x1b[35m",   // Magenta (FATAL)
    "\x1b[90m"    // Bright Black/Gray (ASSRT)
};

#define ANSI_RESET "\x1b[0m"

static inline void libmse_debug_write_log(int level, const char *file, int line, const char *fmt, ...)
{
    char time_buffer[20];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", tm_info);

    char log_message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(log_message, sizeof(log_message), fmt, args);
    va_end(args);

    // Apply color specifically to the bracketed log level tag, then reset immediately
    libmse_logf("%s [%s%s" ANSI_RESET "] %s:%d: %s", 
                time_buffer, 
                libmse_debug_level_esc_colors[level], 
                libmse_debug_level_strings[level], 
                file, 
                line, 
                log_message);
}

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_DEBUG_H