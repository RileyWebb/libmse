#ifndef LIBMSE_INPUT_H
#define LIBMSE_INPUT_H

#include "libmse_api.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Input descriptor (defined by the backend)
 * ---------------------------------------------------------------------- */

typedef enum mse_input_type_e {
    MSE_INPUT_TYPE_BUTTON, /* digital: 0.0f = released, 1.0f = pressed */
    MSE_INPUT_TYPE_AXIS    /* analogue: -1.0f to +1.0f */
} mse_input_type_t;

typedef struct mse_backend_input_desc_s {
    const char      *id;   /* unique identifier, e.g. "RETROPAD_A"  */
    const char      *name; /* display name,       e.g. "Button A"   */
    mse_input_type_t type;
} mse_backend_input_desc_t;

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