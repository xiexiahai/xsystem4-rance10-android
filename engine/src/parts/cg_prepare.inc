#ifndef PARTS_CG_PREPARE_INC
#define PARTS_CG_PREPARE_INC

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Main-thread CG preparation: archive snapshot, metrics and codec. The only
 * worker-side step is the exclusive RGBA copy into parts_cg_prepared.pixels.
 *
 * The macros below are narrow fixture seams; production uses the real asset,
 * codec and pixel-copy functions. This file does not open GL, touch Parts/VM
 * or install a global error handler.
 */
#ifndef PARTS_CG_ARCHIVE_GET
#define PARTS_CG_ARCHIVE_GET asset_get_by_name
#endif
#ifndef PARTS_CG_ARCHIVE_FREE
#define PARTS_CG_ARCHIVE_FREE archive_free_data
#endif
#ifndef PARTS_CG_METRICS
#define PARTS_CG_METRICS cg_get_metrics_data
#endif
#ifndef PARTS_CG_DECODE
#define PARTS_CG_DECODE cg_load_data
#endif
#ifndef PARTS_CG_SOURCE_FREE
#define PARTS_CG_SOURCE_FREE cg_free
#endif
#ifndef PARTS_CG_PIXEL_COPY
#define PARTS_CG_PIXEL_COPY parts_flat_pixels_copy
#endif

#define PARTS_CG_ENCODED_MAX ((size_t)32u * 1024u * 1024u)
#define PARTS_CG_RGBA_MAX ((size_t)64u * 1024u * 1024u)

struct parts_cg_prepared {
	struct cg *source;
	struct string *name;
	int no;
	struct parts_flat_pixels pixels;
	size_t bytes;
};

/* Private credit-refusal detail; decoder/OOM failures never set it. */
static size_t parts_cg_prepare_budget_required;

/*
 * Main thread only. On success, source is kept, name is duplicated, pixels is
 * still empty, and bytes is the budgeted 2*RGBA (source + staging).
 */
static struct parts_cg_prepared *parts_cg_prepare_main(struct string *name,
		size_t remaining)
{
	parts_cg_prepare_budget_required = 0;
	if (!name || name->size <= 0 || name->text[0] == '\0')
		return NULL;
	if (!strncmp(name->text, "<save>", 6))
		return NULL;

	int no = -1;
	struct archive_data *entry = PARTS_CG_ARCHIVE_GET(ASSET_CG,
			name->text, &no);
	if (!entry)
		return NULL;
	if (!entry->data || entry->size < 4 || entry->size > PARTS_CG_ENCODED_MAX ||
			entry->size > remaining) {
		PARTS_CG_ARCHIVE_FREE(entry);
		return NULL;
	}

	size_t encoded_size = entry->size;
	uint8_t *encoded = malloc(encoded_size);
	if (!encoded) {
		PARTS_CG_ARCHIVE_FREE(entry);
		return NULL;
	}
	memcpy(encoded, entry->data, encoded_size);
	struct archive *decode_archive=entry->archive;
	PARTS_CG_ARCHIVE_FREE(entry);

	struct archive_data bitmap = {
		.data = encoded,
		.size = encoded_size,
		.archive = decode_archive,
	};
	struct cg_metrics metrics = {0};
	if (!PARTS_CG_METRICS(&bitmap, &metrics)) {
		free(encoded);
		return NULL;
	}
	if (metrics.w < 1 || metrics.w > 8192 ||
			metrics.h < 1 || metrics.h > 8192) {
		free(encoded);
		return NULL;
	}
	size_t rgba = (size_t)metrics.w * (size_t)metrics.h * 4;
	if (rgba > PARTS_CG_RGBA_MAX) {
		free(encoded);
		return NULL;
	}
	/* Require encoded + 2*RGBA <= remaining without forming the sum. */
	if (encoded_size > remaining ||
			rgba > (remaining - encoded_size) / 2) {
		parts_cg_prepare_budget_required = encoded_size + 2 * rgba;
		free(encoded);
		return NULL;
	}

	/* Main-only: OVER/DCF may read a base image from this archive. The
	 * non-owning archive context never escapes into the prepared payload. */
	struct cg *source = PARTS_CG_DECODE(&bitmap);
	free(encoded);
	if (!source)
		return NULL;
	if (!source->pixels ||
			source->metrics.w != metrics.w ||
			source->metrics.h != metrics.h) {
		PARTS_CG_SOURCE_FREE(source);
		return NULL;
	}

	struct parts_cg_prepared *prepared = calloc(1, sizeof(*prepared));
	if (!prepared) {
		PARTS_CG_SOURCE_FREE(source);
		return NULL;
	}
	prepared->source = source;
	prepared->no = no;
	prepared->bytes = 2 * rgba;
	prepared->name = string_dup(name);
	if (!prepared->name) {
		PARTS_CG_SOURCE_FREE(source);
		free(prepared);
		return NULL;
	}
	return prepared;
}

/*
 * Worker thread only. Makes an exclusive full-image RGBA copy. It does not
 * touch archive, Parts, VM, strings, GL or global error state.
 */
static bool parts_cg_prepare_pixels(struct parts_cg_prepared *p)
{
	if (!p || !p->source || !p->source->pixels)
		return false;
	int w = p->source->metrics.w;
	int h = p->source->metrics.h;
	if (w < 1 || w > 8192 || h < 1 || h > 8192)
		return false;
	size_t bytes = (size_t)w * (size_t)h * 4;
	if (bytes > PARTS_CG_RGBA_MAX)
		return false;
	return PARTS_CG_PIXEL_COPY(p->source->pixels, bytes, w, h, false,
			0, 0, 0, 0, &p->pixels);
}

/* Main thread, or after joining the worker. */
static void parts_cg_prepared_free(struct parts_cg_prepared *p)
{
	if (!p)
		return;
	if (p->source)
		PARTS_CG_SOURCE_FREE(p->source);
	if (p->name)
		free_string(p->name);
	parts_flat_pixels_free(&p->pixels);
	free(p);
}

#endif /* PARTS_CG_PREPARE_INC */
