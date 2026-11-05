/* Copyright (C) 2026 xsystem4 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef XSYSTEM4_PERF_TRACE_H
#define XSYSTEM4_PERF_TRACE_H

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <SDL_timer.h>

/* Opt in before the first call; the setting is cached for this process.
 * Each VM-thread module owns a budget, retained across game resets. With the
 * two current callers this permits at most 128 lines in one process.
 * Disabled or exhausted budgets do not read the performance counter. */
#define PERF_TRACE_SLOW_MS 50.0
#define PERF_TRACE_MODULE_LIMIT 64

struct perf_trace_budget {
	unsigned lines;
};

static inline bool perf_trace_enabled(void)
{
	static int enabled = -1;
	if (enabled < 0) {
		const char *value = getenv("XSYSTEM4_PERF_TRACE");
		enabled = value && !strcmp(value, "1");
	}
	return enabled != 0;
}

static inline Uint64 perf_trace_begin(const struct perf_trace_budget *budget)
{
	if (budget->lines >= PERF_TRACE_MODULE_LIMIT || !perf_trace_enabled())
		return 0;
	return SDL_GetPerformanceCounter();
}

static inline Uint64 perf_trace_stamp(Uint64 started)
{
	return started ? SDL_GetPerformanceCounter() : 0;
}

static inline double perf_trace_ms(Uint64 ticks)
{
	static Uint64 frequency;
	if (!frequency)
		frequency = SDL_GetPerformanceFrequency();
	return frequency ? (double)ticks * 1000.0 / (double)frequency : 0.0;
}

static inline double perf_trace_elapsed_ms(Uint64 started, Uint64 ended)
{
	return started && ended >= started ? perf_trace_ms(ended - started) : 0.0;
}

static inline bool perf_trace_claim(struct perf_trace_budget *budget,
		Uint64 started, Uint64 ended)
{
	if (!started || budget->lines >= PERF_TRACE_MODULE_LIMIT
			|| perf_trace_elapsed_ms(started, ended) < PERF_TRACE_SLOW_MS)
		return false;
	budget->lines++;
	return true;
}

#endif /* XSYSTEM4_PERF_TRACE_H */
