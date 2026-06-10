#ifndef LIBMSE_GFX_H
#define LIBMSE_GFX_H

#include "libmse_api.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mse_gfx_texture_s mse_gfx_texture_t;
typedef struct mse_gfx_transfer_buffer_s mse_gfx_transfer_buffer_t;
typedef struct mse_gfx_shader_s mse_gfx_shader_t;
typedef struct mse_gfx_pipeline_s mse_gfx_pipeline_t;

// Format corresponds to common GPU texture formats
typedef enum mse_gfx_texture_format_e {
    MSE_GFX_TEXTURE_FORMAT_RGBA8_UNORM,
    MSE_GFX_TEXTURE_FORMAT_BGRA8_UNORM
} mse_gfx_texture_format_t;

// Shaders & Pipelines (SPIR-V)
LIBMSE_API mse_gfx_shader_t* mse_gfx_create_shader(const uint8_t *spirv_code, size_t code_size, bool is_vertex);
LIBMSE_API void mse_gfx_destroy_shader(mse_gfx_shader_t *shader);

// Textures & Buffers
LIBMSE_API mse_gfx_texture_t* mse_gfx_create_texture(uint32_t width, uint32_t height, mse_gfx_texture_format_t format);
LIBMSE_API void mse_gfx_destroy_texture(mse_gfx_texture_t *texture);
LIBMSE_API void mse_gfx_get_texture_size(mse_gfx_texture_t *texture, uint32_t *width, uint32_t *height);

LIBMSE_API mse_gfx_transfer_buffer_t* mse_gfx_create_transfer_buffer(size_t size);
LIBMSE_API void mse_gfx_destroy_transfer_buffer(mse_gfx_transfer_buffer_t *buffer);

// Data transfer
LIBMSE_API void* mse_gfx_map_transfer_buffer(mse_gfx_transfer_buffer_t *buffer);
LIBMSE_API void mse_gfx_unmap_transfer_buffer(mse_gfx_transfer_buffer_t *buffer);
LIBMSE_API void mse_gfx_upload_texture(mse_gfx_texture_t *texture, mse_gfx_transfer_buffer_t *buffer, uint32_t pitch);

// Pipelines
LIBMSE_API mse_gfx_pipeline_t* mse_gfx_create_pipeline(mse_gfx_shader_t *vertex_shader, mse_gfx_shader_t *fragment_shader, mse_gfx_texture_format_t target_format);
LIBMSE_API void mse_gfx_destroy_pipeline(mse_gfx_pipeline_t *pipeline);

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_GFX_H
