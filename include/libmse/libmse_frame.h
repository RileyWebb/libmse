#ifndef LIBMSE_FRAME_H
#define LIBMSE_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libmse_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum mse_frame_format_e {
    MSE_FRAME_FORMAT_RGBA8 = 0
} mse_frame_format_t;

typedef struct mse_frame_s {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    mse_frame_format_t format;
    size_t pixels_size;
    uint8_t *pixels;

    bool ready;
    bool locked;
} mse_frame_t;



typedef bool (*mse_backend_frame_callback_t)(mse_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_FRAME_H