#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "libmse/libmse_cvar.h"

#define CVAR_INITIAL_CAPACITY 64

static libmse_cvar_t **g_cvar_registry = NULL;
static size_t		   g_cvar_count	   = 0;
static size_t		   g_cvar_capacity = 0;

// Helper functions to find indices
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

	libmse_cvar_t *cvar = (libmse_cvar_t *)malloc(sizeof(libmse_cvar_t));
	if (!cvar) return false;

	cvar->name		  = strdup(name);
	cvar->description = description ? strdup(description) : NULL;
	cvar->type		  = type;
	cvar->alloc_s	  = NULL;
	cvar->cb		  = NULL;
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
	case LIBMSE_CVAR_STRING:
		cvar->data.s = (const char **)ref;
		if (*cvar->data.s) {
			cvar->alloc_s = strdup(ref);
			cvar->data.s  = (const char **)&cvar->alloc_s;
		}
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
		}
	}
	fclose(file);
	return true;
}

LIBMSE_API bool libmse_cvar_register_change_cb(const char *name, libmse_cvar_change_cb callback, void *user_data)
{
	libmse_cvar_t *cvar = libmse_cvar_get(name);
	if (!cvar) return false;
	cvar->cb = callback;
	cvar->user_data = user_data;
	return true;
}