#define LIBMSE_EXPORTS

#include "libmse/libmse.h"
#include "libmse/libmse_file.h"
#include <stdlib.h>
#include <string.h>

typedef struct file_entry {
    char *extension;
    mse_file_handler_t handler;
} file_entry_t;

static file_entry_t *g_file_entries = NULL;
static size_t g_file_entry_count = 0;
static size_t g_file_entry_capacity = 0;

static const char *normalize_extension(const char *ext) {
    if (ext == NULL) return NULL;
    if (ext[0] == '.') return ext;
    /* caller expects extension with leading dot; allocate temp is unnecessary here */
    return ext;
}

LIBMSE_API bool mse_filetype_register(const char *extension, const mse_file_handler_t *handler) {
    if (extension == NULL || handler == NULL) return false;

    /* ensure we have capacity */
    if (g_file_entry_count + 1 > g_file_entry_capacity) {
        size_t newcap = g_file_entry_capacity == 0 ? 16 : g_file_entry_capacity * 2;
        file_entry_t *n = (file_entry_t *)realloc(g_file_entries, newcap * sizeof(file_entry_t));
        if (!n) return false;
        g_file_entries = n;
        g_file_entry_capacity = newcap;
    }

    /* copy extension and handler */
    char *ext_copy = strdup(extension);
    if (!ext_copy) return false;

    g_file_entries[g_file_entry_count].extension = ext_copy;
    g_file_entries[g_file_entry_count].handler.description = handler->description ? strdup(handler->description) : NULL;
    g_file_entries[g_file_entry_count].handler.extension = g_file_entries[g_file_entry_count].extension;
    g_file_entries[g_file_entry_count].handler.handler = handler->handler;
    g_file_entry_count++;

    return true;
}

LIBMSE_API bool mse_filetype_unregister(const char *extension) {
    if (extension == NULL) return false;
    for (size_t i = 0; i < g_file_entry_count; ++i) {
        if (strcmp(g_file_entries[i].extension, extension) == 0 || (g_file_entries[i].extension[0] == '.' && strcmp(g_file_entries[i].extension + 1, extension) == 0)) {
            free(g_file_entries[i].extension);
            free((void *)g_file_entries[i].handler.description);
            /* shift remaining */
            for (size_t j = i + 1; j < g_file_entry_count; ++j) {
                g_file_entries[j - 1] = g_file_entries[j];
            }
            g_file_entry_count--;
            return true;
        }
    }
    return false;
}

LIBMSE_API const mse_file_handler_t *mse_filetype_find(const char *extension) {
    if (extension == NULL) return NULL;
    for (size_t i = 0; i < g_file_entry_count; ++i) {
        if (strcmp(g_file_entries[i].extension, extension) == 0) return &g_file_entries[i].handler;
        if (g_file_entries[i].extension[0] == '.' && strcmp(g_file_entries[i].extension + 1, extension) == 0) return &g_file_entries[i].handler;
    }
    return NULL;
}
