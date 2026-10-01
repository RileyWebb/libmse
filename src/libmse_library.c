#include "libmse/libmse_library.h"
#include "libmse/libmse_thread.h"
#include "libmse/libmse_lua.h"
#include "libmse/libmse_log.h"
#include "libmse/libmse_cvar.h"
#include "libmse/libmse_cmd.h"

#include <SDL3/SDL_filesystem.h>

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

// File scope rather than a field on the library: a cvar holds a raw pointer to
// whatever it was registered against, and a heap struct that can be destroyed
// would leave it dangling.
LIBMSE_CVAR_DEFINE_INT(g_download_covers, "libmse_scraper_covers", 1,
                       "Download cover art while scraping (0 = No, 1 = Yes)");

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

    // The cover is raw image bytes, so it comes across as a Lua string rather
    // than a C string: it is full of NULs and lua_tolstring hands back a
    // length. The mime and the URL it came from ride along so the UI knows how
    // to decode it and a rescrape can tell whether anything changed.
    size_t artwork_size = 0;
    const char *artwork = lua_tolstring(L, 11, &artwork_size);
    const char *artwork_mime = luaL_optstring(L, 12, NULL);
    const char *artwork_url  = luaL_optstring(L, 13, NULL);

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
    // COALESCE on every artwork column: a rescrape that fails to fetch a cover
    // must not wipe the one already stored.
    const char *sql_game = "INSERT INTO games (playmatch_id, name, release_year, artwork_blob, artwork_mime, "
                           "artwork_url, platform_id) "
                           "VALUES (?1, ?2, ?3, ?4, ?5, ?6, (SELECT id FROM platforms WHERE playmatch_id=?7)) "
                           "ON CONFLICT(playmatch_id) DO UPDATE SET "
                           "name=excluded.name, release_year=excluded.release_year, "
                           "artwork_blob=COALESCE(excluded.artwork_blob, games.artwork_blob), "
                           "artwork_mime=COALESCE(excluded.artwork_mime, games.artwork_mime), "
                           "artwork_url=COALESCE(excluded.artwork_url, games.artwork_url), "
                           "platform_id=excluded.platform_id;";
    if ((stmt = libmse_db_stmt_prepare(lib->db, sql_game))) {
        libmse_db_bind_text(stmt, 1, pm_game_id);
        libmse_db_bind_text(stmt, 2, pm_game_name);
        libmse_db_bind_int(stmt, 3, release_year);
        if (artwork && artwork_size > 0) libmse_db_bind_blob(stmt, 4, artwork, artwork_size);
        else libmse_db_bind_null(stmt, 4);
        if (artwork_mime) libmse_db_bind_text(stmt, 5, artwork_mime); else libmse_db_bind_null(stmt, 5);
        if (artwork_url) libmse_db_bind_text(stmt, 6, artwork_url); else libmse_db_bind_null(stmt, 6);
        libmse_db_bind_text(stmt, 7, pm_plat_id);
        libmse_db_stmt_step(stmt);
        libmse_db_stmt_finalize(stmt);
    }

    if (artwork_size > 0) {
        libmse_logf("library: stored %zu byte cover for '%s' (%s)", artwork_size, pm_game_name,
                    artwork_mime ? artwork_mime : "unknown type");
    }

    // D. UPDATE GAME FILE (Store Physical Hash Info & Link everything)
    //
    // A "local:" id means the scraper could not identify the dump and the row
    // above is a placeholder built from the file name. Recorded as Unmatched so
    // the difference is visible, and so a later pass can find them again.
    const bool unmatched = strncmp(pm_game_id, "local:", 6) == 0;

    const char *sql_file = unmatched
        ? "UPDATE game_files SET file_size = ?1, md5 = ?2, playmatch_id = ?3, status = 'Unmatched', "
          "game_id = (SELECT id FROM games WHERE playmatch_id = ?3) WHERE rom_path = ?4;"
        : "UPDATE game_files SET file_size = ?1, md5 = ?2, playmatch_id = ?3, status = 'Scraped', "
          "game_id = (SELECT id FROM games WHERE playmatch_id = ?3) WHERE rom_path = ?4;";
    if ((stmt = libmse_db_stmt_prepare(lib->db, sql_file))) {
        libmse_db_bind_int64(stmt, 1, file_size);
        libmse_db_bind_text(stmt, 2, file_md5);
        libmse_db_bind_text(stmt, 3, pm_game_id);
        libmse_db_bind_text(stmt, 4, rom_path);
        libmse_db_stmt_step(stmt);
        libmse_db_stmt_finalize(stmt);
    }

    // A rescrape that finally identifies a file leaves its placeholder behind
    // with nothing pointing at it. Only placeholders are swept up; a real game
    // with no files is the user's to remove.
    if (!unmatched) {
        libmse_db_exec("DELETE FROM games WHERE playmatch_id LIKE 'local:%' "
                       "AND id NOT IN (SELECT game_id FROM game_files WHERE game_id IS NOT NULL);",
                       lib->db);
    }

    libmse_db_unlock(lib->db);
    return 0;
}

// The library the console commands act on. There is one per process in
// practice, and a command has nowhere else to get it from.
static libmse_library_t *g_console_library = NULL;

static bool cmd_library_add_handler(int argc, const char **argv)
{
    if (argc < 1 || !g_console_library) return false;
    return libmse_library_add_game(g_console_library, argv[0]);
}

static bool cmd_library_add_folder_handler(int argc, const char **argv)
{
    if (argc < 1 || !g_console_library) return false;
    return libmse_library_add_folder(g_console_library, argv[0], true) > 0;
}

static bool cmd_library_forget_missing_handler(int argc, const char **argv)
{
    (void)argc;
    (void)argv;
    if (!g_console_library) return false;
    libmse_library_forget_missing(g_console_library);
    return true;
}

// Re-queues everything already in the database. The usual reason is that a
// scrape ran before cover art existed, or before the network was up.
static bool cmd_library_rescrape_handler(int argc, const char **argv)
{
    (void)argc;
    (void)argv;
    if (!g_console_library) return false;

    libmse_db_lock(g_console_library->db);
    libmse_stmt_t *stmt = libmse_db_stmt_prepare(g_console_library->db, "SELECT rom_path FROM game_files;");

    char **paths = NULL;
    size_t count = 0, capacity = 0;
    if (stmt) {
        while (libmse_db_stmt_step(stmt) == 1) {
            const char *path = libmse_db_col_text(stmt, 0);
            if (!path) continue;
            if (count == capacity) {
                capacity = capacity ? capacity * 2 : 16;
                char **grown = (char **)realloc(paths, capacity * sizeof(char *));
                if (!grown) break;
                paths = grown;
            }
            paths[count++] = strdup(path);
        }
        libmse_db_stmt_finalize(stmt);
    }
    libmse_db_unlock(g_console_library->db);

    // Queued outside the database lock: add_game takes it again itself.
    for (size_t i = 0; i < count; ++i) {
        libmse_library_add_game(g_console_library, paths[i]);
        free(paths[i]);
    }
    free(paths);

    libmse_logf("library: queued %zu title%s for rescrape", count, count == 1 ? "" : "s");
    return true;
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

    g_console_library = lib;
    libmse_cmd_register(&(libmse_cmd_t){"libmse_library_add", "Scans a ROM into the library", 1,
                                        cmd_library_add_handler, "<rom path>"});
    libmse_cmd_register(&(libmse_cmd_t){"libmse_library_rescrape",
                                        "Re-scrapes every title already in the library", 0,
                                        cmd_library_rescrape_handler});

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
    if (g_console_library == lib) g_console_library = NULL;

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

// ==========================================
// Folder scanning
// ==========================================

// Deliberately excludes the ambiguous containers -- bin, iso, img, cue -- which
// are as likely to be something else entirely. Those can still be added one at
// a time by path.
static const char *const LIBRARY_ROM_EXTENSIONS[] = {
    "nes", "fds", "unf", "unif", "tnes",
    "sfc", "smc", "fig", "swc",
    "gb", "gbc", "gba",
    "n64", "z64", "v64",
    "md", "gen", "smd", "sms", "gg", "sg",
    "pce", "sgx", "ws", "wsc", "ngp", "ngc",
    "a26", "a78", "lnx", "col", "int", "vec",
    "zip", "7z",
    NULL
};

static bool library_extension_matches(const char *ext)
{
    for (size_t i = 0; LIBRARY_ROM_EXTENSIONS[i] != NULL; ++i) {
        const char *candidate = LIBRARY_ROM_EXTENSIONS[i];
        size_t j = 0;
        for (; candidate[j] != '\0' && ext[j] != '\0'; ++j) {
            char a = ext[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
            if (a != candidate[j]) break;
        }
        if (candidate[j] == '\0' && ext[j] == '\0') return true;
    }
    return false;
}

LIBMSE_API bool libmse_library_is_rom_path(const char *path)
{
    if (!path) return false;

    const char *dot = strrchr(path, '.');
    if (!dot || dot[1] == '\0') return false;

    // A dot in a directory name further up the path is not an extension.
    if (strchr(dot, '/') || strchr(dot, '\\')) return false;

    return library_extension_matches(dot + 1);
}

typedef struct {
    libmse_library_t *lib;
    size_t            added;
    bool              recursive;
    int               depth;
} folder_scan_t;

#define LIBRARY_SCAN_MAX_DEPTH 8

static SDL_EnumerationResult SDLCALL library_scan_entry(void *userdata, const char *dirname, const char *fname)
{
    folder_scan_t *scan = (folder_scan_t *)userdata;

    // SDL hands back the directory with a trailing separator already.
    const size_t needed = strlen(dirname) + strlen(fname) + 2;
    char        *full   = (char *)malloc(needed);
    if (!full) return SDL_ENUM_FAILURE;
    snprintf(full, needed, "%s%s", dirname, fname);

    SDL_PathInfo info;
    if (SDL_GetPathInfo(full, &info)) {
        if (info.type == SDL_PATHTYPE_DIRECTORY) {
            if (scan->recursive && scan->depth < LIBRARY_SCAN_MAX_DEPTH) {
                ++scan->depth;
                SDL_EnumerateDirectory(full, library_scan_entry, scan);
                --scan->depth;
            }
        } else if (info.type == SDL_PATHTYPE_FILE && libmse_library_is_rom_path(full)) {
            if (libmse_library_add_game(scan->lib, full)) {
                scan->added++;
            }
        }
    }

    free(full);
    return SDL_ENUM_CONTINUE;
}

LIBMSE_API size_t libmse_library_add_folder(libmse_library_t *lib, const char *path, bool recursive)
{
    if (!lib || !path) return 0;

    folder_scan_t scan = {lib, 0, recursive, 0};
    if (!SDL_EnumerateDirectory(path, library_scan_entry, &scan)) {
        libmse_logf("library: could not read '%s': %s", path, SDL_GetError());
        return 0;
    }

    libmse_logf("library: queued %zu ROM%s from '%s'", scan.added, scan.added == 1 ? "" : "s", path);
    return scan.added;
}

// ==========================================
// Maintenance
// ==========================================

LIBMSE_API size_t libmse_library_forget_missing(libmse_library_t *lib)
{
    if (!lib) return 0;

    libmse_db_lock(lib->db);

    // Collected first and deleted after: stepping a SELECT while DELETEing the
    // rows out from under it is asking for trouble.
    char **gone  = NULL;
    size_t count = 0, capacity = 0;

    libmse_stmt_t *stmt = libmse_db_stmt_prepare(lib->db, "SELECT rom_path FROM game_files;");
    if (stmt) {
        while (libmse_db_stmt_step(stmt) == 1) {
            const char *path = libmse_db_col_text(stmt, 0);
            if (!path) continue;

            SDL_PathInfo info;
            if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE) continue;

            if (count == capacity) {
                capacity = capacity ? capacity * 2 : 16;
                char **grown = (char **)realloc(gone, capacity * sizeof(char *));
                if (!grown) break;
                gone = grown;
            }
            gone[count++] = strdup(path);
        }
        libmse_db_stmt_finalize(stmt);
    }

    for (size_t i = 0; i < count; ++i) {
        libmse_stmt_t *del = libmse_db_stmt_prepare(lib->db, "DELETE FROM game_files WHERE rom_path = ?1;");
        if (del) {
            libmse_db_bind_text(del, 1, gone[i]);
            libmse_db_stmt_step(del);
            libmse_db_stmt_finalize(del);
        }
        free(gone[i]);
    }
    free(gone);

    // A game with no remaining files is no longer in the library.
    libmse_db_exec("DELETE FROM games WHERE id NOT IN (SELECT game_id FROM game_files WHERE game_id IS NOT NULL);",
                   lib->db);

    libmse_db_unlock(lib->db);

    libmse_logf("library: forgot %zu missing file%s", count, count == 1 ? "" : "s");
    return count;
}

LIBMSE_API bool libmse_library_clear(libmse_library_t *lib)
{
    if (!lib) return false;

    libmse_db_lock(lib->db);
    const bool ok = libmse_db_exec("DELETE FROM game_files; DELETE FROM games; DELETE FROM platforms; "
                                   "DELETE FROM companies; DELETE FROM external_metadata; "
                                   "DELETE FROM verification_data;",
                                   lib->db);
    libmse_db_unlock(lib->db);

    libmse_log(ok ? "library: cleared" : "library: could not be cleared");
    return ok;
}

static size_t library_count_of(libmse_library_t *lib, const char *sql)
{
    libmse_stmt_t *stmt = libmse_db_stmt_prepare(lib->db, sql);
    if (!stmt) return 0;

    const size_t value = (libmse_db_stmt_step(stmt) == 1) ? (size_t)libmse_db_col_int64(stmt, 0) : 0;
    libmse_db_stmt_finalize(stmt);
    return value;
}

LIBMSE_API void libmse_library_stats(libmse_library_t *lib, size_t *out_games, size_t *out_files,
                                     size_t *out_with_art)
{
    if (out_games) *out_games = 0;
    if (out_files) *out_files = 0;
    if (out_with_art) *out_with_art = 0;
    if (!lib) return;

    libmse_db_lock(lib->db);
    if (out_games) *out_games = library_count_of(lib, "SELECT count(*) FROM games;");
    if (out_files) *out_files = library_count_of(lib, "SELECT count(*) FROM game_files;");
    if (out_with_art)
        *out_with_art = library_count_of(lib, "SELECT count(*) FROM games WHERE artwork_blob IS NOT NULL;");
    libmse_db_unlock(lib->db);
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
                     "dispatch_async_scrape_request([====[%s]====], %d, [====[%s]====], [====[%s]====], %d)",
                     job->rom_path, (int)scraper, id ? id : "", secret ? secret : "", *g_download_covers);
            
            libmse_lua_worker_execute_string(lib->lua_worker, lua_payload);
        }

        if (id) free(id);
        if (secret) free(secret);
        free(job->rom_path);
        free(job);
    }

    return 0;
}