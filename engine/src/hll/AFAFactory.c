/* Copyright (C) 2026 kichikuou <KichikuouChrome@gmail.com>
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
#include "system4.h"
#include "system4/archive.h"
#include "system4/hashtable.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "asset_manager.h"
#include "hll.h"
#include "vm.h"

static enum asset_type get_asset_type(int type)
{
	switch (type) {
	case 2: return ASSET_CG;
	case 3: return ASSET_FLAT;
	case 5: return ASSET_SOUND;
	default: return -1;
	}
}

HLL_WARN_UNIMPLEMENTED(false, bool, AFAFactory, IsExistArchive, struct string *ArchiveName);

bool AFAFactory_LoadArchive(int type, struct string *archive_name)
{
	enum asset_type t = get_asset_type(type);
	if (t < 0) {
		WARNING("Unknown asset type: %d", type);
		return false;
	}
	return asset_manager_load_archive(t, archive_name->text);
}

struct afa_title_catalog {
	uint64_t generation;
	bool loaded;
	int count, capacity, matches_count;
	struct string **titles;
	char **keys;
	int *matches;
	struct hash_table *seen;
};
static struct afa_title_catalog catalogs[ASSET_TYPE_MAX];

static void catalog_clear(struct afa_title_catalog *c)
{
	for (int i = 0; i < c->count; i++) { free_string(c->titles[i]); free(c->keys[i]); }
	free(c->titles); free(c->keys); free(c->matches);
	if (c->seen) ht_free(c->seen);
	memset(c, 0, sizeof(*c));
}

static void catalog_add(const char *name, void *user)
{
	struct afa_title_catalog *c = user;
	char *key = archive_basename(name);
	if (ht_get(c->seen, key, NULL)) { free(key); return; }
	ht_put(c->seen, key, c);
	if (c->count == c->capacity) {
		c->capacity = c->capacity ? c->capacity * 2 : 256;
		c->titles = xrealloc(c->titles, c->capacity * sizeof(*c->titles));
		c->keys = xrealloc(c->keys, c->capacity * sizeof(*c->keys));
	}
	/* Resource titles use the same extension-free namespace as Parts lookup.
	 * Keep the original spelling and directory for script string comparisons. */
	const char *dot = strrchr(name, '.');
	c->titles[c->count] = make_string(name, dot ? (size_t)(dot-name) : strlen(name));
	c->keys[c->count++] = key;
}

static struct afa_title_catalog *catalog_get(int type)
{
	enum asset_type asset = get_asset_type(type);
	if (asset < 0) VM_ERROR("Unsupported AFA catalog type: %d", type);
	struct afa_title_catalog *c = &catalogs[asset];
	uint64_t generation = asset_manager_generation();
	if (!c->loaded || c->generation != generation) {
		catalog_clear(c); c->seen = ht_create(1024);
		asset_foreach_name(asset, catalog_add, c);
		c->matches = xcalloc(c->count ? c->count : 1, sizeof(*c->matches));
		c->generation = generation; c->loaded = true;
	}
	return c;
}

static int AFAFactory_GetCountOfData(int type) { return catalog_get(type)->count; }
static struct string *AFAFactory_GetTitleByIndex(int type, int index)
{
	struct afa_title_catalog *c = catalog_get(type);
	return string_dup(index >= 0 && index < c->count ? c->titles[index] : &EMPTY_STRING);
}

static int catalog_search(int type, struct string *name, int mode)
{
	struct afa_title_catalog *c = catalog_get(type);
	char *query = xstrdup(name->text); sjis_normalize_path(query);
	size_t length = strlen(query); c->matches_count = 0;
	for (int i = 0; i < c->count; i++) {
		size_t n = strlen(c->keys[i]);
		bool matches = mode == 1 ? !strncmp(c->keys[i], query, length) :
			mode == 2 ? n >= length && !strcmp(c->keys[i]+n-length, query) :
			strstr(c->keys[i], query) != NULL;
		if (matches) c->matches[c->matches_count++] = i;
	}
	free(query); return c->matches_count;
}
static int AFAFactory_SearchTitle(int type, struct string *name) { return catalog_search(type, name, 0); }
static int AFAFactory_PrefixSearchTitle(int type, struct string *name) { return catalog_search(type, name, 1); }
static int AFAFactory_SuffixSearchTitle(int type, struct string *name) { return catalog_search(type, name, 2); }
static int AFAFactory_GetCountOfSearchData(int type) { return catalog_get(type)->matches_count; }
static struct string *AFAFactory_GetSearchTitleByIndex(int type, int index)
{
	struct afa_title_catalog *c = catalog_get(type);
	return string_dup(index >= 0 && index < c->matches_count ? c->titles[c->matches[index]] : &EMPTY_STRING);
}

HLL_LIBRARY(AFAFactory,
	    HLL_EXPORT(IsExistArchive, AFAFactory_IsExistArchive),
	    HLL_EXPORT(LoadArchive, AFAFactory_LoadArchive),
	    HLL_EXPORT(GetCountOfData, AFAFactory_GetCountOfData),
	    HLL_EXPORT(GetTitleByIndex, AFAFactory_GetTitleByIndex),
	    HLL_EXPORT(SearchTitle, AFAFactory_SearchTitle),
	    HLL_EXPORT(PrefixSearchTitle, AFAFactory_PrefixSearchTitle),
	    HLL_EXPORT(SuffixSearchTitle, AFAFactory_SuffixSearchTitle),
	    HLL_EXPORT(GetCountOfSearchData, AFAFactory_GetCountOfSearchData),
	    HLL_EXPORT(GetSearchTitleByIndex, AFAFactory_GetSearchTitleByIndex)
	    );
