#ifndef PARTS_FLAT_PIXELS_INC
#define PARTS_FLAT_PIXELS_INC

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

struct parts_flat_pixels {
	int w;
	int h;
	uint8_t *rgba;
};

#define PARTS_FLAT_PIXELS_MAX_BYTES ((size_t)64u * 1024u * 1024u)

/*
 * Copy a source RGBA image or an atlas rectangle into a new owned buffer.
 *
 * non-atlas: copy the whole src_w*src_h image and ignore x/y/w/h.
 * atlas:     require a non-empty rectangle wholly inside the source; the
 *            source coordinates are checked by subtraction so adding x+w or
 *            y+h cannot overflow.
 *
 * The output is published only after every validation and the malloc/copy
 * succeed. The source is never retained as an alias.
 */
static bool parts_flat_pixels_copy(const uint8_t *src, size_t src_size,
		int src_w, int src_h, bool atlas,
		uint32_t x, uint32_t y, uint32_t w, uint32_t h,
		struct parts_flat_pixels *out)
{
	if (!src || !out || out->rgba)
		return false;
	if (src_w < 1 || src_w > 8192 || src_h < 1 || src_h > 8192)
		return false;

	size_t sw = (size_t)src_w;
	size_t sh = (size_t)src_h;
	if (sw > SIZE_MAX / 4)
		return false;
	size_t src_row = sw * 4;
	if (sh > SIZE_MAX / src_row)
		return false;
	size_t src_bytes = src_row * sh;
	if (src_bytes > PARTS_FLAT_PIXELS_MAX_BYTES)
		return false;
	if (src_size < src_bytes)
		return false;

	size_t out_w, out_h, src_x, src_y;
	if (atlas) {
		if (w == 0 || h == 0)
			return false;
		if (x >= (uint32_t)src_w || y >= (uint32_t)src_h)
			return false;
		if (w > (uint32_t)src_w - x || h > (uint32_t)src_h - y)
			return false;
		out_w = (size_t)w;
		out_h = (size_t)h;
		src_x = (size_t)x;
		src_y = (size_t)y;
	} else {
		out_w = sw;
		out_h = sh;
		src_x = 0;
		src_y = 0;
	}

	if (out_w > SIZE_MAX / 4)
		return false;
	size_t out_row = out_w * 4;
	if (out_h > SIZE_MAX / out_row)
		return false;
	size_t out_bytes = out_row * out_h;
	if (out_bytes > PARTS_FLAT_PIXELS_MAX_BYTES)
		return false;

	uint8_t *rgba = malloc(out_bytes);
	if (!rgba)
		return false;

	if (atlas) {
		for (size_t row = 0; row < out_h; row++) {
			const uint8_t *source = src + (src_y + row) * src_row + src_x * 4;
			memcpy(rgba + row * out_row, source, out_row);
		}
	} else {
		memcpy(rgba, src, out_bytes);
	}

	out->w = (int)out_w;
	out->h = (int)out_h;
	out->rgba = rgba;
	return true;
}

static void parts_flat_pixels_free(struct parts_flat_pixels *out)
{
	if (!out)
		return;
	free(out->rgba);
	out->rgba = NULL;
	out->w = 0;
	out->h = 0;
}

#endif /* PARTS_FLAT_PIXELS_INC */
