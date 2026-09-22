// libmse_resource.h - Resource management and acquisition for MSE

#ifndef LIBMSE_RESOURCE_H
#define LIBMSE_RESOURCE_H

#include <stdbool.h>
#include <stdio.h>

#include "libmse_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct libmse_resource_s {
    const char* uri;
    const char* data;
    size_t      size;

    bool ready;
} libmse_resource_t;

LIBMSE_API libmse_resource_t *libmse_resource_load(const char *uri);
LIBMSE_API void libmse_resource_free(libmse_resource_t *resource);
LIBMSE_API bool libmse_resource_reload(libmse_resource_t *resource);
LIBMSE_API bool libmse_resource_is_ready(libmse_resource_t *resource);
LIBMSE_API bool libmse_resource_save_to_file(libmse_resource_t *resource, const char *path);
//bool libmse_resource_save_to_temp(libmse_resource_t *resource, char *path);
LIBMSE_API bool libmse_resource_ensure_directory_exists(const char *path);
LIBMSE_API FILE *libmse_resource_create_log_file();

// Path utilities
LIBMSE_API const char *libmse_resource_get_cwd();
LIBMSE_API const char *libmse_resource_get_temp_path();
LIBMSE_API const char *libmse_resource_get_executable_path();
LIBMSE_API const char *libmse_resource_get_appdata_path();
LIBMSE_API const char *libmse_resource_get_cache_path();
LIBMSE_API const char *libmse_resource_get_config_path();
LIBMSE_API const char *libmse_resource_get_log_path();

// Hashing
LIBMSE_API const char *libmse_resource_compute_sha256(libmse_resource_t *resource);
LIBMSE_API const char *libmse_resource_compute_crc32(libmse_resource_t *resource);

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_RESOURCE_H