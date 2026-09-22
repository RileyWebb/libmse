#ifndef LIBMSE_PROFILER_H
#define LIBMSE_PROFILER_H

// A frame profiler light enough to leave switched on.
//
// Instrumented, not sampling: you name the spans you care about and it times
// exactly those. A span costs two clock reads and a handful of adds, so the
// whole frontend loop instrumented end to end costs well under a microsecond
// of a 16.6ms frame. There is no profiler thread, no allocation after the
// first zone on a thread, and no lock on the producing side -- a worker only
// ever writes its own storage.
//
// Reading is done from any thread through libmse_profiler_read, which copies a
// finished frame out from under a seqlock. A reader never blocks a worker; if
// it collides with a publish it retries and, failing that, says so.
//
// Each thread keeps its own call tree, merged per frame: entering the same
// zone ten times under the same parent gives one row with calls == 10, which
// is what you want to read. Rolling min/avg/max accumulate until reset.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libmse_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LIBMSE_PROFILER_MAX_THREADS 8
#define LIBMSE_PROFILER_MAX_ZONES   96  // distinct call-tree nodes, per thread
#define LIBMSE_PROFILER_MAX_DEPTH   16
#define LIBMSE_PROFILER_MAX_COUNTERS 16
#define LIBMSE_PROFILER_HISTORY     180 // frame samples kept for the graph
#define LIBMSE_PROFILER_NAME_MAX    32

// One node of a thread's call tree: a zone as reached from one parent, with
// every entry during the frame merged into it.
typedef struct libmse_profiler_zone_s {
	const char *name;     // the caller's static string, not copied
	int16_t     parent;   // index into the same array; -1 at the root
	uint8_t     depth;

	// This frame.
	uint32_t    calls;
	uint64_t    total_ns; // including everything nested inside
	uint64_t    child_ns; // the part spent in direct children

	// Since the last libmse_profiler_reset. Frames in which the zone did not run
	// are not counted, so the average is "how long when it happens" rather
	// than something diluted by the frames it sat out.
	uint64_t    samples;
	uint64_t    sum_ns;
	uint64_t    min_ns;
	uint64_t    max_ns;
} libmse_profiler_zone_t;

// A number a thread publishes about its frame -- frames emulated, bytes
// uploaded, audio underruns. Timing alone does not explain a stall that is
// really a queue running dry.
typedef struct libmse_profiler_counter_s {
	const char *name;
	double      value;  // as last set this frame
	double      min;
	double      max;
	double      sum;
	uint64_t    samples;
} libmse_profiler_counter_t;

// A snapshot of one thread's most recently completed frame.
typedef struct libmse_profiler_thread_view_s {
	char     name[LIBMSE_PROFILER_NAME_MAX];
	uint64_t frame_count;
	uint64_t frame_ns;     // wall time of the frame just finished

	uint32_t zone_count;
	uint32_t counter_count;

	// Frame times in milliseconds, oldest first, for plotting.
	uint32_t history_count;
	float    history[LIBMSE_PROFILER_HISTORY];

	// Zones that did not fit the table, and zones still open when the frame
	// ended. Either means the instrumentation is wrong, so the UI says so
	// rather than quietly showing numbers that do not add up.
	uint32_t dropped;
	uint32_t unbalanced;

	libmse_profiler_zone_t    zones[LIBMSE_PROFILER_MAX_ZONES];
	libmse_profiler_counter_t counters[LIBMSE_PROFILER_MAX_COUNTERS];
} libmse_profiler_thread_view_t;

// Starts the clock. Called by libmse_init; calling it again is harmless.
LIBMSE_API void libmse_profiler_init(void);

// Switches *zone* collection on and off. Off is the default, and while off
// begin/end cost one relaxed atomic load each and record nothing.
//
// Frame pacing is not covered by this: libmse_profiler_frame keeps timing
// frames either way, because that is one clock read and it is what a frame
// rate display needs. So a thread that calls libmse_profiler_frame always
// publishes frame_count, frame_ns and history; zones and counters are present
// only while this is on.
LIBMSE_API void libmse_profiler_set_enabled(bool enabled);
LIBMSE_API bool libmse_profiler_enabled(void);

// Clears the rolling min/avg/max on every thread. The call tree and the
// in-flight frame are left alone, so this is safe to call at any time.
LIBMSE_API void libmse_profiler_reset(void);

// Names the calling thread for display. Optional -- a thread that starts
// timing without one is listed by index.
LIBMSE_API void libmse_profiler_thread_name(const char *name);

// Opens and closes a zone. `name` must outlive the profiler, which a string
// literal does; it is stored by pointer and never copied.
LIBMSE_API void libmse_profiler_begin(const char *name);
LIBMSE_API void libmse_profiler_end(void);

// Records a value for this frame. The last call in a frame wins.
LIBMSE_API void libmse_profiler_counter(const char *name, double value);

// Closes the calling thread's frame and publishes it. Any zone still open is
// closed here and counted as unbalanced, so an early return cannot wedge the
// stack for good.
//
// Always does its work, whether or not zone collection is enabled -- see
// libmse_profiler_set_enabled.
LIBMSE_API void libmse_profiler_frame(void);

// How many threads have profiled anything.
LIBMSE_API size_t libmse_profiler_thread_count(void);

// Copies thread `index`'s latest published frame into `out`. Returns false for
// an index that is not in use, or if a publish kept winning the race -- rare
// enough that a UI can simply skip the frame.
LIBMSE_API bool libmse_profiler_read(size_t index, libmse_profiler_thread_view_t *out);

// The form to instrument with:
//
//   LIBMSE_PROFILE_START("upload frame");
//   ...
//   LIBMSE_PROFILE_END();
//
// A plain pair rather than a scoped block. A block form would have to be a
// for-loop, which silently captures any `break` or `continue` written inside
// it -- and the places worth profiling are loop bodies. Returning between the
// two is safe regardless: libmse_profiler_frame closes whatever is still open.
#define LIBMSE_PROFILE_START(zone_name) libmse_profiler_begin(zone_name)
#define LIBMSE_PROFILE_END()            libmse_profiler_end()

#ifdef __cplusplus
}
#endif

#endif // LIBMSE_PROFILER_H
