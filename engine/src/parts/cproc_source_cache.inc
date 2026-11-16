/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PARTS_CPROC_SOURCE_CACHE_INC
#define PARTS_CPROC_SOURCE_CACHE_INC

/* Main-thread, single-build scratch cache. Only repeatedly sampled sources
 * qualify; no image survives the build or its final use in that build.
 * Limits count level-zero RGBA texture bytes, not driver allocation overhead. */
#define CPROC_SOURCE_CACHE_BYTES (4u * 1024u * 1024u)
#define CPROC_SOURCE_ENTRY_BYTES (2u * 1024u * 1024u)
#define CPROC_SOURCE_CACHE_ENTRIES 8
#define CPROC_SOURCE_SCAN_LIMIT 4096
#ifndef PARTS_CPROC_CACHE_OBSERVE
#define PARTS_CPROC_CACHE_OBSERVE(cache) ((void)0)
#endif

struct cproc_source_entry {
	const char *name; /* Borrowed from the stable operation list. */
	unsigned remaining, last_use;
	size_t bytes;
	Texture texture;
};

struct cproc_source_cache {
	struct cproc_source_entry entries[CPROC_SOURCE_CACHE_ENTRIES];
	unsigned count, tick, evictions;
	size_t bytes;
	uint64_t generation;
};

static struct cproc_source_entry *cproc_source_find(struct cproc_source_cache *cache,
		const char *name)
{
	for (unsigned i = 0; i < cache->count; i++)
		if (!strcmp(cache->entries[i].name, name))
			return &cache->entries[i];
	return NULL;
}

static void cproc_source_drop(struct cproc_source_cache *cache,
		struct cproc_source_entry *entry)
{
	if (!entry->texture.handle)
		return;
	cache->bytes -= entry->bytes;
	gfx_delete_texture(&entry->texture);
	entry->bytes = 0;
	PARTS_CPROC_CACHE_OBSERVE(cache);
}

static void cproc_source_clear(struct cproc_source_cache *cache)
{
	for (unsigned i = 0; i < cache->count; i++)
		cproc_source_drop(cache, &cache->entries[i]);
	PARTS_CPROC_CACHE_OBSERVE(cache);
}

static void cproc_source_init(struct cproc_source_cache *cache,
		struct parts_construction_process *cproc)
{
	memset(cache, 0, sizeof(*cache));
	cache->generation = asset_manager_generation();
	unsigned scanned = 0;
	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &cproc->ops, entry) {
		/* A huge operation list still renders normally, without caching. */
		if (++scanned > CPROC_SOURCE_SCAN_LIMIT) {
			cache->count = 0;
			return;
		}
		switch (op->type) {
		case PARTS_CP_DRAW_CUT_CG:
		case PARTS_CP_COPY_CUT_CG:
		case PARTS_CP_ALPHA_BLEND_CUT_CG:
		case PARTS_CP_ONLY_ALPHA_CUT_CG:
			break;
		default:
			continue;
		}
		const char *name = op->cut_cg.cg_name->text;
		struct cproc_source_entry *entry = cproc_source_find(cache, name);
		if (!entry && cache->count < CPROC_SOURCE_CACHE_ENTRIES) {
			entry = &cache->entries[cache->count++];
			entry->name = name;
		}
		if (entry)
			entry->remaining++;
	}
}

static Texture *cproc_source_get(struct cproc_source_cache *cache,
		const char *name, Texture *scratch)
{
	uint64_t generation = asset_manager_generation();
	if (generation != cache->generation) {
		cproc_source_clear(cache);
		cache->generation = generation;
	}
	struct cproc_source_entry *entry = cproc_source_find(cache, name);
	if (entry && entry->texture.handle) {
		entry->last_use = ++cache->tick;
		return &entry->texture;
	}
	struct cg *cg = asset_cg_load_by_name(name, NULL);
	if (!cg)
		return NULL;
	int w = cg->metrics.w, h = cg->metrics.h;
	/* Division first bounds the multiplication on 32-bit targets too. */
	bool small = w > 0 && h > 0 &&
		(size_t)w <= CPROC_SOURCE_ENTRY_BYTES / 4u / (size_t)h;
	size_t bytes = small ? (size_t)w * (size_t)h * 4u : 0;
	bool retain = entry && entry->remaining > 1 && small;
	if (retain) {
		/* Evict before upload so even transient cache occupancy is bounded. */
		while (cache->bytes > CPROC_SOURCE_CACHE_BYTES - bytes) {
			struct cproc_source_entry *oldest = NULL;
			for (unsigned i = 0; i < cache->count; i++) {
				struct cproc_source_entry *candidate = &cache->entries[i];
				if (candidate->texture.handle &&
						(!oldest || candidate->last_use < oldest->last_use))
					oldest = candidate;
			}
			assert(oldest);
			cache->evictions++;
			cproc_source_drop(cache, oldest);
		}
	}
	Texture *texture = retain ? &entry->texture : scratch;
	/* These sources never park or cross a frame: do not duplicate their
	 * decoded pixels in the texture parking store. */
	gfx_init_texture_with_pixels(texture, w, h, cg->pixels);
	cg_free(cg);
	if (!texture->handle)
		return NULL;
	if (retain) {
		entry->bytes = bytes;
		entry->last_use = ++cache->tick;
		cache->bytes += bytes;
		PARTS_CPROC_CACHE_OBSERVE(cache);
	}
	return texture;
}

static void cproc_source_release(struct cproc_source_cache *cache,
		const char *name, Texture *scratch)
{
	gfx_delete_texture(scratch);
	struct cproc_source_entry *entry = cproc_source_find(cache, name);
	if (entry && entry->remaining && !--entry->remaining)
		cproc_source_drop(cache, entry);
}
#endif
