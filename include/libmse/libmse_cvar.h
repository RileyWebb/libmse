// libmse_cvar.h - CVar (Console Variable) API for libmse

#ifndef LIBMSE_CVAR_H
#define LIBMSE_CVAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libmse_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct libmse_cvar_s libmse_cvar_t;

typedef void (*libmse_cvar_iterate_cb)(libmse_cvar_t* cvar, void* user_data);
typedef void (*libmse_cvar_change_cb)(libmse_cvar_t* cvar, void* user_data);

typedef enum libmse_cvar_type_e {
    LIBMSE_CVAR_INT,
    LIBMSE_CVAR_FLOAT,
    LIBMSE_CVAR_DOUBLE,
    LIBMSE_CVAR_STRING,

    // Fixed-length runs of floats, written and read as one value: "set
    // mse_theme_accent 0.35 0.62 1.0 1.0". A colour or a padding pair is one
    // thing, and setting it a component at a time means three frames drawn
    // with a value nobody asked for and three change callbacks to match.
    LIBMSE_CVAR_VEC2,
    LIBMSE_CVAR_VEC3,
    LIBMSE_CVAR_VEC4,
} libmse_cvar_type_t;

// How many floats a vector type carries; 0 for everything else.
LIBMSE_API size_t libmse_cvar_type_components(libmse_cvar_type_t type);

// Four floats, laid out so a pointer to one can be handed to the cvar system
// as a vec2, vec3 or vec4 by naming the type. Layout-compatible with ImVec2
// and ImVec4, which is why the frontend can bind its theme tokens straight to
// these -- with a _Static_assert, because the cast puts the check beyond what
// LIBMSE_CVAR_BIND_* can see.
typedef struct libmse_vec_s {
    float v[4];
} libmse_vec_t;

typedef struct libmse_cvar_s {
    const char* name;
    const char* description;
    libmse_cvar_type_t type;

    union {
        int *i;
        float *f;
        double *d;
        const char** s;
        float *v;      // vec2/3/4: components laid out consecutively
    } data;

    char* alloc_s;

    libmse_cvar_change_cb cb;
    void* user_data;

    union {
        int i;
        float f;
        double d;
        const char* s;
        float v[4];
    } storage;

    bool owned;
} libmse_cvar_t;

LIBMSE_API bool libmse_cvar_register(const char* name, libmse_cvar_type_t type, void* ptr, const char* description);

LIBMSE_API libmse_cvar_t* libmse_cvar_define_int(const char* name, int value, const char* description);
LIBMSE_API libmse_cvar_t* libmse_cvar_define_float(const char* name, float value, const char* description);
LIBMSE_API libmse_cvar_t* libmse_cvar_define_double(const char* name, double value, const char* description);
LIBMSE_API libmse_cvar_t* libmse_cvar_define_string(const char* name, const char* value, const char* description);

// `components` is 2, 3 or 4, and `value` points at that many floats.
LIBMSE_API libmse_cvar_t* libmse_cvar_define_vec(const char* name, size_t components, const float* value,
                                                 const char* description);
LIBMSE_API bool libmse_cvar_register_change_cb(const char* name, libmse_cvar_change_cb callback, void* user_data);
LIBMSE_API bool libmse_cvar_destroy(const char* name);
LIBMSE_API void libmse_cvar_iterate(libmse_cvar_iterate_cb callback, void* user_data);

LIBMSE_API libmse_cvar_t* libmse_cvar_get(const char* name);
LIBMSE_API libmse_cvar_t* libmse_cvar_get_reversed(const char* name);
LIBMSE_API void *libmse_cvar_get_ptr(const char* name);
LIBMSE_API int *libmse_cvar_get_i(const char* name);
LIBMSE_API float *libmse_cvar_get_f(const char* name);
LIBMSE_API double *libmse_cvar_get_d(const char* name);
LIBMSE_API const char **libmse_cvar_get_s(const char* name);

LIBMSE_API bool libmse_cvar_set_i(const char* name, int value);
LIBMSE_API bool libmse_cvar_set_f(const char* name, float value);
LIBMSE_API bool libmse_cvar_set_d(const char* name, double value);
LIBMSE_API bool libmse_cvar_set_s(const char* name, const char *value);

// Writes as many components as the cvar has, from `value`. Fails on a cvar
// that is not a vector.
LIBMSE_API bool libmse_cvar_set_v(const char* name, const float* value);

// The components, or NULL if the cvar is missing or not a vector.
LIBMSE_API float* libmse_cvar_get_v(const char* name);

// Parses "0.35 0.62 1.0 1.0" into `out`, which must hold `components` floats.
// Separators are spaces or commas, so a value pasted out of source compiles
// back in. Returns false unless every component was present.
LIBMSE_API bool libmse_cvar_parse_vec(const char* text, size_t components, float* out);

// Prints `components` floats into `buf` in the form parse_vec reads back.
LIBMSE_API bool libmse_cvar_format_vec(const float* value, size_t components, char* buf, size_t buf_size);

LIBMSE_API bool libmse_cvar_export(const char* filename);

LIBMSE_API void* libmse_get_cvar_registry();
LIBMSE_API size_t libmse_get_cvar_count();

typedef struct libmse_cvar_def_s {
    const char* name;
    libmse_cvar_type_t type;
    const char* description;

    union {
        int i;
        float f;
        double d;
        const char* s;
        float v[4];
    } value;

    // Receives the address of the cvar's value once it is defined: an int*,
    // float*, double* or const char** to match `type`.
    void** out;

    struct libmse_cvar_def_s* next;
    bool queued;
} libmse_cvar_def_t;

// Adds a descriptor to the pending list. Safe to call from a static
// initialiser: it allocates nothing, logs nothing and cannot fail. Queueing
// the same descriptor twice is a no-op.
LIBMSE_API void libmse_cvar_queue(libmse_cvar_def_t* def);

// Defines everything queued since the last flush and returns how many. Call it
// once libmse is initialised, and again after loading a module that brings its
// own cvars with it -- a backend's static initialisers run when the library is
// loaded, which is long after libmse started.
LIBMSE_API size_t libmse_cvar_flush(void);

// Runs a function before main (or, in a shared library, when it is loaded).
#if defined(_MSC_VER)
    // Untested here: this project builds with GCC. Kept so the macros below do
    // not silently compile to nothing under MSVC -- a cvar that quietly fails
    // to register is worse than one that fails to build. The linker directive
    // is what stops /OPT:REF discarding a pointer nothing references; on x86
    // it would need a leading underscore on the symbol name.
    #pragma section(".CRT$XCU", read)
    #define LIBMSE_AUTO_INIT(fn)                                                   \
        static void fn(void);                                                      \
        __declspec(allocate(".CRT$XCU")) void (*fn##_ptr)(void) = fn;               \
        __pragma(comment(linker, "/include:" #fn "_ptr"))                           \
        static void fn(void)
#else
    #define LIBMSE_AUTO_INIT(fn)                                                   \
        __attribute__((constructor)) static void fn(void);                          \
        static void fn(void)
#endif

#define LIBMSE_CVAR_CAT_(a, b) a##b
#define LIBMSE_CVAR_CAT(a, b) LIBMSE_CVAR_CAT_(a, b)

#define LIBMSE_CVAR_DEFINE_(sym, ctype, field, cvtype, name, dflt, desc)           \
    static ctype LIBMSE_CVAR_CAT(sym, _fallback) = (dflt);                         \
    static ctype* sym = &LIBMSE_CVAR_CAT(sym, _fallback);                          \
    static libmse_cvar_def_t LIBMSE_CVAR_CAT(sym, _def) = {                        \
        (name), (cvtype), (desc), { .field = (dflt) }, (void**)&sym, NULL, false    \
    };                                                                             \
    LIBMSE_AUTO_INIT(LIBMSE_CVAR_CAT(sym, LIBMSE_CVAR_CAT(_queue_, __COUNTER__)))  \
    {                                                                              \
        libmse_cvar_queue(&LIBMSE_CVAR_CAT(sym, _def));                            \
    }

// Creates a cvar that lives in the cvar system
#define LIBMSE_CVAR_DEFINE_INT(sym, name, dflt, desc)                              \
    LIBMSE_CVAR_DEFINE_(sym, int, i, LIBMSE_CVAR_INT, name, dflt, desc)
#define LIBMSE_CVAR_DEFINE_FLOAT(sym, name, dflt, desc)                            \
    LIBMSE_CVAR_DEFINE_(sym, float, f, LIBMSE_CVAR_FLOAT, name, dflt, desc)
#define LIBMSE_CVAR_DEFINE_DOUBLE(sym, name, dflt, desc)                           \
    LIBMSE_CVAR_DEFINE_(sym, double, d, LIBMSE_CVAR_DOUBLE, name, dflt, desc)
#define LIBMSE_CVAR_DEFINE_STRING(sym, name, dflt, desc)                           \
    LIBMSE_CVAR_DEFINE_(sym, const char*, s, LIBMSE_CVAR_STRING, name, dflt, desc)

// Vectors declare `sym` as a float* onto the components. The default is given
// component by component rather than as an array so it reads like the value it
// is: LIBMSE_CVAR_DEFINE_VEC2(g_pad, "mse_style_pad", 18.0f, 16.0f, "...").
#define LIBMSE_CVAR_DEFINE_VEC_(sym, cvtype, name, desc, ...)                      \
    static float LIBMSE_CVAR_CAT(sym, _fallback)[4] = {__VA_ARGS__};               \
    static float* sym = LIBMSE_CVAR_CAT(sym, _fallback);                           \
    static libmse_cvar_def_t LIBMSE_CVAR_CAT(sym, _def) = {                        \
        (name), (cvtype), (desc), { .v = {__VA_ARGS__} }, (void**)&sym, NULL, false \
    };                                                                             \
    LIBMSE_AUTO_INIT(LIBMSE_CVAR_CAT(sym, LIBMSE_CVAR_CAT(_queue_, __COUNTER__)))  \
    {                                                                              \
        libmse_cvar_queue(&LIBMSE_CVAR_CAT(sym, _def));                            \
    }

#define LIBMSE_CVAR_DEFINE_VEC2(sym, name, x, y, desc)                             \
    LIBMSE_CVAR_DEFINE_VEC_(sym, LIBMSE_CVAR_VEC2, name, desc, x, y)
#define LIBMSE_CVAR_DEFINE_VEC3(sym, name, x, y, z, desc)                          \
    LIBMSE_CVAR_DEFINE_VEC_(sym, LIBMSE_CVAR_VEC3, name, desc, x, y, z)
#define LIBMSE_CVAR_DEFINE_VEC4(sym, name, x, y, z, w, desc)                       \
    LIBMSE_CVAR_DEFINE_VEC_(sym, LIBMSE_CVAR_VEC4, name, desc, x, y, z, w)


// Binds a cvar to memory the caller owns
#define LIBMSE_CVAR_BIND_INT(name, ptr, desc)                                      \
    libmse_cvar_register((name), LIBMSE_CVAR_INT, _Generic((ptr), int*: (ptr)), (desc))
#define LIBMSE_CVAR_BIND_FLOAT(name, ptr, desc)                                    \
    libmse_cvar_register((name), LIBMSE_CVAR_FLOAT, _Generic((ptr), float*: (ptr)), (desc))
#define LIBMSE_CVAR_BIND_DOUBLE(name, ptr, desc)                                   \
    libmse_cvar_register((name), LIBMSE_CVAR_DOUBLE, _Generic((ptr), double*: (ptr)), (desc))
#define LIBMSE_CVAR_BIND_STRING(name, ptr, desc)                                   \
    libmse_cvar_register((name), LIBMSE_CVAR_STRING,                               \
                         _Generic((ptr), const char**: (ptr), char**: (ptr)), (desc))

// Vectors bind to the floats themselves. The _Generic can only confirm it was
// handed floats and not how many there are, so a caller binding a struct -- an
// ImVec4, say -- should assert its size next to the cast.
#define LIBMSE_CVAR_BIND_VEC2(name, ptr, desc)                                     \
    libmse_cvar_register((name), LIBMSE_CVAR_VEC2, _Generic((ptr), float*: (ptr)), (desc))
#define LIBMSE_CVAR_BIND_VEC3(name, ptr, desc)                                     \
    libmse_cvar_register((name), LIBMSE_CVAR_VEC3, _Generic((ptr), float*: (ptr)), (desc))
#define LIBMSE_CVAR_BIND_VEC4(name, ptr, desc)                                     \
    libmse_cvar_register((name), LIBMSE_CVAR_VEC4, _Generic((ptr), float*: (ptr)), (desc))


#ifdef __cplusplus
}
#endif

#endif // LIBMSE_CVAR_H