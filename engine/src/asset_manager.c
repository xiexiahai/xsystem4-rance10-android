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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <assert.h>
#ifdef __ANDROID__
#include <SDL.h>
#include <SDL_system.h>
#include <android/asset_manager_jni.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "system4.h"
#include "system4/ald.h"
#include "system4/afa.h"
#include "system4/ain.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/hashtable.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "xsystem4.h"
#include "asset_manager.h"
#include "gfx/font.h"
#include "vm.h"

enum archive_type {
	AR_TYPE_ALD,
	AR_TYPE_AFA,
	AR_TYPE_MAX
};

const char *asset_strtype(enum asset_type type)
{
	switch (type) {
	case ASSET_BGM:   return "BGM";
	case ASSET_SOUND: return "Sound";
	case ASSET_VOICE: return "Voice";
	case ASSET_CG:    return "CG";
	case ASSET_FLAT:  return "Flat";
	case ASSET_PACT:  return "Pact";
	case ASSET_DATA:  return "Data";
	case ASSET_FLASH: return "Flash";
	}
	return "Invalid";
}

#define MAX_ARCHIVES 8

struct asset_manager {
	bool (*load_archive)(struct asset_manager *manager, const char *name);
	bool (*exists_by_id)(struct asset_manager *manager, int id);
	bool (*exists_by_name)(struct asset_manager *manager, const char *name, int *id_out);
	struct archive_data *(*get_by_id)(struct asset_manager *manager, int id);
	struct archive_data *(*get_by_name)(struct asset_manager *manager, const char *name, int *id_out);
};

// XXX: also used for AFAv1 in Daiteikoku, Shaman's Sanctuary
struct asset_manager_ald {
	struct asset_manager manager;
	struct archive *archive;
};

struct asset_manager_afa {
	struct asset_manager manager;
	struct afa_archive *archives[MAX_ARCHIVES];
	// Borrowed aliases in original load order; numeric lookup keeps archives.
	struct afa_archive *name_archives[MAX_ARCHIVES];
	bool first_loaded_names;
};

static struct asset_manager *assets[ASSET_TYPE_MAX] = {0};
static uint64_t assets_generation;

static bool prefer_first_cg_names(enum asset_type type)
{
	if (type != ASSET_CG || !ain || ain->version < 11 || !config.ain_filename)
		return false;
	const char *name = config.ain_filename;
	for (const char *p = name; *p; p++)
		if (*p == '/' || *p == '\\') name = p + 1;
	// Project compatibility policy: this localized distribution stores its
	// translated duplicate CGs in the first archive. Other games keep the
	// existing newest-loaded precedence; this is not a general AIN12 rule.
	return !strcasecmp(name,"Rance10.ain");
}

#ifdef __ANDROID__
struct apk_mapping { void *base; size_t length; };

static void release_apk_mapping(void *owner)
{
	struct apk_mapping *mapping = owner;
	munmap(mapping->base, mapping->length);
	free(mapping);
}

static bool is_apk_archive(const char *name)
{
	const char *list = SDL_getenv("XSYSTEM4_APK_AFA");
	for (const char *p = list; p && *p;) {
		const char *end = strchr(p, ';');
		size_t length = end ? (size_t)(end - p) : strlen(p);
		if (strlen(name) == length && !strncmp(name, p, length))
			return true;
		p = end ? end + 1 : NULL;
	}
	return false;
}

static struct afa_archive *open_apk_archive(const char *name, int *error)
{
	*error = ARCHIVE_FILE_ERROR;
	JNIEnv *env = SDL_AndroidGetJNIEnv();
	jobject activity = SDL_AndroidGetActivity();
	if (!env || !activity)
		return NULL;
	jclass cls = (*env)->GetObjectClass(env, activity);
	jmethodID method = (*env)->GetMethodID(env, cls, "getAssets", "()Landroid/content/res/AssetManager;");
	jobject java_assets = (*env)->CallObjectMethod(env, activity, method);
	AAssetManager *manager = AAssetManager_fromJava(env, java_assets);
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "game/%s", name);
	AAsset *asset = manager ? AAssetManager_open(manager, path, AASSET_MODE_RANDOM) : NULL;
	(*env)->DeleteLocalRef(env, java_assets);
	(*env)->DeleteLocalRef(env, cls);
	(*env)->DeleteLocalRef(env, activity);
	if (!asset)
		return NULL;
	off64_t start = 0, length = 0;
	int fd = AAsset_openFileDescriptor64(asset, &start, &length);
	AAsset_close(asset);
	if (fd < 0) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "APK archive must be stored uncompressed: %s", name);
		return NULL;
	}
	struct stat status;
	long page = sysconf(_SC_PAGESIZE);
	if (page <= 0 || fstat(fd, &status) || start < 0 || length <= 0 ||
		start > status.st_size || length > status.st_size - start) {
		close(fd);
		return NULL;
	}
	off64_t aligned = start - start % page;
	size_t delta = start - aligned;
	if ((uint64_t)length > SIZE_MAX - delta) { close(fd); return NULL; }
	size_t map_length = length + delta;
	void *base = mmap(NULL, map_length, PROT_READ, MAP_PRIVATE, fd, aligned);
	close(fd);
	if (base == MAP_FAILED)
		return NULL;
	struct afa_archive *ar = afa_open_memory(name, (uint8_t*)base + delta, length, error);
	if (!ar) { munmap(base, map_length); return NULL; }
	struct apk_mapping *owner = xmalloc(sizeof(*owner));
	*owner = (struct apk_mapping){base, map_length};
	ar->release_mapping = release_apk_mapping;
	ar->mapping_owner = owner;
	SDL_Log("APK_AFA mapped %s bytes=%lld entries=%u", name, (long long)length, ar->nr_files);
	return ar;
}
#endif

static struct afa_archive *open_asset_archive(const char *path, int *error)
{
#ifdef __ANDROID__
	const char *name = strrchr(path, '/');
	name = name ? name + 1 : path;
	if (is_apk_archive(name)) {
		struct afa_archive *ar = open_apk_archive(name, error);
		/* Dedupe compares archive->filename with the "name.afa" passed by the
		 * caller; APK archives are opened by basename, so restore the full key. */
		if (ar) {
			free(ar->filename);
			ar->filename = xstrdup(path);
		}
		return ar;
	}
#endif
	return afa_open(path, MMAP_IF_64BIT, error);
}

uint64_t asset_manager_generation(void)
{
	return assets_generation;
}

bool asset_manager_load_archive(enum asset_type type, const char *archive_name)
{
	if (getenv("XSYSTEM4_TRACE_ASSETS"))
		fprintf(stderr,"[asset] load %s: %s\n",asset_strtype(type),archive_name);
	if (!assets[type])
		return false;
	if (!assets[type]->load_archive)
		ERROR("load_archive not supported on this archive type");
	char *path = gamedir_path(archive_name);
	bool r = assets[type]->load_archive(assets[type], path);
	free(path);
	if (r)
		assets_generation++;
	return r;
}

bool asset_exists(enum asset_type type, int id)
{
	if (!assets[type])
		return false;
	return assets[type]->exists_by_id(assets[type], id);
}

bool asset_exists_by_name(enum asset_type type, const char *name, int *id_out)
{
	if (!assets[type])
		return false;
	if (!assets[type]->exists_by_name)
		ERROR("exists_by_name not supported on this archive type");
	return assets[type]->exists_by_name(assets[type], name, id_out);
}

struct archive_data *asset_get(enum asset_type type, int id)
{
	if (!assets[type])
		return NULL;
	return assets[type]->get_by_id(assets[type], id);
}

struct archive_data *asset_get_by_name(enum asset_type type, const char *name, int *id_out)
{
	if (!assets[type])
		return NULL;
	if (!assets[type]->get_by_name)
		ERROR("get_by_name not supported on this archive type");
	return assets[type]->get_by_name(assets[type], name, id_out);
}

struct cg *asset_cg_load(int id)
{
	struct archive_data *data = asset_get(ASSET_CG, id);
	if (!data)
		return NULL;
	struct cg *cg = cg_load_data(data);
	archive_free_data(data);
	return cg;
}

struct cg *asset_cg_load_by_name(const char *name, int *id_out)
{
	struct archive_data *data = asset_get_by_name(ASSET_CG, name, id_out);
	if (!data)
		return NULL;
	struct cg *cg = cg_load_data(data);
	archive_free_data(data);
	return cg;
}

bool asset_cg_get_metrics(int id, struct cg_metrics *metrics)
{
	struct archive_data *data = asset_get(ASSET_CG, id);
	if (!data)
		return false;
	bool r = cg_get_metrics_data(data, metrics);
	archive_free_data(data);
	return r;
}

bool asset_cg_get_metrics_by_name(const char *name, struct cg_metrics *metrics)
{
	struct archive_data *data = asset_get_by_name(ASSET_CG, name, NULL);
	if (!data)
		return false;
	bool r = cg_get_metrics_data(data, metrics);
	archive_free_data(data);
	return r;
}

static bool ald_exists_by_id(struct asset_manager *_manager, int id)
{
	struct asset_manager_ald *manager = (struct asset_manager_ald*)_manager;
	return archive_exists(manager->archive, id - 1);
}

static struct archive_data *ald_get_by_id(struct asset_manager *_manager, int id)
{
	struct asset_manager_ald *manager = (struct asset_manager_ald*)_manager;
	return archive_get(manager->archive, id - 1);
}

static bool afa_load_archive(struct asset_manager *_manager, const char *name)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;
	char path[PATH_MAX];
	snprintf(path, PATH_MAX, "%s.afa", name);
	// Loading the same archive again changes precedence, not its lifetime.
	for(int i=0;i<MAX_ARCHIVES && manager->archives[i];i++) {
		if(!strcasecmp(manager->archives[i]->filename,path)) {
			struct afa_archive *existing=manager->archives[i];
			memmove(manager->archives+1,manager->archives,i*sizeof(*manager->archives));
			manager->archives[0]=existing;
			return true;
		}
	}
	if (manager->archives[MAX_ARCHIVES-1])
		ERROR("Archive limit exceeded while loading %s",name);

	int error;
	struct afa_archive *ar = open_asset_archive(path, &error);
	if (!ar) {
		snprintf(path, PATH_MAX, "%s.AFA", name);
		ar = open_asset_archive(path, &error);
		if (!ar) {
			WARNING("Failed to open archive: %s", display_utf0(path));
			return false;
		}
	}
	if (manager->first_loaded_names) {
		for (unsigned i = 0; i < MAX_ARCHIVES; i++)
			if (!manager->name_archives[i]) {
				manager->name_archives[i] = ar;
				break;
			}
	}

	for (int i = MAX_ARCHIVES - 1; i > 0; i--) {
		manager->archives[i] = manager->archives[i-1];
	}
	manager->archives[0] = ar;
	return true;
}

static bool afa_exists_by_id(struct asset_manager *_manager, int id)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;
	for (int i = 0; i < MAX_ARCHIVES && manager->archives[i]; i++) {
		if (archive_exists(&manager->archives[i]->ar, id - 1))
			return true;
	}
	return false;
}

static struct archive_data *afa_get_by_id(struct asset_manager *_manager, int id)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;
	for (int i = 0; i < MAX_ARCHIVES && manager->archives[i]; i++) {
		struct archive_data *data = archive_get(&manager->archives[i]->ar, id - 1);
		if (data)
			return data;
	}
	return NULL;
}

static struct archive_data *afa_get_by_name(struct asset_manager *_manager, const char *name,
		int *id_out)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;
	struct afa_archive **ordered = manager->first_loaded_names ? manager->name_archives : manager->archives;

	for (unsigned i = 0; i < MAX_ARCHIVES && ordered[i]; i++) {
		struct archive_data *data;
		data = archive_get_by_basename(&ordered[i]->ar, name);
		if (data) {
			if (id_out)
				*id_out = data->no + 1;
			return data;
		}
	}
	return NULL;
}

static bool afa_exists_by_name(struct asset_manager *_manager, const char *name, int *id_out)
{
	struct asset_manager_afa *manager = (struct asset_manager_afa*)_manager;
	struct afa_archive **ordered = manager->first_loaded_names ? manager->name_archives : manager->archives;

	for (unsigned i = 0; i < MAX_ARCHIVES && ordered[i]; i++) {
		if (archive_exists_by_basename(&ordered[i]->ar, name, id_out)) {
			if (id_out)
				*id_out += 1;
			return true;
		}
	}
	return false;
}

struct asset_name_iterator {
	void (*iter)(const char *, void *);
	void *user;
};

static void asset_iter_name(struct archive_data *data, void *user)
{
	struct asset_name_iterator *ctx = user;
	if (data->name) ctx->iter(data->name, ctx->user);
}

void asset_foreach_name(enum asset_type type, void (*iter)(const char *, void *), void *user)
{
	if (type < 0 || type >= ASSET_TYPE_MAX || !assets[type] || !iter) return;
	struct asset_name_iterator ctx = { iter, user };
	if (assets[type]->get_by_name == afa_get_by_name) {
		struct asset_manager_afa *manager = (struct asset_manager_afa *)assets[type];
		struct afa_archive **ordered = manager->first_loaded_names ? manager->name_archives : manager->archives;
		for (int i = 0; i < MAX_ARCHIVES && ordered[i]; i++)
			archive_for_each(&ordered[i]->ar, asset_iter_name, &ctx);
	} else {
		struct asset_manager_ald *manager = (struct asset_manager_ald *)assets[type];
		archive_for_each(manager->archive, asset_iter_name, &ctx);
	}
}

static void _ald_init(enum asset_type type, struct archive *ar)
{
	struct asset_manager_ald *manager = xcalloc(1, sizeof(struct asset_manager_ald));
	manager->manager.exists_by_id = ald_exists_by_id;
	manager->manager.get_by_id = ald_get_by_id;
	manager->manager.load_archive = NULL;
	manager->manager.exists_by_name = NULL;
	manager->manager.get_by_name = NULL;
	manager->archive = ar;
	assets[type] = &manager->manager;
}

static void ald_init(enum asset_type type, char **files, int count)
{
	if (count < 1)
		return;
	if (assets[type])
		WARNING("Multiple asset archives for type %s", asset_strtype(type));

	int error;
	struct archive *ar = ald_open(files, count, MMAP_IF_64BIT, &error);
	if (!ar)
		ERROR("Failed to open ALD file: %s", archive_strerror(error));

	_ald_init(type, ar);

	for (int i = 0; i < count; i++) {
		free(files[i]);
	}
}

static void afa_init(enum asset_type type, char *file)
{
	if (!file)
		return;
	if (assets[type])
		WARNING("Multiple asset archives for type %s", asset_strtype(type));

	int error;
	struct afa_archive *ar = open_asset_archive(file, &error);
	if (!ar)
		ERROR("Failed to open AFA file: %s", archive_strerror(error));

	struct asset_manager_afa *manager = xcalloc(1, sizeof(struct asset_manager_afa));
	manager->manager.load_archive = afa_load_archive;
	manager->manager.exists_by_id = afa_exists_by_id;
	manager->manager.get_by_id = afa_get_by_id;
	manager->manager.exists_by_name = afa_exists_by_name;
	manager->manager.get_by_name = afa_get_by_name;
	manager->archives[0] = ar;
	manager->first_loaded_names = prefer_first_cg_names(type);
	manager->name_archives[0] = ar;
	assets[type] = &manager->manager;

	free(file);
}

static char *get_base_name(const char *ain_filename)
{
	char *path = sjis2utf(ain_filename, 0);
	char *dot = strrchr(path, '.');
	if (dot)
		*dot = '\0';
	return path;
}

void asset_manager_init(void)
{
	assets_generation++;
	char *ald_filenames[ASSET_TYPE_MAX][ALD_FILEMAX] = {0};
	int ald_count[ASSET_TYPE_MAX] = {0};
	char *afa_filenames[ASSET_TYPE_MAX] = {0};

	UDIR *dir;
	char *d_name;

	if (!(dir = opendir_utf8(config.game_dir))) {
		ERROR("Failed to open directory: %s", display_utf0(config.game_dir));
	}

	char *base = get_base_name(config.ain_filename);
	size_t base_len = strlen(base);

	// get ALD filenames
	while ((d_name = readdir_utf8(dir))) {
		// archive filename must begin with ain filename
		if (strncasecmp(base, d_name, base_len))
			goto loop_next;
		const char *ext = file_extension(d_name);
		if (!ext)
			goto loop_next;
		if (!strcasecmp(ext, "ald")) {
			int dno = toupper(d_name[base_len+1]) - 'A';
			if (dno < 0 || dno >= ALD_FILEMAX) {
				WARNING("Invalid ALD index: %s", display_utf0(d_name));
				goto loop_next;
			}

			switch (d_name[base_len]) {
			case 'b':
			case 'B':
				ald_filenames[ASSET_BGM][dno] = path_join(config.game_dir, d_name);
				ald_count[ASSET_BGM] = max(ald_count[ASSET_BGM], dno+1);
				break;
			case 'g':
			case 'G':
				ald_filenames[ASSET_CG][dno] = path_join(config.game_dir, d_name);
				ald_count[ASSET_CG] = max(ald_count[ASSET_CG], dno+1);
				break;
			case 'w':
			case 'W':
				ald_filenames[ASSET_SOUND][dno] = path_join(config.game_dir, d_name);
				ald_count[ASSET_SOUND] = max(ald_count[ASSET_SOUND], dno+1);
				break;
			case 'd':
			case 'D':
				ald_filenames[ASSET_DATA][dno] = path_join(config.game_dir, d_name);
				ald_count[ASSET_DATA] = max(ald_count[ASSET_DATA], dno+1);
				break;
			default:
				WARNING("Unhandled ALD file: %s", display_utf0(d_name));
				break;
			}
		} else if (!strcasecmp(ext, "bgi")) {
			if (config.bgi_path) {
				WARNING("Multiple bgi files");
				goto loop_next;
			}
			config.bgi_path = path_join(config.game_dir, d_name);
		} else if (!strcasecmp(ext, "wai")) {
			if (config.wai_path) {
				WARNING("Multiple wai files");
				goto loop_next;
			}
			config.wai_path = path_join(config.game_dir, d_name);
		} else if (!strcasecmp(ext, "ex")) {
			if (config.ex_path) {
				WARNING("Multiple ex files");
				goto loop_next;
			}
			config.ex_path = path_join(config.game_dir, d_name);
		} else if (!strcasecmp(ext, "afa")) {
			const char *type = d_name + base_len;
			if (!strcmp(type, "BA.afa")) {
				afa_filenames[ASSET_BGM] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "WA.afa") || !strcmp(type, "Sound.afa")) {
				afa_filenames[ASSET_SOUND] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "Voice.afa")) {
				afa_filenames[ASSET_VOICE] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "CG.afa")) {
				afa_filenames[ASSET_CG] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "Flat.afa")) {
				afa_filenames[ASSET_FLAT] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "Pact.afa")) {
				afa_filenames[ASSET_PACT] = path_join(config.game_dir, d_name);
			} else if (!strcmp(type, "AFF.afa")) {
				afa_filenames[ASSET_FLASH] = path_join(config.game_dir, d_name);
			}
		} else if (!strcasecmp(ext, "fnl")) {
			if (!config.fnl_path)
				config.fnl_path = path_join(config.game_dir, d_name);
		}
	loop_next:
		free(d_name);
	}
	closedir_utf8(dir);
	free(base);

	// open ALD archives
	ald_init(ASSET_BGM, ald_filenames[ASSET_BGM], ald_count[ASSET_BGM]);
	ald_init(ASSET_SOUND, ald_filenames[ASSET_SOUND], ald_count[ASSET_SOUND]);
	ald_init(ASSET_VOICE, ald_filenames[ASSET_VOICE], ald_count[ASSET_VOICE]);
	ald_init(ASSET_CG, ald_filenames[ASSET_CG], ald_count[ASSET_CG]);
	ald_init(ASSET_FLAT, ald_filenames[ASSET_FLAT], ald_count[ASSET_FLAT]);
	ald_init(ASSET_PACT, ald_filenames[ASSET_PACT], ald_count[ASSET_PACT]);
	ald_init(ASSET_DATA, ald_filenames[ASSET_DATA], ald_count[ASSET_DATA]);
	ald_init(ASSET_FLASH, ald_filenames[ASSET_FLASH], ald_count[ASSET_FLASH]);

	// open AFA archives
	afa_init(ASSET_BGM, afa_filenames[ASSET_BGM]);
	afa_init(ASSET_SOUND, afa_filenames[ASSET_SOUND]);
	afa_init(ASSET_VOICE, afa_filenames[ASSET_VOICE]);
	afa_init(ASSET_CG, afa_filenames[ASSET_CG]);
	afa_init(ASSET_FLAT, afa_filenames[ASSET_FLAT]);
	afa_init(ASSET_PACT, afa_filenames[ASSET_PACT]);
	afa_init(ASSET_DATA, afa_filenames[ASSET_DATA]);
	afa_init(ASSET_FLASH, afa_filenames[ASSET_FLASH]);
}
