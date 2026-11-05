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

#include <string.h>
#include "system4/ain.h"
#include "system4/cg.h"
#include "system4/string.h"
#include "asset_manager.h"
#include "hll.h"

static bool CGManager_Init(void *imain_system, int cg_cache_size)
{
	return true;
}

static bool CGManager_LoadArchive(struct string *archive_name)
{
	return asset_manager_load_archive(ASSET_CG, archive_name->text);
}

static bool CGManager_IsExist(struct string *cg_name)
{
	if (!cg_name)
		return false;
	return asset_exists_by_name(ASSET_CG, cg_name->text, NULL);
}

static bool CGManager_GetInfo(struct string *cg_name, int *width, int *height, int *bpp, bool *exist_pixel, bool *exist_alpha)
{
	struct cg_metrics metrics;
	if (!cg_name || !asset_cg_get_metrics_by_name(cg_name->text, &metrics))
		return false;
	*width = metrics.w;
	*height = metrics.h;
	*bpp = metrics.bpp;
	*exist_pixel = metrics.has_pixel;
	*exist_alpha = metrics.has_alpha;
	return true;
}

static void CGManager_GetSize(struct string *cg_name,int *width,int *height)
{
	struct cg_metrics metrics;
	if (!cg_name || !width || !height ||
		!asset_cg_get_metrics_by_name(cg_name->text,&metrics) || metrics.w<=0 || metrics.h<=0) {
		WARNING("CGManager.GetSize: image dimensions unavailable");
		return;
	}
	*width=metrics.w;
	*height=metrics.h;
}

/* Scalar dimensions have no failure out-parameter. Refuse unavailable or
 * corrupt metadata instead of publishing a fabricated image size. */
static int CGManager_GetDimension(struct string *cg_name, bool height)
{
	struct cg_metrics metrics = {0};
	if (!cg_name || cg_name->size < 0 ||
			memchr(cg_name->text, 0, cg_name->size) ||
			!asset_cg_get_metrics_by_name(cg_name->text, &metrics) ||
			metrics.w <= 0 || metrics.h <= 0)
		VM_ERROR("CGManager.Get%s: image dimensions unavailable", height ? "Height" : "Width");
	return height ? metrics.h : metrics.w;
}

static int CGManager_GetWidth(struct string *cg_name)
{
	return CGManager_GetDimension(cg_name, false);
}

static int CGManager_GetHeight(struct string *cg_name)
{
	return CGManager_GetDimension(cg_name, true);
}

extern struct static_library lib_CGManager;

static void CGManager_PreLink(void)
{
	if (!ain) return;
	for (int i = 0; i < ain->nr_libraries; i++) {
		struct ain_library *lib = &ain->libraries[i];
		if (strcmp(lib->name, "CGManager")) continue;
		for (int j = 0; j < lib->nr_functions; j++) {
			struct ain_hll_function *f = &lib->functions[j];
			bool width = !strcmp(f->name, "GetWidth");
			if (!width && strcmp(f->name, "GetHeight")) continue;
			bool valid = f->return_type.data == AIN_INT && !f->return_type.rank &&
				!f->return_type.array_type && f->nr_arguments == 1 && f->arguments &&
				f->arguments[0].type.data == AIN_STRING && !f->arguments[0].type.rank &&
				!f->arguments[0].type.array_type;
			static_library_replace(&lib_CGManager, f->name, valid ?
				(width ? CGManager_GetWidth : CGManager_GetHeight) : NULL);
		}
	}
}

//static int CGManager_GetCountOfDataFromArchive(void);
//static void CGManager_GetTitleByIndexFromArchive(int index, struct string **cg_name);
//static int CGManager_SearchTitleFromArchive(struct string *cg_name);
//static int CGManager_PrefixSearchTitleFromArchive(struct string *cg_name);
//static int CGManager_SuffixSearchTitleFromArchive(struct string *cg_name);
//static int CGManager_GetCountOfSearchDataFromArchive(void);
//static void CGManager_GetSearchTitleByIndexFromArchive(int index, struct string **cg_name);

HLL_LIBRARY(CGManager,
	    HLL_EXPORT(_PreLink, CGManager_PreLink),
	    HLL_EXPORT(Init, CGManager_Init),
	    HLL_EXPORT(LoadArchive, CGManager_LoadArchive),
	    HLL_EXPORT(IsExist, CGManager_IsExist),
	    HLL_EXPORT(GetInfo, CGManager_GetInfo),
	    HLL_EXPORT(GetSize, CGManager_GetSize),
	    HLL_EXPORT(GetWidth, CGManager_GetWidth),
	    HLL_EXPORT(GetHeight, CGManager_GetHeight),
	    HLL_TODO_EXPORT(GetCountOfDataFromArchive, CGManager_GetCountOfDataFromArchive),
	    HLL_TODO_EXPORT(GetTitleByIndexFromArchive, CGManager_GetTitleByIndexFromArchive),
	    HLL_TODO_EXPORT(SearchTitleFromArchive, CGManager_SearchTitleFromArchive),
	    HLL_TODO_EXPORT(PrefixSearchTitleFromArchive, CGManager_PrefixSearchTitleFromArchive),
	    HLL_TODO_EXPORT(SuffixSearchTitleFromArchive, CGManager_SuffixSearchTitleFromArchive),
	    HLL_TODO_EXPORT(GetCountOfSearchDataFromArchive, CGManager_GetCountOfSearchDataFromArchive),
	    HLL_TODO_EXPORT(GetSearchTitleByIndexFromArchive, CGManager_GetSearchTitleByIndexFromArchive));

