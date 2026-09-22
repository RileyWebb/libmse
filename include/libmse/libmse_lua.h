#ifndef LIBMSE_LUA_H
#define LIBMSE_LUA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "libmse_thread.h"
#include "libmse_api.h"

typedef struct lua_State lua_State;
typedef struct LuaScript LuaScript;

typedef struct libmse_lua_worker_s {
    lua_State *L;
    libmse_thread_t *thread;
    char *name;
    bool is_busy;
    bool is_threaded;
} libmse_lua_worker_t;

//lua_ctx.c
LIBMSE_API libmse_lua_worker_t *libmse_lua_worker_create(char *name, bool is_threaded);
LIBMSE_API void libmse_lua_worker_destroy(libmse_lua_worker_t *worker);
LIBMSE_API libmse_lua_worker_t *libmse_lua_get_default_worker();

LIBMSE_API bool libmse_lua_worker_execute_string(libmse_lua_worker_t *worker, const char *code);
LIBMSE_API bool libmse_lua_worker_execute_script(libmse_lua_worker_t *worker, const char *script_path);
LIBMSE_API bool libmse_lua_worker_is_busy(libmse_lua_worker_t *worker);
LIBMSE_API void libmse_lua_worker_set_busy(libmse_lua_worker_t *worker, bool busy);
LIBMSE_API void libmse_lua_worker_wait(libmse_lua_worker_t *worker);
LIBMSE_API void libmse_lua_get_worker_list(libmse_lua_worker_t ***workers, size_t *count);
LIBMSE_API void libmse_lua_register_include_dir(lua_State* L, const char* path);

void libmse_lua_init(void);
void libmse_lua_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_LUA_H