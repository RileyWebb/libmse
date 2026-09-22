// libmse_cvar.h - CVar (Console Variable) API for libmse

#ifndef LIBMSE_CVAR_H
#define LIBMSE_CVAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libmse_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct libmse_cvar_s libmse_cvar_t;

typedef void (*libmse_cvar_iterate_cb)(libmse_cvar_t* cvar, void* user_data);
typedef void (*libmse_cvar_change_cb)(libmse_cvar_t* cvar, void* user_data);

typedef enum libmse_cvar_type_e {
    LIBMSE_CVAR_INT,
    LIBMSE_CVAR_FLOAT,
    LIBMSE_CVAR_DOUBLE,
    LIBMSE_CVAR_STRING,
} libmse_cvar_type_t;

typedef struct libmse_cvar_s {
    const char* name;
    const char* description;
    libmse_cvar_type_t type;

    union {
        int *i;
        float *f;
        double *d;
        const char** s;
    } data;

    char* alloc_s;

    libmse_cvar_change_cb cb;
    void* user_data;
} libmse_cvar_t;

LIBMSE_API bool libmse_cvar_register(const char* name, libmse_cvar_type_t type, void* ptr, const char* description);
LIBMSE_API bool libmse_cvar_register_change_cb(const char* name, libmse_cvar_change_cb callback, void* user_data);
LIBMSE_API bool libmse_cvar_destroy(const char* name);
LIBMSE_API void libmse_cvar_iterate(libmse_cvar_iterate_cb callback, void* user_data);

LIBMSE_API libmse_cvar_t* libmse_cvar_get(const char* name);
LIBMSE_API libmse_cvar_t* libmse_cvar_get_reversed(const char* name);
LIBMSE_API void *libmse_cvar_get_ptr(const char* name);
LIBMSE_API int *libmse_cvar_get_i(const char* name);
LIBMSE_API float *libmse_cvar_get_f(const char* name);
LIBMSE_API double *libmse_cvar_get_d(const char* name);
LIBMSE_API const char **libmse_cvar_get_s(const char* name);

LIBMSE_API bool libmse_cvar_set_i(const char* name, int value);
LIBMSE_API bool libmse_cvar_set_f(const char* name, float value);
LIBMSE_API bool libmse_cvar_set_d(const char* name, double value);
LIBMSE_API bool libmse_cvar_set_s(const char* name, const char *value);

LIBMSE_API bool libmse_cvar_export(const char* filename);

LIBMSE_API void* libmse_get_cvar_registry();
LIBMSE_API size_t libmse_get_cvar_count();

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_CVAR_H