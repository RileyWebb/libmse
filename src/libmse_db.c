#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "libmse/libmse_debug.h"
#include "libmse/libmse_db.h"

// ==========================================
// Connection Management
// ==========================================

LIBMSE_API libmse_db_t* libmse_db_open(const char *path) {
    libmse_db_t *db = (libmse_db_t*)malloc(sizeof(libmse_db_t));
    if (!db) return NULL;

    db->db_path = strdup(path);
    db->mutex = libmse_mutex_create();
    
    // Initialize the libmse mutex with default attributes
    if (!db->mutex) {
        DEBUG_ERROR("Failed to initialize database mutex");
        free(db->db_path);
        free(db);
        return NULL;
    }
    
    // Open in Serialized (Thread-Safe) mode natively
    int rc = sqlite3_open_v2(path, &db->handle, 
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, 
                             NULL);

    if (rc != SQLITE_OK) {
        DEBUG_ERROR("Cannot open database: %s", sqlite3_errmsg(db->handle));
        if (db->handle) sqlite3_close(db->handle);
        libmse_mutex_destroy(db->mutex);
        free(db->db_path);
        free(db);
        return NULL;
    }
    
    // Enable WAL mode for concurrent read/write scaling
    libmse_db_exec("PRAGMA journal_mode=WAL;", db);
    
    return db;
}

LIBMSE_API void libmse_db_close(libmse_db_t *db) {
    if (!db) return;
    
    // Ensure no one is currently writing before closing
    libmse_mutex_lock(db->mutex);
    if (db->handle) sqlite3_close(db->handle);
    libmse_mutex_unlock(db->mutex);

    // Destroy the mutex immediately after we are done with the lock
    libmse_mutex_destroy(db->mutex);
    
    if (db->db_path) free(db->db_path);
    free(db);
}

LIBMSE_API bool libmse_db_exec(const char *sql, libmse_db_t *db) {
    if (!db || !db->handle || !sql) return false;

    char *err_msg = NULL;
    
    // Auto-lock for simple fire-and-forget executions
    libmse_mutex_lock(db->mutex);
    int rc = sqlite3_exec(db->handle, sql, 0, 0, &err_msg);
    libmse_mutex_unlock(db->mutex);

    if (rc != SQLITE_OK) {
        DEBUG_ERROR("SQL Error: %s | Query: %s", err_msg, sql);
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

LIBMSE_API int64_t libmse_db_last_insert_rowid(libmse_db_t *db) {
    if (!db || !db->handle) return 0;
    
    // Safety check under mutex isolation since multiple insertion worker structures could race
    libmse_mutex_lock(db->mutex);
    int64_t rowid = (int64_t)sqlite3_last_insert_rowid(db->handle);
    libmse_mutex_unlock(db->mutex);
    
    return rowid;
}

LIBMSE_API bool libmse_db_begin(libmse_db_t *db)    { return libmse_db_exec("BEGIN TRANSACTION;", db); }
LIBMSE_API bool libmse_db_commit(libmse_db_t *db)   { return libmse_db_exec("COMMIT;", db); }
LIBMSE_API bool libmse_db_rollback(libmse_db_t *db) { return libmse_db_exec("ROLLBACK;", db); }

LIBMSE_API libmse_stmt_t* libmse_db_stmt_prepare(libmse_db_t *db, const char *sql) {
    if (!db || !db->handle || !sql) return NULL;

    sqlite3_stmt *sqlite_stmt = NULL;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &sqlite_stmt, NULL) != SQLITE_OK) {
        DEBUG_ERROR("Failed to prepare statement: %s | SQL: %s", sqlite3_errmsg(db->handle), sql);
        return NULL;
    }

    libmse_stmt_t *stmt = (libmse_stmt_t*)malloc(sizeof(libmse_stmt_t));
    stmt->handle = sqlite_stmt;
    stmt->db = db;
    return stmt;
}

LIBMSE_API void libmse_db_stmt_finalize(libmse_stmt_t *stmt) {
    if (!stmt) return;
    if (stmt->handle) sqlite3_finalize(stmt->handle);
    free(stmt);
}

LIBMSE_API int libmse_db_stmt_step(libmse_stmt_t *stmt) {
    if (!stmt || !stmt->handle) return -1;
    
    int rc = sqlite3_step(stmt->handle);
    if (rc == SQLITE_ROW)  return 1;  
    if (rc == SQLITE_DONE) return 0;  
    
    DEBUG_ERROR("Step failed: %s", sqlite3_errmsg(stmt->db->handle));
    return -1; 
}

LIBMSE_API bool libmse_db_stmt_reset(libmse_stmt_t *stmt) {
    if (!stmt || !stmt->handle) return false;
    sqlite3_clear_bindings(stmt->handle);
    return sqlite3_reset(stmt->handle) == SQLITE_OK;
}

LIBMSE_API bool libmse_db_bind_int(libmse_stmt_t *stmt, int index, int value) {
    return sqlite3_bind_int(stmt->handle, index, value) == SQLITE_OK;
}

LIBMSE_API bool libmse_db_bind_int64(libmse_stmt_t *stmt, int index, int64_t value) {
    return sqlite3_bind_int64(stmt->handle, index, value) == SQLITE_OK;
}

LIBMSE_API bool libmse_db_bind_double(libmse_stmt_t *stmt, int index, double value) {
    return sqlite3_bind_double(stmt->handle, index, value) == SQLITE_OK;
}

LIBMSE_API bool libmse_db_bind_text(libmse_stmt_t *stmt, int index, const char *value) {
    return sqlite3_bind_text(stmt->handle, index, value, -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

LIBMSE_API bool libmse_db_bind_blob(libmse_stmt_t *stmt, int index, const void *data, size_t size) {
    if (!data || size == 0) return libmse_db_bind_null(stmt, index);
    return sqlite3_bind_blob(stmt->handle, index, data, (int)size, SQLITE_TRANSIENT) == SQLITE_OK;
}

LIBMSE_API bool libmse_db_bind_null(libmse_stmt_t *stmt, int index) {
    return sqlite3_bind_null(stmt->handle, index) == SQLITE_OK;
}

LIBMSE_API int libmse_db_col_int(libmse_stmt_t *stmt, int col) {
    return sqlite3_column_int(stmt->handle, col);
}

LIBMSE_API int64_t libmse_db_col_int64(libmse_stmt_t *stmt, int col) {
    return sqlite3_column_int64(stmt->handle, col);
}

LIBMSE_API double libmse_db_col_double(libmse_stmt_t *stmt, int col) {
    return sqlite3_column_double(stmt->handle, col);
}

LIBMSE_API const char* libmse_db_col_text(libmse_stmt_t *stmt, int col) {
    return (const char*)sqlite3_column_text(stmt->handle, col);
}

LIBMSE_API const void* libmse_db_col_blob(libmse_stmt_t *stmt, int col) {
    return sqlite3_column_blob(stmt->handle, col);
}

LIBMSE_API size_t libmse_db_col_bytes(libmse_stmt_t *stmt, int col) {
    return (size_t)sqlite3_column_bytes(stmt->handle, col);
}