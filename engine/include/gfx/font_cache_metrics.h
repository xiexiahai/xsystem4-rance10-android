#ifndef XSYSTEM4_FONT_CACHE_METRICS_H
#define XSYSTEM4_FONT_CACHE_METRICS_H

#include <stddef.h>
#include <stdint.h>
#include "gfx/font.h"

enum glyph_unique_status {
	GLYPH_UNIQUE_COMPLETE,
	GLYPH_UNIQUE_ALLOC_FAILED,
	GLYPH_UNIQUE_BUDGET_EXCEEDED,
	GLYPH_UNIQUE_COUNTER_OVERFLOW,
	GLYPH_UNIQUE_CAPACITY_OVERFLOW,
	GLYPH_UNIQUE_TABLE_FULL
};

struct glyph_cache_metrics {
	uint64_t ttf_slots, ttf_fonts, ft_fonts, unsupported_ttf_fonts;
	uint64_t fnl_slots_unmeasured;
	uint64_t sizes, glyph_slots, glyph_values;
	uint64_t texture_refs[NR_FONT_WEIGHTS];
	/* RGBA32-normalized w*h*4 from Texture metadata, NOT driver VRAM. */
	uint64_t texture_rgba_bytes[NR_FONT_WEIGHTS];
	uint64_t total_texture_refs, unique_texture_handles, unique_rgba_bytes;
	uint64_t invalid_dimension_refs, inconsistent_handle_refs;
	uint64_t dedup_scratch_bytes; /* requested temporary allocation, never retained */
	bool rgba_bytes_valid, unique_bytes_valid, counters_saturated;
	enum glyph_unique_status unique_status;
};

/* Render/VM thread only; no concurrent cache mutation. No font/size creation,
 * GL calls, texture reads, logging, or fatal allocation helpers. The visitor
 * returns false for a non-FreeType font or an invalid private size count. */
bool ft_font_visit_cached_sizes(const struct font *font,
		void (*visit)(const struct font_size *, void *), void *data);

/* Array parameters keep the collector independent of text.c's global storage.
 * Font aliases are deduplicated by object pointer. FNL is reported, not walked.
 * Optional unique-handle scratch is bounded to 8 MiB; failure keeps reference
 * counts usable and sets unique_status. Scratch is released before returning. */
void font_collect_cache_metrics(struct font *const *ttf, size_t nr_ttf,
		struct font *const *fnl, size_t nr_fnl, struct glyph_cache_metrics *out);

/* Small text.c wrapper using its current font_ttf[] / font_fnl[] arrays. */
void gfx_get_glyph_cache_metrics(struct glyph_cache_metrics *out);

/* Explicit diagnostic only; the collector runs at most once per two seconds
 * on the render/VM thread. Ordinary rendering returns after a cached guard. */
void gfx_maybe_log_glyph_cache_metrics(void);

#endif
