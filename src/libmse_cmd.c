#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "libmse/libmse_debug.h"
#include "libmse/libmse_log.h"
#include "libmse/libmse_lua.h"
#include "libmse/libmse_cvar.h"
#include "libmse/libmse_resource.h"
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
	new_cmd->usage				 = cmd->usage ? strdup(cmd->usage) : NULL;

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
	if (cmd->usage) free((void *)cmd->usage);
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

// "name <arg> <arg>", falling back to a positional placeholder per expected
// argument when a command has not declared its usage.
static void cmd_format_signature(const libmse_cmd_t *cmd, char *out, size_t out_size)
{
	int written = snprintf(out, out_size, "%s", cmd->name);
	if (written < 0 || (size_t)written >= out_size) return;

	if (cmd->usage && cmd->usage[0] != (char)0) {
		snprintf(out + written, out_size - (size_t)written, " %s", cmd->usage);
		return;
	}

	for (size_t i = 0; i < cmd->expected_args_count; i++) {
		const int n = snprintf(out + written, out_size - (size_t)written, " <arg%zu>", i + 1);
		if (n < 0 || (size_t)(written + n) >= out_size) return;
		written += n;
	}
}

static void help_cmd_callback(const libmse_cmd_t *cmd, void *user_data)
{
	char signature[192];
	cmd_format_signature(cmd, signature, sizeof(signature));
	libmse_logf("  %-44s - %s", signature, cmd->description ? cmd->description : "");
}

static void help_alias_callback(const libmse_alias_t *alias, void *user_data)
{
	libmse_logf("  %-44s -> %s", alias->name, alias->cmd ? alias->cmd : "");
}

static void list_cmd_callback(const libmse_cmd_t *cmd, void *user_data)
{
	char signature[192];
	cmd_format_signature(cmd, signature, sizeof(signature));
	libmse_logf("  %-44s - %s (%zu required)", signature, cmd->description ? cmd->description : "",
				cmd->expected_args_count);
}

static void list_cvar_callback(libmse_cvar_t *cvar, void *user_data)
{
	if (cvar->type == LIBMSE_CVAR_INT) {
		libmse_logf("  %-30s = %-10d [%-6s] : %s", cvar->name, *cvar->data.i, "Int", cvar->description ? cvar->description : "");
	} else if (cvar->type == LIBMSE_CVAR_DOUBLE) {
		libmse_logf("  %-30s = %-10.4f [%-6s] : %s", cvar->name, *cvar->data.d, "Double", cvar->description ? cvar->description : "");
	} else if (cvar->type == LIBMSE_CVAR_FLOAT) {
		libmse_logf("  %-30s = %-10.4f [%-6s] : %s", cvar->name, *cvar->data.f, "Float", cvar->description ? cvar->description : "");
	} else if (cvar->type == LIBMSE_CVAR_STRING) {
		libmse_logf("  %-30s = %-10s [%-6s] : %s", cvar->name, *cvar->data.s ? *cvar->data.s : "NULL", "String", cvar->description ? cvar->description : "");
	} else if (libmse_cvar_type_components(cvar->type) > 0) {
		const size_t components = libmse_cvar_type_components(cvar->type);
		char		 text[96];
		char		 label[8];
		libmse_cvar_format_vec(cvar->data.v, components, text, sizeof(text));
		snprintf(label, sizeof(label), "Vec%zu", components);
		libmse_logf("  %-30s = %-10s [%-6s] : %s", cvar->name, text, label,
					cvar->description ? cvar->description : "");
	}
}

static bool cmd_listcmds_handler(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	libmse_log("Registered Commands:");
	libmse_cmd_iterate(list_cmd_callback, NULL);
	if (libmse_alias_count() > 0) {
		libmse_log("Aliases:");
		libmse_alias_iterate(help_alias_callback, NULL);
	}
	libmse_log("--- End of List ---");
	return true;
}

static bool cmd_listvars_handler(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	libmse_log("Registered CVars:");
	libmse_cvar_iterate(list_cvar_callback, NULL);
	libmse_log("--- End of List ---");
	return true;
}

static bool cmd_print_handler(int argc, const char **argv)
{
	for (int i = 0; i < argc; i++) {
		libmse_log_print(argv[i]);
		if (i < argc - 1) {
			libmse_log_print(" ");
		}
	}

	libmse_log_print("\n");
	return true;
}

LIBMSE_API bool libmse_cmd_get_handler(int argc, const char **argv)
{
	(void)argc;
	const char	  *name = argv[0];
	libmse_cvar_t *cvar = libmse_cvar_get(name);
	if (!cvar) {
		libmse_logf("CVar '%s' identifier target not found.", name);
		return false;
	}

	switch (cvar->type) {
	case LIBMSE_CVAR_INT:
		libmse_logf("%s = %d (Int)", cvar->name, *cvar->data.i);
		break;
	case LIBMSE_CVAR_FLOAT:
		libmse_logf("%s = %.4f (Float)", cvar->name, *cvar->data.f);
		break;
	case LIBMSE_CVAR_DOUBLE:
		libmse_logf("%s = %.4f (Double)", cvar->name, *cvar->data.d);
		break;
	case LIBMSE_CVAR_STRING:
		libmse_logf("%s = \"%s\" (String)", cvar->name, *cvar->data.s ? *cvar->data.s : "NULL");
		break;
	case LIBMSE_CVAR_VEC2:
	case LIBMSE_CVAR_VEC3:
	case LIBMSE_CVAR_VEC4: {
		const size_t components = libmse_cvar_type_components(cvar->type);
		char		 text[96];
		libmse_cvar_format_vec(cvar->data.v, components, text, sizeof(text));
		libmse_logf("%s = %s (Vec%zu)", cvar->name, text, components);
		break;
	}
	}

	return true;
}

LIBMSE_API bool libmse_cmd_set_handler(int argc, const char **argv)
{
	const char *name	= argv[0];
	const char *val_str = argv[1];

	// A vector is written as its components -- "set mse_theme_accent 0.35 0.62
	// 1 1" -- and the console split them into separate arguments. Joined back
	// up so the value is one string again, which is what every path below
	// wants; quoting it works too and arrives here already joined.
	char joined[192];
	if (argc > 2) {
		size_t written = 0;
		for (int i = 1; i < argc && written < sizeof(joined); ++i) {
			const int n = snprintf(joined + written, sizeof(joined) - written, (i == 1) ? "%s" : " %s", argv[i]);
			if (n < 0) break;
			written += (size_t)n;
		}
		val_str = joined;
	}

	libmse_cvar_t *cvar = libmse_cvar_get(name);
	if (!cvar) {
		// Defined rather than registered: a cvar created here has no variable
		// anywhere to point at, and the value has to outlive this handler.
		// It arrives as a string because there is nothing here to say what it
		// should be; whichever module owns the name adopts and retypes it when
		// it declares itself.
		libmse_cvar_define_string(name, val_str, "Dynamically created CVar (default string type)");
		cvar = libmse_cvar_get(name);
		if (!cvar) {
			DEBUG_ERROR("Failed to create new CVar for assignment.");
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
	else if (libmse_cvar_type_components(cvar->type) > 0) {
		float parsed[4] = {0};
		if (libmse_cvar_parse_vec(val_str, libmse_cvar_type_components(cvar->type), parsed)) {
			sync_success = libmse_cvar_set_v(name, parsed);
		} else {
			libmse_logf("'%s' wants %zu numbers, got \"%s\".", name,
						libmse_cvar_type_components(cvar->type), val_str);
		}
	}

	if (sync_success) {
		if (cvar->type == LIBMSE_CVAR_INT)
			libmse_logf("Set '%s' to %d.", name, *cvar->data.i);
		else if (cvar->type == LIBMSE_CVAR_FLOAT)
			libmse_logf("Set '%s' to %.4f.", name, *cvar->data.f);
		else if (cvar->type == LIBMSE_CVAR_DOUBLE)
			libmse_logf("Set '%s' to %.4f.", name, *cvar->data.d);
		else if (cvar->type == LIBMSE_CVAR_STRING)
			libmse_logf("Set '%s' to \"%s\".", name, cvar->data.s ? *cvar->data.s : "NULL");
		else if (libmse_cvar_type_components(cvar->type) > 0) {
			char text[96];
			libmse_cvar_format_vec(cvar->data.v, libmse_cvar_type_components(cvar->type), text, sizeof(text));
			libmse_logf("Set '%s' to %s.", name, text);
		}
	} else {
		libmse_logf("Failed to process assignment for CVar '%s'.", name);
	}
	return true;
}

static bool cmd_delete_handler(int argc, const char **argv)
{
	(void)argc;
	const char *name = argv[0];
	if (libmse_cvar_destroy(name)) {
		libmse_logf("Deleted CVar '%s' successfully.", name);
		return true;
	} else {
		libmse_logf("Failed to destroy CVar '%s' (Not found?).", name);
		return false;
	}
}

static bool cmd_help_handler(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	libmse_log("Available Commands:");
	libmse_cmd_iterate(help_cmd_callback, NULL);
	libmse_log("\n");
	return true;
}

static bool cmd_exec_handler(int argc, const char **argv)
{
	(void)argc;
	const char *filename = argv[0];

	// Relative to the working directory first, then to the app data directory,
	// which is where autoexec.cfg lives. That is what lets a user drop a
	// themes/mine.cfg next to their autoexec and have "exec themes/mine.cfg"
	// find it without knowing where the executable was installed.
	FILE *file = fopen(filename, "r");

	char resolved[512];
	if (!file) {
		const char *appdata = libmse_resource_get_appdata_path();
		if (appdata != NULL) {
			// Forward slash on both platforms: Windows accepts it, and a config
			// full of backslashes is no easier to read for having them.
			snprintf(resolved, sizeof(resolved), "%s/%s", appdata, filename);
			file = fopen(resolved, "r");
			if (file) {
				filename = resolved;
			}
		}
	}

	if (!file) {
		libmse_logf("Failed to open script file '%s'.", filename);
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
			libmse_logf("Failed at '%s' line %d: \"%s\"", filename, line_count, line);
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
	} else if (strcmp(type_str, "vec2") == 0) {
		type = LIBMSE_CVAR_VEC2;
	} else if (strcmp(type_str, "vec3") == 0) {
		type = LIBMSE_CVAR_VEC3;
	} else if (strcmp(type_str, "vec4") == 0) {
		type = LIBMSE_CVAR_VEC4;
	} else {
		libmse_log("Invalid CVar type specified. Use int, float, double, string, vec2, vec3 or vec4.");
		return false;
	}

	// These used to be bound to locals of this function, which stopped existing
	// the moment it returned: every cvar made this way pointed into a dead
	// stack frame, and setting one wrote there. The cvar system owns the value
	// now, so there is nothing here for it to outlive.
	libmse_cvar_t *created = NULL;
	switch (type) {
	case LIBMSE_CVAR_INT:
		created = libmse_cvar_define_int(name, 0, desc);
		break;
	case LIBMSE_CVAR_FLOAT:
		created = libmse_cvar_define_float(name, 0.0f, desc);
		break;
	case LIBMSE_CVAR_DOUBLE:
		created = libmse_cvar_define_double(name, 0.0, desc);
		break;
	case LIBMSE_CVAR_STRING:
		created = libmse_cvar_define_string(name, "", desc);
		break;
	case LIBMSE_CVAR_VEC2:
	case LIBMSE_CVAR_VEC3:
	case LIBMSE_CVAR_VEC4: {
		const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		created = libmse_cvar_define_vec(name, libmse_cvar_type_components(type), zero, desc);
		break;
	}
	default:
		libmse_log("Unsupported CVar type.");
		return false;
	}

	if (created != NULL) {
		char output[256];
		libmse_logf("Created CVar '%s' of type '%s'.", name, type_str);
		return true;
	} else {
		libmse_logf("Failed to create CVar '%s' (Already exists?).", name);
		return false;
	}
}

LIBMSE_API const libmse_alias_t *libmse_alias_get(const char *name)
{
	if (!name || !g_alias_registry) return NULL;
	for (size_t i = 0; i < g_alias_count; i++) {
		if (g_alias_registry[i] && strcmp(g_alias_registry[i]->name, name) == 0) {
			return g_alias_registry[i];
		}
	}
	return NULL;
}

LIBMSE_API void libmse_alias_iterate(libmse_alias_iterate_cb callback, void *user_data)
{
	if (!callback) return;
	for (size_t i = 0; i < g_alias_count; i++) {
		if (g_alias_registry[i]) callback(g_alias_registry[i], user_data);
	}
}

LIBMSE_API size_t libmse_alias_count(void)
{
	return g_alias_count;
}

static bool cmd_alias_handler(int argc, const char **argv)
{
	if (argc < 2) {
		libmse_log("Missing arguments for alias.");
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
		libmse_logf("Alias name '%s' conflicts with existing command and will be ignored.", alias_name);
		free(cmd_str);
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

static bool cmd_lua_handler(int argc, const char **argv)
{
	if (argc < 1) {
		libmse_log("Missing Lua code to execute.");
		return false;
	}

	size_t total_len = 0;
	for (int i = 0; i < argc; i++) {
		total_len += strlen(argv[i]);
		if (i < argc - 1) {
			total_len += 1; // For the space between arguments
		}
	}

	char *cmd_str = (char *)malloc(total_len + 1);
	if (!cmd_str) return false;

	cmd_str[0] = '\0'; // Start with an empty string
	for (int i = 0; i < argc; i++) {
		strcat(cmd_str, argv[i]);
		if (i < argc - 1) {
			strcat(cmd_str, " ");
		}
	}

	libmse_lua_worker_execute_string(libmse_lua_get_default_worker(), cmd_str);
	free(cmd_str);
	return true;
}

LIBMSE_API void libmse_cmd_register_default()
{
	if (g_cmd_defaults_registered) return;

	libmse_cmd_register(&(libmse_cmd_t){"help", "Lists all available commands", 0, cmd_help_handler});
	libmse_cmd_register(&(libmse_cmd_t){"listvars", "Lists all registered CVars", 0, cmd_listvars_handler});
	libmse_cmd_register(&(libmse_cmd_t){"listcmds", "Lists all registered Commands", 0, cmd_listcmds_handler});

	libmse_cmd_register(&(libmse_cmd_t){"print", "Prints the specified string", 1, cmd_print_handler, "<text...>"});
	libmse_cmd_register(&(libmse_cmd_t){"get", "Reads current CVar variant", 1, libmse_cmd_get_handler, "<cvar>"});
	libmse_cmd_register(&(libmse_cmd_t){"delete", "Destroys a target CVar", 1, cmd_delete_handler, "<cvar>"});

	libmse_cmd_register(&(libmse_cmd_t){"set", "Modifies target CVar allocation", 2, libmse_cmd_set_handler, "<cvar> <value>"});
	libmse_cmd_register(&(libmse_cmd_t){"create", "Registers a new CVar", 3, cmd_create_handler, "<name> <int|float|double|string> <value>"});

	libmse_cmd_register(&(libmse_cmd_t){"exec", "Executes a script file", 1, cmd_exec_handler, "<file>"});
	libmse_cmd_register(&(libmse_cmd_t){"alias", "Creates a new command alias", 2, cmd_alias_handler, "<name> <command...>"});

	libmse_cmd_register(&(libmse_cmd_t){"lua", "Executes inline Lua code", 1, cmd_lua_handler, "<code...>"});

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

	// Every path out of here frees `args`; it used to leak on all of them.
	libmse_cmd_t *cmd_def = libmse_cmd_get(cmd);
	if (cmd_def) {
		const bool ok = libmse_cmd_execute(cmd, argc - 1, (const char **)&args[1]);
		if (!ok) {
			char signature[192];
			cmd_format_signature(cmd_def, signature, sizeof(signature));
			libmse_logf("Command '%s' failed. Usage: %s", cmd, signature);
		}
		free(args);
		return ok;
	}

	libmse_cvar_t *cvar = libmse_cvar_get(cmd);
	if (cvar) {
		bool ok = true;
		if (argc == 1) { // Get logic
			const char *get_args[] = {cmd};
			libmse_cmd_get_handler(1, get_args);
		} else if (argc == 2) { // Set logic
			const char *set_args[] = {cmd, args[1]};
			libmse_cmd_set_handler(2, set_args);
		} else {
			libmse_logf("Usage: %s [new_value]", cmd);
			ok = false;
		}
		free(args);
		return ok;
	}

	// Matched on the first word: comparing against the whole line meant an
	// alias only ever resolved when it was typed with no arguments at all.
	for (size_t i = 0; i < g_alias_count; i++) {
		if (g_alias_registry[i] && strcmp(g_alias_registry[i]->name, cmd) == 0) {
			char expanded[LIBMSE_CMD_INPUT_BUFFER_SIZE];
			int  written = snprintf(expanded, sizeof(expanded), "%s", g_alias_registry[i]->cmd);
			for (int a = 1; a < argc && written > 0 && (size_t)written < sizeof(expanded); ++a) {
				const int n = snprintf(expanded + written, sizeof(expanded) - (size_t)written, " %s", args[a]);
				if (n < 0) break;
				written += n;
			}
			free(args);
			return libmse_cmd_parse(expanded);
		}
	}

	libmse_logf("Unknown command or CVar '%s'. Enter 'help' for a list.", cmd);
	free(args);
	return false;
}

LIBMSE_API bool libmse_cmd_execute(const char *name, int argc, const char **argv)
{
	libmse_cmd_t *cmd = libmse_cmd_get(name);
	if (!cmd) return false;

	if ((size_t)argc < cmd->expected_args_count) return false;

	return cmd->handler(argc, argv);
}