/* SPDX-License-Identifier: GPL-2.0-or-later
 * Included once by vm.c: no new build-system source entry is needed.
 * Default off; XSYSTEM4_PERF_WINDOW=1 is cached at first consumed mouse press.
 * Fixed windows, bounded storage/output, VM thread only, inclusive-only timing.
 */
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <SDL_timer.h>
#include "perf_window.h"

#define HWINDOW_WINDOWS 8u
#define HWINDOW_SECONDS 3u
#define HWINDOW_KEYS 256u
#define HWINDOW_SPANS 64u
#define HWINDOW_TOP 12u
#define HWINDOW_INVALID_GAP_SECONDS 10.0
#define HWINDOW_OVERFLOW HWINDOW_KEYS
#define HWINDOW_NO_SLOT HWINDOW_SPANS

struct hwindow_row {
	bool used;
	int kind, library, function;
	uint64_t calls, ticks, max_ticks, clipped;
};
struct hwindow_span {
	uint64_t serial, started;
	unsigned row, next_free;
};
static struct {
	int enabled; /* 0 unchecked, 1 off, 2 on */
	unsigned windows, free_head;
	uint64_t frequency, started, deadline, serial;
	uint64_t last_observed, max_gap;
	uint64_t dropped_spans, overflow_calls, ignored_presses;
	struct hwindow_row rows[HWINDOW_KEYS + 1];
	struct hwindow_span spans[HWINDOW_SPANS];
} hwindow;

bool perf_window_active;

static double hwindow_ms(uint64_t ticks)
{
	return (double)ticks * 1000.0 / (double)hwindow.frequency;
}

static uint64_t hwindow_now(void)
{
	uint64_t now = SDL_GetPerformanceCounter();
	if (perf_window_active) {
		uint64_t gap = now >= hwindow.last_observed ? now - hwindow.last_observed : 0;
		if (gap > hwindow.max_gap) hwindow.max_gap = gap;
		hwindow.last_observed = now;
	}
	return now;
}

static void hwindow_accumulate(struct hwindow_span *span, uint64_t ended,
		bool clipped)
{
	struct hwindow_row *row = &hwindow.rows[span->row];
	uint64_t ticks = ended >= span->started ? ended - span->started : 0;
	row->ticks += ticks;
	if (ticks > row->max_ticks)
		row->max_ticks = ticks;
	row->clipped += clipped;
	span->serial = 0;
}

static bool hwindow_precedes(const struct hwindow_row *a,
		const struct hwindow_row *b)
{
	if (a->ticks != b->ticks) return a->ticks > b->ticks;
	if (a->kind != b->kind) return a->kind < b->kind;
	if (a->library != b->library) return a->library < b->library;
	return a->function < b->function;
}

static void hwindow_close(uint64_t observed)
{
	/* Expire all tokens before logging. Long calls are clipped to the exact
	 * deadline; missing/unwound ends cannot hold a window open indefinitely. */
	perf_window_active = false;
	for (unsigned i = 0; i < HWINDOW_SPANS; i++)
		if (hwindow.spans[i].serial)
			hwindow_accumulate(&hwindow.spans[i], hwindow.deadline, true);
	uint64_t calls = 0, clipped = 0;
	unsigned keys = 0;
	for (unsigned i = 0; i <= HWINDOW_KEYS; i++) {
		calls += hwindow.rows[i].calls;
		clipped += hwindow.rows[i].clipped;
		keys += hwindow.rows[i].used;
	}
	uint64_t elapsed = observed >= hwindow.started ? observed - hwindow.started : 0;
	bool invalid_gap = (double)elapsed / (double)hwindow.frequency > HWINDOW_INVALID_GAP_SECONDS ||
		(double)hwindow.max_gap / (double)hwindow.frequency > HWINDOW_INVALID_GAP_SECONDS;
	sys_message("[perf-window] window=%u mode=inclusive_nonadditive timer=monotonic_elapsed span_ms=%.3f elapsed_ms=%.3f close_delay_ms=%.3f max_gap_ms=%.3f invalid_gap=%u calls=%"PRIu64" keys=%u overflow_calls=%"PRIu64" dropped_spans=%"PRIu64" clipped=%"PRIu64" ignored_presses=%"PRIu64"\n",
		hwindow.windows, hwindow_ms(hwindow.deadline - hwindow.started),
		hwindow_ms(elapsed),
		hwindow_ms(observed >= hwindow.deadline ? observed - hwindow.deadline : 0),
		hwindow_ms(hwindow.max_gap), (unsigned)invalid_gap,
		calls, keys, hwindow.overflow_calls, hwindow.dropped_spans, clipped,
		hwindow.ignored_presses);
	bool emitted[HWINDOW_KEYS + 1] = {0};
	for (unsigned rank = 0; rank < HWINDOW_TOP; rank++) {
		unsigned best = HWINDOW_KEYS + 1;
		for (unsigned i = 0; i <= HWINDOW_KEYS; i++) {
			if (!hwindow.rows[i].used || emitted[i]) continue;
			if (best > HWINDOW_KEYS ||
					hwindow_precedes(&hwindow.rows[i], &hwindow.rows[best])) best = i;
		}
		if (best > HWINDOW_KEYS) break;
		emitted[best] = true;
		const struct hwindow_row *row = &hwindow.rows[best];
		const char *kind = best == HWINDOW_OVERFLOW ? "overflow" :
			row->kind == PERF_WINDOW_HLL ? "hll" :
			row->kind == PERF_WINDOW_UPDATE ? "update" : "render";
		sys_message("[perf-window-top] window=%u rank=%u kind=%s lib=%d fn=%d calls=%"PRIu64" inclusive_ms=%.3f max_inclusive_ms=%.3f clipped=%"PRIu64"\n",
			hwindow.windows, rank + 1, kind, row->library, row->function,
			row->calls, hwindow_ms(row->ticks), hwindow_ms(row->max_ticks), row->clipped);
	}
}

void perf_window_poll_impl(void)
{
	if (!perf_window_active) return;
	uint64_t now = hwindow_now();
	if (now >= hwindow.deadline) hwindow_close(now);
}

void perf_window_mouse_press(void)
{
	if (!hwindow.enabled) {
		const char *value = getenv("XSYSTEM4_PERF_WINDOW");
		hwindow.enabled = value && !strcmp(value, "1") ? 2 : 1;
	}
	if (hwindow.enabled != 2 ||
			(!perf_window_active && hwindow.windows >= HWINDOW_WINDOWS)) return;
	uint64_t now = hwindow_now();
	if (perf_window_active && now >= hwindow.deadline) {
		hwindow_close(now);
		if (hwindow.windows < HWINDOW_WINDOWS) now = hwindow_now();
	}
	if (perf_window_active) {
		hwindow.ignored_presses++;
		return;
	}
	if (hwindow.windows >= HWINDOW_WINDOWS) return;
	if (!hwindow.frequency) hwindow.frequency = SDL_GetPerformanceFrequency();
	/* Reject unusable timers instead of dividing by zero or wrapping. */
	if (!hwindow.frequency || hwindow.frequency > UINT64_MAX / HWINDOW_SECONDS ||
			now > UINT64_MAX - hwindow.frequency * HWINDOW_SECONDS) {
		hwindow.enabled = 1;
		return;
	}
	hwindow.windows++;
	hwindow.started = now;
	hwindow.last_observed = now;
	hwindow.max_gap = 0;
	hwindow.deadline = now + hwindow.frequency * HWINDOW_SECONDS;
	hwindow.dropped_spans = hwindow.overflow_calls = hwindow.ignored_presses = 0;
	memset(hwindow.rows, 0, sizeof(hwindow.rows));
	memset(hwindow.spans, 0, sizeof(hwindow.spans));
	for (unsigned i = 0; i < HWINDOW_SPANS; i++) hwindow.spans[i].next_free = i + 1;
	hwindow.free_head = 0;
	perf_window_active = true;
}

static unsigned hwindow_find_row(int kind, int library, int function)
{
	uint32_t hash = (uint32_t)kind * UINT32_C(0x9e3779b9) ^
		(uint32_t)library * UINT32_C(0x85ebca6b) ^ (uint32_t)function * UINT32_C(0xc2b2ae35);
	for (unsigned n = 0; n < HWINDOW_KEYS; n++) {
		unsigned i = (hash + n) % HWINDOW_KEYS;
		struct hwindow_row *row = &hwindow.rows[i];
		if (row->used && (row->kind != kind || row->library != library ||
				row->function != function)) continue;
		if (!row->used) {
			row->used = true;
			row->kind = kind; row->library = library; row->function = function;
		}
		return i;
	}
	hwindow.overflow_calls++;
	struct hwindow_row *row = &hwindow.rows[HWINDOW_OVERFLOW];
	row->used = true;
	row->kind = -1; row->library = row->function = -1;
	return HWINDOW_OVERFLOW;
}

struct perf_window_scope perf_window_begin_impl(enum perf_window_kind kind,
		int library, int function)
{
	if (!perf_window_active) return (struct perf_window_scope){0};
	uint64_t now = hwindow_now();
	if (now >= hwindow.deadline) {
		hwindow_close(now);
		return (struct perf_window_scope){0};
	}
	if (hwindow.free_head == HWINDOW_NO_SLOT || hwindow.serial == UINT64_MAX) {
		hwindow.dropped_spans++;
		return (struct perf_window_scope){0};
	}
	unsigned slot = hwindow.free_head;
	struct hwindow_span *span = &hwindow.spans[slot];
	hwindow.free_head = span->next_free;
	span->started = now;
	span->row = hwindow_find_row(kind, library, function);
	hwindow.rows[span->row].calls++;
	span->serial = ++hwindow.serial; /* never reset: stale ends cannot hit new spans */
	return (struct perf_window_scope){.serial = span->serial, .slot = slot};
}

void perf_window_end_impl(struct perf_window_scope scope)
{
	if (!perf_window_active || scope.slot >= HWINDOW_SPANS || !scope.serial ||
			hwindow.spans[scope.slot].serial != scope.serial) return;
	uint64_t now = hwindow_now();
	if (now >= hwindow.deadline) {
		hwindow_close(now);
		return;
	}
	struct hwindow_span *span = &hwindow.spans[scope.slot];
	hwindow_accumulate(span, now, false);
	span->next_free = hwindow.free_head;
	hwindow.free_head = scope.slot;
}
