#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "libmse/libmse_debug.h"
#include "libmse/libmse_cvar.h"
#include "libmse/libmse_cmd.h"

#define CMD_INITIAL_CAPACITY 32

static libmse_cmd_t **g_cmd_registry			= NULL;
static size_t		  g_cmd_count				= 0;
static size_t		  g_cmd_capacity			= 0;
static bool			  g_cmd_defaults_registered = false;

static int find_cmd_index(const char *name)
{
	if (!name || !g_cmd_registry) return -1;
	for (size_t i = 0; i < g_cmd_count; i++) {
		if (g_cmd_registry[i] && strcmp(g_cmd_registry[i]->name, name) == 0) {
			return (int)i;
		}
	}
	return -1;
}

LIBMSE_API bool libmse_cmd_register(const libmse_cmd_t *cmd)
{
	if (!cmd || !cmd->name || !cmd->handler) return false;

	if (find_cmd_index(cmd->name) != -1) return false;

	if (g_cmd_count >= g_cmd_capacity) {
		size_t		   new_capacity = (g_cmd_capacity == 0) ? CMD_INITIAL_CAPACITY : g_cmd_capacity * 2;
		libmse_cmd_t **new_registry = (libmse_cmd_t **)realloc(g_cmd_registry, new_capacity * sizeof(libmse_cmd_t *));
		if (!new_registry) return false;

		g_cmd_registry = new_registry;
		g_cmd_capacity = new_capacity;
	}

	libmse_cmd_t *new_cmd = (libmse_cmd_t *)malloc(sizeof(libmse_cmd_t));
	if (!new_cmd) return false;

	// Deep copy struct variables safely
	new_cmd->name				 = strdup(cmd->name);
	new_cmd->description		 = cmd->description ? strdup(cmd->description) : NULL;
	new_cmd->expected_args_count = cmd->expected_args_count;

	if (cmd->expected_args_count > 0 && cmd->expected_types) {
		libmse_cmd_type_t *types = (libmse_cmd_type_t *)malloc(cmd->expected_args_count * sizeof(libmse_cmd_type_t));
		memcpy(types, cmd->expected_types, cmd->expected_args_count * sizeof(libmse_cmd_type_t));
		new_cmd->expected_types = types;
	} else {
		new_cmd->expected_types = NULL;
	}

	new_cmd->handler = cmd->handler;

	g_cmd_registry[g_cmd_count++] = new_cmd;
	return true;
}

LIBMSE_API bool libmse_cmd_destroy(const char *name)
{
	int index = find_cmd_index(name);
	if (index == -1) return false;

	libmse_cmd_t *cmd = g_cmd_registry[index];
	free((void *)cmd->name);
	if (cmd->description) free((void *)cmd->description);
	if (cmd->expected_types) free((void *)cmd->expected_types);
	free(cmd);

	for (size_t i = (size_t)index; i < g_cmd_count - 1; i++) {
		g_cmd_registry[i] = g_cmd_registry[i + 1];
	}
	g_cmd_count--;
	g_cmd_registry[g_cmd_count] = NULL;
	return true;
}

LIBMSE_API void libmse_cmd_iterate(libmse_cmd_iterate_cb callback, void *user_data)
{
	if (!callback) return;
	for (size_t i = 0; i < g_cmd_count; i++) {
		callback(g_cmd_registry[i], user_data);
	}
}

LIBMSE_API libmse_cmd_t *libmse_cmd_get(const char *name)
{
	int index = find_cmd_index(name);
	if (index == -1) return NULL;
	return g_cmd_registry[index];
}

static void help_cmd_callback(const libmse_cmd_t *cmd, void *user_data)
{
	char output[256];
	snprintf(output, sizeof(output), "  %-30s - %s", cmd->name, cmd->description ? cmd->description : "");
	libmse_debug_printf(output);
}

static void list_cmd_callback(const libmse_cmd_t *cmd, void *user_data)
{
	char output[256];
	snprintf(output, sizeof(output), "  %-30s (", cmd->name);
	for (size_t i = 0; i < cmd->expected_args_count; i++) {
		libmse_cvar_type_t type		= cmd->expected_types[i];
		const char		  *type_str = "Unknown";
		if (type == LIBMSE_CVAR_INT) {
			type_str = "Int";
		} else if (type == LIBMSE_CVAR_DOUBLE) {
			type_str = "Double";
		} else if (type == LIBMSE_CVAR_FLOAT) {
			type_str = "Float";
		} else if (type == LIBMSE_CVAR_STRING) {
			type_str = "String";
		}
		strncat(output, type_str, sizeof(output) - strlen(output) - 1);
		if (i < cmd->expected_args_count - 1) strncat(output, ", ", sizeof(output) - strlen(output) - 1);
	}

	strncat(output, ") - ", sizeof(output) - strlen(output) - 1);
	strncat(output, cmd->description ? cmd->description : "", sizeof(output) - strlen(output) - 1);

	libmse_debug_printf(output);
}

static void list_cvar_callback(libmse_cvar_t *cvar, void *user_data)
{
	char		output[256], val_str[64] = "";
	const char *type_str = "Unknown";

	if (cvar->type == LIBMSE_CVAR_INT) {
		type_str = "Int";
		snprintf(val_str, sizeof(val_str), "%d", *cvar->data.i);
	} else if (cvar->type == LIBMSE_CVAR_DOUBLE) {
		type_str = "Double";
		snprintf(val_str, sizeof(val_str), "%.4f", *cvar->data.d);
	} else if (cvar->type == LIBMSE_CVAR_FLOAT) {
		type_str = "Float";
		snprintf(val_str, sizeof(val_str), "%.4f", *cvar->data.f);
	} else if (cvar->type == LIBMSE_CVAR_STRING) {
		type_str = "String";
		snprintf(val_str, sizeof(val_str), "\"%s\"", *cvar->data.s ? *cvar->data.s : "NULL");
	}

	snprintf(output, sizeof(output), "  %-30s = %-10s [%-6s] : %s", cvar->name, val_str, type_str,
			 cvar->description ? cvar->description : "");
	libmse_debug_printf(output);
}

static bool cmd_listcmds_handler(const libmse_cmd_arg_t *args)
{
	libmse_debug_printf("Registered Commands:");
	libmse_cmd_iterate(help_cmd_callback, NULL);
	libmse_debug_printf("--- End of List ---");
	return true;
}

static bool cmd_listvars_handler(const libmse_cmd_arg_t *args)
{
	libmse_debug_printf("Registered CVars:");
	libmse_cvar_iterate(list_cvar_callback, NULL);
	libmse_debug_printf("--- End of List ---");
	return true;
}

static bool cmd_print_handler(const libmse_cmd_arg_t *args)
{
	libmse_debug_printf(args[0].s);
	return true;
}

LIBMSE_API bool libmse_cmd_get_handler(const libmse_cmd_arg_t *args)
{
	const char	  *name = args[0].s;
	libmse_cvar_t *cvar = libmse_cvar_get(name);
	if (!cvar) {
		libmse_debug_printf("Error: CVar identifier target not found.");
		return false;
	}

	char output[256];
	switch (cvar->type) {
	case LIBMSE_CVAR_INT:
		snprintf(output, sizeof(output), "%s = %d (Int)", cvar->name, *cvar->data.i);
		break;
	case LIBMSE_CVAR_FLOAT:
		snprintf(output, sizeof(output), "%s = %.4f (Float)", cvar->name, *cvar->data.f);
		break;
	case LIBMSE_CVAR_DOUBLE:
		snprintf(output, sizeof(output), "%s = %.4f (Double)", cvar->name, *cvar->data.d);
		break;
	case LIBMSE_CVAR_STRING:
		snprintf(output, sizeof(output), "%s = \"%s\" (String)", cvar->name, *cvar->data.s ? *cvar->data.s : "NULL");
		break;
	}
	libmse_debug_printf(output);
	return true;
}

LIBMSE_API bool libmse_cmd_set_handler(const libmse_cmd_arg_t *args)
{
	const char *name	= args[0].s;
	const char *val_str = args[1].s;

	libmse_cvar_t *cvar = libmse_cvar_get(name);
	if (!cvar) {
		libmse_debug_printf("Error: CVar identifier target not found.");
		return false;
	}

	bool sync_success = false;
	if (cvar->type == LIBMSE_CVAR_INT)
		sync_success = libmse_cvar_set_i(name, atoi(val_str));
	else if (cvar->type == LIBMSE_CVAR_FLOAT)
		sync_success = libmse_cvar_set_f(name, (float)atof(val_str));
	else if (cvar->type == LIBMSE_CVAR_DOUBLE)
		sync_success = libmse_cvar_set_d(name, atof(val_str));
	else if (cvar->type == LIBMSE_CVAR_STRING)
		sync_success = libmse_cvar_set_s(name, val_str);

	if (sync_success) {
		char output[256];
		if (cvar->type == LIBMSE_CVAR_INT)
			snprintf(output, sizeof(output), "Set '%s' to %d.", name, *cvar->data.i);
		else if (cvar->type == LIBMSE_CVAR_FLOAT)
			snprintf(output, sizeof(output), "Set '%s' to %.4f.", name, *cvar->data.f);
		else if (cvar->type == LIBMSE_CVAR_DOUBLE)
			snprintf(output, sizeof(output), "Set '%s' to %.4f.", name, *cvar->data.d);
		else if (cvar->type == LIBMSE_CVAR_STRING)
			snprintf(output, sizeof(output), "Set '%s' to \"%s\".", name, cvar->data.s ? *cvar->data.s : "NULL");
		libmse_debug_printf(output);
	} else {
		libmse_debug_printf("Error: Failed to process assignment.");
	}
	return true;
}

static bool cmd_create_handler(const libmse_cmd_arg_t *args)
{
	const char *type_str = args[0].s;
	const char *name	 = args[1].s;
	const char *val_str	 = args[2].s;

	// Heap allocate backing variables so they aren't destroyed when this stack ends
	void			  *ref = NULL;
	libmse_cvar_type_t type;

	if (strcmp(type_str, "int") == 0) {
		type	 = LIBMSE_CVAR_INT;
		int *val = malloc(sizeof(int));
		*val	 = atoi(val_str);
		ref		 = val;
	} else if (strcmp(type_str, "float") == 0) {
		type	   = LIBMSE_CVAR_FLOAT;
		float *val = malloc(sizeof(float));
		*val	   = (float)atof(val_str);
		ref		   = val;
	} else if (strcmp(type_str, "string") == 0) {
		type			 = LIBMSE_CVAR_STRING;
		const char **val = malloc(sizeof(const char *));
		*val			 = strdup(val_str); // Needs to exist persistently
		ref				 = val;
	} else {
		libmse_debug_printf("Error: Invalid type. Use 'int', 'float', or 'string'.");
		return false;
	}

	if (libmse_cvar_register(name, type, ref, "Dynamically created via console")) {
		char output[256];
		snprintf(output, sizeof(output), "Created CVar '%s' successfully.", name);
		libmse_debug_printf(output);
		return true;
	} else {
		libmse_debug_printf("Error: Failed to register CVar (Already exists?).");
		free(ref);
		return false;
	}
}

static bool cmd_delete_handler(const libmse_cmd_arg_t *args)
{
	const char *name = args[0].s;
	if (libmse_cvar_destroy(name)) {
		char output[256];
		snprintf(output, sizeof(output), "Deleted CVar '%s' successfully.", name);
		libmse_debug_printf(output);
		return true;
	} else {
		libmse_debug_printf("Error: Failed to destroy CVar (Not found?).");
		return false;
	}
}

static bool cmd_help_handler(const libmse_cmd_arg_t *args)
{
	libmse_debug_printf("Available Commands:");
	libmse_cmd_iterate(help_cmd_callback, NULL);
	libmse_debug_printf("");
	return true;
}

static bool cmd_exec_handler(const libmse_cmd_arg_t *args)
{
	const char *filename = args[0].s;
	FILE	   *file	 = fopen(filename, "r");

	if (!file) {
		char output[256];
		snprintf(output, sizeof(output), "Error: Failed to open script file '%s'.", filename);
		libmse_debug_printf(output);
		return false;
	}

	char line[512];
	int	 line_count = 0;

	while (fgets(line, sizeof(line), file)) {
		line_count++;

		// Strip trailing newline characters (\r or \n)
		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
			line[--len] = '\0';
		}

		if (len == 0 || strncmp(line, "//", 2) == 0 || line[0] == '#') {
			continue;
		}

		if (!libmse_cmd_parse(line)) {
			char output[256];
			DEBUG_ERROR(output, sizeof(output), "Script Error: Failed at '%s' line %d: \"%s\"", filename, line_count,
						line);
			//libmse_debug_printf(output);
		}
	}

	fclose(file);
	return true;
}

LIBMSE_API void libmse_cmd_register_default()
{
	if (g_cmd_defaults_registered) return;

	static const libmse_cmd_type_t arg_string_1[] = {LIBMSE_CMD_STRING};
	static const libmse_cmd_type_t arg_string_2[] = {LIBMSE_CMD_STRING, LIBMSE_CMD_STRING};
	static const libmse_cmd_type_t arg_string_3[] = {LIBMSE_CMD_STRING, LIBMSE_CMD_STRING, LIBMSE_CMD_STRING};

	libmse_cmd_register(&(libmse_cmd_t){"help", "Lists all available commands", 0, NULL, cmd_help_handler});
	libmse_cmd_register(&(libmse_cmd_t){"listvars", "Lists all registered CVars", 0, NULL, cmd_listvars_handler});
	libmse_cmd_register(&(libmse_cmd_t){"listcmds", "Lists all registered Commands", 0, NULL, cmd_listcmds_handler});

	libmse_cmd_register(&(libmse_cmd_t){"print", "Prints the specified string", 1, arg_string_1, cmd_print_handler});
	libmse_cmd_register(&(libmse_cmd_t){"get", "Reads current CVar variant", 1, arg_string_1, libmse_cmd_get_handler});
	libmse_cmd_register(&(libmse_cmd_t){"delete", "Destroys a target CVar", 1, arg_string_1, cmd_delete_handler});

	libmse_cmd_register(
		&(libmse_cmd_t){"set", "Modifies target CVar allocation", 2, arg_string_2, libmse_cmd_set_handler});
	libmse_cmd_register(&(libmse_cmd_t){"create", "Registers a new CVar", 3, arg_string_3, cmd_create_handler});

	libmse_cmd_register(&(libmse_cmd_t){"exec", "Executes a script file", 1, arg_string_1, cmd_exec_handler});
}

#define LIBMSE_CMD_INPUT_BUFFER_SIZE 512

LIBMSE_API bool libmse_cmd_parse(const char *input)
{
    char line_copy[LIBMSE_CMD_INPUT_BUFFER_SIZE];
	strncpy(line_copy, input, sizeof(line_copy) - 1);
	line_copy[sizeof(line_copy) - 1] = '\0';

	char *args[16];
	int	  argc = 0;
	char *p	   = line_copy;

	// Robust Tokenization (Supports "Spaces inside Quotes")
	while (*p && argc < 16) {
		while (*p == ' ') p++;
		if (!*p) break;
		if (*p == '"') {
			p++;
			args[argc++] = p;
			while (*p && *p != '"') p++;
			if (*p == '"') {
				*p = '\0';
				p++;
			}
		} else {
			args[argc++] = p;
			while (*p && *p != ' ') p++;
			if (*p == ' ') {
				*p = '\0';
				p++;
			}
		}
	}

	if (argc == 0) return false;
	const char *cmd = args[0];

	// 1. Check if input maps to a Registered Command
	libmse_cmd_t *cmd_def = libmse_cmd_get(cmd);
	if (cmd_def) {
		if (!libmse_cmd_execute(cmd, argc - 1, (const char **)&args[1])) {
			DEBUG_ERROR("Error: Command '%s' failed or expected %zu arguments.", cmd, cmd_def->expected_args_count);
            return false;
        }
		return true;
	}

	// 2. Fallback check: Direct implicit CVar Get/Set (e.g. typing 'cl_fov 90')
	libmse_cvar_t *cvar = libmse_cvar_get(cmd);
	if (cvar) {
		if (argc == 1) { // Get logic
			libmse_cmd_get_handler(&(libmse_cmd_arg_t){.s = cmd});
            return true;
		} else if (argc == 2) { // Set logic
			libmse_cmd_arg_t set_args[2] = {{.s = cmd}, {.s = args[1]}};
			libmse_cmd_set_handler(set_args);
            return true;
		} else {
			DEBUG_ERROR("Usage: <cvar_name> [new_value]");
            return false;
		}
	}

    DEBUG_ERROR("Unknown command or CVar. Enter 'help' for instructions.");
    return false;
}

LIBMSE_API bool libmse_cmd_execute(const char *name, int argc, const char **argv)
{
	libmse_cmd_t *cmd = libmse_cmd_get(name);
	if (!cmd) return false;

	if ((size_t)argc < cmd->expected_args_count) return false;

	libmse_cmd_arg_t parsed_args[16];
	size_t			 limit = cmd->expected_args_count > 16 ? 16 : cmd->expected_args_count;

	for (size_t i = 0; i < limit; i++) {
		switch (cmd->expected_types[i]) {
		case LIBMSE_CMD_INT:
			parsed_args[i].i = atoi(argv[i]);
			break;
		case LIBMSE_CMD_FLOAT:
			parsed_args[i].f = (float)atof(argv[i]);
			break;
		case LIBMSE_CMD_DOUBLE:
			parsed_args[i].d = atof(argv[i]);
			break;
		case LIBMSE_CMD_STRING:
			parsed_args[i].s = argv[i];
			break;
		}
	}

	return cmd->handler((cmd->expected_args_count > 0) ? parsed_args : NULL);
}