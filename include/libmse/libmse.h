/*
 * libmse.h - Main header file for the libmse library
 * 
 * Copyright (c) 2026 Riley Webb <rileyjoshuawebb@gmail.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef LIBMSE_H
#define LIBMSE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libmse_api.h"
#include "libmse_backend.h"
#include "libmse_file.h"
#include "libmse_frame.h"
#include "libmse_input.h"
#include "libmse_cvar.h"
#include "libmse_cmd.h"
#include "libmse_lua.h"
#include "libmse_library.h"

#if defined(_WIN32)
    #define MSE_PLATFORM_NAME "windows"
    #define MSE_LIBRARY_EXTENSION ".dll"
#else
    #if defined(__APPLE__)
        #define MSE_PLATFORM_NAME "macos"
        #define MSE_LIBRARY_EXTENSION ".dylib"
    #else
        #define MSE_PLATFORM_NAME "linux"
        #define MSE_LIBRARY_EXTENSION ".so"
    #endif
#endif

typedef struct mse_backend_s {
    mse_backend_info_t info;
    mse_backend_source_t source;
    mse_backend_start_callback_t start;
    // Pulls the latest video frame as CPU pixels. The frontend owns the GPU, so
    // backends publish a buffer rather than a texture; that keeps plugins from
    // having to link the renderer.
    mse_backend_frame_callback_t get_frame;
    mse_backend_init_callback_t init;
    mse_backend_shutdown_callback_t shutdown;
    mse_backend_load_rom_callback_t load_rom;
    mse_backend_update_inputs_callback_t update_inputs;

    /* Transport control; required, see libmse_backend.h. */
    mse_backend_pause_callback_t pause;
    mse_backend_resume_callback_t resume;
    mse_backend_stop_callback_t stop;
    mse_backend_state_callback_t get_state;

    libmse_library_meta_handler_t metadata_handler;

    /* Input control scheme declared by the backend */
    const mse_backend_input_desc_t *input_descs;
    size_t input_count;

    /* Optional: how the backend's pad is laid out, for the configurator.
     * input_layouts runs parallel to input_descs when present. */
    const mse_backend_input_layout_t *input_layouts;
    const mse_backend_controller_desc_t *controller_desc;

    /* Live input state written by the frontend input thread. */
    float *input_states;

    const char **lua_libraries;
    size_t lua_library_count;
} libmse_backend_t;

typedef struct mse_backends_s {
    size_t count;
    libmse_backend_t **items;
} mse_backends_t;

typedef struct libmse_ctx_s {
    mse_backends_t backends;

    //libmse_library_t *library;
    libmse_lua_worker_t *main_lua_worker;
} libmse_ctx_t;

// Registers a new backend from a compressed file.
LIBMSE_API libmse_backend_t *mse_backend_register(const char *filename);
// Registers a new backend from a folder.
LIBMSE_API libmse_backend_t *mse_backend_register_folder(const char *foldername);
// Lifecycle and operations
LIBMSE_API bool mse_backend_init(libmse_backend_t *backend);
LIBMSE_API void mse_backend_shutdown(libmse_backend_t *backend);
LIBMSE_API bool mse_backend_load_rom(libmse_backend_t *backend, const uint8_t *data, size_t size);
LIBMSE_API void mse_backend_update_inputs(libmse_backend_t *backend, const float *inputs);

// Transport control. Each returns false only when the backend is missing --
// the callbacks themselves are required, so there is no "unsupported" case.
LIBMSE_API bool mse_backend_pause(libmse_backend_t *backend);
LIBMSE_API bool mse_backend_resume(libmse_backend_t *backend);
LIBMSE_API bool mse_backend_stop(libmse_backend_t *backend);
LIBMSE_API libmse_backend_state_t mse_backend_get_state(const libmse_backend_t *backend);
LIBMSE_API const char *mse_backend_state_name(libmse_backend_state_t state);

// Convenience wrapper around backend->get_frame. Returns false when the backend
// publishes no video or has not produced a frame yet.
LIBMSE_API bool mse_backend_get_frame(libmse_backend_t *backend, mse_frame_t *frame);

LIBMSE_API bool libmse_init(void);
LIBMSE_API void libmse_shutdown(void);

//TEMP
LIBMSE_API extern libmse_db_t *g_temp_db;
LIBMSE_API extern libmse_library_t *g_temp_lib;

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_H