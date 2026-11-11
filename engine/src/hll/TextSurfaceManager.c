/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <limits.h>
#include <math.h>
#include <string.h>

#include "system4/ain.h"
#include "system4/string.h"
#include "gfx/font.h"
#include "hll.h"

static bool TextSurfaceManager_GetFontWidth(struct string *text, int *width,
		int type, int size, int r, int g, int b, float bold, float edge,
		int edge_r, int edge_g, int edge_b)
{
	if (!text || !width || type < 0 || size <= 0 ||
			!isfinite(bold) || bold < 0 || !isfinite(edge) || edge < 0)
		return false;
	struct text_style style = {
		.face = type, .size = size, .bold_width = bold, .weight = 0,
		.edge_left = edge, .edge_up = edge, .edge_right = edge, .edge_down = edge,
		.color = {r, g, b, 255}, .edge_color = {edge_r, edge_g, edge_b, 255},
		.scale_x = 1.f, .space_scale_x = 1.f,
	};
	gfx_font_init();
	float measured = ceilf(gfx_size_text(&style, text->text));
	if (!isfinite(measured) || measured < 0 || (double)measured > INT_MAX)
		return false;
	*width = (int)measured;
	return true;
}

extern struct static_library lib_TextSurfaceManager;

static void TextSurfaceManager_PreLink(void)
{
	static const enum ain_data_type types[] = {
		AIN_STRING, AIN_REF_INT, AIN_INT, AIN_INT, AIN_INT, AIN_INT, AIN_INT,
		AIN_FLOAT, AIN_FLOAT, AIN_INT, AIN_INT, AIN_INT,
	};
	if (!ain) return;
	for (int i = 0; i < ain->nr_libraries; i++) {
		struct ain_library *lib = &ain->libraries[i];
		if (strcmp(lib->name, "TextSurfaceManager")) continue;
		for (int j = 0; j < lib->nr_functions; j++) {
			struct ain_hll_function *f = &lib->functions[j];
			if (strcmp(f->name, "GetFontWidth")) continue;
			bool valid = f->return_type.data == AIN_BOOL && !f->return_type.rank &&
				!f->return_type.array_type &&
				f->nr_arguments == 12 && f->arguments;
			for (int k = 0; valid && k < 12; k++)
				valid = f->arguments[k].type.data == types[k] &&
					f->arguments[k].type.rank == 0 && !f->arguments[k].type.array_type;
			static_library_replace(&lib_TextSurfaceManager, "GetFontWidth",
				valid ? TextSurfaceManager_GetFontWidth : NULL);
		}
	}
}

HLL_LIBRARY(TextSurfaceManager,
	HLL_EXPORT(_PreLink, TextSurfaceManager_PreLink),
	HLL_EXPORT(GetFontWidth, TextSurfaceManager_GetFontWidth));
