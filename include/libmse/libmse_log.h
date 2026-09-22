#ifndef LIBMSE_LOG_H
#define LIBMSE_LOG_H

#include <stdio.h>
#include <stdbool.h>

#include "libmse_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct libmse_log_string_buffer_s
{
    char   *buffer;
    size_t  size;
    size_t  capacity;
} libmse_log_string_buffer_t;

typedef struct libmse_log_buffer_s {
    union {
        FILE *file;
        libmse_log_string_buffer_t string_buffer;
    };
    bool is_file;
    bool supports_escape_codes;
    bool flush_on_write;
} libmse_log_buffer_t;

typedef void (*libmse_log_callback)(const char *message);

LIBMSE_API void libmse_log_register_file(FILE *file, bool supports_escape_codes, bool flush_on_write);
LIBMSE_API void libmse_log_register_string_buffer(libmse_log_string_buffer_t *buffer, bool supports_escape_codes);
LIBMSE_API void libmse_log_register_buffer(libmse_log_buffer_t *buffer);
LIBMSE_API void libmse_log_register_callback(libmse_log_callback callback);

LIBMSE_API void libmse_log_print(const char *str);
LIBMSE_API void libmse_log_printf(const char *fmt, ...);
LIBMSE_API void libmse_log(const char *log);
LIBMSE_API void libmse_logf(const char *fmt, ...);
LIBMSE_API void libmse_log_flush(libmse_log_buffer_t *buffer);
LIBMSE_API void libmse_log_flush_all(void);

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_LOG_H