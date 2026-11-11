#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "gfx/font_cache_metrics.h"
#include "system4/hashtable.h"

#define MAX_DIAGNOSTIC_SCRATCH (8u * 1024u * 1024u)

struct texture_seen {
	GLuint handle;
	int w, h;
};

struct collect_context {
	struct glyph_cache_metrics *out;
	struct texture_seen *seen;
	size_t capacity;
	bool unique_pass;
};

static void add_count(uint64_t *target, uint64_t value, struct glyph_cache_metrics *out)
{
	if (UINT64_MAX - *target < value) {
		*target = UINT64_MAX;
		out->counters_saturated = true;
	} else {
		*target += value;
	}
}

static uint64_t rgba_bytes(const Texture *texture)
{
	/* Positive int dimensions fit uint64_t for this normalized single surface. */
	return texture->w > 0 && texture->h > 0
		? (uint64_t)texture->w * (uint64_t)texture->h * 4 : 0;
}

static void collect_unique(const Texture *texture, struct collect_context *ctx)
{
	struct glyph_cache_metrics *out = ctx->out;
	size_t index = ((uint32_t)texture->handle * UINT32_C(2654435761)) & (ctx->capacity - 1);
	for (size_t probe = 0; probe < ctx->capacity; probe++) {
		struct texture_seen *entry = &ctx->seen[index];
		if (!entry->handle) {
			*entry = (struct texture_seen){texture->handle, texture->w, texture->h};
			add_count(&out->unique_texture_handles, 1, out);
			add_count(&out->unique_rgba_bytes, rgba_bytes(texture), out);
			return;
		}
		if (entry->handle == texture->handle) {
			if (entry->w != texture->w || entry->h != texture->h) {
				add_count(&out->inconsistent_handle_refs, 1, out);
				out->unique_bytes_valid = false;
			}
			return;
		}
		index = (index + 1) & (ctx->capacity - 1);
	}
	/* A violated no-concurrent-mutation precondition must not spin forever. */
	out->unique_status = GLYPH_UNIQUE_TABLE_FULL;
}

static void collect_slot(struct ht_slot *slot, void *data)
{
	struct collect_context *ctx = data;
	struct glyph_cache_metrics *out = ctx->out;
	const struct glyph *glyph = slot->value;
	if (!ctx->unique_pass) {
		add_count(&out->glyph_slots, 1, out);
		if (glyph) add_count(&out->glyph_values, 1, out);
	}
	if (!glyph) return;
	for (unsigned weight = 0; weight < NR_FONT_WEIGHTS; weight++) {
		const Texture *texture = &glyph->t[weight];
		if (!texture->handle) continue;
		if (ctx->unique_pass) {
			collect_unique(texture, ctx);
		} else {
			add_count(&out->texture_refs[weight], 1, out);
			add_count(&out->total_texture_refs, 1, out);
			add_count(&out->texture_rgba_bytes[weight], rgba_bytes(texture), out);
			if (texture->w <= 0 || texture->h <= 0) {
				add_count(&out->invalid_dimension_refs, 1, out);
				out->rgba_bytes_valid = false;
			}
		}
	}
}

static void collect_size(const struct font_size *size, void *data)
{
	struct collect_context *ctx = data;
	if (!ctx->unique_pass) add_count(&ctx->out->sizes, 1, ctx->out);
	if (size->glyph_table) ht_foreach(size->glyph_table, collect_slot, ctx);
}

static void visit_ttf(struct font *const *ttf, size_t count, struct collect_context *ctx)
{
	if (!ttf) return;
	for (size_t i = 0; i < count; i++) {
		if (!ttf[i]) continue;
		if (!ctx->unique_pass) add_count(&ctx->out->ttf_slots, 1, ctx->out);
		bool duplicate = false;
		for (size_t prior = 0; prior < i; prior++) {
			if (ttf[i] == ttf[prior]) { duplicate = true; break; }
		}
		if (duplicate) continue;
		if (!ctx->unique_pass) add_count(&ctx->out->ttf_fonts, 1, ctx->out);
		bool supported = ft_font_visit_cached_sizes(ttf[i], collect_size, ctx);
		if (!ctx->unique_pass) {
			add_count(supported ? &ctx->out->ft_fonts : &ctx->out->unsupported_ttf_fonts, 1, ctx->out);
		}
	}
}

void font_collect_cache_metrics(struct font *const *ttf, size_t nr_ttf,
		struct font *const *fnl, size_t nr_fnl, struct glyph_cache_metrics *out)
{
	if (!out) return;
	memset(out, 0, sizeof(*out));
	out->rgba_bytes_valid = true;
	out->unique_bytes_valid = true;
	struct collect_context ctx = {.out = out};
	visit_ttf(ttf, nr_ttf, &ctx);
	for (size_t i = 0; fnl && i < nr_fnl; i++) {
		if (fnl[i]) add_count(&out->fnl_slots_unmeasured, 1, out);
	}
	if (out->counters_saturated) out->rgba_bytes_valid = false;
	if (!out->total_texture_refs) return;
	if (out->counters_saturated || out->total_texture_refs > SIZE_MAX / 2) {
		out->unique_status = out->counters_saturated ? GLYPH_UNIQUE_COUNTER_OVERFLOW
			: GLYPH_UNIQUE_CAPACITY_OVERFLOW;
		out->unique_bytes_valid = false;
		return;
	}
	size_t capacity = 64;
	size_t needed = (size_t)out->total_texture_refs * 2;
	while (capacity < needed) {
		if (capacity > SIZE_MAX / 2) {
			out->unique_status = GLYPH_UNIQUE_CAPACITY_OVERFLOW;
			out->unique_bytes_valid = false;
			return;
		}
		capacity *= 2;
	}
	if (capacity > MAX_DIAGNOSTIC_SCRATCH / sizeof(*ctx.seen)) {
		out->unique_status = GLYPH_UNIQUE_BUDGET_EXCEEDED;
		out->unique_bytes_valid = false;
		return;
	}
	out->dedup_scratch_bytes = capacity * sizeof(*ctx.seen);
	ctx.seen = calloc(capacity, sizeof(*ctx.seen));
	if (!ctx.seen) {
		out->unique_status = GLYPH_UNIQUE_ALLOC_FAILED;
		out->unique_bytes_valid = false;
		return;
	}
	ctx.capacity = capacity;
	ctx.unique_pass = true;
	out->unique_bytes_valid = out->rgba_bytes_valid;
	visit_ttf(ttf, nr_ttf, &ctx);
	free(ctx.seen);
	if (out->counters_saturated) out->rgba_bytes_valid = false;
	if (out->counters_saturated || out->unique_status != GLYPH_UNIQUE_COMPLETE)
		out->unique_bytes_valid = false;
	if (!out->unique_bytes_valid) out->unique_rgba_bytes = 0;
}
