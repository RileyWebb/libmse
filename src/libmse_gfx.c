#include "libmse/libmse_gfx.h"
#include "libmse/libmse_gfx_internal.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

static SDL_GPUDevice *g_device = NULL;

struct mse_gfx_texture_s {
    SDL_GPUTexture *texture;
    uint32_t width;
    uint32_t height;
};

struct mse_gfx_pipeline_s {
    SDL_GPUGraphicsPipeline *pipeline;
};

struct mse_gfx_transfer_buffer_s {
    SDL_GPUTransferBuffer *buffer;
    size_t size;
};

struct mse_gfx_shader_s {
    SDL_GPUShader *shader;
};

LIBMSE_API void mse_gfx_init(SDL_GPUDevice *device) {
    g_device = device;
}

LIBMSE_API SDL_GPUTextureSamplerBinding mse_gfx_get_texture_sampler_binding(mse_gfx_texture_t *texture) {
    SDL_GPUTextureSamplerBinding binding = {0};
    
    if (!texture)
        return binding;
  
    binding.texture = texture->texture;

    return binding;
}

LIBMSE_API mse_gfx_shader_t* mse_gfx_create_shader(const uint8_t *spirv_code, size_t code_size, bool is_vertex) {
    if (!g_device || !spirv_code || code_size == 0) return NULL;
    
    SDL_GPUShaderCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.code_size = code_size;
    info.code = spirv_code;
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage = is_vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = is_vertex ? 0 : 1;
    info.num_storage_textures = 0;
    info.num_storage_buffers = 0;
    info.num_uniform_buffers = 0;
    
    SDL_GPUShader *sdl_shader = SDL_CreateGPUShader(g_device, &info);
    if (!sdl_shader) return NULL;
    
    mse_gfx_shader_t *shader = (mse_gfx_shader_t*)malloc(sizeof(mse_gfx_shader_t));
    if (!shader) {
        SDL_ReleaseGPUShader(g_device, sdl_shader);
        return NULL;
    }
    
    shader->shader = sdl_shader;
    return shader;
}

LIBMSE_API mse_gfx_texture_t* mse_gfx_create_texture(uint32_t width, uint32_t height, mse_gfx_texture_format_t format) {
    if (!g_device || width == 0 || height == 0) return NULL;
    
    SDL_GPUTextureCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.type = SDL_GPU_TEXTURETYPE_2D;
    
    if (format == MSE_GFX_TEXTURE_FORMAT_RGBA8_UNORM) {
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    } else {
        info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    }
    
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    
    SDL_GPUTexture *sdl_texture = SDL_CreateGPUTexture(g_device, &info);
    if (!sdl_texture) return NULL;

    mse_gfx_texture_t *tex = (mse_gfx_texture_t*)malloc(sizeof(mse_gfx_texture_t));
    if (!tex) {
        SDL_ReleaseGPUTexture(g_device, sdl_texture);
        return NULL;
    }
    
    tex->texture = sdl_texture;
    tex->width = width;
    tex->height = height;
    return tex;
}

LIBMSE_API void mse_gfx_destroy_texture(mse_gfx_texture_t *texture) {
    if (!texture || !g_device) return;

    if (texture->texture) {
        SDL_ReleaseGPUTexture(g_device, texture->texture);
    }
    free(texture);
}

LIBMSE_API void mse_gfx_get_texture_size(mse_gfx_texture_t *texture, uint32_t *width, uint32_t *height) {
    if (!texture) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    if (width) *width = texture->width;
    if (height) *height = texture->height;
}

LIBMSE_API mse_gfx_transfer_buffer_t* mse_gfx_create_transfer_buffer(size_t size) {
    if (!g_device || size == 0) return NULL;
    
    SDL_GPUTransferBufferCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = size;
    
    SDL_GPUTransferBuffer *sdl_buffer = SDL_CreateGPUTransferBuffer(g_device, &info);
    if (!sdl_buffer) return NULL;
    
    mse_gfx_transfer_buffer_t *buf = (mse_gfx_transfer_buffer_t*)malloc(sizeof(mse_gfx_transfer_buffer_t));
    if (!buf) {
        SDL_ReleaseGPUTransferBuffer(g_device, sdl_buffer);
        return NULL;
    }
    
    buf->buffer = sdl_buffer;
    buf->size = size;
    return buf;
}

LIBMSE_API void mse_gfx_destroy_transfer_buffer(mse_gfx_transfer_buffer_t *buffer) {
    if (!buffer || !g_device) return;
    SDL_ReleaseGPUTransferBuffer(g_device, buffer->buffer);
    free(buffer);
}

LIBMSE_API void* mse_gfx_map_transfer_buffer(mse_gfx_transfer_buffer_t *buffer) {
    if (!buffer || !g_device) return NULL;
    return SDL_MapGPUTransferBuffer(g_device, buffer->buffer, true);
}

LIBMSE_API void mse_gfx_unmap_transfer_buffer(mse_gfx_transfer_buffer_t *buffer) {
    if (!buffer || !g_device) return;
    SDL_UnmapGPUTransferBuffer(g_device, buffer->buffer);
}

LIBMSE_API void mse_gfx_upload_texture(mse_gfx_texture_t *texture, mse_gfx_transfer_buffer_t *buffer, uint32_t pitch) {
    if (!texture || !buffer || !g_device) return;
    
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g_device);
    if (!cmd) return;
    
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(cmd);
    if (copy_pass) {
        SDL_GPUTextureTransferInfo src_info;
        memset(&src_info, 0, sizeof(src_info));
        src_info.transfer_buffer = buffer->buffer;
        src_info.offset = 0;
        src_info.pixels_per_row = pitch / 4; // Assuming 4 bytes per pixel
        src_info.rows_per_layer = texture->height;
        
        SDL_GPUTextureRegion dst_region;
        memset(&dst_region, 0, sizeof(dst_region));
        dst_region.texture = texture->texture;
        dst_region.w = texture->width;
        dst_region.h = texture->height;
        dst_region.d = 1;
        dst_region.x = 0;
        dst_region.y = 0;
        dst_region.z = 0;
        
        SDL_UploadToGPUTexture(copy_pass, &src_info, &dst_region, false);
        SDL_EndGPUCopyPass(copy_pass);
    }
    
    SDL_SubmitGPUCommandBuffer(cmd);
}

LIBMSE_API void mse_gfx_destroy_shader(mse_gfx_shader_t *shader) {
    if (!shader || !g_device) return;
    
    if (shader->shader) {
        SDL_ReleaseGPUShader(g_device, shader->shader);
    }
    free(shader);
}

// --- Pipeline Implementation ---

LIBMSE_API mse_gfx_pipeline_t* mse_gfx_create_pipeline(mse_gfx_shader_t *vertex_shader, mse_gfx_shader_t *fragment_shader, mse_gfx_texture_format_t target_format) {
    if (!g_device || !vertex_shader || !fragment_shader) return NULL;
    
    SDL_GPUGraphicsPipelineCreateInfo info;
    memset(&info, 0, sizeof(info));
    
    // Bind the SPIR-V shaders
    info.vertex_shader = vertex_shader->shader;
    info.fragment_shader = fragment_shader->shader;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    
    // Zero vertex buffers/attributes (ideal for generating a fullscreen quad in the vertex shader)
    info.vertex_input_state.num_vertex_buffers = 0;
    info.vertex_input_state.num_vertex_attributes = 0;
    
    // Standard rasterizer state
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    
    // Set up the color target (the swapchain or render texture format)
    SDL_GPUColorTargetDescription color_target_desc;
    memset(&color_target_desc, 0, sizeof(color_target_desc));
    
    if (target_format == MSE_GFX_TEXTURE_FORMAT_RGBA8_UNORM) {
        color_target_desc.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    } else {
        color_target_desc.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    }
    
    // Disable blending for straight framebuffer blitting
    color_target_desc.blend_state.enable_blend = false;
    
    info.target_info.num_color_targets = 1;
    info.target_info.color_target_descriptions = &color_target_desc;
    
    // Generate the pipeline
    SDL_GPUGraphicsPipeline *sdl_pipeline = SDL_CreateGPUGraphicsPipeline(g_device, &info);
    if (!sdl_pipeline) return NULL;
    
    mse_gfx_pipeline_t *pipeline = (mse_gfx_pipeline_t*)malloc(sizeof(mse_gfx_pipeline_t));
    if (!pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(g_device, sdl_pipeline);
        return NULL;
    }
    
    pipeline->pipeline = sdl_pipeline;
    return pipeline;
}

LIBMSE_API void mse_gfx_destroy_pipeline(mse_gfx_pipeline_t *pipeline) {
    if (!pipeline || !g_device) return;
    
    if (pipeline->pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(g_device, pipeline->pipeline);
    }
    free(pipeline);
}