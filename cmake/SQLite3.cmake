include(FetchContent)

set(SQLITE_ENABLE_FTS5 ON CACHE BOOL "Enable Full-Text Search" FORCE)
set(SQLITE_ENABLE_JSON1 ON CACHE BOOL "Enable JSON functions" FORCE)
set(SQLITE_THREADSAFE ON CACHE BOOL "Multi-thread safety mode" FORCE)

FetchContent_Declare(
    sqlite3
    GIT_REPOSITORY https://github.com/azadkuh/sqlite-amalgamation.git
    GIT_TAG        master
)

FetchContent_GetProperties(sqlite3)
if(NOT sqlite3_POPULATED)
    FetchContent_MakeAvailable(sqlite3)
endif()

#target_include_directories(SQLite3 PUBLIC 
#    $<BUILD_INTERFACE:${sqlite3_SOURCE_DIR}>
#)