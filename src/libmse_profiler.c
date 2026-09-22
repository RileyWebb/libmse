#define DEBUG_LOG_SOURCE "profiler"

// See libmse/include/libmse/libmse_profiler.h for what this is and why it is
// shaped the way it is. The short version: a worker only ever writes its own
// thread slot, so the hot path takes no lock, and readers copy a finished
// frame out from under a seqlock.

#include <stdatomic.h>
#include <string.h>
#include <stdio.h>

#if defined(_WIN32)
	#include <windows.h>
#else
	#include <time.h>
#endif

#include "libmse/libmse_profiler.h"

// ---------------------------------------------------------------------------
// Clock
// ---------------------------------------------------------------------------

#if defined(_WIN32)
static uint64_t g_qpc_frequency = 0;
#endif

static uint64_t profiler_now_ns(void)
{
#if defined(_WIN32)
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);

	uint64_t ticks = (uint64_t)counter.QuadPart;
	uint64_t freq  = g_qpc_frequency;
	if (freq == 0) {
		return 0;
	}

	// Split rather than multiplying first: ticks * 1e9 overflows 64 bits after
	// a few weeks of uptime on a 10MHz counter, and the wrap would show up as
	// a single absurd frame rather than anything obviously wrong.
	return (ticks / freq) * 1000000000ull + ((ticks % freq) * 1000000000ull) / freq;
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

// ---------------------------------------------------------------------------
// Per-thread state
// ---------------------------------------------------------------------------

typedef struct profiler_open_zone_s {
	int16_t  zone;
	uint64_t start_ns;
} profiler_open_zone_t;

typedef struct profiler_thread_s {
	char name[LIBMSE_PROFILER_NAME_MAX];

	// The call tree persists across frames: the per-frame counters are cleared
	// at each publish while the rolling statistics stay, which is what lets a
	// row keep its history instead of restarting every 16ms.
	libmse_profiler_zone_t zones[LIBMSE_PROFILER_MAX_ZONES];
	uint32_t               zone_count;

	libmse_profiler_counter_t counters[LIBMSE_PROFILER_MAX_COUNTERS];
	uint32_t                  counter_count;

	profiler_open_zone_t stack[LIBMSE_PROFILER_MAX_DEPTH];
	int                  depth;

	uint64_t frame_start_ns;
	uint64_t frame_count;
	uint32_t dropped;
	uint32_t unbalanced;
	uint32_t reset_generation;

	// Frame times in ms as a ring; linearised into the published view so the
	// reader does not have to know where the head is.
	float    ring[LIBMSE_PROFILER_HISTORY];
	uint32_t ring_head;
	uint32_t ring_count;

	// Even when the published view is stable, odd while it is being written.
	_Atomic uint32_t              seq;
	libmse_profiler_thread_view_t published;
} profiler_thread_t;

static profiler_thread_t g_threads[LIBMSE_PROFILER_MAX_THREADS];
static _Atomic uint32_t  g_thread_count = 0;
static _Atomic bool      g_enabled      = false;

// Bumped by libmse_profiler_reset. Each thread remembers the value it last acted
// on, so one reset reaches every thread exactly once -- a flag would be
// consumed by whichever thread hit its frame boundary first.
static _Atomic uint32_t g_reset_generation = 0;

// Which slot this thread owns. The whole no-locking story rests on this: two
// threads can never be looking at the same profiler_thread_t.
static _Thread_local profiler_thread_t *g_tls_thread = NULL;

void libmse_profiler_init(void)
{
#if defined(_WIN32)
	if (g_qpc_frequency == 0) {
		LARGE_INTEGER frequency;
		if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0) {
			g_qpc_frequency = (uint64_t)frequency.QuadPart;
		}
	}
#endif
}

void libmse_profiler_set_enabled(bool enabled)
{
	atomic_store_explicit(&g_enabled, enabled, memory_order_relaxed);
}

bool libmse_profiler_enabled(void)
{
	return atomic_load_explicit(&g_enabled, memory_order_relaxed);
}

void libmse_profiler_reset(void)
{
	// Signalled rather than done here: the rolling statistics belong to the
	// worker threads, and clearing them from the UI thread would be the one
	// place in this file that writes another thread's slot. Each thread picks
	// this up at its next frame boundary.
	atomic_fetch_add_explicit(&g_reset_generation, 1, memory_order_relaxed);
}

// The name goes out as soon as it is known rather than waiting for the first
// publish, so a thread that has registered but not yet finished a frame is
// still identifiable instead of reading as a nameless empty slot.
static void profiler_publish_name(profiler_thread_t *thread)
{
	atomic_fetch_add_explicit(&thread->seq, 1, memory_order_relaxed);
	atomic_thread_fence(memory_order_release);

	memcpy(thread->published.name, thread->name, sizeof(thread->published.name));

	atomic_thread_fence(memory_order_release);
	atomic_fetch_add_explicit(&thread->seq, 1, memory_order_relaxed);
}

// Claims this thread's slot, on its first zone. Returns NULL once every slot
// is taken, which switches profiling off for that thread rather than
// corrupting somebody else's.
static profiler_thread_t *profiler_thread(void)
{
	profiler_thread_t *thread = g_tls_thread;
	if (thread != NULL) {
		return thread;
	}

	uint32_t index = atomic_fetch_add_explicit(&g_thread_count, 1, memory_order_acq_rel);
	if (index >= LIBMSE_PROFILER_MAX_THREADS) {
		atomic_fetch_sub_explicit(&g_thread_count, 1, memory_order_acq_rel);
		return NULL;
	}

	thread = &g_threads[index];
	if (thread->name[0] == '\0') {
		snprintf(thread->name, sizeof(thread->name), "thread %u", index);
	}
	thread->frame_start_ns = profiler_now_ns();

	g_tls_thread = thread;
	profiler_publish_name(thread);
	return thread;
}

void libmse_profiler_thread_name(const char *name)
{
	profiler_thread_t *thread = profiler_thread();
	if (thread == NULL || name == NULL) {
		return;
	}

	snprintf(thread->name, sizeof(thread->name), "%s", name);
	profiler_publish_name(thread);
}

// ---------------------------------------------------------------------------
// The hot path
// ---------------------------------------------------------------------------

// Zones are keyed by name and parent, so the same span entered repeatedly from
// one place folds into a single row. The pointer compare hits almost always --
// callers pass string literals -- and strcmp is only reached when two
// translation units spell the same name separately.
static int16_t profiler_find_zone(profiler_thread_t *thread, const char *name, int16_t parent)
{
	for (uint32_t i = 0; i < thread->zone_count; ++i) {
		libmse_profiler_zone_t *zone = &thread->zones[i];
		if (zone->parent != parent) {
			continue;
		}
		if (zone->name == name || strcmp(zone->name, name) == 0) {
			return (int16_t)i;
		}
	}

	if (thread->zone_count >= LIBMSE_PROFILER_MAX_ZONES) {
		thread->dropped++;
		return -1;
	}

	int16_t index = (int16_t)thread->zone_count++;
	libmse_profiler_zone_t *zone = &thread->zones[index];

	memset(zone, 0, sizeof(*zone));
	zone->name   = name;
	zone->parent = parent;
	zone->depth  = (uint8_t)(parent >= 0 ? thread->zones[parent].depth + 1 : 0);
	zone->min_ns = UINT64_MAX;

	return index;
}

void libmse_profiler_begin(const char *name)
{
	if (!atomic_load_explicit(&g_enabled, memory_order_relaxed) || name == NULL) {
		return;
	}

	profiler_thread_t *thread = profiler_thread();
	if (thread == NULL) {
		return;
	}

	if (thread->depth >= LIBMSE_PROFILER_MAX_DEPTH) {
		// Still pushed, as an invalid entry, so the matching end() pops the
		// right thing and everything below stays attributed correctly.
		thread->depth++;
		thread->dropped++;
		return;
	}

	int16_t parent = (thread->depth > 0) ? thread->stack[thread->depth - 1].zone : -1;
	int16_t index  = profiler_find_zone(thread, name, parent);

	thread->stack[thread->depth].zone     = index;
	thread->stack[thread->depth].start_ns = profiler_now_ns();
	thread->depth++;

	if (index >= 0) {
		thread->zones[index].calls++;
	}
}

void libmse_profiler_end(void)
{
	if (!atomic_load_explicit(&g_enabled, memory_order_relaxed)) {
		return;
	}

	profiler_thread_t *thread = g_tls_thread;
	if (thread == NULL || thread->depth <= 0) {
		return;
	}

	thread->depth--;
	if (thread->depth >= LIBMSE_PROFILER_MAX_DEPTH) {
		return; // One of the over-deep pushes above.
	}

	int16_t index = thread->stack[thread->depth].zone;
	if (index < 0) {
		return;
	}

	uint64_t elapsed = profiler_now_ns() - thread->stack[thread->depth].start_ns;

	libmse_profiler_zone_t *zone = &thread->zones[index];
	zone->total_ns += elapsed;

	// Charged to the parent separately so the UI can show self time without
	// having to walk the tree.
	if (zone->parent >= 0) {
		thread->zones[zone->parent].child_ns += elapsed;
	}
}

void libmse_profiler_counter(const char *name, double value)
{
	if (!atomic_load_explicit(&g_enabled, memory_order_relaxed) || name == NULL) {
		return;
	}

	profiler_thread_t *thread = profiler_thread();
	if (thread == NULL) {
		return;
	}

	for (uint32_t i = 0; i < thread->counter_count; ++i) {
		libmse_profiler_counter_t *counter = &thread->counters[i];
		if (counter->name == name || strcmp(counter->name, name) == 0) {
			counter->value = value;
			return;
		}
	}

	if (thread->counter_count >= LIBMSE_PROFILER_MAX_COUNTERS) {
		thread->dropped++;
		return;
	}

	libmse_profiler_counter_t *counter = &thread->counters[thread->counter_count++];
	counter->name    = name;
	counter->value   = value;
	counter->min     = value;
	counter->max     = value;
	counter->sum     = 0.0;
	counter->samples = 0;
}

// ---------------------------------------------------------------------------
// Frame boundary
// ---------------------------------------------------------------------------

static void profiler_clear_stats(profiler_thread_t *thread)
{
	for (uint32_t i = 0; i < thread->zone_count; ++i) {
		libmse_profiler_zone_t *zone = &thread->zones[i];
		zone->samples = 0;
		zone->sum_ns  = 0;
		zone->min_ns  = UINT64_MAX;
		zone->max_ns  = 0;
	}

	for (uint32_t i = 0; i < thread->counter_count; ++i) {
		libmse_profiler_counter_t *counter = &thread->counters[i];
		counter->samples = 0;
		counter->sum     = 0.0;
		counter->min     = counter->value;
		counter->max     = counter->value;
	}

	thread->ring_head  = 0;
	thread->ring_count = 0;
	thread->dropped    = 0;
	thread->unbalanced = 0;
}

static void profiler_publish(profiler_thread_t *thread, uint64_t frame_ns, bool with_zones)
{
	libmse_profiler_thread_view_t *view = &thread->published;

	// Odd sequence: a reader that sees this discards what it read and retries.
	atomic_fetch_add_explicit(&thread->seq, 1, memory_order_relaxed);
	atomic_thread_fence(memory_order_release);

	memcpy(view->name, thread->name, sizeof(view->name));
	view->frame_count   = thread->frame_count;
	view->frame_ns      = frame_ns;
	view->zone_count    = with_zones ? thread->zone_count : 0;
	view->counter_count = with_zones ? thread->counter_count : 0;
	view->dropped       = thread->dropped;
	view->unbalanced    = thread->unbalanced;

	memcpy(view->zones, thread->zones, sizeof(libmse_profiler_zone_t) * view->zone_count);
	memcpy(view->counters, thread->counters,
	       sizeof(libmse_profiler_counter_t) * view->counter_count);

	// Oldest first, so the reader can plot it straight through.
	uint32_t count = thread->ring_count;
	uint32_t start = (thread->ring_head + LIBMSE_PROFILER_HISTORY - count) % LIBMSE_PROFILER_HISTORY;
	for (uint32_t i = 0; i < count; ++i) {
		view->history[i] = thread->ring[(start + i) % LIBMSE_PROFILER_HISTORY];
	}
	view->history_count = count;

	atomic_thread_fence(memory_order_release);
	atomic_fetch_add_explicit(&thread->seq, 1, memory_order_relaxed);
}

void libmse_profiler_frame(void)
{
	// Deliberately not gated on g_enabled. Frame pacing is one clock read and
	// a float, and it is what a frame counter needs; only zone collection is
	// worth switching off. Gating this too would mean a frame-rate display
	// stops working when somebody unticks a box in the profiler window.
	const bool zones_enabled = atomic_load_explicit(&g_enabled, memory_order_relaxed);

	profiler_thread_t *thread = profiler_thread();
	if (thread == NULL) {
		return;
	}

	// A zone left open means somebody returned out of the middle of one. Close
	// it here rather than letting the stack grow until the depth limit turns
	// the thread's profile into nonsense.
	while (thread->depth > 0) {
		thread->unbalanced++;
		libmse_profiler_end();
	}

	uint64_t now = profiler_now_ns();
	uint64_t frame_ns = (now > thread->frame_start_ns) ? now - thread->frame_start_ns : 0;
	thread->frame_start_ns = now;
	thread->frame_count++;

	uint32_t generation = atomic_load_explicit(&g_reset_generation, memory_order_relaxed);
	if (generation != thread->reset_generation) {
		thread->reset_generation = generation;
		profiler_clear_stats(thread);
	}

	thread->ring[thread->ring_head] = (float)((double)frame_ns / 1000000.0);
	thread->ring_head = (thread->ring_head + 1) % LIBMSE_PROFILER_HISTORY;
	if (thread->ring_count < LIBMSE_PROFILER_HISTORY) {
		thread->ring_count++;
	}

	if (zones_enabled) {
		for (uint32_t i = 0; i < thread->zone_count; ++i) {
			libmse_profiler_zone_t *zone = &thread->zones[i];
			// Frames in which a zone did not run are not samples of it.
			// Counting them would make a once-a-second job look fast rather
			// than rare.
			if (zone->calls == 0) {
				continue;
			}

			zone->samples++;
			zone->sum_ns += zone->total_ns;
			if (zone->total_ns < zone->min_ns) zone->min_ns = zone->total_ns;
			if (zone->total_ns > zone->max_ns) zone->max_ns = zone->total_ns;
		}

		for (uint32_t i = 0; i < thread->counter_count; ++i) {
			libmse_profiler_counter_t *counter = &thread->counters[i];
			counter->samples++;
			counter->sum += counter->value;
			if (counter->value < counter->min) counter->min = counter->value;
			if (counter->value > counter->max) counter->max = counter->value;
		}
	}

	// With zones off, the tables hold whatever they held when it was switched
	// off. Publishing zero of them says "no zone data" rather than handing a
	// reader stale rows that look current.
	profiler_publish(thread, frame_ns, zones_enabled);

	// Clear only what is per-frame. The tree and its statistics stay.
	for (uint32_t i = 0; i < thread->zone_count; ++i) {
		thread->zones[i].calls    = 0;
		thread->zones[i].total_ns = 0;
		thread->zones[i].child_ns = 0;
	}
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

size_t libmse_profiler_thread_count(void)
{
	uint32_t count = atomic_load_explicit(&g_thread_count, memory_order_acquire);
	return count > LIBMSE_PROFILER_MAX_THREADS ? LIBMSE_PROFILER_MAX_THREADS : count;
}

bool libmse_profiler_read(size_t index, libmse_profiler_thread_view_t *out)
{
	if (out == NULL || index >= libmse_profiler_thread_count()) {
		return false;
	}

	profiler_thread_t *thread = &g_threads[index];

	// Seqlock: read, then check the sequence has neither been odd nor moved.
	// A publish takes a few microseconds at most, so colliding twice running
	// is already unlikely and four attempts makes it not worth thinking about.
	for (int attempt = 0; attempt < 4; ++attempt) {
		uint32_t before = atomic_load_explicit(&thread->seq, memory_order_acquire);
		if (before & 1u) {
			continue;
		}

		memcpy(out, &thread->published, sizeof(*out));

		atomic_thread_fence(memory_order_acquire);
		if (atomic_load_explicit(&thread->seq, memory_order_relaxed) == before) {
			// Clamp what the reader will iterate: a torn read is rejected
			// above, but a UI should not be able to walk off the array even if
			// this ever changes.
			if (out->zone_count > LIBMSE_PROFILER_MAX_ZONES) {
				out->zone_count = LIBMSE_PROFILER_MAX_ZONES;
			}
			if (out->counter_count > LIBMSE_PROFILER_MAX_COUNTERS) {
				out->counter_count = LIBMSE_PROFILER_MAX_COUNTERS;
			}
			if (out->history_count > LIBMSE_PROFILER_HISTORY) {
				out->history_count = LIBMSE_PROFILER_HISTORY;
			}
			return true;
		}
	}

	return false;
}
