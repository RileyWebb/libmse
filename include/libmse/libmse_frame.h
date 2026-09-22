#ifndef LIBMSE_FRAME_H
#define LIBMSE_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libmse_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// Byte order in memory, not word order. BGRA8 is what a little-endian host sees
// when a backend stores 0xAARRGGBB words, which is the common case for palette
// based cores; offering it lets them publish with no per-pixel swizzle.
typedef enum mse_frame_format_e {
    MSE_FRAME_FORMAT_RGBA8 = 0,
    MSE_FRAME_FORMAT_BGRA8 = 1
} mse_frame_format_t;

typedef struct mse_frame_s {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;   // Bytes per row
    mse_frame_format_t format;
    size_t pixels_size;
    const uint8_t *pixels;

    // True when this frame is newer than the one the caller last received. A
    // backend that has produced nothing since the previous call reports false
    // here while still filling in the geometry, so the caller can keep showing
    // the previous image rather than blanking.
    bool ready;
} mse_frame_t;

// Fills in the most recently completed frame. Returns false if the backend has
// no frame at all yet.
//
// The pixel buffer stays valid until the next call, so the caller may read it
// but must not hold the pointer across calls. Backends are expected to be
// emulating on their own thread, so the call has to be safe against that.
typedef bool (*mse_backend_frame_callback_t)(mse_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_FRAME_H