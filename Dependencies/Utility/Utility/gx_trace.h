// GeneralsX @build WebPort 2026-07-07 — central switch for web-port trace
// logging (GX-TRACE, Blit_Char, TIMEGETTIME-RAW, audio bridge traces).
//
// The per-frame/per-event trace spam churns the JS heap (100–200MB observed)
// and triggers a V8 compacting-GC crash in current Chrome stable, so it is
// compiled out of Release builds (CMake defines NDEBUG for Release) and kept
// in every other profile. Genuine error/warning logs must NOT use these
// macros — they stay unconditional.
//
// Override with -DGX_TRACE_LOGGING=0/1 to force either way.
#pragma once

#include <stdio.h>

#ifndef GX_TRACE_LOGGING
#ifdef NDEBUG
#define GX_TRACE_LOGGING 0
#else
#define GX_TRACE_LOGGING 1
#endif
#endif

#if GX_TRACE_LOGGING
// Console trace — stderr reaches the browser console on Emscripten.
#define GX_TRACE_LOG(...) fprintf(stderr, __VA_ARGS__)
// File trace — append to the MEMFS trace log, surviving a hung main loop.
#define GX_TRACE_FILE_LOG(...) do { \
	FILE *gx_trace_file_ = fopen("/gx_trace.log", "a"); \
	if (gx_trace_file_) { fprintf(gx_trace_file_, __VA_ARGS__); fclose(gx_trace_file_); } \
} while (0)
#else
#define GX_TRACE_LOG(...) ((void)0)
#define GX_TRACE_FILE_LOG(...) ((void)0)
#endif
