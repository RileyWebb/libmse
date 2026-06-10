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

#ifdef __cplusplus
extern "C" {
#endif

typedef enum mse_log_level_e {
    MSE_LOG_TRACE = 0,
    MSE_LOG_DEBUG,
    MSE_LOG_INFO,
    MSE_LOG_WARN,
    MSE_LOG_ERROR,
    MSE_LOG_FATAL
} mse_log_level_t;

typedef struct mse_backend_s {
    mse_backend_info_t info;
    mse_backend_source_t source;
    mse_backend_caps_e capabilities;
    mse_backend_start_callback_t start;
    mse_backend_get_texture_callback_t get_texture;
    mse_backend_init_callback_t init;
    mse_backend_shutdown_callback_t shutdown;
    mse_backend_load_rom_callback_t load_rom;
    mse_backend_update_inputs_callback_t update_inputs;

    /* Input control scheme declared by the backend */
    const mse_backend_input_desc_t *input_descs;
    size_t input_count;

    /* Live input state written by the frontend input thread. */
    float *input_states;
} mse_backend_t;

typedef struct mse_backends_s {
    size_t count;
    mse_backend_t **items;
} mse_backends_t;

typedef struct mse_emulator_s mse_emulator_t;
typedef struct mse_rom_s mse_rom_t;

// Registers a new backend from a compressed file.
LIBMSE_API mse_backend_t *mse_backend_register(const char *filename);
// Registers a new backend from a folder.
LIBMSE_API mse_backend_t *mse_backend_register_folder(const char *foldername);
// Lifecycle and operations
LIBMSE_API bool mse_backend_init(mse_backend_t *backend);
LIBMSE_API void mse_backend_shutdown(mse_backend_t *backend);
LIBMSE_API bool mse_backend_load_rom(mse_backend_t *backend, const uint8_t *data, size_t size);
LIBMSE_API void mse_backend_update_inputs(mse_backend_t *backend, const float *inputs);

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_H