#ifndef LIBMSE_GFX_INTERNAL_H
#define LIBMSE_GFX_INTERNAL_H

#include "libmse/libmse_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations for SDL3 so we don't need to include SDL_gpu.h everywhere in frontend
typedef struct SDL_GPUDevice SDL_GPUDevice;
typedef struct SDL_GPUTexture SDL_GPUTexture;
typedef struct SDL_GPUSampler SDL_GPUSampler;
typedef struct SDL_GPUTextureSamplerBinding SDL_GPUTextureSamplerBinding;

// Called by frontend on startup
LIBMSE_API void mse_gfx_init(SDL_GPUDevice *device);

// Called by frontend to get the raw SDL_GPUTexture for ImGui
LIBMSE_API SDL_GPUTextureSamplerBinding mse_gfx_get_texture_sampler_binding(mse_gfx_texture_t *texture);

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_GFX_INTERNAL_H
