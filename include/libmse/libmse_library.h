#ifndef LIBMSE_LIBRARY_H
#define LIBMSE_LIBRARY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#include "libmse/libmse_api.h"
#include "libmse/libmse_db.h"

typedef enum {
    LIBMSE_SCRAPER_PLAYMATCH = 0, // https://playmatch.retrorealm.dev
    LIBMSE_SCRAPER_SCREENSCRAPER, // https://www.screenscraper.fr
    LIBMSE_SCRAPER_IGDB           // https://www.igdb.com
} libmse_scraper_t;

typedef struct {
    char *name;
    char *description;
    char *developer;
    char *platform;
    int release_year;
    
    void *artwork_data;
    size_t artwork_size;
} libmse_game_meta_t;

typedef struct libmse_library_s libmse_library_t;

typedef bool (*libmse_library_meta_handler_t)(const char *rom_path, 
                                              libmse_game_meta_t *out_meta, 
                                              void *user_data);

// Lifecyle & Allocation
LIBMSE_API libmse_library_t* libmse_library_create(libmse_db_t *db);
LIBMSE_API void              libmse_library_destroy(libmse_library_t *lib);

// Scraper Engine Configuration
LIBMSE_API void libmse_library_set_scraper(libmse_library_t *lib, libmse_scraper_t scraper);
LIBMSE_API void libmse_library_set_credentials(libmse_library_t *lib, 
                                               const char *client_id, 
                                               const char *client_secret);

// Extension Handler Registry
LIBMSE_API void libmse_library_register_handler(libmse_library_t *lib, 
                                                const char *extension, 
                                                libmse_library_meta_handler_t handler, 
                                                void *user_data);

// Non-blocking asynchronous task pipeline entrypoint
LIBMSE_API bool libmse_library_add_game(libmse_library_t *lib, const char *rom_path);

// Queues every file under `path` whose extension looks like a ROM, returning
// how many were queued. Scanning is synchronous, but the scraping it queues is
// not, so this returns as soon as the walk is done.
LIBMSE_API size_t libmse_library_add_folder(libmse_library_t *lib, const char *path, bool recursive);

// True when `path` ends in an extension the folder scan recognises.
LIBMSE_API bool libmse_library_is_rom_path(const char *path);

// Drops entries whose file is no longer on disk. Returns how many went.
LIBMSE_API size_t libmse_library_forget_missing(libmse_library_t *lib);

// Empties every table. The database file itself is left in place.
LIBMSE_API bool libmse_library_clear(libmse_library_t *lib);

// Counts for the management UI. Any out pointer may be NULL.
LIBMSE_API void libmse_library_stats(libmse_library_t *lib, size_t *out_games, size_t *out_files,
                                     size_t *out_with_art);

LIBMSE_API void libmse_game_meta_cleanup(libmse_game_meta_t *meta);

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_LIBRARY_H