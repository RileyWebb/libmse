// Tests for the cvar system's two ways of holding a value: bound to memory the
// caller owns, and owned by the cvar system after being declared next to the
// code that reads it.
//
// The declared half is the part worth testing hardest, because its moving
// pieces are invisible. A declaration queues itself from a static initialiser
// before main; nothing observes that it happened until a flush; and the
// pointer it hands back is repointed underneath the reader. Each of those can
// fail by quietly doing nothing, which is indistinguishable from a cvar that
// was never declared.

#include <stdio.h>
#include <string.h>

#include "libmse/libmse_cmd.h"
#include "libmse/libmse_cvar.h"

static int g_failures = 0;

#define CHECK(condition, ...)                                                                                          \
	do {                                                                                                               \
		if (!(condition)) {                                                                                            \
			printf("[FAIL] " __VA_ARGS__);                                                                             \
			printf("\n       %s:%d: %s\n", __FILE__, __LINE__, #condition);                                            \
			g_failures++;                                                                                              \
		} else {                                                                                                       \
			printf("[PASS] " __VA_ARGS__);                                                                             \
			printf("\n");                                                                                              \
		}                                                                                                              \
	} while (0)

// Declared at file scope, exactly as a module would. These queue themselves
// before main runs.
LIBMSE_CVAR_DEFINE_INT(g_cv_int, "test_int", 7, "an int");
LIBMSE_CVAR_DEFINE_FLOAT(g_cv_float, "test_float", 1.5f, "a float");
LIBMSE_CVAR_DEFINE_STRING(g_cv_string, "test_string", "hello", "a string");

// The one a config file gets to first. Declared here, but the test creates a
// placeholder under the same name before flushing.
LIBMSE_CVAR_DEFINE_INT(g_cv_adopted, "test_adopted", 1, "adopted from a config");

LIBMSE_CVAR_DEFINE_VEC2(g_cv_vec2, "test_vec2", 18.0f, 16.0f, "a pair");
LIBMSE_CVAR_DEFINE_VEC4(g_cv_vec4, "test_vec4", 0.35f, 0.62f, 1.0f, 1.0f, "a colour");
LIBMSE_CVAR_DEFINE_VEC4(g_cv_vec_adopted, "test_vec_adopted", 0.0f, 0.0f, 0.0f, 0.0f, "adopted vector");

static int g_bound = 3;

static void test_before_flush(void)
{
	// The declaration has not been defined yet, so these still point at the
	// module's own copy of the default. Reading one must not be a null
	// dereference -- a module can run code before libmse is up.
	CHECK(g_cv_int != NULL && *g_cv_int == 7, "a declared int reads its default before the flush");
	CHECK(g_cv_string != NULL && strcmp(*g_cv_string, "hello") == 0,
	      "a declared string reads its default before the flush");
	CHECK(libmse_cvar_get("test_int") == NULL, "nothing is in the registry before the flush");
}

static void test_flush_defines(void)
{
	// The placeholder a config file leaves behind when it sets a name nothing
	// has registered: a string, because the console has no way to know better.
	// This is exactly what "set test_adopted 42" does before anything owns it.
	libmse_cvar_define_string("test_adopted", "42", "set by a config file");
	libmse_cvar_define_string("test_vec_adopted", "0.1 0.2 0.3 0.4", "set by a config file");

	// Not an exact count: libmse declares cvars of its own, and they are on the
	// same pending list. What matters is that these four arrived.
	const size_t defined = libmse_cvar_flush();
	CHECK(defined >= 4, "flush defined at least the declared cvars (got %zu)", defined);

	CHECK(libmse_cvar_get("test_int") != NULL, "a declared cvar reaches the registry");
	CHECK(*g_cv_int == 7, "a declared int keeps its default through the flush");
	CHECK(*g_cv_float == 1.5f, "a declared float keeps its default through the flush");
	CHECK(strcmp(*g_cv_string, "hello") == 0, "a declared string keeps its default through the flush");

	// The pointer has to have moved off the module's copy and onto the cvar's
	// own storage, or writes through the console would never be seen here.
	CHECK(g_cv_int == libmse_cvar_get_i("test_int"), "the declared pointer is the cvar's own storage");

	// The value a config file asked for survives being read before the module
	// that owns the name existed. This is the whole reason the placeholder
	// path is there.
	CHECK(*g_cv_adopted == 42, "a config value set before the declaration is adopted (got %d)", *g_cv_adopted);
	CHECK(libmse_cvar_get("test_adopted")->type == LIBMSE_CVAR_INT, "the adopted cvar took the declared type");

	// The description belongs to whoever declared the cvar, not to the config
	// that happened to mention the name first.
	CHECK(strcmp(libmse_cvar_get("test_adopted")->description, "adopted from a config") == 0,
	      "the declaration's description replaces the placeholder's (got \"%s\")",
	      libmse_cvar_get("test_adopted")->description);
}

static void test_set_is_visible(void)
{
	CHECK(libmse_cvar_set_i("test_int", 99), "setting a declared int succeeds");
	CHECK(*g_cv_int == 99, "a set through the registry is visible through the declared pointer");

	CHECK(libmse_cvar_set_s("test_string", "world"), "setting a declared string succeeds");
	CHECK(strcmp(*g_cv_string, "world") == 0, "a set string is visible through the declared pointer");
}

static void test_double_define_keeps_the_first(void)
{
	libmse_cvar_t *again = libmse_cvar_define_int("test_int", 1234, "defined twice");
	CHECK(again == libmse_cvar_get("test_int"), "defining a name twice returns the existing cvar");
	CHECK(*g_cv_int == 99, "defining a name twice does not reset its value");
}

static void test_vectors(void)
{
	CHECK(*g_cv_vec2 == 18.0f && g_cv_vec2[1] == 16.0f, "a declared vec2 keeps its default");
	CHECK(g_cv_vec4[0] == 0.35f && g_cv_vec4[3] == 1.0f, "a declared vec4 keeps its default");
	CHECK(g_cv_vec4 == libmse_cvar_get_v("test_vec4"), "a declared vector points at the cvar's storage");

	// The console splits "set name 1 2 3 4" into separate arguments, so the
	// handler has to put them back together before anything can parse them.
	const char *args[] = {"test_vec4", "0.1", "0.2", "0.3", "0.4"};
	CHECK(libmse_cmd_set_handler(5, args), "setting a vec4 from split arguments succeeds");
	CHECK(g_cv_vec4[0] == 0.1f && g_cv_vec4[1] == 0.2f && g_cv_vec4[2] == 0.3f && g_cv_vec4[3] == 0.4f,
	      "every component of a split set lands");

	// And as one quoted argument, which is the form export writes.
	const char *one[] = {"test_vec2", "4 5"};
	CHECK(libmse_cmd_set_handler(2, one), "setting a vec2 from one argument succeeds");
	CHECK(g_cv_vec2[0] == 4.0f && g_cv_vec2[1] == 5.0f, "a quoted vector parses");

	// Commas too, so a value pasted out of a struct initialiser works.
	float parsed[3] = {0};
	CHECK(libmse_cvar_parse_vec("1.0, 2.0, 3.0", 3, parsed) && parsed[2] == 3.0f, "commas separate components");
	CHECK(!libmse_cvar_parse_vec("1.0 2.0", 3, parsed), "a short vector is rejected");

	char text[64];
	CHECK(libmse_cvar_format_vec(g_cv_vec2, 2, text, sizeof(text)) && strcmp(text, "4 5") == 0,
	      "formatting round-trips what parse reads (got \"%s\")", text);

	CHECK(g_cv_vec_adopted[1] == 0.2f && g_cv_vec_adopted[3] == 0.4f,
	      "a config vector set before the declaration is adopted");
}

static void test_bound_still_works(void)
{
	CHECK(LIBMSE_CVAR_BIND_INT("test_bound", &g_bound, "bound to the caller"), "a bound cvar registers");
	CHECK(libmse_cvar_get_i("test_bound") == &g_bound, "a bound cvar points at the caller's memory");

	CHECK(libmse_cvar_set_i("test_bound", 11), "setting a bound cvar succeeds");
	CHECK(g_bound == 11, "a set writes through to the caller's variable");
}

static void test_flush_is_drained(void)
{
	CHECK(libmse_cvar_flush() == 0, "a second flush has nothing left to do");
}

int main(void)
{
	test_before_flush();
	test_flush_defines();
	test_set_is_visible();
	test_double_define_keeps_the_first();
	test_vectors();
	test_bound_still_works();
	test_flush_is_drained();

	if (g_failures > 0) {
		printf("RESULT: FAIL - %d check(s) failed\n", g_failures);
		return 1;
	}

	printf("RESULT: PASS\n");
	return 0;
}
