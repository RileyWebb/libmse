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

LIBMSE_API void libmse_game_meta_cleanup(libmse_game_meta_t *meta);

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_LIBRARY_H