/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include <math.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_BITMAP_H
#include FT_ADVANCES_H
#include <SDL.h>

#include "system4.h"
#include "system4/hashtable.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "gfx/gfx.h"
#include "gfx/font.h"
#include "gfx/font_cache_metrics.h"
#include "xsystem4.h"

#define MAX_FONT_SIZES 64

struct font_ft {
	struct font super;
	FT_Face font;
	unsigned current_size;
	bool fixed_half_width;
	unsigned nr_sizes;
	struct font_size sizes[MAX_FONT_SIZES];
};

static FT_Library ft_lib;

static bool is_half_width(uint32_t code)
{
	// ASCII characters
	if (code < 128)
		return true;
	// Latin letters
	if (0xbf <= code && code <= 0xff && code != 0xd7 && code != 0xf7)
		return true;
	// Halfwidth punctuation / katakana
	if (0xff61 <= code && code <= 0xff9f)
		return true;
	return false;
}

void ft_font_init(void)
{
	if (FT_Init_FreeType(&ft_lib)) {
		ERROR("Failed to initialize FreeType");
	}
}

static struct font_size *ft_font_get_size(struct font *_font, float size)
{
	struct font_ft *font = (struct font_ft*)_font;
	size = roundf(size);
	for (int i = 0; i < font->nr_sizes; i++) {
		if (fabsf(font->sizes[i].size - size) < 0.01) {
			return &font->sizes[i];
		}
	}

	if (font->nr_sizes + 1 >= MAX_FONT_SIZES)
		ERROR("Exceeded maximum number of font sizes");
	struct font_size *fs = &font->sizes[font->nr_sizes++];
	fs->size = size;
	fs->y_offset = -roundf(size * 0.15f);
	fs->font = _font;
	fs->glyph_table = ht_create(4096);
	return fs;
}

static float ft_font_get_actual_size(struct font *_, float size)
{
	return roundf(size);
}

// NOTE: we create glyph textures slightly larger than necessary so that outlines
//       aren't cut off at the texture boundary.
// FIXME: the outline rendering code should be fixed so this isn't necessary.
#define GLYPH_BORDER_SIZE 4

// Convert a glyph rendered by FreeType to a block-sized texture.
// Block size is size x 1.5*size (full-width) or size/2 x 1.5*size (half-width)
// Area-average a wide fallback glyph into its existing fixed half-width cell.
// Only explicitly opted-in fallback font objects use this path.
static uint8_t fixed_cell_pixel(FT_Bitmap *glyph, int row, int col,
		int bearing, int natural_width, int cell_width)
{
	double begin = (double)col * natural_width / cell_width;
	double end = (double)(col + 1) * natural_width / cell_width;
	double coverage = 0;
	int pitch = glyph->pitch < 0 ? -glyph->pitch : glyph->pitch;
	int source_row = glyph->pitch < 0 ? (int)glyph->rows - 1 - row : row;
	const uint8_t *pixels = glyph->buffer + source_row * pitch;
	for (int source = (int)floor(begin); source < (int)ceil(end); source++) {
		int x = source - bearing;
		if (x < 0 || x >= (int)glyph->width) continue;
		double amount = fmin(end, source + 1.) - fmax(begin, source);
		if (amount > 0) coverage += pixels[x] * amount;
	}
	return (uint8_t)lround(coverage / (end - begin));
}

static Rectangle init_glyph_texture(Texture *dst, FT_Bitmap *glyph, int bitmap_left, int bitmap_top, int size, bool half_width, bool fixed_half_width)
{
	// calculate block size and offsets
	int natural_width = max(half_width ? size/2 : size, max(0, bitmap_left) + glyph->width);
	int cell_width = max(1, size / 2);
	bool fit = fixed_half_width && half_width && natural_width > cell_width;
	int block_width = fit ? cell_width : natural_width;
	int block_height = size + size/2;
	int width = block_width + GLYPH_BORDER_SIZE*2;
	int height = block_height + GLYPH_BORDER_SIZE*2;
	int off_x = max(0, bitmap_left) + GLYPH_BORDER_SIZE;
	int off_y = max(0, size - bitmap_top) + GLYPH_BORDER_SIZE;
	uint8_t *bitmap = xcalloc(1, width * height);

	// Expand the glyph bitmap to block size
	for (int row = 0; row < glyph->rows && row + off_y < height; row++) {
		if (fit) {
			for (int col = 0; col < block_width; col++)
				bitmap[(row+off_y)*width + col + GLYPH_BORDER_SIZE] =
					fixed_cell_pixel(glyph, row, col, max(0, bitmap_left), natural_width, block_width);
		} else {
			for (int col = 0; col < glyph->width && col + off_x < width; col++) {
				uint8_t p = glyph->buffer[row*glyph->width + col];
				bitmap[(row+off_y)*width + (col+off_x)] = p;
			}
		}
	}

	// create texture from block-size bitmap
	gfx_init_texture_rmap(dst, width, height, bitmap);
	free(bitmap);

	return (Rectangle) {
		.x = GLYPH_BORDER_SIZE,
		.y = GLYPH_BORDER_SIZE,
		.w = block_width,
		.h = block_height
	};
}

static void ft_font_set_size(struct font_ft *font, unsigned size)
{
	if (font->current_size != size && FT_Set_Pixel_Sizes(font->font, 0, size)) {
		WARNING("Failed setting font size to %u", size);
	}
	font->current_size = size;
}

static bool ft_font_get_glyph(struct font_size *size, struct glyph *glyph, uint32_t code, enum font_weight weight)
{
	Texture *t = &glyph->t[weight];

	// render bitmap
	bool half_width = is_half_width(code);
	struct font_ft *font = (struct font_ft*)size->font;
	ft_font_set_size(font, size->size);
	if (FT_Load_Char(font->font, code, FT_LOAD_RENDER)) {
		WARNING("Failed to load glyph for codepoint 0x%x", code);
		return false;
	}
	if (weight != FONT_WEIGHT_NORMAL) {
		int bold_weight = weight == FONT_WEIGHT_HEAVY ? 128 : 64;
		FT_GlyphSlot_Own_Bitmap(font->font->glyph);
		FT_Bitmap_Embolden(ft_lib, &font->font->glyph->bitmap, bold_weight, 0);
	}

	// create texture from bitmap
	FT_Bitmap *bitmap = &font->font->glyph->bitmap;
	if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY) {
		glyph->rect = init_glyph_texture(t, bitmap, font->font->glyph->bitmap_left,
				font->font->glyph->bitmap_top, size->size, half_width, font->fixed_half_width);
	} else if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO) {
		FT_Bitmap tmp;
		FT_Bitmap_New(&tmp);
		if (FT_Bitmap_Convert(ft_lib, &font->font->glyph->bitmap, &tmp, 1)) {
			WARNING("Failed to convert monochrome glyph to grayscale");
			FT_Bitmap_Done(ft_lib, &tmp);
			return false;
		}
		// round non-zero values up to 255 because FreeType doesn't
		for (int i = 0; i < tmp.rows * tmp.width; i++) {
			if (tmp.buffer[i])
				tmp.buffer[i] = 255;
		}
		glyph->rect = init_glyph_texture(t, &tmp, font->font->glyph->bitmap_left,
				font->font->glyph->bitmap_top, size->size, half_width, font->fixed_half_width);
		FT_Bitmap_Done(ft_lib, &tmp);
	} else {
		WARNING("Font returned glyph with unsupported pixel mode");
		return false;
	}

	glyph->advance = half_width ? size->size / 2 : size->size;
	return true;
}

static float ft_font_size_char(struct font_size *size, uint32_t code)
{
	return is_half_width(code) ? size->size / 2 : size->size;
}

static float ft_font_size_char_kerning(struct font_size *size, uint32_t code,
		uint32_t code_next)
{
	struct font_ft *font = (struct font_ft*)size->font;
	ft_font_set_size(font, size->size);

	FT_UInt index = FT_Get_Char_Index(font->font, code);
	FT_UInt next_index = FT_Get_Char_Index(font->font, code_next);

	FT_Fixed advance;
	FT_Get_Advance(font->font, index, FT_LOAD_DEFAULT, &advance);

	if (!FT_HAS_KERNING(font->font)) {
		return advance / 65536.f;
	}

	FT_Vector delta;
	FT_Get_Kerning(font->font, index, next_index, FT_KERNING_DEFAULT, &delta);

	return (advance / 65536.f) + (delta.x / 64.f);
}

struct font *ft_font_load(const char *path)
{
	struct font_ft *font = xcalloc(1, sizeof(struct font_ft));

#ifdef __ANDROID__
	// On Android, path may be an asset name which FT_New_Face cannot read
	// directly, so use SDL_LoadFile to load the content into memory.
	size_t size;
	void *buf = SDL_LoadFile(path, &size);
	if (!buf) {
		free(font);
		return NULL;
	}
	if (FT_New_Memory_Face(ft_lib, buf, size, 0, &font->font)) {
		free(buf);
		free(font);
		return NULL;
	}
#else
	if (FT_New_Face(ft_lib, path, 0, &font->font)) {
		free(font);
		return NULL;
	}
#endif

	if (!font->font->charmap) {
		WARNING("Font '%s' doesn't contain unicode charmap", path);
#ifdef __ANDROID__
		free(buf);
#endif
		free(font);
		return NULL;
	}

	font->super.get_size = ft_font_get_size;
	font->super.get_actual_size = ft_font_get_actual_size;
	font->super.get_actual_size_round_down = ft_font_get_actual_size;
	font->super.get_glyph = ft_font_get_glyph;
	font->super.size_char = ft_font_size_char;
	font->super.size_char_kerning = ft_font_size_char_kerning;
	return &font->super;
}

struct font *ft_font_load_fixed_cells(const char *path)
{
	struct font *font = ft_font_load(path);
	if (font) ((struct font_ft *)font)->fixed_half_width = true;
	return font;
}

/* Inspect only already-created FreeType sizes. Never call get_size here. */
bool ft_font_visit_cached_sizes(const struct font *_font,
        void (*visit)(const struct font_size *, void *), void *data)
{
    if (!_font || !visit || _font->get_size != ft_font_get_size)
        return false;
    const struct font_ft *font = (const struct font_ft *)_font;
    if (font->nr_sizes > MAX_FONT_SIZES)
        return false;
    for (unsigned i = 0; i < font->nr_sizes; i++)
        visit(&font->sizes[i], data);
    return true;
}
