#ifndef LIBMSE_THREAD_H
#define LIBMSE_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "libmse_api.h"

typedef struct libmse_mutex_s libmse_mutex_t;
typedef struct libmse_thread_s libmse_thread_t;
typedef struct libmse_cond_s libmse_cond_t;

typedef int (*libmse_thread_func_t)(void *data);

// Mutex
LIBMSE_API libmse_mutex_t* libmse_mutex_create(void);
LIBMSE_API void            libmse_mutex_destroy(libmse_mutex_t *mutex);
LIBMSE_API void            libmse_mutex_lock(libmse_mutex_t *mutex);
LIBMSE_API void            libmse_mutex_unlock(libmse_mutex_t *mutex);

// Threading
LIBMSE_API libmse_thread_t* libmse_thread_create(libmse_thread_func_t func, const char *name, void *data);
LIBMSE_API int libmse_thread_join(libmse_thread_t *thread);
LIBMSE_API void libmse_thread_detach(libmse_thread_t *thread);

// Condition Variables
LIBMSE_API libmse_cond_t*  libmse_cond_create(void);
LIBMSE_API void            libmse_cond_destroy(libmse_cond_t *cond);
LIBMSE_API void            libmse_cond_wait(libmse_cond_t *cond, libmse_mutex_t *mutex);
LIBMSE_API void            libmse_cond_signal(libmse_cond_t *cond);

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_THREAD_H