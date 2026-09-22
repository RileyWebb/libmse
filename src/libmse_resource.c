#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libmse/libmse_resource.h"

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define getcwd _getcwd
#define mkdir(path, mode) _mkdir(path)
#define LIBMSE_PATH_SEPARATOR '\\'
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define LIBMSE_PATH_SEPARATOR '/'
#endif

//#define PATH_MAX 1024

#define SHA256_STRING_LENGTH 65 // 64 chars + null terminator
#define CRC32_STRING_LENGTH 9   // 8 chars + null terminator

static char *cwd = NULL;
static char *temp_path = NULL;
static char *executable_path = NULL;
static char *appdata_path = NULL;
static char *libmse_cache_path = NULL;
static char *libmse_config_path = NULL;
static char *libmse_log_path = NULL;
static char *libmse_current_log_file = NULL;

LIBMSE_API const char *libmse_resource_get_cwd()
{
    if (cwd)
        return cwd;

    char buffer[PATH_MAX];
    if (getcwd(buffer, sizeof(buffer)) != NULL) {
        cwd = strdup(buffer);
        return cwd;
    } else {
        return NULL;
    }
}

LIBMSE_API const char *libmse_resource_get_temp_path()
{
    if (temp_path)
        return temp_path;

#ifdef _WIN32
    temp_path = getenv("TEMP");
    if (temp_path)
        return temp_path;

    return "C:\\Temp";
#else
    temp_path = getenv("TMPDIR");
    if (temp_path)
        return temp_path;
    
    return "/tmp";
#endif
}

LIBMSE_API const char *libmse_resource_get_executable_path()
{
#ifdef _WIN32
    char path[PATH_MAX];
    if (GetModuleFileNameA(NULL, path, PATH_MAX) > 0) {
        return strdup(path);
    }
    return NULL;
#else
    char path[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (count != -1) {
        path[count] = '\0';
        return strdup(path);
    }
    return NULL;
#endif
}

LIBMSE_API const char *libmse_resource_get_appdata_path() 
{
    if (appdata_path)
        return appdata_path;

#ifdef _WIN32
    const char *env_appdata = getenv("APPDATA");
    if (!env_appdata) {
        env_appdata = "C:\\Windows\\Temp"; // Safe system fallback
    }
    
    size_t len = strlen(env_appdata) + strlen("\\libmse") + 1;
    appdata_path = malloc(len);
    if (appdata_path) {
        snprintf(appdata_path, len, "%s\\libmse", env_appdata);
    }
#else
    const char *home = getenv("HOME");
    if (home) {
        size_t len = strlen(home) + strlen("/.local/share/libmse") + 1;
        appdata_path = malloc(len);
        if (appdata_path) {
            snprintf(appdata_path, len, "%s/.local/share/libmse", home);
        }
    } else {
        const char *fallback = "/usr/local/share/libmse";
        appdata_path = malloc(strlen(fallback) + 1);
        if (appdata_path) {
            strcpy(appdata_path, fallback);
        }
    }
#endif

    return appdata_path;
}

LIBMSE_API const char *libmse_resource_get_cache_path() 
{
    if (libmse_cache_path)
        return libmse_cache_path;

    if (!appdata_path)
        libmse_resource_get_appdata_path();

    if (!appdata_path) return NULL;

    size_t len = strlen(appdata_path) + 1 + strlen("cache") + 1;
    libmse_cache_path = malloc(len);
    if (libmse_cache_path) {
        snprintf(libmse_cache_path, len, "%s%ccache", appdata_path, LIBMSE_PATH_SEPARATOR);
    }
    return libmse_cache_path;
}

LIBMSE_API const char *libmse_resource_get_config_path() 
{
    if (libmse_config_path)
        return libmse_config_path;

    if (!appdata_path)
        libmse_resource_get_appdata_path();

    if (!appdata_path) return NULL;

    size_t len = strlen(appdata_path) + 1 + strlen("config") + 1;
    libmse_config_path = malloc(len);
    if (libmse_config_path) {
        snprintf(libmse_config_path, len, "%s%cconfig", appdata_path, LIBMSE_PATH_SEPARATOR);
    }
    return libmse_config_path;
}

LIBMSE_API const char *libmse_resource_get_log_path() 
{
    if (libmse_log_path)
        return libmse_log_path;

    if (!appdata_path)
        libmse_resource_get_appdata_path();

    if (!appdata_path) return NULL;

    size_t len = strlen(appdata_path) + 1 + strlen("logs") + 1;
    libmse_log_path = malloc(len);
    if (libmse_log_path) {
        snprintf(libmse_log_path, len, "%s%clogs", appdata_path, LIBMSE_PATH_SEPARATOR);
    }
    return libmse_log_path;
}

LIBMSE_API bool libmse_resource_ensure_directory_exists(const char *path) {
    if (path == NULL || *path == '\0')
        return false;

    // Note: This creates the *leaf* directory. If parent folders do not exist,
    // you may want to wrap a recursive loop here if appdata paths are clean slates.
    (void)mkdir(path, 0755);
    return true;
}

LIBMSE_API FILE *libmse_resource_create_log_file() {
    const char *logs_dir = libmse_resource_get_log_path();
    if (!logs_dir) return NULL;

    char log_timestamp[32] = "unknown_time";
    time_t now = time(NULL);

    if (now != (time_t)-1) {
        struct tm local_tm;
        bool time_ok = false;
#ifdef _WIN32
        if (localtime_s(&local_tm, &now) == 0) {
            time_ok = true;
        }
#else
        if (localtime_r(&now, &local_tm) != NULL) {
            time_ok = true;
        }
#endif
        if (time_ok) {
            if (strftime(log_timestamp, sizeof(log_timestamp), "%Y%m%d_%H%M%S", &local_tm) == 0) {
                snprintf(log_timestamp, sizeof(log_timestamp), "unknown_time");
            }
        }
    }

    libmse_resource_ensure_directory_exists(logs_dir);

    // Dynamic length tracking allocation block
    size_t filename_len = strlen(logs_dir) + 1 + strlen("log_") + strlen(log_timestamp) + strlen(".txt") + 1;
    char *log_file = malloc(filename_len);
    if (!log_file) return NULL;

    if (snprintf(log_file, filename_len, "%s%clog_%s.txt", logs_dir, LIBMSE_PATH_SEPARATOR, log_timestamp) > 0) {
        FILE *file = fopen(log_file, "a");
        free(log_file);        
        return file;
    }

    free(log_file);
    return NULL;
}