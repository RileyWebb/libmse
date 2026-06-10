#ifndef LIBMSE_FILE_H
#define LIBMSE_FILE_H

#include <stdbool.h>

#include "libmse_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mse_file_handler {
    const char *extension;
    const char *description;
    bool (*handler)(const char *filename);
} mse_file_handler_t;

LIBMSE_API bool mse_filetype_register(const char *extension, const mse_file_handler_t *handler);
LIBMSE_API bool mse_filetype_unregister(const char *extension);
LIBMSE_API const mse_file_handler_t *mse_filetype_find(const char *extension);

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_FILE_H