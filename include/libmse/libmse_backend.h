#ifndef LIBMSE_BACKEND_H
#define LIBMSE_BACKEND_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "libmse_sync.h"
#include "libmse_gfx.h"

typedef struct mse_backend_shader_s mse_backend_shader_t;
typedef struct mse_backend_image_s mse_backend_image_t;

typedef struct mse_backend_info_s {
    const char *name;
    const char *version;
    const char *author;
    const char *description;
    const char *licence;
    const char *repository;

    const char *build_date;
    const char *build_time;
} mse_backend_info_t;

typedef mse_backend_info_t libmse_backend_info_t;

typedef struct mse_backend_source_s {
    const char *repository;
    const char *commit;
    const char *branch;
} mse_backend_source_t;

typedef struct mse_backend_resources_s {
    mse_backend_image_t *icon;
    mse_backend_image_t *banner;

    mse_backend_shader_t *shaders;
    size_t shader_count;
} mse_backend_resources_t;

typedef bool (*mse_backend_init_callback_t)(void);
typedef void (*mse_backend_shutdown_callback_t)(void);
typedef void (*mse_backend_start_callback_t)(mse_event_t *stop_event);
//typedef mse_gfx_texture_t* (*mse_backend_get_texture_callback_t)(void);
typedef bool (*mse_backend_load_rom_callback_t)(const uint8_t *data, size_t size);
typedef void (*mse_backend_update_inputs_callback_t)(const float *inputs);

//typedef cJSON *(*mse_backend_serialize_settings_callback_t)(void);
//typedef bool (*mse_backend_deserialize_settings_callback_t)(const cJSON *settings);

//typedef struct mse_backend_settings_callbacks_s {
//    mse_backend_serialize_settings_callback_t serialize_settings;
//    mse_backend_deserialize_settings_callback_t deserialize_settings;
//} mse_backend_settings_callbacks_t;

#endif // LIBMSE_BACKEND_H