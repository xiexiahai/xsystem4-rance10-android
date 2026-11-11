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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/cg.h"
#include "system4/string.h"

#include "xsystem4.h"
#include "asset_manager.h"
#include "parts.h"
#include "parts_internal.h"

static struct parts_construction_process *get_cproc(int parts_no, int state)
{
	return parts_get_construction_process(parts_get(parts_no), state);
}


bool parts_cp_amap_gradation_valid(int x, int y, int w, int h, int up_a, int down_a)
{
	return x >= -4096 && x <= 4096 && y >= -4096 && y <= 4096 &&
		w >= 1 && w <= 4096 && h >= 1 && h <= 4096 &&
		up_a >= 0 && up_a <= 255 && down_a >= 0 && down_a <= 255;
}
void parts_cp_op_free(struct parts_cp_op *op)
{
	switch (op->type) {
	case PARTS_CP_CREATE:
	case PARTS_CP_MULTIPLY_AMAP_GRADATION_UD:
	case PARTS_CP_CREATE_PIXEL_ONLY:
	case PARTS_CP_FILL:
	case PARTS_CP_FILL_ALPHA_COLOR:
	case PARTS_CP_FILL_AMAP:
	case PARTS_CP_FILL_WITH_ALPHA:
	case PARTS_CP_DRAW_RECT:
	case PARTS_CP_GRAY_FILTER:
		break;
	case PARTS_CP_CG:
		free_string(op->cg.name);
		break;
	case PARTS_CP_DRAW_CUT_CG:
	case PARTS_CP_COPY_CUT_CG:
	case PARTS_CP_ALPHA_BLEND_CUT_CG:
	case PARTS_CP_ONLY_ALPHA_CUT_CG:
		free_string(op->cut_cg.cg_name);
		break;
	case PARTS_CP_DRAW_TEXT:
	case PARTS_CP_COPY_TEXT:
	case PARTS_CP_ALPHA_BLEND_TEXT:
		free_string(op->text.text);
		break;
	}
	free(op);
}

void parts_add_cp_op(struct parts_construction_process *cproc, struct parts_cp_op *op)
{
	TAILQ_INSERT_TAIL(&cproc->ops, op, entry);
}

bool PE_ClearPartsConstructionProcess(int parts_no, int state);

bool PE_AddCreateToPartsConstructionProcess(int parts_no, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_CREATE;
	op->create.w = w;
	op->create.h = h;
	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddCreatePixelOnlyToPartsConstructionProcess(int parts_no, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_CREATE_PIXEL_ONLY;
	op->create.w = w;
	op->create.h = h;
	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddCreateCGToProcess(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;

	if (!asset_exists_by_name(ASSET_CG, cg_name->text, NULL)) {
		WARNING("Invalid CG name: %s", display_sjis0(cg_name->text));
		return false;
	}

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_CG;
	op->cg.name = string_ref(cg_name);
	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddFillToPartsConstructionProcess(int parts_no, int x, int y, int w, int h, int r, int g, int b, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_FILL;
	op->fill = (struct parts_cp_fill) {
		.x = x, .y = y, .w = w, .h = h,
		.r = r, .g = g, .b = b, .a = 255
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddFillAlphaColorToPartsConstructionProcess(int parts_no, int x, int y, int w, int h, int r, int g, int b, int a, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_FILL_ALPHA_COLOR;
	op->fill = (struct parts_cp_fill) {
		.x = x, .y = y, .w = w, .h = h,
		.r = r, .g = g, .b = b, .a = a
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddFillAMapToPartsConstructionProcess(int parts_no, int x, int y, int w, int h, int a, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_FILL_AMAP;
	op->fill = (struct parts_cp_fill) {
		.x = x, .y = y, .w = w, .h = h,
		.r = 0, .g = 0, .b = 0, .a = a
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddFillWithAlphaToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int a, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_FILL_WITH_ALPHA;
	op->fill = (struct parts_cp_fill) {
		.x = x, .y = y, .w = w, .h = h,
		.r = r, .g = g, .b = b, .a = a
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddMultiplyAMapGradationUDToPartsConstructionProcess(int parts_no, int x, int y,
		int w, int h, int up_a, int down_a, int state)
{
	if (state < 1 || state > PARTS_NR_STATES ||
			!parts_cp_amap_gradation_valid(x, y, w, h, up_a, down_a))
		return false;
	struct parts_construction_process *cproc = get_cproc(parts_no, state - 1);
	struct parts_cp_op *op = xcalloc(1, sizeof(*op));
	op->type = PARTS_CP_MULTIPLY_AMAP_GRADATION_UD;
	op->amap_grad = (struct parts_cp_amap_gradation){x, y, w, h, up_a, down_a};
	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddFillGradationHorizonToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int top_r, int top_g, int top_b, int bot_r, int bot_g, int bot_b, int state);

bool PE_AddDrawRectToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_DRAW_RECT;
	op->fill = (struct parts_cp_fill) {
		.x = x, .y = y, .w = w, .h = h,
		.r = r, .g = g, .b = b, .a = 255
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddDrawCutCGToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, int state)
{
	if (!parts_state_valid(--state))
		return false;

	if (!asset_exists_by_name(ASSET_CG, cg_name->text, NULL)) {
		WARNING("Invalid CG name: %s", display_sjis0(cg_name->text));
		return false;
	}

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_DRAW_CUT_CG;
	op->cut_cg = (struct parts_cp_cut_cg) {
		.cg_name = string_ref(cg_name),
		.dx = dx, .dy = dy, .dw = dw, .dh = dh,
		.sx = sx, .sy = sy, .sw = sw, .sh = sh,
		.interp_type = interp_type
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddDrawCGToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int state)
{
	struct cg_metrics metrics;
	if (!parts_state_valid(state - 1) || !cg_name || cg_name->size <= 0 ||
			!asset_cg_get_metrics_by_name(cg_name->text, &metrics) ||
			metrics.w < 1 || metrics.w > 4096 || metrics.h < 1 || metrics.h > 4096)
		return false;
	// A whole-CG blend is the same persisted cut operation with source origin
	// zero and native dimensions. The destination canvas is not recreated.
	return PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name,
			dx, dy, metrics.w, metrics.h, 0, 0, metrics.w, metrics.h, 0, state);
}

bool PE_AddCopyCutCGToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, int state)
{
	if (!parts_state_valid(--state))
		return false;

	if (!asset_exists_by_name(ASSET_CG, cg_name->text, NULL)) {
		WARNING("Invalid CG name: %s", display_sjis0(cg_name->text));
		return false;
	}

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_COPY_CUT_CG;
	op->cut_cg = (struct parts_cp_cut_cg) {
		.cg_name = string_ref(cg_name),
		.dx = dx, .dy = dy, .dw = dw, .dh = dh,
		.sx = sx, .sy = sy, .sw = sw, .sh = sh,
		.interp_type = interp_type
	};

	parts_add_cp_op(cproc, op);
	return true;
}

static bool add_alpha_cut_cg(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh,
		int interp_type, int state, enum parts_cp_op_type type)
{
	if (!parts_state_valid(--state))
		return false;
	if (!asset_exists_by_name(ASSET_CG, cg_name->text, NULL))
		return false;
	struct parts_cp_op *op = xcalloc(1, sizeof(*op));
	op->type = type;
	op->cut_cg = (struct parts_cp_cut_cg) {
		.cg_name = string_ref(cg_name),
		.dx = dx, .dy = dy, .dw = dw, .dh = dh,
		.sx = sx, .sy = sy, .sw = sw, .sh = sh,
		.interp_type = interp_type
	};
	parts_add_cp_op(get_cproc(parts_no, state), op);
	return true;
}

bool PE_AddAlphaBlendCutCGToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, int state)
{
	return add_alpha_cut_cg(parts_no, cg_name, dx, dy, dw, dh, sx, sy, sw, sh,
			interp_type, state, PARTS_CP_ALPHA_BLEND_CUT_CG);
}

bool PE_AddOnlyAlphaCutCGToPartsConstructionProcess(int parts_no, struct string *cg_name,
		int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int interp_type, int state)
{
	return add_alpha_cut_cg(parts_no, cg_name, dx, dy, dw, dh, sx, sy, sw, sh,
			interp_type, state, PARTS_CP_ONLY_ALPHA_CUT_CG);
}

bool PE_AddGrayFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		bool full_size, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_construction_process *cproc = get_cproc(parts_no, state);
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = PARTS_CP_GRAY_FILTER;
	op->filter = (struct parts_cp_filter) {
		.x = x, .y = y, .w = w, .h = h,
		.full_size = full_size
	};

	parts_add_cp_op(cproc, op);
	return true;
}

bool PE_AddAddFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, bool full_size, int state);
bool PE_AddMulFilterToPartsConstructionProcess(int parts_no, int x, int y, int w, int h,
		int r, int g, int b, bool full_size, int state);
bool PE_AddDrawLineToPartsConstructionProcess(int parts_no, int x1, int y1, int x2, int y2,
		int r, int g, int b, int a, int state);

static bool add_text_to_cproc(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state, enum parts_cp_op_type op_type)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = op_type;
	op->text = (struct parts_cp_text) {
		.text = string_dup(text),
		.x = x,
		.y = y,
		.line_space = line_space,
		.style = {
			.face = type,
			.size = size,
			.bold_width = bold_weight,
			.weight = 0,
			.edge_left = edge_weight,
			.edge_up = edge_weight,
			.edge_right = edge_weight,
			.edge_down = edge_weight,
			.color = { r, g, b, 255 },
			.edge_color = { edge_r, edge_g, edge_b, 255 },
			.scale_x = 1.0f,
			.space_scale_x = 1.0f,
			.font_spacing = char_space
		}
	};

	parts_add_cp_op(get_cproc(parts_no, state), op);
	return true;

}


bool PE_AddDrawTextToPartsConstructionProcess(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state)
{
	return add_text_to_cproc(parts_no, x, y, text, type, size, r, g, b, bold_weight,
			edge_r, edge_g, edge_b, edge_weight, char_space, line_space, state,
			PARTS_CP_DRAW_TEXT);
}

bool PE_AddCopyTextToPartsConstructionProcess(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state)
{
	return add_text_to_cproc(parts_no, x, y, text, type, size, r, g, b, bold_weight,
			edge_r, edge_g, edge_b, edge_weight, char_space, line_space, state,
			PARTS_CP_COPY_TEXT);
}
bool PE_AddAlphaBlendTextToPartsConstructionProcess(int parts_no, int x, int y, struct string *text,
		int type, int size, int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight,
		int char_space, int line_space, int state)
{
	return add_text_to_cproc(parts_no, x, y, text, type, size, r, g, b, bold_weight,
			edge_r, edge_g, edge_b, edge_weight, char_space, line_space, state,
			PARTS_CP_ALPHA_BLEND_TEXT);
}

static bool build_create(struct parts *parts, struct parts_construction_process *cproc,
		struct parts_cp_create *op)
{
	gfx_delete_texture(&cproc->common.texture);
	gfx_init_texture_rgba(&cproc->common.texture, op->w, op->h, (SDL_Color){0,0,0,255});
	if (!cproc->common.texture.handle)
		return false;
	parts_set_dims(parts, &cproc->common, op->w, op->h);
	return true;
}

static bool build_create_pixel_only(struct parts *parts, struct parts_construction_process *cproc,
		struct parts_cp_create *op)
{
	gfx_delete_texture(&cproc->common.texture);
	gfx_init_texture_rgb(&cproc->common.texture, op->w, op->h, (SDL_Color){0,0,0,255});
	if (!cproc->common.texture.handle)
		return false;
	parts_set_dims(parts, &cproc->common, op->w, op->h);
	return true;
}

static bool build_cg(struct parts *parts, struct parts_construction_process *cproc, struct parts_cp_cg *op)
{
	struct cg *cg = asset_cg_load_by_name(op->name->text, NULL);
	if (!cg)
		return false;
	gfx_delete_texture(&cproc->common.texture);
	gfx_init_texture_with_cg(&cproc->common.texture, cg);
	parts_set_dims(parts, &cproc->common, cg->metrics.w, cg->metrics.h);
	cg_free(cg);
	return true;
}

static bool build_fill(struct parts_construction_process *cproc, struct parts_cp_fill *op)
{
	if (!cproc->common.texture.handle)
		return false;
	gfx_fill(&cproc->common.texture, op->x, op->y, op->w, op->h, op->r, op->g, op->b);
	return true;
}

static bool build_fill_alpha_color(struct parts_construction_process *cproc, struct parts_cp_fill *op)
{
	if (!cproc->common.texture.handle)
		return false;
	gfx_fill_alpha_color(&cproc->common.texture, op->x, op->y, op->w, op->h, op->r, op->g, op->b, op->a);
	return true;
}

static bool build_fill_amap(struct parts_construction_process *cproc, struct parts_cp_fill *op)
{
	if (!cproc->common.texture.handle)
		return false;
	gfx_fill_amap(&cproc->common.texture, op->x, op->y, op->w, op->h, op->a);
	return true;
}

static bool build_fill_with_alpha(struct parts_construction_process *cproc, struct parts_cp_fill *op)
{
	if (!cproc->common.texture.handle)
		return false;
	gfx_fill_with_alpha(&cproc->common.texture, op->x, op->y, op->w, op->h, op->r, op->g, op->b, op->a);
	return true;
}

static bool build_draw_rect(struct parts_construction_process *cproc, struct parts_cp_fill *op)
{
	if (!cproc->common.texture.handle)
		return false;
	int x2 = op->x + op->w - 1;
	int y2 = op->y + op->h - 1;
	gfx_draw_line(&cproc->common.texture, op->x, op->y, x2, op->y, op->r, op->g, op->b);
	gfx_draw_line(&cproc->common.texture, op->x, op->y, op->x, y2, op->r, op->g, op->b);
	gfx_draw_line(&cproc->common.texture, x2, op->y, x2, y2, op->r, op->g, op->b);
	gfx_draw_line(&cproc->common.texture, op->x, y2, x2, y2, op->r, op->g, op->b);
	return true;
}

static bool build_draw_cut_cg(struct parts_construction_process *cproc, struct parts_cp_cut_cg *op)
{
	if (!cproc->common.texture.handle)
		return false;

	struct cg *cg = asset_cg_load_by_name(op->cg_name->text, NULL);
	if (!cg) return false;

	Texture src;
	gfx_init_texture_with_cg(&src, cg);
	cg_free(cg);

	gfx_copy_stretch_blend_amap(&cproc->common.texture, op->dx, op->dy, op->dw, op->dh,
			&src, op->sx, op->sy, op->sw, op->sh);
	gfx_delete_texture(&src);
	return true;
}

static bool build_copy_cut_cg(struct parts_construction_process *cproc, struct parts_cp_cut_cg *op,
		enum parts_cp_op_type type)
{
	if (!cproc->common.texture.handle)
		return false;

	struct cg *cg = asset_cg_load_by_name(op->cg_name->text, NULL);
	if (!cg) return false;

	Texture src;
	gfx_init_texture_with_cg(&src, cg);
	cg_free(cg);

	void (*copy)(Texture *, int, int, int, int, Texture *, int, int, int, int) =
		type == PARTS_CP_ALPHA_BLEND_CUT_CG ? gfx_copy_stretch_alpha_blend :
		type == PARTS_CP_ONLY_ALPHA_CUT_CG ? gfx_copy_stretch_amap :
		gfx_copy_stretch_with_alpha_map;
	copy(&cproc->common.texture, op->dx, op->dy, op->dw, op->dh,
			&src, op->sx, op->sy, op->sw, op->sh);
	gfx_delete_texture(&src);
	return true;
}

static bool build_text(struct parts_construction_process *cproc,struct parts_cp_text *op,bool copy)
{
	if (!cproc->common.texture.handle)
		return false;
	int height=ceilf(text_style_height(&op->style)),y=op->y;
	const char *text=op->text->text;
	while (*text) {
		const char *end=strpbrk(text,"\r\n");
		struct string *line=make_string(text,end?(size_t)(end-text):strlen(text));
		if (copy) {
			int width=ceilf(gfx_size_text(&op->style,line->text));
			gfx_fill_with_alpha(&cproc->common.texture,op->x,y,width,height,
				op->style.edge_color.r,op->style.edge_color.g,op->style.edge_color.b,0);
			gfx_clear_text_raster(&cproc->common.texture,op->x,y,line->text,&op->style);
		}
		gfx_render_text(&cproc->common.texture,op->x,y,line->text,&op->style,!copy);
		free_string(line);
		if (!end) break;
		if (*end=='\r' && end[1]=='\n') end++;
		text=end+1;
		y+=height+op->line_space;
	}
	return true;
}

static bool build_draw_text(struct parts_construction_process *cproc, struct parts_cp_text *op)
{
	return build_text(cproc,op,false);
}

static bool build_copy_text(struct parts_construction_process *cproc, struct parts_cp_text *op)
{
	return build_text(cproc,op,true);
}

static bool build_alpha_blend_text(struct parts_construction_process *cproc, struct parts_cp_text *op)
{
	if (!cproc->common.texture.handle) return false;
	int height = ceilf(text_style_height(&op->style)), y = op->y;
	const char *text = op->text->text;
	while (*text) {
		const char *end = strpbrk(text, "\r\n");
		struct string *line = make_string(text, end ? (size_t)(end-text) : strlen(text));
		bool ok = gfx_render_text_alpha_blend(&cproc->common.texture, op->x, y, line->text, &op->style);
		free_string(line);
		if (!ok) return false;
		if (!end) break;
		if (*end == '\r' && end[1] == '\n') end++;
		text = end + 1;
		y += height + op->line_space;
	}
	return true;
}

static bool build_gray_filter(struct parts_construction_process *cproc, struct parts_cp_filter *op)
{
	if (!cproc->common.texture.handle)
		return false;
	int x = op->x, y = op->y, w = op->w, h = op->h;
	if (op->full_size) {
		x = 0; y = 0;
		w = cproc->common.texture.w;
		h = cproc->common.texture.h;
	}

	gfx_copy_grayscale(&cproc->common.texture, x, y, &cproc->common.texture, x, y, w, h);
	return true;
}

bool parts_build_construction_process(struct parts *parts,
		struct parts_construction_process *cproc)
{
	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &cproc->ops, entry) {
		switch (op->type) {
		case PARTS_CP_CREATE:
			if (!build_create(parts, cproc, &op->create))
				return false;
			break;
		case PARTS_CP_CREATE_PIXEL_ONLY:
			if (!build_create_pixel_only(parts, cproc, &op->create))
				return false;
			break;
		case PARTS_CP_CG:
			if (!build_cg(parts, cproc, &op->cg))
				return false;
			break;
		case PARTS_CP_MULTIPLY_AMAP_GRADATION_UD:
			if (!cproc->common.texture.handle) return false;
			gfx_multiply_amap_gradation_ud(&cproc->common.texture, op->amap_grad.x,
				op->amap_grad.y, op->amap_grad.w, op->amap_grad.h,
				op->amap_grad.up_a, op->amap_grad.down_a);
			break;
		case PARTS_CP_FILL:
			if (!build_fill(cproc, &op->fill))
				return false;
			break;
		case PARTS_CP_FILL_ALPHA_COLOR:
			if (!build_fill_alpha_color(cproc, &op->fill))
				return false;
			break;
		case PARTS_CP_FILL_AMAP:
			if (!build_fill_amap(cproc, &op->fill))
				return false;
			break;
		case PARTS_CP_FILL_WITH_ALPHA:
			if (!build_fill_with_alpha(cproc, &op->fill))
				return false;
			break;
		case PARTS_CP_DRAW_RECT:
			if (!build_draw_rect(cproc, &op->fill))
				return false;
			break;
		case PARTS_CP_DRAW_CUT_CG:
			if (!build_draw_cut_cg(cproc, &op->cut_cg))
				return false;
			break;
		case PARTS_CP_COPY_CUT_CG:
		case PARTS_CP_ALPHA_BLEND_CUT_CG:
		case PARTS_CP_ONLY_ALPHA_CUT_CG:
			if (!build_copy_cut_cg(cproc, &op->cut_cg, op->type))
				return false;
			break;
		case PARTS_CP_DRAW_TEXT:
			if (!build_draw_text(cproc, &op->text))
				return false;
			break;
		case PARTS_CP_COPY_TEXT:
			if (!build_copy_text(cproc, &op->text))
				return false;
			break;
		case PARTS_CP_ALPHA_BLEND_TEXT:
			if (!build_alpha_blend_text(cproc, &op->text)) return false;
			break;
		case PARTS_CP_GRAY_FILTER:
			if (!build_gray_filter(cproc, &op->filter))
				return false;
			break;
		}
	}
	/* Only a successful replay beginning with a complete initializer can
	 * discard a previous, non-replayable pixel base. A later initializer is
	 * deliberately conservative: earlier ops might require that base. */
	struct parts_cp_op *first = TAILQ_FIRST(&cproc->ops);
	if (first && (first->type == PARTS_CP_CREATE ||
			first->type == PARTS_CP_CREATE_PIXEL_ONLY || first->type == PARTS_CP_CG))
		cproc->needs_pixel_snapshot = false;
	parts_dirty(parts);
	return true;
}


static bool construction_snapshot_size(int w, int h, size_t remaining)
{
    return w >= 1 && h >= 1 && w <= 8192 && h <= 8192 &&
        (size_t)w * h * 4 <= 64u * 1024u * 1024u &&
        (size_t)w * h * 4 <= remaining / 2;
}

/* Main thread only. Execute into a private surface while the live operation
 * list is stable, then freeze owned RGBA. No borrowed op/string/font/asset is
 * retained by the worker. Rendering itself remains on main for GL safety. */
struct cg *parts_construction_snapshot(struct parts *parts, int state, size_t remaining)
{
    if (!parts || state < 0 || state >= PARTS_NR_STATES || parts->widget ||
            parts->states[state].type != PARTS_CONSTRUCTION_PROCESS) return NULL;
    parts_textures_require_resident(parts);
    struct parts_construction_process *live = &parts->states[state].cproc;
    unsigned count = 0;
    struct parts_cp_op *op;
    TAILQ_FOREACH(op, &live->ops, entry) {
        if (++count > 4096 || op->type < 0 || op->type >= PARTS_NR_CP_TYPES) return NULL;
        if (op->type == PARTS_CP_CREATE || op->type == PARTS_CP_CREATE_PIXEL_ONLY) {
            if (!construction_snapshot_size(op->create.w, op->create.h, remaining)) return NULL;
        }
        struct string *name = op->type == PARTS_CP_CG ? op->cg.name :
            (op->type == PARTS_CP_DRAW_CUT_CG || op->type == PARTS_CP_COPY_CUT_CG ||
             op->type == PARTS_CP_ALPHA_BLEND_CUT_CG || op->type == PARTS_CP_ONLY_ALPHA_CUT_CG) ?
            op->cut_cg.cg_name : NULL;
        if (op->type == PARTS_CP_CG || op->type == PARTS_CP_DRAW_CUT_CG ||
                op->type == PARTS_CP_COPY_CUT_CG || op->type == PARTS_CP_ALPHA_BLEND_CUT_CG ||
                op->type == PARTS_CP_ONLY_ALPHA_CUT_CG) {
            struct cg_metrics metrics;
            if (!name || !name->size || !asset_cg_get_metrics_by_name(name->text, &metrics) ||
                    !construction_snapshot_size(metrics.w, metrics.h, remaining)) return NULL;
        }
    }
    if (glGetError() != GL_NO_ERROR) return NULL;
    struct parts_construction_process next = {.common = live->common, .ops = live->ops,
        .needs_pixel_snapshot = live->needs_pixel_snapshot};
    next.common.texture = (Texture){0};
    if (live->common.texture.handle) {
        Texture *texture = &live->common.texture;
        if (!construction_snapshot_size(texture->w, texture->h, remaining)) return NULL;
        struct parts_cp_op *first = TAILQ_FIRST(&live->ops);
        /* The first CREATE discards this private copy before reading it.
         * Keep the live size/budget check above even when no copy is needed. */
        if (!first || (first->type != PARTS_CP_CREATE &&
                first->type != PARTS_CP_CREATE_PIXEL_ONLY)) {
            void *pixels = gfx_get_pixels(texture);
            if (!pixels) return NULL;
            gfx_init_texture_with_pixels(&next.common.texture, texture->w, texture->h, pixels);
            free(pixels);
            if (!next.common.texture.handle || glGetError() != GL_NO_ERROR) {
                gfx_delete_texture(&next.common.texture);return NULL;
            }
        } else {
            /* Bounded evidence for an explicitly instrumented launch only. */
            static int trace = -1;
            static unsigned reports;
            if (trace < 0) {
                const char *value = getenv("XSYSTEM4_PERF_WINDOW");
                trace = value && !strcmp(value, "1");
            }
            if (trace && reports < 16) {
                reports++;
                NOTICE("[perf-cproc] old_copy_skipped bytes=%zu",
                    (size_t)texture->w * texture->h * 4);
            }
        }
    }
    struct parts shadow = *parts;
    shadow.parent = NULL;
    shadow.is_back_scene = true;
    shadow.sp.in_scene = false;
    bool ok = parts_build_construction_process(&shadow, &next);
    Texture *texture = &next.common.texture;
    if (!ok || !texture->handle || glGetError() != GL_NO_ERROR ||
            !construction_snapshot_size(texture->w, texture->h, remaining)) {
        gfx_delete_texture(texture);return NULL;
    }
    struct cg *image = calloc(1, sizeof(*image));
    if (!image) {gfx_delete_texture(texture);return NULL;}
    image->type = ALCG_UNKNOWN;
    image->metrics = (struct cg_metrics){.w=texture->w,.h=texture->h,.bpp=32,
        .has_pixel=true,.has_alpha=true,.pixel_pitch=texture->w*4,.alpha_pitch=texture->w};
    image->pixels = gfx_get_pixels(texture);
    gfx_delete_texture(texture);
    if (!image->pixels || glGetError() != GL_NO_ERROR) {cg_free(image);return NULL;}
    return image;
}

bool PE_BuildPartsConstructionProcess(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_construction_process *cproc = parts_get_construction_process(parts, state);
	bool ok = parts_build_construction_process(parts, cproc);
	if (ok) parts_flat_async_content_changed(parts, &cproc->common);
	return ok;
}

bool parts_clear_construction_process(struct parts_construction_process *cproc)
{
	while (!TAILQ_EMPTY(&cproc->ops)) {
		struct parts_cp_op *op = TAILQ_FIRST(&cproc->ops);
		TAILQ_REMOVE(&cproc->ops, op, entry);
		parts_cp_op_free(op);
	}
	return true;
}

bool PE_ClearPartsConstructionProcess(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_get(parts_no);
	struct parts_construction_process *cproc = parts_get_construction_process(parts, state);
	/* Internal clear is also a destructor/error helper: only the public
	 * command-discard operation changes snapshot provenance. The typed
	 * accessor above already made a parked surface resident. */
	if (!TAILQ_EMPTY(&cproc->ops) && cproc->common.texture.handle)
		cproc->needs_pixel_snapshot = true;
	return parts_clear_construction_process(cproc);
}

bool PE_SetPartsConstructionSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_construction_process *cproc = parts_get_construction_process(parts, state);
	parts_set_surface_area(parts, &cproc->common, x, y, w, h);
	return true;
}
