#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "libmse/libmse_debug.h"
#include "libmse/libmse_cvar.h"
#include "libmse/libmse_cmd.h"

#define CMD_INITIAL_CAPACITY 32
#define ALIAS_INITIAL_CAPACITY 16

static libmse_cmd_t **g_cmd_registry			= NULL;
static size_t		  g_cmd_count				= 0;
static size_t		  g_cmd_capacity			= 0;
static bool			  g_cmd_defaults_registered = false;

static libmse_alias_t **g_alias_registry			= NULL;
static size_t			g_alias_count				= 0;
static size_t			g_alias_capacity			= 0;
static bool				g_alias_defaults_registered = false;

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
	new_cmd->handler			 = cmd->handler;

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
	libmse_cmd_register_default();

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
		strncat(output, "String", sizeof(output) - strlen(output) - 1);
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

static bool cmd_listcmds_handler(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	libmse_debug_printf("Registered Commands:");
	libmse_cmd_iterate(list_cmd_callback, NULL);
	libmse_debug_printf("--- End of List ---");
	return true;
}

static bool cmd_listvars_handler(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	libmse_debug_printf("Registered CVars:");
	libmse_cvar_iterate(list_cvar_callback, NULL);
	libmse_debug_printf("--- End of List ---");
	return true;
}

static bool cmd_print_handler(int argc, const char **argv)
{
	size_t total_len = 0;
	for (int i = 0; i < argc; i++) {
		total_len += strlen(argv[i]);
		if (i < argc - 1) {
			total_len += 1;
		}
	}

	char *str = (char *)malloc(total_len + 1);
	if (!str) return false;

	str[0] = '\0';
	for (int i = 0; i < argc; i++) {
		strcat(str, argv[i]);
		if (i < argc - 1) {
			strcat(str, " ");
		}
	}

	libmse_debug_printf(str);
	free(str);
	return true;
}

LIBMSE_API bool libmse_cmd_get_handler(int argc, const char **argv)
{
	(void)argc;
	const char	  *name = argv[0];
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

LIBMSE_API bool libmse_cmd_set_handler(int argc, const char **argv)
{
	(void)argc;
	const char *name	= argv[0];
	const char *val_str = argv[1];

	libmse_cvar_t *cvar = libmse_cvar_get(name);
	if (!cvar) {
		libmse_cvar_register(name, LIBMSE_CVAR_STRING, val_str, "Dynamically created CVar (default string type)");
		cvar = libmse_cvar_get(name);
		if (!cvar) {
			libmse_debug_printf("Error: Failed to create new CVar for assignment.");
			return false;
		}
		return true;
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

static bool cmd_delete_handler(int argc, const char **argv)
{
	(void)argc;
	const char *name = argv[0];
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

static bool cmd_help_handler(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	libmse_debug_printf("Available Commands:");
	libmse_cmd_iterate(help_cmd_callback, NULL);
	libmse_debug_printf("");
	return true;
}

static bool cmd_exec_handler(int argc, const char **argv)
{
	(void)argc;
	const char *filename = argv[0];
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
		}
	}

	fclose(file);
	return true;
}

static bool cmd_create_handler(int argc, const char **argv)
{
	(void)argc;
	const char		  *name		= argv[0];
	const char		  *type_str = argv[1];
	const char		  *desc		= argv[2];
	libmse_cvar_type_t type;

	if (strcmp(type_str, "int") == 0) {
		type = LIBMSE_CVAR_INT;
	} else if (strcmp(type_str, "float") == 0) {
		type = LIBMSE_CVAR_FLOAT;
	} else if (strcmp(type_str, "double") == 0) {
		type = LIBMSE_CVAR_DOUBLE;
	} else if (strcmp(type_str, "string") == 0) {
		type = LIBMSE_CVAR_STRING;
	} else {
		libmse_debug_printf("Error: Invalid CVar type specified. Use int, float, double, or string.");
		return false;
	}

	int			dummy_int	 = 0;
	float		dummy_float	 = 0.0f;
	double		dummy_double = 0.0;
	const char *dummy_string = NULL;

	void *ref = NULL;
	switch (type) {
	case LIBMSE_CVAR_INT:
		ref = &dummy_int;
		break;
	case LIBMSE_CVAR_FLOAT:
		ref = &dummy_float;
		break;
	case LIBMSE_CVAR_DOUBLE:
		ref = &dummy_double;
		break;
	case LIBMSE_CVAR_STRING:
		ref = &dummy_string;
		break;
	default:
		libmse_debug_printf("Error: Unsupported CVar type.");
		return false;
	}

	if (libmse_cvar_register(name, type, ref, desc)) {
		char output[256];
		snprintf(output, sizeof(output), "Created CVar '%s' of type '%s'.", name, type_str);
		libmse_debug_printf(output);
		return true;
	} else {
		libmse_debug_printf("Error: Failed to create CVar (Already exists?).");
		return false;
	}
}

/*LIBMSE_API libmse_alias_t *libmse_alias_get(const char *name)
{
	int index = -1;
	if (!name || !g_alias_registry) return NULL;
	for (size_t i = 0; i < g_alias_count; i++) {
		if (g_alias_registry[i] && strcmp(g_alias_registry[i]->name, name) == 0) {
			index = (int)i;
			break;
		}
	}
	if (index == -1) return NULL;
	return g_alias_registry[index];
}*/

static bool cmd_alias_handler(int argc, const char **argv)
{
	if (argc < 2) {
		libmse_debug_printf("Error: Missing arguments for alias.");
		return false;
	}

	const char *alias_name = argv[0];

	size_t total_len = 0;
	for (int i = 1; i < argc; i++) {
		total_len += strlen(argv[i]);
		if (i < argc - 1) {
			total_len += 1; // For the space between arguments
		}
	}

	char *cmd_str = (char *)malloc(total_len + 1);
	if (!cmd_str) return false;

	cmd_str[0] = '\0'; // Start with an empty string
	for (int i = 1; i < argc; i++) {
		strcat(cmd_str, argv[i]);
		if (i < argc - 1) {
			strcat(cmd_str, " "); // Append space between args
		}
	}

	if (libmse_cmd_get(alias_name) || libmse_cvar_get(alias_name)) {
		libmse_debug_printf("Error: Alias name conflicts with existing command.");
		free(cmd_str); // Don't leak the newly allocated string!
		return false;
	}

	if (g_alias_count >= g_alias_capacity) {
		size_t			 new_capacity = (g_alias_capacity == 0) ? ALIAS_INITIAL_CAPACITY : g_alias_capacity * 2;
		libmse_alias_t **new_registry =
			(libmse_alias_t **)realloc(g_alias_registry, new_capacity * sizeof(libmse_alias_t *));
		if (!new_registry) {
			free(cmd_str);
			return false;
		}

		g_alias_registry = new_registry;
		g_alias_capacity = new_capacity;
	}

	for (size_t i = 0; i < g_alias_count; i++) {
		if (g_alias_registry[i] && strcmp(g_alias_registry[i]->name, alias_name) == 0) {
			free((void *)g_alias_registry[i]->cmd);
			// We can directly assign cmd_str here instead of strdup'ing again
			g_alias_registry[i]->cmd = cmd_str;
			return true;
		}
	}

	libmse_alias_t *new_alias = (libmse_alias_t *)malloc(sizeof(libmse_alias_t));
	if (!new_alias) {
		free(cmd_str);
		return false;
	}

	new_alias->name = strdup(alias_name);
	new_alias->cmd	= cmd_str;

	g_alias_registry[g_alias_count++] = new_alias;
	return true;
}

LIBMSE_API void libmse_cmd_register_default()
{
	if (g_cmd_defaults_registered) return;

	libmse_cmd_register(&(libmse_cmd_t){"help", "Lists all available commands", 0, cmd_help_handler});
	libmse_cmd_register(&(libmse_cmd_t){"listvars", "Lists all registered CVars", 0, cmd_listvars_handler});
	libmse_cmd_register(&(libmse_cmd_t){"listcmds", "Lists all registered Commands", 0, cmd_listcmds_handler});

	libmse_cmd_register(&(libmse_cmd_t){"print", "Prints the specified string", 1, cmd_print_handler});
	libmse_cmd_register(&(libmse_cmd_t){"get", "Reads current CVar variant", 1, libmse_cmd_get_handler});
	libmse_cmd_register(&(libmse_cmd_t){"delete", "Destroys a target CVar", 1, cmd_delete_handler});

	libmse_cmd_register(&(libmse_cmd_t){"set", "Modifies target CVar allocation", 2, libmse_cmd_set_handler});
	libmse_cmd_register(&(libmse_cmd_t){"create", "Registers a new CVar", 3, cmd_create_handler});

	libmse_cmd_register(&(libmse_cmd_t){"exec", "Executes a script file", 1, cmd_exec_handler});
	libmse_cmd_register(&(libmse_cmd_t){"alias", "Creates a new command alias", 2, cmd_alias_handler});

	g_cmd_defaults_registered = true;
}

LIBMSE_API bool libmse_cmd_parse(const char *input)
{
	char line_copy[LIBMSE_CMD_INPUT_BUFFER_SIZE];
	strncpy(line_copy, input, sizeof(line_copy) - 1);
	line_copy[sizeof(line_copy) - 1] = '\0';

	size_t		total_args = 0;
	const char *count_p	   = line_copy;

	while (*count_p) {
		while (*count_p == ' ') count_p++;
		if (!*count_p) break;

		total_args++;

		if (*count_p == '"') {
			count_p++;
			while (*count_p && *count_p != '"') count_p++;
			if (*count_p == '"') count_p++;
		} else {
			while (*count_p && *count_p != ' ') count_p++;
		}
	}

	if (total_args == 0) return false;

	char **args = (char **)malloc(total_args * sizeof(char *));
	if (!args) return false; // Out of memory

	int	  argc = 0;
	char *p	   = line_copy;

	while (*p && argc < total_args) {
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

	const char *cmd = args[0];

	libmse_cmd_t *cmd_def = libmse_cmd_get(cmd);
	if (cmd_def) {
		if (!libmse_cmd_execute(cmd, argc - 1, (const char **)&args[1])) {
			DEBUG_ERROR("Error: Command '%s' failed or expected %zu arguments.", cmd, cmd_def->expected_args_count);
			return false;
		}
		return true;
	}

	libmse_cvar_t *cvar = libmse_cvar_get(cmd);
	if (cvar) {
		if (argc == 1) { // Get logic
			const char *get_args[] = {cmd};
			libmse_cmd_get_handler(1, get_args);
			return true;
		} else if (argc == 2) { // Set logic
			const char *set_args[] = {cmd, args[1]};
			libmse_cmd_set_handler(2, set_args);
			return true;
		} else {
			DEBUG_ERROR("Usage: <cvar_name> [new_value]");
			return false;
		}
	}

	for (size_t i = 0; i < g_alias_count; i++) {
		if (g_alias_registry[i] && strcmp(g_alias_registry[i]->name, input) == 0) {
			return libmse_cmd_parse(g_alias_registry[i]->cmd);
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

	return cmd->handler(argc, argv);
}