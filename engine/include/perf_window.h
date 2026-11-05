/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XSYSTEM4_PERF_WINDOW_H
#define XSYSTEM4_PERF_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

/* VM/render/input thread only. Inclusive durations overlap: NEVER add rows.
 * No names, script addresses, parameters, text or resource paths are captured. */
enum perf_window_kind {
	PERF_WINDOW_HLL,
	PERF_WINDOW_UPDATE,
	PERF_WINDOW_RENDER
};

struct perf_window_scope {
	uint64_t serial;
	unsigned slot;
};

extern bool perf_window_active;
void perf_window_mouse_press(void);
void perf_window_poll_impl(void);
struct perf_window_scope perf_window_begin_impl(enum perf_window_kind kind,
		int library, int function);
void perf_window_end_impl(struct perf_window_scope scope);

/* Ordinary play never calls the timer, scans a table, allocates or logs. */
static inline struct perf_window_scope perf_window_begin(
		enum perf_window_kind kind, int library, int function)
{
	if (!perf_window_active)
		return (struct perf_window_scope){0};
	return perf_window_begin_impl(kind, library, function);
}

static inline void perf_window_end(struct perf_window_scope scope)
{
	if (scope.serial)
		perf_window_end_impl(scope);
}

static inline void perf_window_poll(void)
{
	if (perf_window_active)
		perf_window_poll_impl();
}

#endif
