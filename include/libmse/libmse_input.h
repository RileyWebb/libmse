#ifndef LIBMSE_INPUT_H
#define LIBMSE_INPUT_H

#include "libmse_api.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mse_backend_input_desc_s {
    const char      *id;   /* unique identifier, e.g. "RETROPAD_A"  */
    const char      *name; /* display name,       e.g. "Button A"   */
    enum mse_input_type_e {
        MSE_INPUT_TYPE_BUTTON, /* digital: 0.0f = released, 1.0f = pressed */
        MSE_INPUT_TYPE_AXIS    /* analogue: -1.0f to +1.0f */
    } type;
} mse_backend_input_desc_t;


/* -----------------------------------------------------------------------
 * Controller layout (declared by the backend, drawn by the frontend)
 *
 * A backend that wants its own pad drawn rather than a plain list of inputs
 * exports `controller_desc` and an `input_layouts` array running parallel to
 * `inputs`. Both are optional: without them the frontend falls back to a list.
 *
 * Coordinates are in body units -- the body is `aspect` wide and exactly 1
 * tall -- so a control with w == h is square whatever the body's proportions.
 * x and y are the centre of the control.
 * ---------------------------------------------------------------------- */

typedef enum mse_input_shape_e {
    MSE_INPUT_SHAPE_NONE = 0, /* not drawn; the input is list-only */
    MSE_INPUT_SHAPE_RECT,     /* rounded rectangle: shoulder buttons */
    MSE_INPUT_SHAPE_PILL,     /* fully rounded ends: Select, Start */
    MSE_INPUT_SHAPE_CIRCLE,   /* round face button */
    MSE_INPUT_SHAPE_DPAD      /* one arm of a direction cross */
} mse_input_shape_t;

typedef struct mse_backend_input_layout_s {
    float    x, y; /* centre, in body units */
    float    w, h; /* size, in body units */
    uint32_t shape; /* mse_input_shape_t */
} mse_backend_input_layout_t;

typedef struct mse_backend_controller_desc_s {
    const char *name;   /* "NES Controller" */
    float       aspect; /* body width, with the height taken as 1 */
} mse_backend_controller_desc_t;

/* -----------------------------------------------------------------------
 * Binding source (set by the frontend / user)
 * ---------------------------------------------------------------------- */
typedef enum mse_input_source_type_e {
    MSE_INPUT_SOURCE_NONE = 0,
    MSE_INPUT_SOURCE_KEYBOARD,
    MSE_INPUT_SOURCE_GAMEPAD_BUTTON,
    MSE_INPUT_SOURCE_GAMEPAD_AXIS
} mse_input_source_type_t;

typedef struct mse_input_binding_s {
    mse_input_source_type_t source_type;

    /* keyboard */
    int scancode; /* SDL_Scancode value */

    /* gamepad */
    int gamepad_button; /* SDL_GamepadButton value */
    int gamepad_axis;   /* SDL_GamepadAxis value */
    float axis_deadzone;
} mse_input_binding_t;

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_INPUT_H