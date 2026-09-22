#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "libmse/libmse_log.h"
#include "libmse/libmse_debug.h"

#define MAX_LOG_MESSAGE 1024
#define MAX_BUFFERS 8

static libmse_log_buffer_t buffers[MAX_BUFFERS] = {0};
static size_t buffer_count = 0;

static libmse_log_callback callbacks[MAX_BUFFERS] = {0};
static size_t callback_count = 0;

LIBMSE_API void libmse_log_register_file(FILE *file, bool supports_escape_codes, bool flush_on_write) {
    if (file == NULL)
        return;

    if (buffer_count >= MAX_BUFFERS) {
        // Too many buffers registered, ignore this one
        DEBUG_WARN("Maximum log buffers reached. Ignoring new buffer registration.");
        return;
    }

    buffers[buffer_count++] = (libmse_log_buffer_t){
        .file = file,
        .is_file = true,
        .supports_escape_codes = supports_escape_codes,
        .flush_on_write = flush_on_write
    };
}

LIBMSE_API void libmse_log_register_string_buffer(libmse_log_string_buffer_t *string_buffer, bool supports_escape_codes) {
    if (string_buffer == NULL)
        return;

    if (buffer_count >= MAX_BUFFERS) {
        // Too many buffers registered, ignore this one
        DEBUG_WARN("Maximum log buffers reached. Ignoring new buffer registration.");
        return;
    }

    buffers[buffer_count++] = (libmse_log_buffer_t){
        .string_buffer = *string_buffer,
        .is_file = false,
        .supports_escape_codes = supports_escape_codes,
        .flush_on_write = false // Not applicable for string buffers
    };
}

LIBMSE_API void libmse_log_register_callback(libmse_log_callback callback) {
    if (callback == NULL)
        return;

    if (callback_count >= MAX_BUFFERS) {
        // Too many callbacks registered, ignore this one
        DEBUG_WARN("Maximum log callbacks reached. Ignoring new callback registration.");
        return;
    }

    callbacks[callback_count++] = callback;
}

// Appends string data to the dynamic buffer in O(1) time complexity
static void append_to_string_buffer(libmse_log_string_buffer_t *sb, const char *str, size_t len) {
    size_t new_size = sb->size + len;
    
    // Ensure room for data + null terminator
    if (new_size + 1 > sb->capacity) {
        size_t new_capacity = (sb->capacity == 0) ? MAX_LOG_MESSAGE : sb->capacity * 2;
        while (new_capacity <= new_size) {
            new_capacity *= 2;
        }
        char *new_buffer = (char *)realloc(sb->buffer, new_capacity);
        if (new_buffer == NULL) {
            DEBUG_ERROR("Failed to allocate memory for log string buffer.");
            return;
        }
        sb->buffer = new_buffer;
        sb->capacity = new_capacity;
    }
    
    memcpy(sb->buffer + sb->size, str, len);
    sb->size = new_size;
    sb->buffer[sb->size] = '\0';
}

// Low-level write router
static inline void write_to_buffer(libmse_log_buffer_t *buffer, const char *str, size_t len) {
    if (buffer == NULL || str == NULL || len == 0)
        return;

    if (buffer->is_file) {
        if (!buffer->file)
            return;

        fwrite(str, 1, len, buffer->file);
        if (buffer->flush_on_write) {
            fflush(buffer->file);
        }
    } else {
        append_to_string_buffer(&buffer->string_buffer, str, len);
    }
}

// Non-destructive, fast ASCII escape code stripping filter
static size_t copy_stripping_escape_codes(char *dest, const char *src, size_t src_len) {
    size_t write_idx = 0;
    bool in_escape = false;
    
    for (size_t i = 0; i < src_len; i++) {
        char c = src[i];
        if (!in_escape) {
            if (c == 0x1B) { 
                in_escape = true;
            } else {
                dest[write_idx++] = c;
            }
        } else {
            if (c >= 0x40 && c <= 0x7E) {
                in_escape = false;
            }
        }
    }
    dest[write_idx] = '\0';
    return write_idx;
}

// --- Public API ---

// Directly prints raw string straight to all registered buffers (No newlines, no filtering)
LIBMSE_API void libmse_log_print(const char *str) {
    if (!str) return;
    size_t len = strlen(str);

    for (size_t i = 0; i < callback_count; i++)
        callbacks[i](str);

    for (size_t i = 0; i < buffer_count; i++)
        write_to_buffer(&buffers[i], str, len);
}

// Directly prints formatted string straight to all registered buffers (No newlines, no filtering)
LIBMSE_API void libmse_log_printf(const char *fmt, ...) {
    if (!fmt) return;
    
    char log[MAX_LOG_MESSAGE];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(log, sizeof(log), fmt, args);
    va_end(args);

    if (len < 0) return;
    size_t write_len = ((size_t)len >= sizeof(log)) ? sizeof(log) - 1 : (size_t)len;

    for (size_t i = 0; i < callback_count; i++) {
        callbacks[i](log);
    }

    for (size_t i = 0; i < buffer_count; i++) {
        write_to_buffer(&buffers[i], log, write_len);
    }
}

// Prints string with automated trailing newlines and escape code stripping for unsupported buffers
LIBMSE_API void libmse_log(const char *str) {
    if (!str) return;
    size_t len = strlen(str);

    for (size_t i = 0; i < callback_count; i++) {
        callbacks[i](str);
    }

    char stack_buf[MAX_LOG_MESSAGE];
    char *stripped_str = NULL;
    size_t stripped_len = 0;
    bool built_stripped = false;

    for (size_t i = 0; i < buffer_count; i++) {
        libmse_log_buffer_t *buffer = &buffers[i];

        if (buffer->supports_escape_codes) {
            write_to_buffer(buffer, str, len);
            write_to_buffer(buffer, "\n", 1);
        } else {
            if (!built_stripped) {
                if (len < MAX_LOG_MESSAGE) {
                    stripped_str = stack_buf;
                } else {
                    stripped_str = (char *)malloc(len + 1);
                    if (!stripped_str) { // Allocation failure fallback
                        stripped_str = (char *)str;
                        stripped_len = len;
                        built_stripped = true;
                        goto fallback_write;
                    }
                }
                stripped_len = copy_stripping_escape_codes(stripped_str, str, len);
                built_stripped = true;
            }

        fallback_write:
            write_to_buffer(buffer, stripped_str, stripped_len);
            write_to_buffer(buffer, "\n", 1);
        }
    }

    if (built_stripped && stripped_str != stack_buf && stripped_str != str) {
        free(stripped_str);
    }
}

LIBMSE_API void libmse_logf(const char *fmt, ...) {
    if (!fmt) return;

    char log[MAX_LOG_MESSAGE];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(log, sizeof(log), fmt, args);
    va_end(args);

    if (len < 0) return;
    
    libmse_log(log);
}

LIBMSE_API void libmse_log_flush(libmse_log_buffer_t *buffer) {
    if (buffer == NULL)
        return;

    if (buffer->is_file) {
        fflush(buffer->file);
    }
}

LIBMSE_API void libmse_log_flush_all(void) {
    for (size_t i = 0; i < MAX_BUFFERS; ++i) {
        libmse_log_flush(&buffers[i]);
    }
}