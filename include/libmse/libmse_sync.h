#ifndef LIBMSE_SYNC_H
#define LIBMSE_SYNC_H

#include "libmse_api.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mse_event_s mse_event_t;

LIBMSE_API mse_event_t* mse_event_create(void);
LIBMSE_API void mse_event_destroy(mse_event_t *event);
LIBMSE_API void mse_event_set(mse_event_t *event);
LIBMSE_API void mse_event_reset(mse_event_t *event);
LIBMSE_API void mse_event_wait(mse_event_t *event);
LIBMSE_API bool mse_event_wait_timeout(mse_event_t *event, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_SYNC_H
