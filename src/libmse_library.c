#include "libmse/libmse_library.h"
#include "libmse/libmse_thread.h"
#include "libmse/libmse_lua.h"

// Bring in core Lua stack definitions for metadata injection bindings
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <sqlite3.h> // TODO: Remove direct sqlite3.h dependency and use libmse_db_t API wrappers instead

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct handler_node_s {
    char                          *extension;
    libmse_library_meta_handler_t  handler;
    void                          *user_data;
    struct handler_node_s         *next;
} handler_node_t;

typedef struct job_node_s {
    char              *rom_path;
    struct job_node_s *next;
} job_node_t;

struct libmse_library_s {
    libmse_db_t         *db;
    handler_node_t      *handlers_head;
    
    libmse_scraper_t     active_scraper;
    char                *api_client_id;
    char                *api_client_secret;

    // Orchestration built entirely on top of your wrappers
    libmse_thread_t     *worker_thread;
    libmse_mutex_t      *queue_lock;
    libmse_cond_t       *queue_cond;
    libmse_lua_worker_t *lua_worker;
    
    job_node_t          *job_head;
    job_node_t          *job_tail;
    bool                 shutdown;
};

// Forward declaration matching engine framework standards
static int background_worker_loop(void *arg);

LIBMSE_API void libmse_game_meta_cleanup(libmse_game_meta_t *meta)
{
    if (!meta) return;
    if (meta->name) free(meta->name);
    if (meta->description) free(meta->description);
    if (meta->developer) free(meta->developer);
    if (meta->platform) free(meta->platform);
    if (meta->artwork_data) free(meta->artwork_data);
    memset(meta, 0, sizeof(libmse_game_meta_t));
}

static char *extract_fallback_title(const char *path)
{
    const char *base = strrchr(path, '/');
    base             = base ? base + 1 : path;
    const char *ext  = strrchr(base, '.');
    size_t      len  = ext ? (size_t)(ext - base) : strlen(base);

    char *title = (char *)malloc(len + 1);
    strncpy(title, base, len);
    title[len] = '\0';
    return title;
}

// C Interop Bridge: Handles database commits pushed from inside running Lua worker steps
static int l_update_game_metadata(lua_State *L) {
    libmse_library_t *lib = (libmse_library_t *)lua_touserdata(L, lua_upvalueindex(1));
    
    // 1. File Info
    const char *rom_path   = luaL_checkstring(L, 1);
    int64_t file_size      = (int64_t)luaL_checkinteger(L, 2);
    const char *file_md5   = luaL_checkstring(L, 3);
    
    // 2. Game Info
    const char *pm_game_id   = luaL_checkstring(L, 4);
    const char *pm_game_name = luaL_checkstring(L, 5);
    
    // 3. Platform Info
    const char *pm_plat_id   = luaL_optstring(L, 6, "");
    const char *pm_plat_name = luaL_optstring(L, 7, "");
    
    // 4. Company (Developer/Publisher) Info
    const char *pm_comp_id   = luaL_optstring(L, 8, "");
    const char *pm_comp_name = luaL_optstring(L, 9, "");
    
    // 5. Enrichment
    int release_year = (int)luaL_checkinteger(L, 10);
    size_t artwork_size = 0;
    const char *artwork = lua_tolstring(L, 11, &artwork_size);

    libmse_db_lock(lib->db);
    libmse_stmt_t *stmt;

    // A. UPSERT COMPANY (Developer)
    if (strlen(pm_comp_id) > 0) {
        const char *sql_comp = "INSERT INTO companies (playmatch_id, name) VALUES (?1, ?2) "
                               "ON CONFLICT(playmatch_id) DO UPDATE SET name=excluded.name;";
        if ((stmt = libmse_db_stmt_prepare(lib->db, sql_comp))) {
            libmse_db_bind_text(stmt, 1, pm_comp_id);
            libmse_db_bind_text(stmt, 2, pm_comp_name);
            libmse_db_stmt_step(stmt);
            libmse_db_stmt_finalize(stmt);
        }
    }

    // B. UPSERT PLATFORM (and link to Company)
    if (strlen(pm_plat_id) > 0) {
        const char *sql_plat = "INSERT INTO platforms (playmatch_id, name, company_id) "
                               "VALUES (?1, ?2, (SELECT id FROM companies WHERE playmatch_id=?3)) "
                               "ON CONFLICT(playmatch_id) DO UPDATE SET "
                               "name=excluded.name, company_id=excluded.company_id;";
        if ((stmt = libmse_db_stmt_prepare(lib->db, sql_plat))) {
            libmse_db_bind_text(stmt, 1, pm_plat_id);
            libmse_db_bind_text(stmt, 2, pm_plat_name);
            libmse_db_bind_text(stmt, 3, pm_comp_id);
            libmse_db_stmt_step(stmt);
            libmse_db_stmt_finalize(stmt);
        }
    }

    // C. UPSERT CORE GAME (and link to Platform)
    const char *sql_game = "INSERT INTO games (playmatch_id, name, release_year, artwork_blob, platform_id) "
                           "VALUES (?1, ?2, ?3, ?4, (SELECT id FROM platforms WHERE playmatch_id=?5)) "
                           "ON CONFLICT(playmatch_id) DO UPDATE SET "
                           "name=excluded.name, release_year=excluded.release_year, "
                           "artwork_blob=COALESCE(excluded.artwork_blob, games.artwork_blob), "
                           "platform_id=excluded.platform_id;";
    if ((stmt = libmse_db_stmt_prepare(lib->db, sql_game))) {
        libmse_db_bind_text(stmt, 1, pm_game_id);
        libmse_db_bind_text(stmt, 2, pm_game_name);
        libmse_db_bind_int(stmt, 3, release_year);
        if (artwork && artwork_size > 0) libmse_db_bind_blob(stmt, 4, artwork, artwork_size);
        else libmse_db_bind_null(stmt, 4);
        libmse_db_bind_text(stmt, 5, pm_plat_id);
        libmse_db_stmt_step(stmt);
        libmse_db_stmt_finalize(stmt);
    }

    // D. UPDATE GAME FILE (Store Physical Hash Info & Link everything)
    const char *sql_file = "UPDATE game_files SET "
                           "file_size = ?1, md5 = ?2, playmatch_id = ?3, "
                           "status = 'Scraped', "
                           "game_id = (SELECT id FROM games WHERE playmatch_id = ?3) "
                           "WHERE rom_path = ?4;";
    if ((stmt = libmse_db_stmt_prepare(lib->db, sql_file))) {
        libmse_db_bind_int64(stmt, 1, file_size);
        libmse_db_bind_text(stmt, 2, file_md5);
        libmse_db_bind_text(stmt, 3, pm_game_id);
        libmse_db_bind_text(stmt, 4, rom_path);
        libmse_db_stmt_step(stmt);
        libmse_db_stmt_finalize(stmt);
    }

    libmse_db_unlock(lib->db);
    return 0;
}

LIBMSE_API libmse_library_t *libmse_library_create(libmse_db_t *db)
{
    if (!db) return NULL;

    libmse_library_t *lib = (libmse_library_t *)malloc(sizeof(libmse_library_t));
    lib->db                 = db;
    lib->handlers_head      = NULL;
    lib->active_scraper     = LIBMSE_SCRAPER_PLAYMATCH;
    lib->api_client_id      = NULL;
    lib->api_client_secret  = NULL;
    lib->job_head           = NULL;
    lib->job_tail           = NULL;
    lib->shutdown           = false;

    // Allocate structural wrappers cleanly
    lib->queue_lock         = libmse_mutex_create();
    lib->queue_cond         = libmse_cond_create();
    
    // Create an unthreaded Lua worker. We run it directly inside our managed loop thread.
    lib->lua_worker         = libmse_lua_worker_create("libmse Scraper Engine Context", false);

    if (lib->lua_worker && lib->lua_worker->L) {
        // Inject runtime context and database persistence bridges directly into the worker state
        lua_pushlightuserdata(lib->lua_worker->L, lib);
        lua_pushcclosure(lib->lua_worker->L, l_update_game_metadata, 1);
        lua_setglobal(lib->lua_worker->L, "update_game_db_record");

        // Execute the orchestration script initialization step
        libmse_lua_worker_execute_script(lib->lua_worker, "data/lua/metadata_scrapper.lua");
    }

    // Spawn a long-running, persistent background task loop thread
    lib->worker_thread = libmse_thread_create(background_worker_loop, "libmse_library_worker", lib);

    return lib;
}

LIBMSE_API void libmse_library_destroy(libmse_library_t *lib)
{
    if (!lib) return;

    // Raise shutdown flags under mutex protections
    libmse_mutex_lock(lib->queue_lock);
    lib->shutdown = true;
    libmse_cond_signal(lib->queue_cond);
    libmse_mutex_unlock(lib->queue_lock);

    // Block until the background run cycle cleans up safely
    libmse_thread_join(lib->worker_thread);

    // Free stuck queue entries
    job_node_t *job = lib->job_head;
    while (job) {
        job_node_t *next = job->next;
        free(job->rom_path);
        free(job);
        job = next;
    }

    handler_node_t *current = lib->handlers_head;
    while (current) {
        handler_node_t *next = current->next;
        free(current->extension);
        free(current);
        current = next;
    }

    if (lib->api_client_id) free(lib->api_client_id);
    if (lib->api_client_secret) free(lib->api_client_secret);
    
    // Destroy managed worker contexts
    libmse_lua_worker_destroy(lib->lua_worker);
    libmse_mutex_destroy(lib->queue_lock);
    libmse_cond_destroy(lib->queue_cond);

    free(lib);
}

LIBMSE_API void libmse_library_set_scraper(libmse_library_t *lib, libmse_scraper_t scraper)
{
    if (!lib) return;
    libmse_mutex_lock(lib->queue_lock);
    lib->active_scraper = scraper;
    libmse_mutex_unlock(lib->queue_lock);
}

LIBMSE_API void libmse_library_set_credentials(libmse_library_t *lib, const char *client_id, const char *client_secret)
{
    if (!lib) return;
    libmse_mutex_lock(lib->queue_lock);
    if (lib->api_client_id) { free(lib->api_client_id); lib->api_client_id = NULL; }
    if (lib->api_client_secret) { free(lib->api_client_secret); lib->api_client_secret = NULL; }
    
    if (client_id) lib->api_client_id = strdup(client_id);
    if (client_secret) lib->api_client_secret = strdup(client_secret);
    libmse_mutex_unlock(lib->queue_lock);
}

LIBMSE_API bool libmse_library_add_game(libmse_library_t *lib, const char *rom_path)
{
    if (!lib || !rom_path) return false;

    // Helper to grab just the file name
    const char *file_name = strrchr(rom_path, '/');
    if (!file_name) file_name = strrchr(rom_path, '\\');
    file_name = file_name ? file_name + 1 : rom_path;

    bool file_inserted = false;

    libmse_db_lock(lib->db);
    
    // Insert the physical file stub ONLY. game_id is left NULL initially.
    const char *sql_file = "INSERT OR IGNORE INTO game_files (rom_path, file_name, status) VALUES (?1, ?2, 'Pending');";
    libmse_stmt_t *stmt_file = libmse_db_stmt_prepare(lib->db, sql_file);
    if (stmt_file) {
        libmse_db_bind_text(stmt_file, 1, rom_path);
        libmse_db_bind_text(stmt_file, 2, file_name);
        
        if (libmse_db_stmt_step(stmt_file) == 0) { // SQLITE_DONE
            file_inserted = true;
        }
        libmse_db_stmt_finalize(stmt_file);
    }
    
    libmse_db_unlock(lib->db);

    // Guard: Only queue async job if the file insertion succeeded
    if (!file_inserted) return false;

    // Push into the thread-safe queue for scraping
    job_node_t *job = (job_node_t *)malloc(sizeof(job_node_t));
    if (!job) return false;
    job->rom_path = strdup(rom_path);
    job->next = NULL;

    libmse_mutex_lock(lib->queue_lock);
    if (!lib->job_tail) {
        lib->job_head = job;
        lib->job_tail = job;
    } else {
        lib->job_tail->next = job;
        lib->job_tail = job;
    }
    libmse_cond_signal(lib->queue_cond);
    libmse_mutex_unlock(lib->queue_lock);

    return true;
}

// Background Worker Consumer Loop Structure
static int background_worker_loop(void *arg)
{
    libmse_library_t *lib = (libmse_library_t *)arg;

    while (true) {
        libmse_mutex_lock(lib->queue_lock);
        while (!lib->job_head && !lib->shutdown) {
            libmse_cond_wait(lib->queue_cond, lib->queue_lock);
        }

        if (lib->shutdown) {
            libmse_mutex_unlock(lib->queue_lock);
            break;
        }

        // Pop the front task node safely
        job_node_t *job = lib->job_head;
        lib->job_head = job->next;
        if (!lib->job_head) lib->job_tail = NULL;
        
        libmse_scraper_t scraper = lib->active_scraper;
        char *id = lib->api_client_id ? strdup(lib->api_client_id) : NULL;
        char *secret = lib->api_client_secret ? strdup(lib->api_client_secret) : NULL;
        libmse_mutex_unlock(lib->queue_lock);

        // Safely evaluate queries over the unthreaded worker instance inside this worker context
        if (lib->lua_worker) {
            char lua_payload[2048];
            snprintf(lua_payload, sizeof(lua_payload), 
                     "dispatch_async_scrape_request([====[%s]====], %d, [====[%s]====], [====[%s]====])",
                     job->rom_path, (int)scraper, id ? id : "", secret ? secret : "");
            
            libmse_lua_worker_execute_string(lib->lua_worker, lua_payload);
        }

        if (id) free(id);
        if (secret) free(secret);
        free(job->rom_path);
        free(job);
    }

    return 0;
}