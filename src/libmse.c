#include "libmse/libmse.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "compat/unzip.h"

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#include <sys/types.h>
#else
#include <dlfcn.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define MSE_BACKEND_LIBRARY_NAME "libemulator"

#define MSE_PATH_BUFFER 1024
#define MSE_COPY_BUFFER 8192

typedef struct mse_backend_record_s {
	mse_backend_t backend;
	void *library_handle;
	struct mse_backend_record_s *next;
} mse_backend_record_t;

static mse_backend_record_t *g_backends = NULL;

static bool mse_path_exists(const char *path) {
	struct stat path_stat;

	if (path == NULL) {
		return false;
	}

	return stat(path, &path_stat) == 0;
}

static bool mse_path_is_directory(const char *path) {
	struct stat path_stat;

	if (path == NULL) {
		return false;
	}

	if (stat(path, &path_stat) != 0) {
		return false;
	}

#if defined(_WIN32)
	return (path_stat.st_mode & _S_IFDIR) != 0;
#else
	return S_ISDIR(path_stat.st_mode);
#endif
}

static bool mse_mkdir_single(const char *path) {
	if (path == NULL || *path == '\0') {
		return false;
	}

#if defined(_WIN32)
	if (_mkdir(path) == 0 || errno == EEXIST) {
		return true;
	}
#else
	if (mkdir(path, 0775) == 0 || errno == EEXIST) {
		return true;
	}
#endif

	return false;
}

static bool mse_make_directory_recursive(const char *path) {
	char buffer[MSE_PATH_BUFFER];
	size_t index;
	size_t length;
	size_t start_index;

	if (path == NULL) {
		return false;
	}

	length = strlen(path);
	if (length >= sizeof(buffer)) {
		return false;
	}

	memcpy(buffer, path, length + 1U);
	start_index = (buffer[1] == ':') ? 3U : 1U;

	for (index = start_index; buffer[index] != '\0'; ++index) {
		if (buffer[index] == '/' || buffer[index] == '\\') {
			char saved = buffer[index];
			buffer[index] = '\0';
			if (!mse_mkdir_single(buffer)) {
				buffer[index] = saved;
				return false;
			}
			buffer[index] = saved;
		}
	}

	return mse_mkdir_single(buffer);
}

static bool mse_path_join(char *out, size_t out_size, const char *left, const char *right) {
	size_t left_length;
	size_t right_length;

	if (out == NULL || out_size == 0U || left == NULL || right == NULL) {
		return false;
	}

	left_length = strlen(left);
	while (left_length > 0U && (left[left_length - 1U] == '/' || left[left_length - 1U] == '\\')) {
		--left_length;
	}

	right_length = strlen(right);
	if (left_length + 1U + right_length + 1U > out_size) {
		return false;
	}

	if (snprintf(out, out_size, "%.*s/%s", (int)left_length, left, right) < 0) {
		return false;
	}

	return true;
}

static bool mse_is_safe_relative_path(const char *path) {
	const char *cursor;

	if (path == NULL || *path == '\0') {
		return false;
	}

	if (path[0] == '/' || path[0] == '\\') {
		return false;
	}

	if (strlen(path) > 1U && path[1] == ':') {
		return false;
	}

	cursor = path;
	while (*cursor != '\0') {
		const char *segment_start = cursor;
		size_t segment_length;

		while (*cursor != '\0' && *cursor != '/' && *cursor != '\\') {
			++cursor;
		}

		segment_length = (size_t)(cursor - segment_start);
		if (segment_length == 2U && segment_start[0] == '.' && segment_start[1] == '.') {
			return false;
		}

		if (*cursor != '\0') {
			++cursor;
		}
	}

	return true;
}

static void *mse_library_open(const char *path) {
	if (path == NULL)
		return NULL;

#if defined(_WIN32)
	return (void *)LoadLibraryA(path);
#else
	return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void mse_library_close(void *handle) {
	if (handle == NULL) {
		return;
	}

#if defined(_WIN32)
	FreeLibrary((HMODULE)handle);
#else
	dlclose(handle);
#endif
}

static void *mse_library_symbol(void *handle, const char *symbol) {
	if (handle == NULL || symbol == NULL) {
		return NULL;
	}

#if defined(_WIN32)
	return (void *)GetProcAddress((HMODULE)handle, symbol);
#else
	return dlsym(handle, symbol);
#endif
}

static bool mse_create_temp_directory(char *out, size_t out_size) {
	if (out == NULL || out_size == 0U) {
		return false;
	}

#if defined(_WIN32)
	char temp_path[MAX_PATH];

	if (GetTempPathA((DWORD)sizeof(temp_path), temp_path) == 0U) {
		return false;
	}

	for (unsigned int attempt = 0U; attempt < 128U; ++attempt) {
		ULONGLONG tick = GetTickCount64();
		DWORD pid = GetCurrentProcessId();
		if (snprintf(out, out_size, "%s/mse_backend_%lu_%llu_%u", temp_path, (unsigned long)pid, (unsigned long long)tick, attempt) < 0) {
			return false;
		}

		if (CreateDirectoryA(out, NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS) {
			return true;
		}
	}

	return false;
#else
	char template_buffer[MSE_PATH_BUFFER];
	char *result;
	size_t length;

	if (snprintf(template_buffer, sizeof(template_buffer), "/tmp/mse_backend_XXXXXX") < 0) {
		return false;
	}

	result = mkdtemp(template_buffer);
	if (result == NULL) {
		return false;
	}

	length = strlen(result);
	if (length >= out_size) {
		return false;
	}

	memcpy(out, result, length + 1U);
	return true;
#endif
}

static bool mse_extract_zip_entry(unzFile archive, const char *entry_name, const char *destination_root) {
	char normalized_name[MSE_PATH_BUFFER];
	char destination_path[MSE_PATH_BUFFER];
	char *last_separator;
	FILE *output = NULL;
	int read_bytes;
	size_t entry_length;
	char buffer[MSE_COPY_BUFFER];

	if (archive == NULL || entry_name == NULL || destination_root == NULL) {
		return false;
	}

	if (!mse_is_safe_relative_path(entry_name)) {
		return false;
	}

	entry_length = strlen(entry_name);
	if (entry_length == 0U || entry_length >= sizeof(normalized_name)) {
		return false;
	}

	memcpy(normalized_name, entry_name, entry_length + 1U);
	for (char *cursor = normalized_name; *cursor != '\0'; ++cursor) {
		if (*cursor == '\\') {
			*cursor = '/';
		}
	}

	if (!mse_path_join(destination_path, sizeof(destination_path), destination_root, normalized_name)) {
		return false;
	}

	if (normalized_name[entry_length - 1U] == '/') {
		return mse_make_directory_recursive(destination_path);
	}

	last_separator = strrchr(destination_path, '/');
	if (last_separator != NULL) {
		*last_separator = '\0';
		if (!mse_make_directory_recursive(destination_path)) {
			return false;
		}
		*last_separator = '/';
	}

	if (unzOpenCurrentFile(archive) != UNZ_OK) {
		return false;
	}

	output = fopen(destination_path, "wb");
	if (output == NULL) {
		unzCloseCurrentFile(archive);
		return false;
	}

	while ((read_bytes = unzReadCurrentFile(archive, buffer, (uint32_t)sizeof(buffer))) > 0) {
		if (fwrite(buffer, 1U, (size_t)read_bytes, output) != (size_t)read_bytes) {
			fclose(output);
			unzCloseCurrentFile(archive);
			return false;
		}
	}

	fclose(output);
	unzCloseCurrentFile(archive);

	return read_bytes == 0;
}

static bool mse_extract_zip_archive(const char *zip_path, char *out_root, size_t out_root_size) {
	unzFile archive;
	int unzip_status;

	if (zip_path == NULL || out_root == NULL) {
		return false;
	}

	if (!mse_create_temp_directory(out_root, out_root_size)) {
		return false;
	}

	archive = unzOpen(zip_path);
	if (archive == NULL) {
		return false;
	}

	unzip_status = unzGoToFirstFile(archive);
	if (unzip_status != UNZ_OK) {
		unzClose(archive);
		return false;
	}

	while (unzip_status == UNZ_OK) {
		char entry_name[MSE_PATH_BUFFER];
		unz_file_info64 file_info;

		memset(&file_info, 0, sizeof(file_info));
		memset(entry_name, 0, sizeof(entry_name));

		if (unzGetCurrentFileInfo64(archive, &file_info, entry_name, (unsigned long)sizeof(entry_name), NULL, 0U, NULL, 0U) != UNZ_OK) {
			unzClose(archive);
			return false;
		}

		if (!mse_extract_zip_entry(archive, entry_name, out_root)) {
			unzClose(archive);
			return false;
		}

		unzip_status = unzGoToNextFile(archive);
	}

	unzClose(archive);
	return true;
}

static bool mse_backend_load_info(void *library_handle, mse_backend_t *backend) {
	const mse_backend_info_t *info = NULL;
	const mse_backend_source_t *source = NULL;
	const mse_backend_caps_e *capabilities = NULL;
	mse_backend_start_callback_t start = NULL;
	mse_backend_get_texture_callback_t get_texture = NULL;
	const mse_backend_input_desc_t *inputs = NULL;
	const size_t *input_count = NULL;
	mse_backend_init_callback_t init = NULL;
	mse_backend_shutdown_callback_t shutdown = NULL;
	mse_backend_load_rom_callback_t load_rom = NULL;
	mse_backend_update_inputs_callback_t update_inputs = NULL;

	if (library_handle == NULL || backend == NULL) {
		return false;
	}

	info = (const mse_backend_info_t *)mse_library_symbol(library_handle, "info");
	if (info == NULL) {
		info = (const mse_backend_info_t *)mse_library_symbol(library_handle, "backend_info");
	}
	if (info == NULL) {
		info = (const mse_backend_info_t *)mse_library_symbol(library_handle, "mse_backend_info");
	}
	if (info == NULL) {
		return false;
	}

	source = (const mse_backend_source_t *)mse_library_symbol(library_handle, "source");
	capabilities = (const mse_backend_caps_e *)mse_library_symbol(library_handle, "capabilities");
	start = (mse_backend_start_callback_t)mse_library_symbol(library_handle, "start");
	if (start == NULL) {
		start = (mse_backend_start_callback_t)mse_library_symbol(library_handle, "backend_start");
	}

	get_texture = (mse_backend_get_texture_callback_t)mse_library_symbol(library_handle, "get_texture");
	if (get_texture == NULL) {
		get_texture = (mse_backend_get_texture_callback_t)mse_library_symbol(library_handle, "backend_get_texture");
	}

	/* Input control scheme (optional) */
	inputs = (const mse_backend_input_desc_t *)mse_library_symbol(library_handle, "inputs");
	if (inputs == NULL) {
		inputs = (const mse_backend_input_desc_t *)mse_library_symbol(library_handle, "backend_inputs");
	}
	input_count = (const size_t *)mse_library_symbol(library_handle, "input_count");
	if (input_count == NULL) {
		input_count = (const size_t *)mse_library_symbol(library_handle, "backend_input_count");
	}

	init = (mse_backend_init_callback_t)mse_library_symbol(library_handle, "init");
	if (init == NULL) {
		init = (mse_backend_init_callback_t)mse_library_symbol(library_handle, "backend_init");
	}

	shutdown = (mse_backend_shutdown_callback_t)mse_library_symbol(library_handle, "shutdown");
	if (shutdown == NULL) {
		shutdown = (mse_backend_shutdown_callback_t)mse_library_symbol(library_handle, "backend_shutdown");
	}

	load_rom = (mse_backend_load_rom_callback_t)mse_library_symbol(library_handle, "load_rom");
	if (load_rom == NULL) {
		load_rom = (mse_backend_load_rom_callback_t)mse_library_symbol(library_handle, "backend_load_rom");
	}

	update_inputs = (mse_backend_update_inputs_callback_t)mse_library_symbol(library_handle, "update_inputs");
	if (update_inputs == NULL) {
		update_inputs = (mse_backend_update_inputs_callback_t)mse_library_symbol(library_handle, "backend_update_inputs");
	}

	backend->info = *info;
	backend->source.repository = info->repository;
	backend->source.commit = NULL;
	backend->source.branch = NULL;

	if (source != NULL) {
		if (source->repository != NULL) {
			backend->source.repository = source->repository;
		}
		backend->source.commit = source->commit;
		backend->source.branch = source->branch;
	}

	backend->capabilities = capabilities != NULL ? *capabilities : MSE_BACKEND_CAPS_NONE;
	backend->start = start;
	backend->get_texture = get_texture;
	backend->init = init;
	backend->shutdown = shutdown;
	backend->load_rom = load_rom;
	backend->update_inputs = update_inputs;

	/* Register any exported file handlers provided by the backend plugin */
	const mse_file_handler_t *handlers = (const mse_file_handler_t *)mse_library_symbol(library_handle, "mse_file_handlers");
	if (handlers != NULL) {
		for (size_t i = 0; handlers[i].extension != NULL; ++i) {
			mse_filetype_register(handlers[i].extension, &handlers[i]);
		}
	}

	/* Wire up the input control scheme */
	if (inputs != NULL && input_count != NULL && *input_count > 0U) {
		backend->input_descs = inputs;
		backend->input_count = *input_count;
		backend->input_states = (float *)calloc(backend->input_count, sizeof(float));
		/* Non-fatal: if allocation fails inputs just won't work */
	} else {
		backend->input_descs = NULL;
		backend->input_count = 0U;
		backend->input_states = NULL;
	}

	return true;
}

static mse_backend_t *mse_backend_register_loaded(void *library_handle) {
	mse_backend_record_t *record;

	record = (mse_backend_record_t *)calloc(1, sizeof(mse_backend_record_t));
	if (record == NULL) {
		mse_library_close(library_handle);
		return NULL;
	}

	record->library_handle = library_handle;
	if (!mse_backend_load_info(library_handle, &record->backend)) {
		mse_library_close(library_handle);
		free(record->backend.input_states);
		free(record);
		return NULL;
	}

	record->next = g_backends;
	g_backends = record;

	return &record->backend;
}

static bool mse_backend_resolve_library_path(char *out, size_t out_size, const char *root) {
	char lib_dir[MSE_PATH_BUFFER];
	char platform_dir[MSE_PATH_BUFFER];
	char library_name[MSE_PATH_BUFFER];

	if (!mse_path_join(lib_dir, sizeof(lib_dir), root, "lib")) {
		return false;
	}

	if (!mse_path_join(platform_dir, sizeof(platform_dir), lib_dir, MSE_PLATFORM_FOLDER)) {
		return false;
	}

	if (snprintf(library_name, sizeof(library_name), "%s%s", MSE_BACKEND_LIBRARY_NAME, MSE_LIBRARY_EXTENSION) < 0) {
		return false;
	}

	return mse_path_join(out, out_size, platform_dir, library_name);
}

static mse_backend_t *mse_backend_register_from_root(const char *root) {
	char library_path[MSE_PATH_BUFFER];
	void *library_handle;

	if (root == NULL) {
		return NULL;
	}

	if (!mse_backend_resolve_library_path(library_path, sizeof(library_path), root)) {
		return NULL;
	}

	if (!mse_path_exists(library_path)) {
		return NULL;
	}

	library_handle = mse_library_open(library_path);
	if (library_handle == NULL) {
		return NULL;
	}

	return mse_backend_register_loaded(library_handle);
}

static mse_backend_t *mse_backend_register_from_zip(const char *zip_path) {
	char extracted_root[MSE_PATH_BUFFER];

	if (!mse_extract_zip_archive(zip_path, extracted_root, sizeof(extracted_root))) {
		return NULL;
	}

	return mse_backend_register_from_root(extracted_root);
}

static mse_backend_t *mse_backend_register_path(const char *path) {
	if (path == NULL) {
		return NULL;
	}

	if (!mse_path_exists(path)) {
		return NULL;
	}

	if (mse_path_is_directory(path)) {
		return mse_backend_register_from_root(path);
	}

	return mse_backend_register_from_zip(path);
}

LIBMSE_API mse_backend_t *mse_backend_register(const char *filename) {
	return mse_backend_register_path(filename);
}

LIBMSE_API mse_backend_t *mse_backend_register_folder(const char *foldername) {
	return mse_backend_register_path(foldername);
}

LIBMSE_API bool mse_backend_init(mse_backend_t *backend) {
	if (backend == NULL || backend->init == NULL)
		return true;

	libmse_cmd_register_default();
	//libmse_cvar_register_default();

	return backend->init();
}

LIBMSE_API void mse_backend_shutdown(mse_backend_t *backend) {
	if (backend == NULL || backend->shutdown == NULL) {
		return; // Optional callback
	}
	backend->shutdown();
}

LIBMSE_API bool mse_backend_load_rom(mse_backend_t *backend, const uint8_t *data, size_t size) {
	if (backend == NULL || backend->load_rom == NULL) {
		return false; // Cannot load ROM without callback
	}
	return backend->load_rom(data, size);
}

LIBMSE_API void mse_backend_update_inputs(mse_backend_t *backend, const float *inputs) {
	if (backend == NULL || backend->update_inputs == NULL) {
		return;
	}
	backend->update_inputs(inputs);
}