#ifndef LIBMSE_DB_H
#define LIBMSE_DB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libmse/libmse_thread.h"
#include "libmse/libmse_api.h"

typedef struct sqlite3		sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

typedef struct libmse_db_s {
	sqlite3		   *handle;
	char		   *db_path;
	libmse_mutex_t *mutex;
} libmse_db_t;

typedef struct libmse_stmt_s {
	sqlite3_stmt *handle;
	libmse_db_t	 *db;
} libmse_stmt_t;

// Lifecycle & Execution
LIBMSE_API libmse_db_t *libmse_db_open(const char *path);
LIBMSE_API void			libmse_db_close(libmse_db_t *db);
LIBMSE_API bool			libmse_db_exec(const char *sql, libmse_db_t *db);

// Utility
LIBMSE_API int64_t libmse_db_last_insert_rowid(libmse_db_t *db);

// Transactions
LIBMSE_API bool libmse_db_begin(libmse_db_t *db);
LIBMSE_API bool libmse_db_commit(libmse_db_t *db);
LIBMSE_API bool libmse_db_rollback(libmse_db_t *db);

// Prepared Statements
LIBMSE_API libmse_stmt_t *libmse_db_stmt_prepare(libmse_db_t *db, const char *sql);
LIBMSE_API void			  libmse_db_stmt_finalize(libmse_stmt_t *stmt);
LIBMSE_API int			  libmse_db_stmt_step(libmse_stmt_t *stmt);
LIBMSE_API bool			  libmse_db_stmt_reset(libmse_stmt_t *stmt);

// Parameter Binding
LIBMSE_API bool libmse_db_bind_int(libmse_stmt_t *stmt, int index, int value);
LIBMSE_API bool libmse_db_bind_int64(libmse_stmt_t *stmt, int index, int64_t value);
LIBMSE_API bool libmse_db_bind_double(libmse_stmt_t *stmt, int index, double value);
LIBMSE_API bool libmse_db_bind_text(libmse_stmt_t *stmt, int index, const char *value);
LIBMSE_API bool libmse_db_bind_blob(libmse_stmt_t *stmt, int index, const void *data, size_t size);
LIBMSE_API bool libmse_db_bind_null(libmse_stmt_t *stmt, int index);

// Column Retrieval
LIBMSE_API int		   libmse_db_col_int(libmse_stmt_t *stmt, int col);
LIBMSE_API int64_t	   libmse_db_col_int64(libmse_stmt_t *stmt, int col);
LIBMSE_API double	   libmse_db_col_double(libmse_stmt_t *stmt, int col);
LIBMSE_API const char *libmse_db_col_text(libmse_stmt_t *stmt, int col);
LIBMSE_API const void *libmse_db_col_blob(libmse_stmt_t *stmt, int col);
LIBMSE_API size_t	   libmse_db_col_bytes(libmse_stmt_t *stmt, int col);

inline void libmse_db_lock(libmse_db_t *db) { if (db) libmse_mutex_lock(db->mutex); }
inline void libmse_db_unlock(libmse_db_t *db) { if (db) libmse_mutex_unlock(db->mutex); }

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_DB_H