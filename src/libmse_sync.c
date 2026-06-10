#include "libmse/libmse_sync.h"
#include <SDL3/SDL.h>
#include <stdlib.h>

struct mse_event_s {
    SDL_Mutex *mutex;
    SDL_Condition *cond;
    bool signaled;
};

LIBMSE_API mse_event_t* mse_event_create(void) {
    mse_event_t *ev = (mse_event_t *)malloc(sizeof(mse_event_t));
    if (!ev) return NULL;
    
    ev->mutex = SDL_CreateMutex();
    ev->cond = SDL_CreateCondition();
    ev->signaled = false;
    
    if (!ev->mutex || !ev->cond) {
        if (ev->cond) SDL_DestroyCondition(ev->cond);
        if (ev->mutex) SDL_DestroyMutex(ev->mutex);
        free(ev);
        return NULL;
    }
    
    return ev;
}

LIBMSE_API void mse_event_destroy(mse_event_t *event) {
    if (!event) return;
    
    SDL_DestroyCondition(event->cond);
    SDL_DestroyMutex(event->mutex);
    free(event);
}

LIBMSE_API void mse_event_set(mse_event_t *event) {
    if (!event) return;
    
    SDL_LockMutex(event->mutex);
    event->signaled = true;
    SDL_BroadcastCondition(event->cond);
    SDL_UnlockMutex(event->mutex);
}

LIBMSE_API void mse_event_reset(mse_event_t *event) {
    if (!event) return;
    
    SDL_LockMutex(event->mutex);
    event->signaled = false;
    SDL_UnlockMutex(event->mutex);
}

LIBMSE_API void mse_event_wait(mse_event_t *event) {
    if (!event) return;
    
    SDL_LockMutex(event->mutex);
    while (!event->signaled) {
        SDL_WaitCondition(event->cond, event->mutex);
    }
    SDL_UnlockMutex(event->mutex);
}

LIBMSE_API bool mse_event_wait_timeout(mse_event_t *event, uint32_t timeout_ms) {
    if (!event) return false;
    
    SDL_LockMutex(event->mutex);
    bool result = true;
    if (!event->signaled) {
        if (timeout_ms == 0) {
            result = false;
        } else {
            result = SDL_WaitConditionTimeout(event->cond, event->mutex, (Sint32)timeout_ms);
        }
    }
    bool was_signaled = event->signaled;
    SDL_UnlockMutex(event->mutex);
    
    return result && was_signaled;
}
