#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "libmse/libmse_cvar.h"
#include "libmse/libmse_log.h"

#define CVAR_INITIAL_CAPACITY 64

static libmse_cvar_t **g_cvar_registry = NULL;
static size_t		   g_cvar_count	   = 0;
static size_t		   g_cvar_capacity = 0;

LIBMSE_API size_t libmse_cvar_type_components(libmse_cvar_type_t type)
{
	switch (type) {
	case LIBMSE_CVAR_VEC2: return 2;
	case LIBMSE_CVAR_VEC3: return 3;
	case LIBMSE_CVAR_VEC4: return 4;
	default:               return 0;
	}
}

LIBMSE_API bool libmse_cvar_parse_vec(const char *text, size_t components, float *out)
{
	if (!text || !out || components == 0 || components > 4) return false;

	const char *p = text;
	for (size_t i = 0; i < components; ++i) {
		// Commas as well as spaces, so a value copied out of a struct
		// initialiser can be pasted back in without being retyped.
		while (*p == ' ' || *p == '\t' || *p == ',') p++;
		if (*p == '\0') return false;

		char *end = NULL;
		out[i]	  = strtof(p, &end);
		if (end == p) return false;
		p = end;
	}
	return true;
}

LIBMSE_API bool libmse_cvar_format_vec(const float *value, size_t components, char *buf, size_t buf_size)
{
	if (!value || !buf || buf_size == 0 || components == 0 || components > 4) return false;

	size_t written = 0;
	for (size_t i = 0; i < components && written < buf_size; ++i) {
		const int n = snprintf(buf + written, buf_size - written, (i == 0) ? "%g" : " %g", (double)value[i]);
		if (n < 0) return false;
		written += (size_t)n;
	}
	return true;
}

static int find_cvar_index(const char *name)
{
	if (!name || !g_cvar_registry) return -1;
	for (size_t i = 0; i < g_cvar_count; i++) {
		if (g_cvar_registry[i] && strcmp(g_cvar_registry[i]->name, name) == 0) {
			return (int)i;
		}
	}
	return -1;
}

static int find_cvar_index_reversed(const char *name)
{
	if (!name || !g_cvar_registry) return -1;
	for (size_t i = g_cvar_count; i > 0; i--) {
		if (g_cvar_registry[i - 1] && strcmp(g_cvar_registry[i - 1]->name, name) == 0) {
			return (int)(i - 1);
		}
	}
	return -1;
}

LIBMSE_API bool libmse_cvar_register(const char *name, libmse_cvar_type_t type, void *ref, const char *description)
{
	if (!name || !ref) return false;

	int index = find_cvar_index(name);
	if (index != -1) {
		libmse_cvar_t *cvar = g_cvar_registry[index];

		if (cvar->type != type) {
			// --- DATA PARSING & CONVERSION ---

			// 1. Converting from STRING to NUMERIC (Common config file scenario)
			if (cvar->type == LIBMSE_CVAR_STRING && cvar->data.s && *cvar->data.s) {
				const char *old_str = *cvar->data.s;
				if (type == LIBMSE_CVAR_INT)
					*(int *)ref = atoi(old_str);
				else if (type == LIBMSE_CVAR_FLOAT)
					*(float *)ref = (float)atof(old_str);
				else if (type == LIBMSE_CVAR_DOUBLE)
					*(double *)ref = atof(old_str);

				// We are no longer a string, free the allocated string memory!
				if (cvar->alloc_s) {
					free(cvar->alloc_s);
					cvar->alloc_s = NULL;
				}
			}
			// 2. Converting from NUMERIC to STRING
			else if (type == LIBMSE_CVAR_STRING) {
				char temp[64];
				if (cvar->type == LIBMSE_CVAR_INT)
					snprintf(temp, sizeof(temp), "%d", *cvar->data.i);
				else if (cvar->type == LIBMSE_CVAR_FLOAT)
					snprintf(temp, sizeof(temp), "%f", *cvar->data.f);
				else if (cvar->type == LIBMSE_CVAR_DOUBLE)
					snprintf(temp, sizeof(temp), "%lf", *cvar->data.d);
				else
					temp[0] = '\0';

				// Track allocated string to avoid memory leak and allow proper cleanup
				if (cvar->alloc_s) {
					free(cvar->alloc_s);
				}
				cvar->alloc_s		= strdup(temp);
				*(const char **)ref = cvar->alloc_s;
			}
			// 3. Converting between NUMERIC types
			else {
				if (type == LIBMSE_CVAR_INT) {
					if (cvar->type == LIBMSE_CVAR_FLOAT)
						*(int *)ref = (int)*cvar->data.f;
					else if (cvar->type == LIBMSE_CVAR_DOUBLE)
						*(int *)ref = (int)*cvar->data.d;
				} else if (type == LIBMSE_CVAR_FLOAT) {
					if (cvar->type == LIBMSE_CVAR_INT)
						*(float *)ref = (float)*cvar->data.i;
					else if (cvar->type == LIBMSE_CVAR_DOUBLE)
						*(float *)ref = (float)*cvar->data.d;
				} else if (type == LIBMSE_CVAR_DOUBLE) {
					if (cvar->type == LIBMSE_CVAR_INT)
						*(double *)ref = (double)*cvar->data.i;
					else if (cvar->type == LIBMSE_CVAR_FLOAT)
						*(double *)ref = (double)*cvar->data.f;
				}
			}

			// Update to the newly requested type
			cvar->type = type;

			// Point the CVar to the newly provided memory reference
			switch (type) {
			case LIBMSE_CVAR_INT:
				cvar->data.i = (int *)ref;
				break;
			case LIBMSE_CVAR_FLOAT:
				cvar->data.f = (float *)ref;
				break;
			case LIBMSE_CVAR_DOUBLE:
				cvar->data.d = (double *)ref;
				break;
			case LIBMSE_CVAR_STRING:
				cvar->data.s = (const char **)ref;
				break;
			}
		} else {
			// Types match: Push the existing value into the new reference memory,
			// then update the CVar's internal pointer to point to the new reference.
			switch (type) {
			case LIBMSE_CVAR_INT:
				*(int *)ref	 = *cvar->data.i;
				cvar->data.i = (int *)ref;
				break;
			case LIBMSE_CVAR_FLOAT:
				*(float *)ref = *cvar->data.f;
				cvar->data.f  = (float *)ref;
				break;
			case LIBMSE_CVAR_DOUBLE:
				*(double *)ref = *cvar->data.d;
				cvar->data.d   = (double *)ref;
				break;
			case LIBMSE_CVAR_STRING:
				*(const char **)ref = *cvar->data.s;
				cvar->data.s		= (const char **)ref;
				break;
			}
		}

		// Prevent crashes when testing descriptions that may be NULL and free overwrites
		const char *safe_old_desc = cvar->description ? cvar->description : "";
		const char *safe_new_desc = description ? description : "";

		if (strcmp(safe_old_desc, safe_new_desc) != 0) {
			if (cvar->description) {
				free((void *)cvar->description);
			}
			cvar->description = description ? strdup(description) : NULL;
		}

		return true;
	}

	if (g_cvar_count >= g_cvar_capacity) {
		size_t			new_capacity = (g_cvar_capacity == 0) ? CVAR_INITIAL_CAPACITY : g_cvar_capacity * 2;
		libmse_cvar_t **new_registry =
			(libmse_cvar_t **)realloc(g_cvar_registry, new_capacity * sizeof(libmse_cvar_t *));
		if (!new_registry) return false;

		g_cvar_registry = new_registry;
		g_cvar_capacity = new_capacity;
	}

	// calloc, not malloc: this used to hand back a struct whose tail was
	// whatever was on the heap, and `owned` reading as garbage true made a
	// placeholder from a config file refuse to be adopted by the module that
	// owns the name.
	libmse_cvar_t *cvar = (libmse_cvar_t *)calloc(1, sizeof(libmse_cvar_t));
	if (!cvar) return false;

	cvar->name		  = strdup(name);
	cvar->description = description ? strdup(description) : NULL;
	cvar->type		  = type;
	cvar->alloc_s	  = NULL;
	cvar->cb		  = NULL;
	cvar->owned		  = false;
	memset(&cvar->data, 0, sizeof(cvar->data));

	switch (type) {
	case LIBMSE_CVAR_INT:
		cvar->data.i = (int *)ref;
		break;
	case LIBMSE_CVAR_FLOAT:
		cvar->data.f = (float *)ref;
		break;
	case LIBMSE_CVAR_DOUBLE:
		cvar->data.d = (double *)ref;
		break;
	case LIBMSE_CVAR_VEC2:
	case LIBMSE_CVAR_VEC3:
	case LIBMSE_CVAR_VEC4:
		cvar->data.v = (float *)ref;
		break;
	case LIBMSE_CVAR_STRING:
		// ref is a const char**, like every other type here is a pointer to
		// its value. This used to strdup(ref) -- the pointer's own bytes, read
		// as a string -- and then read *ref before knowing there was anything
		// there, which worked only because its one caller passed a string
		// literal instead of a pointer to one. That caller now defines its
		// cvar instead; see libmse_cvar_define_string.
		cvar->data.s = (const char **)ref;
		break;
	}

	g_cvar_registry[g_cvar_count++] = cvar;
	return true;
}

LIBMSE_API bool libmse_cvar_destroy(const char *name)
{
	int index = find_cvar_index(name);
	if (index == -1) return false;

	libmse_cvar_t *cvar = g_cvar_registry[index];
	free((void *)cvar->name);
	if (cvar->description) free((void *)cvar->description);
	if (cvar->alloc_s) free(cvar->alloc_s); // Only free memory allocated via console string overrides
	free(cvar);

	for (size_t i = (size_t)index; i < g_cvar_count - 1; i++) {
		g_cvar_registry[i] = g_cvar_registry[i + 1];
	}
	g_cvar_count--;
	g_cvar_registry[g_cvar_count] = NULL;
	return true;
}

LIBMSE_API void libmse_cvar_iterate(libmse_cvar_iterate_cb callback, void *user_data)
{
	if (!callback) return;
	for (size_t i = 0; i < g_cvar_count; i++) {
		callback(g_cvar_registry[i], user_data);
	}
}

LIBMSE_API libmse_cvar_t *libmse_cvar_get(const char *name)
{
	int index = find_cvar_index(name);
	if (index == -1) return NULL;
	return g_cvar_registry[index];
}

LIBMSE_API libmse_cvar_t *libmse_cvar_get_reversed(const char *name)
{
	int index = find_cvar_index_reversed(name);
	if (index == -1) return NULL;
	return g_cvar_registry[index];
}

LIBMSE_API void *libmse_cvar_get_ptr(const char *name)
{
	libmse_cvar_t *cvar = libmse_cvar_get(name);
	return cvar ? &cvar->data : NULL;
}

LIBMSE_API int *libmse_cvar_get_i(const char *name)
{
	libmse_cvar_t *cvar = libmse_cvar_get(name);
	return (cvar && cvar->type == LIBMSE_CVAR_INT) ? cvar->data.i : NULL;
}

LIBMSE_API float *libmse_cvar_get_f(const char *name)
{
	libmse_cvar_t *cvar = libmse_cvar_get(name);
	return (cvar && cvar->type == LIBMSE_CVAR_FLOAT) ? cvar->data.f : NULL;
}

LIBMSE_API double *libmse_cvar_get_d(const char *name)
{
	libmse_cvar_t *cvar = libmse_cvar_get(name);
	return (cvar && cvar->type == LIBMSE_CVAR_DOUBLE) ? cvar->data.d : NULL;
}

LIBMSE_API const char **libmse_cvar_get_s(const char *name)
{
	libmse_cvar_t *cvar = libmse_cvar_get(name);
	return (cvar && cvar->type == LIBMSE_CVAR_STRING) ? cvar->data.s : NULL;
}

LIBMSE_API bool libmse_cvar_set_i(const char *name, int value)
{
	libmse_cvar_t *cvar = libmse_cvar_get(name);
	if (!cvar || cvar->type != LIBMSE_CVAR_INT) return false;
	*cvar->data.i = value;
	if (cvar->cb) cvar->cb(cvar, cvar->user_data);
	return true;
}

LIBMSE_API bool libmse_cvar_set_f(const char *name, float value)
{
	libmse_cvar_t *cvar = libmse_cvar_get(name);
	if (!cvar || cvar->type != LIBMSE_CVAR_FLOAT) return false;
	*cvar->data.f = value;
	if (cvar->cb) cvar->cb(cvar, cvar->user_data);
	return true;
}

LIBMSE_API bool libmse_cvar_set_d(const char *name, double value)
{
	libmse_cvar_t *cvar = libmse_cvar_get(name);
	if (!cvar || cvar->type != LIBMSE_CVAR_DOUBLE) return false;
	*cvar->data.d = value;
	if (cvar->cb) cvar->cb(cvar, cvar->user_data);
	return true;
}

LIBMSE_API bool libmse_cvar_set_s(const char *name, const char *value)
{
	libmse_cvar_t *cvar = libmse_cvar_get(name);
	if (!cvar || cvar->type != LIBMSE_CVAR_STRING) return false;

	if (cvar->alloc_s) free(cvar->alloc_s);
	cvar->alloc_s = strdup(value);
	*cvar->data.s = cvar->alloc_s;
	if (cvar->cb) cvar->cb(cvar, cvar->user_data);
	return true;
}

LIBMSE_API bool libmse_cvar_set_v(const char *name, const float *value)
{
	libmse_cvar_t *cvar = libmse_cvar_get(name);
	if (!cvar || !value) return false;

	const size_t components = libmse_cvar_type_components(cvar->type);
	if (components == 0 || !cvar->data.v) return false;

	memcpy(cvar->data.v, value, components * sizeof(float));
	if (cvar->cb) cvar->cb(cvar, cvar->user_data);
	return true;
}

LIBMSE_API float *libmse_cvar_get_v(const char *name)
{
	libmse_cvar_t *cvar = libmse_cvar_get(name);
	if (!cvar || libmse_cvar_type_components(cvar->type) == 0) return NULL;
	return cvar->data.v;
}

LIBMSE_API bool libmse_cvar_export(const char *filename)
{
	FILE *file = fopen(filename, "w");

	if (!file) return false;
	for (size_t i = 0; i < g_cvar_count; i++) {
		libmse_cvar_t *cvar = g_cvar_registry[i];
		if (!cvar) continue;

		switch (cvar->type) {
		case LIBMSE_CVAR_INT:
			fprintf(file, "set %s %d\n", cvar->name, *cvar->data.i);
			break;
		case LIBMSE_CVAR_FLOAT:
			fprintf(file, "set %s %f\n", cvar->name, *cvar->data.f);
			break;
		case LIBMSE_CVAR_DOUBLE:
			fprintf(file, "set %s %lf\n", cvar->name, *cvar->data.d);
			break;
		case LIBMSE_CVAR_STRING:
			fprintf(file, "set %s \"%s\"\n", cvar->name, *cvar->data.s ? *cvar->data.s : "");
			break;
		case LIBMSE_CVAR_VEC2:
		case LIBMSE_CVAR_VEC3:
		case LIBMSE_CVAR_VEC4: {
			// Quoted, so the line reads back as one argument whichever path the
			// console takes it down.
			char text[96];
			const size_t components = libmse_cvar_type_components(cvar->type);
			if (cvar->data.v && libmse_cvar_format_vec(cvar->data.v, components, text, sizeof(text))) {
				fprintf(file, "set %s \"%s\"\n", cvar->name, text);
			}
			break;
		}
		}
	}
	fclose(file);
	return true;
}

// --- defining a cvar the system owns ----------------------------------------

// The pointer into a cvar's own storage, which is what data.* is set to and
// what a LIBMSE_CVAR_DEFINE_* declaration gets handed back.
static void *cvar_storage_ptr(libmse_cvar_t *cvar)
{
	switch (cvar->type) {
	case LIBMSE_CVAR_INT:
		return &cvar->storage.i;
	case LIBMSE_CVAR_FLOAT:
		return &cvar->storage.f;
	case LIBMSE_CVAR_DOUBLE:
		return &cvar->storage.d;
	case LIBMSE_CVAR_STRING:
		return &cvar->storage.s;
	case LIBMSE_CVAR_VEC2:
	case LIBMSE_CVAR_VEC3:
	case LIBMSE_CVAR_VEC4:
		return cvar->storage.v;
	}
	return NULL;
}

// Takes over a cvar that a config file created before anything had registered
// the name. Those arrive as strings -- the console has no way to know better --
// so the text is parsed into the type being defined and the placeholder's
// allocation is released.
static void cvar_adopt_placeholder(libmse_cvar_t *cvar, libmse_cvar_type_t type)
{
	const char *text = (cvar->type == LIBMSE_CVAR_STRING && cvar->data.s && *cvar->data.s)
	                       ? *cvar->data.s
	                       : NULL;

	cvar->type = type;

	switch (type) {
	case LIBMSE_CVAR_INT:
		cvar->storage.i = text ? atoi(text) : 0;
		break;
	case LIBMSE_CVAR_FLOAT:
		cvar->storage.f = text ? (float)atof(text) : 0.0f;
		break;
	case LIBMSE_CVAR_DOUBLE:
		cvar->storage.d = text ? atof(text) : 0.0;
		break;
	case LIBMSE_CVAR_STRING: {
		char *copy = text ? strdup(text) : NULL;
		if (cvar->alloc_s) free(cvar->alloc_s);
		cvar->alloc_s   = copy;
		cvar->storage.s = copy;
		break;
	}
	case LIBMSE_CVAR_VEC2:
	case LIBMSE_CVAR_VEC3:
	case LIBMSE_CVAR_VEC4:
		// A config file writes a vector as "0.35 0.62 1 1", which the console
		// stored verbatim because it had no type to parse it against. It has
		// one now.
		memset(cvar->storage.v, 0, sizeof(cvar->storage.v));
		if (text) {
			libmse_cvar_parse_vec(text, libmse_cvar_type_components(type), cvar->storage.v);
		}
		break;
	}

	if (type != LIBMSE_CVAR_STRING && cvar->alloc_s) {
		free(cvar->alloc_s);
		cvar->alloc_s = NULL;
	}

	cvar->owned = true;
	memset(&cvar->data, 0, sizeof(cvar->data));
	*(void **)&cvar->data = cvar_storage_ptr(cvar);
}

// The value a define starts a brand new cvar at. Existing cvars keep whatever
// they already hold, so that a config file read before the owning module ran
// is not thrown away by the module then declaring its default.
typedef union {
	int         i;
	float       f;
	double      d;
	const char *s;
	float       v[4];
} cvar_default_t;

static libmse_cvar_t *cvar_define(const char *name, libmse_cvar_type_t type, cvar_default_t value,
                                  const char *description)
{
	if (!name) return NULL;

	int index = find_cvar_index(name);
	if (index != -1) {
		libmse_cvar_t *existing = g_cvar_registry[index];

		// A placeholder is the one case worth taking over: it holds a value
		// somebody asked for and has nowhere real to keep it. Anything else is
		// a name defined twice, and silently repointing it would be worse than
		// leaving the first one standing.
		// Not conditioned on ownership: a config file's value is a string
		// because the console had nothing better to guess, however it came to
		// be stored. A string cvar being defined as a number is that signal.
		if (existing->type == LIBMSE_CVAR_STRING && type != LIBMSE_CVAR_STRING) {
			cvar_adopt_placeholder(existing, type);
		}

		// The description comes from whoever defines the cvar, not from
		// whoever mentioned the name first. A cvar a config file created is
		// carrying "Dynamically created CVar" until its owner turns up, and
		// leaving that in place is how every setting a themes/*.cfg touches
		// ended up described as dynamically created.
		if (description != NULL) {
			const char *current = existing->description;
			if (current == NULL || strcmp(current, description) != 0) {
				free((void *)existing->description);
				existing->description = strdup(description);
			}
		}

		return existing;
	}

	if (g_cvar_count >= g_cvar_capacity) {
		size_t			new_capacity = (g_cvar_capacity == 0) ? CVAR_INITIAL_CAPACITY : g_cvar_capacity * 2;
		libmse_cvar_t **new_registry =
			(libmse_cvar_t **)realloc(g_cvar_registry, new_capacity * sizeof(libmse_cvar_t *));
		if (!new_registry) return NULL;

		g_cvar_registry = new_registry;
		g_cvar_capacity = new_capacity;
	}

	libmse_cvar_t *cvar = (libmse_cvar_t *)calloc(1, sizeof(libmse_cvar_t));
	if (!cvar) return NULL;

	cvar->name        = strdup(name);
	cvar->description = description ? strdup(description) : NULL;
	cvar->type        = type;
	cvar->owned       = true;

	switch (type) {
	case LIBMSE_CVAR_INT:
		cvar->storage.i = value.i;
		break;
	case LIBMSE_CVAR_FLOAT:
		cvar->storage.f = value.f;
		break;
	case LIBMSE_CVAR_DOUBLE:
		cvar->storage.d = value.d;
		break;
	case LIBMSE_CVAR_STRING:
		cvar->alloc_s   = value.s ? strdup(value.s) : NULL;
		cvar->storage.s = cvar->alloc_s;
		break;
	case LIBMSE_CVAR_VEC2:
	case LIBMSE_CVAR_VEC3:
	case LIBMSE_CVAR_VEC4:
		memcpy(cvar->storage.v, value.v, sizeof(cvar->storage.v));
		break;
	}

	// The cvar itself never moves -- the registry holds pointers, so growing it
	// reallocates the array and not what it points at -- which is what makes
	// handing this address out safe.
	*(void **)&cvar->data = cvar_storage_ptr(cvar);

	g_cvar_registry[g_cvar_count++] = cvar;
	return cvar;
}

LIBMSE_API libmse_cvar_t *libmse_cvar_define_int(const char *name, int value, const char *description)
{
	return cvar_define(name, LIBMSE_CVAR_INT, (cvar_default_t){.i = value}, description);
}

LIBMSE_API libmse_cvar_t *libmse_cvar_define_float(const char *name, float value, const char *description)
{
	return cvar_define(name, LIBMSE_CVAR_FLOAT, (cvar_default_t){.f = value}, description);
}

LIBMSE_API libmse_cvar_t *libmse_cvar_define_double(const char *name, double value, const char *description)
{
	return cvar_define(name, LIBMSE_CVAR_DOUBLE, (cvar_default_t){.d = value}, description);
}

LIBMSE_API libmse_cvar_t *libmse_cvar_define_string(const char *name, const char *value, const char *description)
{
	return cvar_define(name, LIBMSE_CVAR_STRING, (cvar_default_t){.s = value}, description);
}

LIBMSE_API libmse_cvar_t *libmse_cvar_define_vec(const char *name, size_t components, const float *value,
                                                 const char *description)
{
	libmse_cvar_type_t type;
	switch (components) {
	case 2:  type = LIBMSE_CVAR_VEC2; break;
	case 3:  type = LIBMSE_CVAR_VEC3; break;
	case 4:  type = LIBMSE_CVAR_VEC4; break;
	default: return NULL;
	}

	cvar_default_t dflt = {0};
	if (value) {
		memcpy(dflt.v, value, components * sizeof(float));
	}
	return cvar_define(name, type, dflt, description);
}

// --- declared cvars, queued before main and defined once libmse is up -------

static libmse_cvar_def_t *g_pending_head = NULL;
static libmse_cvar_def_t *g_pending_tail = NULL;

LIBMSE_API void libmse_cvar_queue(libmse_cvar_def_t *def)
{
	if (!def || def->queued) return;

	// Appended rather than pushed, so declarations in one file are defined in
	// the order they were written. Across files the order static initialisers
	// run in is nobody's to promise, which is another reason a declaration
	// carries a default instead of depending on another one having run.
	def->next   = NULL;
	def->queued = true;

	if (g_pending_tail) {
		g_pending_tail->next = def;
	} else {
		g_pending_head = def;
	}
	g_pending_tail = def;
}

LIBMSE_API size_t libmse_cvar_flush(void)
{
	libmse_cvar_def_t *def = g_pending_head;

	// Taken off the list first: defining one can load something that queues
	// more, and those belong to the next flush rather than to this loop.
	g_pending_head = NULL;
	g_pending_tail = NULL;

	size_t defined = 0;

	while (def) {
		libmse_cvar_def_t *next = def->next;
		def->next = NULL;

		// Copied a field at a time because the descriptor is public API and
		// this union is not: the two have the same shape, but saying so by
		// casting between them would make the header's layout load-bearing.
		cvar_default_t dflt = {0};
		switch (def->type) {
		case LIBMSE_CVAR_INT:    dflt.i = def->value.i; break;
		case LIBMSE_CVAR_FLOAT:  dflt.f = def->value.f; break;
		case LIBMSE_CVAR_DOUBLE: dflt.d = def->value.d; break;
		case LIBMSE_CVAR_STRING: dflt.s = def->value.s; break;
		case LIBMSE_CVAR_VEC2:
		case LIBMSE_CVAR_VEC3:
		case LIBMSE_CVAR_VEC4:
			memcpy(dflt.v, def->value.v, sizeof(dflt.v));
			break;
		}

		libmse_cvar_t *cvar = cvar_define(def->name, def->type, dflt, def->description);
		if (cvar) {
			defined++;
			if (def->out) {
				*def->out = cvar_storage_ptr(cvar);
			}
		} else {
			libmse_logf("cvar: could not define '%s'", def->name ? def->name : "(null)");
		}

		def = next;
	}

	return defined;
}

LIBMSE_API bool libmse_cvar_register_change_cb(const char *name, libmse_cvar_change_cb callback, void *user_data)
{
	libmse_cvar_t *cvar = libmse_cvar_get(name);
	if (!cvar) return false;
	cvar->cb = callback;
	cvar->user_data = user_data;
	return true;
}

LIBMSE_API void* libmse_get_cvar_registry() { return (void*)g_cvar_registry; }
LIBMSE_API size_t libmse_get_cvar_count()   { return g_cvar_count; }