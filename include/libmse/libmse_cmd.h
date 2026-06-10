#ifndef LIBMSE_CMD_H
#define LIBMSE_CMD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libmse_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum libmse_cmd_type_e {
    LIBMSE_CMD_INT,
    LIBMSE_CMD_FLOAT,
    LIBMSE_CMD_DOUBLE,
    LIBMSE_CMD_STRING,
} libmse_cmd_type_t;

typedef union {
    int i;
    float f;
    double d;
    const char* s;
} libmse_cmd_arg_t;

typedef struct {
    const char* name;
    const char* description;
    
    size_t expected_args_count;
    const libmse_cmd_type_t* expected_types;

    bool (*handler)(const libmse_cmd_arg_t* args);
} libmse_cmd_t;

typedef void (*libmse_cmd_iterate_cb)(const libmse_cmd_t* cmd, void* user_data);

LIBMSE_API bool libmse_cmd_register(const libmse_cmd_t* cmd);
LIBMSE_API bool libmse_cmd_destroy(const char* name);
LIBMSE_API void libmse_cmd_iterate(libmse_cmd_iterate_cb callback, void* user_data);
LIBMSE_API libmse_cmd_t* libmse_cmd_get(const char* name);

LIBMSE_API bool libmse_cmd_execute(const char* name, int argc, const char** argv);
LIBMSE_API bool libmse_cmd_parse(const char* input_line);

LIBMSE_API bool libmse_cmd_get_handler(const libmse_cmd_arg_t* args);
LIBMSE_API bool libmse_cmd_set_handler(const libmse_cmd_arg_t* args);

LIBMSE_API void libmse_cmd_register_default();

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_CMD_H