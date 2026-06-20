#ifndef LIBMSE_CMD_H
#define LIBMSE_CMD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libmse_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LIBMSE_CMD_INPUT_BUFFER_SIZE 512

typedef struct {
    const char* name;
    const char* description;
    
    size_t expected_args_count;

    bool (*handler)(int argc, const char** argv);
} libmse_cmd_t;

typedef struct {
    const char* name;
    const char* cmd;
} libmse_alias_t;

typedef void (*libmse_cmd_iterate_cb)(const libmse_cmd_t* cmd, void* user_data);

LIBMSE_API bool libmse_cmd_register(const libmse_cmd_t* cmd);
LIBMSE_API bool libmse_cmd_destroy(const char* name);
LIBMSE_API void libmse_cmd_iterate(libmse_cmd_iterate_cb callback, void* user_data);
LIBMSE_API libmse_cmd_t* libmse_cmd_get(const char* name);

LIBMSE_API bool libmse_cmd_execute(const char* name, int argc, const char** argv);
LIBMSE_API bool libmse_cmd_parse(const char* input_line);

LIBMSE_API bool libmse_cmd_get_handler(int argc, const char** argv);
LIBMSE_API bool libmse_cmd_set_handler(int argc, const char** argv);

LIBMSE_API void libmse_cmd_register_default();

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_CMD_H