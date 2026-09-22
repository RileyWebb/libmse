#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "libmse/libmse.h"
#include "libmse/libmse_debug.h"
#include "libmse/libmse_lua.h"

typedef struct {
    libmse_lua_worker_t *worker;
    char *payload;
    bool is_file;
} lua_thread_task_t;

static libmse_lua_worker_t **lua_workers = NULL;
static size_t lua_worker_count = 0;

static libmse_lua_worker_t *default_worker = NULL;

static int libmse_lua_print(lua_State* L) {
    int n = lua_gettop(L);  
    
    lua_getglobal(L, "tostring");

    for (int i = 1; i <= n; i++) {
        const char* s;
        size_t l;
        
        lua_pushvalue(L, -1);  
        lua_pushvalue(L, i);   
        lua_call(L, 1, 1);
        
        s = lua_tolstring(L, -1, &l); 
        if (s == NULL) {
            return luaL_error(L, "'tostring' must return a string to print");
        }
        
        libmse_log_print(s); 
        
        if (i < n) {
            libmse_log_print("\t"); 
        }
        
        lua_pop(L, 1); 
    }
    
    return 0;
}

LIBMSE_API void libmse_lua_register_include_dir(lua_State* L, const char* path) {
    if (!L || !path) return;

    lua_getglobal(L, "package");
    
    lua_getfield(L, -1, "path");
    const char* current_path = lua_tostring(L, -1);

    char new_path[4096];
    snprintf(new_path, sizeof(new_path), "%s;%s/?.lua;%s/?/init.lua", current_path, path, path);

    lua_pop(L, 1); 
    
    lua_pushstring(L, new_path);
    lua_setfield(L, -2, "path");
    
    lua_getfield(L, -1, "cpath");
    const char* current_cpath = lua_tostring(L, -1);

    char new_cpath[4096];
    snprintf(new_cpath, sizeof(new_cpath), "%s;%s/?" MSE_LIBRARY_EXTENSION, current_cpath, path);

    lua_pop(L, 1); 
    
    lua_pushstring(L, new_cpath);
    lua_setfield(L, -2, "cpath");

    lua_pop(L, 1);
}

static int push_traceback_handler(lua_State *L) {
    lua_getglobal(L, "debug");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1); 
        return 0;      
    }
    
    lua_getfield(L, -1, "traceback");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2); 
        return 0;
    }
    
    lua_remove(L, -2); 
    return lua_gettop(L);
}

static void register_lua_functions(lua_State *L) {
    lua_pushcfunction(L, libmse_lua_print);
    lua_setglobal(L, "print");
}

// Synchronous execution core used by both threaded and non-threaded paths
static bool execute_core(lua_State *L, const char *payload, bool is_file) {
    int msgh_idx = push_traceback_handler(L);
    int load_status;

    if (is_file) {
        load_status = luaL_loadfile(L, payload);
    } else {
        load_status = luaL_loadstring(L, payload);
    }

    if (load_status != LUA_OK) {
        DEBUG_ERROR("Lua compilation error in %s:\n%s", is_file ? payload : "string", lua_tostring(L, -1));
        lua_pop(L, 1);
        if (msgh_idx > 0) lua_pop(L, 1);
        return false;
    }

    int result = lua_pcall(L, 0, LUA_MULTRET, msgh_idx);
    if (result != LUA_OK) {
        DEBUG_ERROR("Lua runtime error in %s:\n%s", is_file ? payload : "string", lua_tostring(L, -1));
        lua_pop(L, 1);
        if (msgh_idx > 0) lua_pop(L, 1);
        return false;
    }

    if (msgh_idx > 0) lua_pop(L, 1);
    return true;
}

// Background thread entry point
static void* worker_thread_routine(void* arg) {
    lua_thread_task_t *task = (lua_thread_task_t*)arg;
    
    // Execute the payload
    execute_core(task->worker->L, task->payload, task->is_file);
    
    // Clean up task memory and clear busy flag
    task->worker->is_busy = false;
    free(task->payload);
    free(task);
    
    return NULL;
}

LIBMSE_API libmse_lua_worker_t *libmse_lua_worker_create(char *name, bool is_threaded) {
    libmse_lua_worker_t *worker = malloc(sizeof(libmse_lua_worker_t));
    if (!worker) return NULL;

    worker->name = strdup(name ? name : "unnamed_worker");
    worker->is_threaded = is_threaded;
    worker->is_busy = false;
    
    if (is_threaded) {
        worker->thread = malloc(sizeof(pthread_t));
    } else {
        worker->thread = NULL;
    }

    worker->L = luaL_newstate();
    if (!worker->L) {
        DEBUG_ERROR("Failed to initialize Lua state for worker: %s", worker->name);
        free(worker->name);
        if (worker->thread) free(worker->thread);
        free(worker);
        return NULL;
    }

    luaL_openlibs(worker->L);
    libmse_lua_register_include_dir(worker->L, "data/lua"); 
    libmse_lua_register_include_dir(worker->L, "data/lua/socket"); 
    libmse_lua_register_include_dir(worker->L, "data/lua/mime"); 

    register_lua_functions(worker->L);

    lua_workers = realloc(lua_workers, sizeof(libmse_lua_worker_t*) * (lua_worker_count + 1));
    lua_workers[lua_worker_count++] = worker;

    return worker;
}

LIBMSE_API void libmse_lua_worker_destroy(libmse_lua_worker_t *worker) {
    if (!worker) return;

    // Ensure we don't kill a state while the thread is actively executing it
    libmse_lua_worker_wait(worker);

    for (size_t i = 0; i < lua_worker_count; i++) {
        if (lua_workers[i] == worker) {
            for (size_t j = i; j < lua_worker_count - 1; j++) {
                lua_workers[j] = lua_workers[j + 1];
            }
            lua_worker_count--;
            break;
        }
    }

    if (worker->L)
        lua_close(worker->L);
    
    if (worker->thread)
        free(worker->thread);
    
    if (worker->name)
        free(worker->name);
    
    free(worker);


}

LIBMSE_API bool libmse_lua_worker_execute_string(libmse_lua_worker_t *worker, const char *code) {
    if (!worker || !code) return false;
    if (worker->is_busy) return false;

    if (worker->is_threaded) {
        worker->is_busy = true;
        
        lua_thread_task_t *task = malloc(sizeof(lua_thread_task_t));
        task->worker = worker;
        task->payload = strdup(code);
        task->is_file = false;

        if (pthread_create((pthread_t*)worker->thread, NULL, worker_thread_routine, task) != 0) {
            DEBUG_ERROR("Failed to create Lua worker thread for string execution.");
            worker->is_busy = false;
            free(task->payload);
            free(task);
            return false;
        }
        pthread_setname_np(*(pthread_t*)worker->thread, worker->name ? worker->name : "LuaWorker");
        return true;
    } 

    return execute_core(worker->L, code, false);
}

LIBMSE_API bool libmse_lua_worker_execute_script(libmse_lua_worker_t *worker, const char *script_path) {
    if (!worker || !script_path) return false;
    if (worker->is_busy) return false;

    if (worker->is_threaded) {
        worker->is_busy = true;
        
        lua_thread_task_t *task = malloc(sizeof(lua_thread_task_t));
        task->worker = worker;
        task->payload = strdup(script_path);
        task->is_file = true;

        if (pthread_create((pthread_t*)worker->thread, NULL, worker_thread_routine, task) != 0) {
            DEBUG_ERROR("Failed to create Lua worker thread for script execution.");
            worker->is_busy = false;
            free(task->payload);
            free(task);
            return false;
        }
        
        pthread_setname_np(*(pthread_t*)worker->thread, worker->name ? worker->name : "LuaWorker");
        return true;
    } 

    return execute_core(worker->L, script_path, true);
}

LIBMSE_API bool libmse_lua_worker_is_busy(libmse_lua_worker_t *worker) {
    if (!worker) return false;
    // Note: For strict concurrency, std::atomic or a mutex should protect this read.
    return worker->is_busy;
}

LIBMSE_API void libmse_lua_worker_set_busy(libmse_lua_worker_t *worker, bool busy) {
    if (!worker) return;
    worker->is_busy = busy;
}

LIBMSE_API void libmse_lua_worker_wait(libmse_lua_worker_t *worker) {
    if (!worker || !worker->is_threaded || !worker->thread) return;

    if (worker->is_busy) {
        pthread_join(*(pthread_t*)worker->thread, NULL);
        worker->is_busy = false;
    }
}

LIBMSE_API void libmse_lua_get_worker_list(libmse_lua_worker_t ***workers, size_t *count) {
    if (workers) *workers = lua_workers;
    if (count) *count = lua_worker_count;
}

LIBMSE_API libmse_lua_worker_t *libmse_lua_get_default_worker() {
    return default_worker;
}

void libmse_lua_init(void) {
    default_worker = libmse_lua_worker_create("libmse Default Lua Worker", false);
}

void libmse_lua_shutdown(void) {
    libmse_lua_worker_destroy(default_worker);
    for (size_t i = 0; i < lua_worker_count; i++) {
        libmse_lua_worker_destroy(lua_workers[i]);
    }
    free(lua_workers);
    lua_workers = NULL;
    lua_worker_count = 0;
}
