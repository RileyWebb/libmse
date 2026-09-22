#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h> // For intptr_t

#include "libmse/libmse_debug.h"
#include "libmse/libmse_thread.h"

struct libmse_mutex_s {
    pthread_mutex_t handle;
};

struct libmse_thread_s {
    pthread_t handle;
    char *name;
};

struct libmse_cond_s {
    pthread_cond_t handle;
};

typedef struct {
    libmse_thread_func_t func;
    void *data;
} thread_payload_t;


LIBMSE_API libmse_mutex_t* libmse_mutex_create(void) {
    libmse_mutex_t *mutex = (libmse_mutex_t*)malloc(sizeof(libmse_mutex_t));
    if (!mutex) return NULL;

    if (pthread_mutex_init(&mutex->handle, NULL) != 0) {
        DEBUG_ERROR("Failed to initialize mutex");
        free(mutex);
        return NULL;
    }
    return mutex;
}

LIBMSE_API void libmse_mutex_destroy(libmse_mutex_t *mutex) {
    if (!mutex) return;
    pthread_mutex_destroy(&mutex->handle);
    free(mutex);
}

LIBMSE_API void libmse_mutex_lock(libmse_mutex_t *mutex) {
    if (mutex) pthread_mutex_lock(&mutex->handle);
}

LIBMSE_API void libmse_mutex_unlock(libmse_mutex_t *mutex) {
    if (mutex) pthread_mutex_unlock(&mutex->handle);
}

// The trampoline function that bridges POSIX standards to your engine's standards
static void* internal_thread_runner(void *arg) {
    thread_payload_t *payload = (thread_payload_t*)arg;
    
    // Execute the user's engine function
    int result = payload->func(payload->data);
    
    // Clean up the dynamically allocated payload
    free(payload);
    
    // Cast the integer result into a void pointer for pthread to catch
    return (void*)(intptr_t)result;
}

LIBMSE_API libmse_thread_t* libmse_thread_create(libmse_thread_func_t func, const char *name, void *data) {
    if (!func) return NULL;

    libmse_thread_t *thread = (libmse_thread_t*)malloc(sizeof(libmse_thread_t));
    if (!thread) return NULL;

    thread->name = name ? strdup(name) : strdup("libmse_worker");

    thread_payload_t *payload = (thread_payload_t*)malloc(sizeof(thread_payload_t));
    payload->func = func;
    payload->data = data;

    if (pthread_create(&thread->handle, NULL, internal_thread_runner, payload) != 0) {
        DEBUG_ERROR("Failed to spawn thread: %s", thread->name);
        free(payload);
        free(thread->name);
        free(thread);
        return NULL;
    }

    // Optional: Name the thread for external debuggers (Linux only, macOS requires it from inside)
    #if defined(DEBUG) || defined(OPTIMIZED_DEBUG)
        pthread_setname_np(thread->handle, thread->name);
    #endif

    return thread;
}

LIBMSE_API int libmse_thread_join(libmse_thread_t *thread) {
    if (!thread) return -1;

    void *ret_val = NULL;
    if (pthread_join(thread->handle, &ret_val) != 0) {
        DEBUG_ERROR("Failed to join thread: %s", thread->name);
        return -1; 
    }

    // Cast the pointer back to an integer exit code
    int exit_code = (int)(intptr_t)ret_val;
    
    free(thread->name);
    free(thread);
    
    return exit_code;
}

LIBMSE_API void libmse_thread_detach(libmse_thread_t *thread) {
    if (!thread) return;

    if (pthread_detach(thread->handle) != 0) {
        DEBUG_ERROR("Failed to detach thread: %s", thread->name);
    }

    free(thread->name);
    free(thread);
}

LIBMSE_API libmse_cond_t* libmse_cond_create(void) {
    libmse_cond_t *cond = (libmse_cond_t*)malloc(sizeof(libmse_cond_t));
    if (!cond) return NULL;

    if (pthread_cond_init(&cond->handle, NULL) != 0) {
        DEBUG_ERROR("Failed to initialize condition variable");
        free(cond);
        return NULL;
    }
    return cond;
}

LIBMSE_API void libmse_cond_destroy(libmse_cond_t *cond) {
    if (!cond) return;
    pthread_cond_destroy(&cond->handle);
    free(cond);
}

LIBMSE_API void libmse_cond_wait(libmse_cond_t *cond, libmse_mutex_t *mutex) {
    if (cond && mutex) {
        pthread_cond_wait(&cond->handle, &mutex->handle);
    }
}

LIBMSE_API void libmse_cond_signal(libmse_cond_t *cond) {
    if (cond) {
        pthread_cond_signal(&cond->handle);
    }
}