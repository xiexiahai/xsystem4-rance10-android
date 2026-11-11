/* Copyright (C) 2021 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <SDL.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/archive.h"
#include "system4/ex.h"
#include "system4/hashtable.h"
#include "system4/string.h"

#include "vm.h"
#include "hll.h"
#include "asset_manager.h"
#include "audio.h"
#include "mixer.h"
#include "id_pool.h"
#include "xsystem4.h"

HLL_WARN_UNIMPLEMENTED( , void, KiwiSoundEngine, SetGlobalFocus, possibly_unused int nNum);

static bool KiwiSoundEngine_GetMixerName(int n, struct string **name)
{
	const char *value = mixer_get_name(n);
	if (!value)
		return false;
	free_string(*name);
	*name = cstr_to_string(value);
	return true;
}

static bool KiwiSoundEngine_GetMixerDefaultVolume(int n, int *volume)
{
	if (n < 0 || (unsigned)n >= config.mixer_nr_channels)
		return false;
	*volume = config.mixer_volumes[n];
	return true;
}

static bool KiwiSoundEngine_SetMixerName(int n, struct string *name)
{
	return mixer_set_name(n, name->text);
}

//int KiwiSoundEngine_Sound_GetGroupNum(int nCh);
//static bool KiwiSoundEngine_Sound_PrepareFromFile(int ch, struct string *filename);

static int KiwiSoundEngine_GetMasterGroup(void)
{
	return 0;
}

static int KiwiSoundEngine_GetBGMGroup(void)
{
	return 0;
}

static int KiwiSoundEngine_GetSEGroup(void)
{
	return 0;
}

static int KiwiSoundEngine_GetVoiceGroup(void)
{
	return 0;
}

static int KiwiSoundEngine_GetGimicSEGroup(void)
{
	return 0;
}

static int KiwiSoundEngine_GetBackVoiceGroup(void)
{
	return 0;
}

static bool rance9_bgm_exists(int num)
{
	char name[16];
	snprintf(name, sizeof(name), "%05d", num);
	return asset_exists_by_name(ASSET_BGM, name, NULL);
}

static int rance9_bgm_prepare(int ch, int num)
{
	char name[16];
	snprintf(name, sizeof(name), "%05d", num);
	struct archive_data *dfile = asset_get_by_name(ASSET_BGM, name, NULL);
	if (!dfile)
		return 0;
	return bgm_prepare_from_archive_data(ch, num, dfile);
}

static bool rance9_wav_exists(int num)
{
	char name[16];
	snprintf(name, sizeof(name), "%05d", num);
	return asset_exists_by_name(ASSET_SOUND, name, NULL);
}

static int rance9_wav_prepare(int ch, int num)
{
	char name[16];
	snprintf(name, sizeof(name), "%05d", num);
	struct archive_data *dfile = asset_get_by_name(ASSET_SOUND, name, NULL);
	if (!dfile)
		return 0;
	return wav_prepare_from_archive_data(ch, num, dfile);
}

// Newer KiwiSoundEngine uses one named sound pool, plus separate SE slots
// which can own several simultaneous voices. Keep the legacy Music_/Sound_
// interface above independent of these IDs.
static bool named_interface;
static struct id_pool sounds, effects;
static struct ex *sound_info;
// Cache only validated group numbers, never audio buffers or live channels.
// Bound both entry count and key length; clear with the library's asset lifetime.
static struct hash_table *sound_groups;
static unsigned nr_sound_groups;
static uint64_t sound_groups_generation;
#define SOUND_GROUP_CACHE_LIMIT 1024

static void clear_sound_groups(void)
{
	if (sound_groups)
		ht_free(sound_groups);
	sound_groups = NULL;
	nr_sound_groups = 0;
}

struct se_voice {
	struct channel *channel;
	struct se_voice *next;
};

struct se_slot {
	unsigned play_span;
	bool multi_play;
	bool has_played;
	Uint32 last_play;
	struct se_voice *voices;
};

static void KiwiSoundEngine_ModuleInit(void)
{
	if (!named_interface)
		return;
	// AliceStart.ini has no mixer list in newer games. Their Kiwi interface
	// uses numbered groups (master, music, SE, voice, and four extra groups).
	if (!config.mixer_nr_channels) {
		static const char *names[] = { "Master", "Music", "Sound", "Voice",
			"GimicSE", "BackVoice", "Extra6", "Extra7" };
		config.mixer_nr_channels = 8;
		config.mixer_channels = xcalloc(8, sizeof(char*));
		config.mixer_volumes = xcalloc(8, sizeof(int));
		for (int i = 0; i < 8; i++) {
			config.mixer_channels[i] = strdup(names[i]);
			config.mixer_volumes[i] = config.default_volume;
		}
	}
	audio_init();
	id_pool_init(&sounds, 0);
	id_pool_init(&effects, 0);
	struct archive_data *data = asset_get_by_name(ASSET_SOUND, "timemap_sound2", NULL);
	if (data) {
		sound_info = ex_read(data->data, data->size);
		archive_free_data(data);
	}
}

static void se_collect(struct se_slot *slot, bool stop_all)
{
	struct se_voice **link = &slot->voices;
	while (*link) {
		struct se_voice *voice = *link;
		if (stop_all || !channel_is_playing(voice->channel)) {
			*link = voice->next;
			channel_close(voice->channel);
			free(voice);
		} else {
			link = &voice->next;
		}
	}
}

static void KiwiSoundEngine_ModuleFini(void)
{
	if (!named_interface)
		return;
	for (int id = id_pool_get_first(&sounds); id >= 0; id = id_pool_get_next(&sounds, id))
		channel_close(id_pool_get(&sounds, id));
	for (int id = id_pool_get_first(&effects); id >= 0; id = id_pool_get_next(&effects, id)) {
		struct se_slot *slot = id_pool_get(&effects, id);
		se_collect(slot, true);
		free(slot);
	}
	id_pool_delete(&sounds);
	id_pool_delete(&effects);
	if (sound_info)
		ex_free(sound_info);
	sound_info = NULL;
	clear_sound_groups();
}

static struct archive_data *named_sound(const char *name, enum asset_type *type)
{
	static const enum asset_type types[] = { ASSET_SOUND, ASSET_VOICE, ASSET_BGM };
	for (unsigned i = 0; i < sizeof(types) / sizeof(*types); i++) {
		struct archive_data *data = asset_get_by_name(types[i], name, NULL);
		if (data) {
			*type = types[i];
			return data;
		}
	}
	return NULL;
}

static struct channel *open_named_sound(const char *name)
{
	enum asset_type type;
	struct archive_data *data = named_sound(name, &type);
	if (!data)
		return NULL;
	struct channel *ch = channel_open_archive_data(data, type, AUDIO_NO_METADATA);
	if (!ch)
		return NULL;
	int group = type == ASSET_VOICE ? 3 : type == ASSET_BGM ? 1 : 2;
	struct ex_table *table = sound_info ? ex_get_table(sound_info, "SoundInfo") : NULL;
	if (table) {
		char *base = strdup(name);
		char *extension = strrchr(base, '.');
		if (extension && (!strcmp(extension, ".ogg") || !strcmp(extension, ".wav")))
			*extension = '\0';
		int row = ex_row_at_string_key(table, base);
		free(base);
		if (row >= 0) {
			struct ex_value *g = ex_table_get(table, row, 1);
			struct ex_value *begin = ex_table_get(table, row, 2);
			struct ex_value *end = ex_table_get(table, row, 3);
			if (g && g->type == EX_INT)
				group = g->i;
			int length = channel_get_sample_length(ch);
			if (begin && end && begin->type == EX_INT && end->type == EX_INT
					&& begin->i >= 0 && end->i > begin->i && end->i <= length) {
				channel_set_loop_start_pos(ch, begin->i);
				channel_set_loop_end_pos(ch, end->i);
			}
		}
	}
	if (!channel_set_group_num(ch, group)) {
		channel_close(ch);
		return NULL;
	}
	return ch;
}

static int free_id(struct id_pool *pool, int min_id, int max_id)
{
	if (min_id < 0 || max_id < min_id)
		return -1;
	for (int id = min_id; ; id++) {
		if (!id_pool_get(pool, id))
			return id;
		if (id == max_id)
			return -1;
	}
}

static int KiwiSoundEngine_GetFreeID(int min_id, int max_id) { return free_id(&sounds, min_id, max_id); }
static int KiwiSoundEngine_GetFreeSeID(int min_id, int max_id) { return free_id(&effects, min_id, max_id); }
static bool KiwiSoundEngine_IsExistID(int id) { return id_pool_get(&sounds, id) != NULL; }
static bool KiwiSoundEngine_IsExistSeID(int id) { return id_pool_get(&effects, id) != NULL; }

static bool KiwiSoundEngine_SetSeParam(int id, int span, bool multi)
{
	if (id < 0 || span < 0)
		return false;
	struct se_slot *slot = id_pool_get(&effects, id);
	if (!slot) {
		slot = xcalloc(1, sizeof(*slot));
		id_pool_set(&effects, id, slot);
	}
	slot->play_span = span;
	slot->multi_play = multi;
	return true;
}

static bool KiwiSoundEngine_PlaySe(int id, struct string *name)
{
	if (id < 0)
		return false;
	struct se_slot *slot = id_pool_get(&effects, id);
	Uint32 now = SDL_GetTicks();
	if (slot) {
		se_collect(slot, false);
		if ((slot->has_played && now - slot->last_play < slot->play_span)
			|| (!slot->multi_play && slot->voices))
			return false;
	}
	struct channel *ch = open_named_sound(name->text);
	if (!ch)
		return false;
	if (!channel_play(ch)) {
		channel_close(ch);
		return false;
	}
	if (!slot) {
		// SetSeParam customizes an ID; it is not a prerequisite for playback.
		// Rance03's character voices use an ID absent from its SE settings table.
		// Create defaults only after a valid sound starts, keeping failures inert.
		slot = xcalloc(1, sizeof(*slot));
		slot->multi_play = true;
		id_pool_set(&effects, id, slot);
	}
	struct se_voice *voice = xmalloc(sizeof(*voice));
	voice->channel = ch;
	voice->next = slot->voices;
	slot->voices = voice;
	slot->last_play = now;
	slot->has_played = true;
	return true;
}

static bool KiwiSoundEngine_StopSe(int id)
{
	struct se_slot *slot = id_pool_get(&effects, id);
	if (!slot)
		return false;
	se_collect(slot, true);
	return true;
}

static bool KiwiSoundEngine_IsPlaySe(int id)
{
	struct se_slot *slot = id_pool_get(&effects, id);
	if (!slot)
		return false;
	se_collect(slot, false);
	return slot->voices != NULL;
}

static bool KiwiSoundEngine_Prepare(int id, struct string *name, possibly_unused bool streaming)
{
	if (id < 0)
		return false;
	// libsndfile decodes incrementally for both modes; archive bytes stay
	// mapped/owned by the channel instead of decoding the entire sound.
	struct channel *ch = open_named_sound(name->text);
	if (!ch)
		return false;
	struct channel *old = id_pool_set(&sounds, id, ch);
	if (old)
		channel_close(old);
	return true;
}

static bool KiwiSoundEngine_Unprepare(int id)
{
	struct channel *ch = id_pool_release(&sounds, id);
	if (!ch)
		return false;
	channel_close(ch);
	return true;
}

#define NAMED_CHANNEL_QUERY(name, function, result_type, missing) \
	static result_type KiwiSoundEngine_##name(int id) { \
		struct channel *ch = id_pool_get(&sounds, id); \
		return ch ? function(ch) : missing; \
	}
NAMED_CHANNEL_QUERY(Play, channel_play, bool, false)
NAMED_CHANNEL_QUERY(Stop, channel_stop, bool, false)
NAMED_CHANNEL_QUERY(IsPlay, channel_is_playing, bool, false)
NAMED_CHANNEL_QUERY(GetLoopCount, channel_get_loop_count, int, 0)
NAMED_CHANNEL_QUERY(Pause, channel_pause, bool, false)
NAMED_CHANNEL_QUERY(Restart, channel_restart, bool, false)
NAMED_CHANNEL_QUERY(IsPause, channel_is_paused, bool, false)
NAMED_CHANNEL_QUERY(StopFade, channel_stop_fade, bool, false)
NAMED_CHANNEL_QUERY(IsFade, channel_is_fading, bool, false)
NAMED_CHANNEL_QUERY(GetPos, channel_get_pos, int, 0)
NAMED_CHANNEL_QUERY(GetLength, channel_get_length, int, 0)
NAMED_CHANNEL_QUERY(GetGroupNum, channel_get_group_num, int, -1)
#undef NAMED_CHANNEL_QUERY

static bool KiwiSoundEngine_SetLoopCount(int id, int count)
{
	struct channel *ch = id_pool_get(&sounds, id);
	return ch && count >= 0 && channel_set_loop_count(ch, count);
}

static bool KiwiSoundEngine_Seek(int id, int ms)
{
	struct channel *ch = id_pool_get(&sounds, id);
	return ch && ms >= 0 && channel_seek(ch, ms);
}

static bool KiwiSoundEngine_Fade(int id, int time, float volume, bool stop, int type)
{
	struct channel *ch = id_pool_get(&sounds, id);
	if (!ch || time < 0 || !(volume >= 0.0f && volume <= 1.0f))
		return false;
	/*
	 * NOTE(DSH): only the linear curve (0) is modelled. Other curve types used
	 * to make this return false, and on Rance 03 that aborted the opening
	 * sequence: the game called Fade(curve 3) right after the title screen
	 * appeared and then exited on its own a few seconds later. Treating an
	 * unknown curve as linear keeps the fade working (only the shape differs),
	 * which is far better than failing the call outright.
	 */
	if (time && type != 0)
		WARNING("KiwiSoundEngine.Fade: curve %d not modelled, using linear", type);
	return channel_fade(ch, time, volume * 100.0f, stop);
}

static bool KiwiSoundEngine_IsExistFile(struct string *name)
{
	return asset_exists_by_name(ASSET_SOUND, name->text, NULL)
		|| asset_exists_by_name(ASSET_VOICE, name->text, NULL)
		|| asset_exists_by_name(ASSET_BGM, name->text, NULL);
}

static int KiwiSoundEngine_GetLengthFromFile(struct string *name)
{
	struct channel *ch = open_named_sound(name->text);
	if (!ch)
		return 0;
	int length = channel_get_length(ch);
	channel_close(ch);
	return length;
}

static int KiwiSoundEngine_GetGroupNumFromFile(struct string *name)
{
	uint64_t generation = asset_manager_generation();
	if (generation != sound_groups_generation) {
		clear_sound_groups();
		sound_groups_generation = generation;
	}
	void *cached = sound_groups ? ht_get(sound_groups, name->text, NULL) : NULL;
	if (cached)
		return (int)(intptr_t)cached - 1;
	struct channel *ch = open_named_sound(name->text);
	if (!ch)
		return -1;
	int group = channel_get_group_num(ch);
	channel_close(ch);
	// Failed/missing resources must remain retryable. A successful first query
	// retains the same decoder/metadata validation as normal playback.
	if (nr_sound_groups < SOUND_GROUP_CACHE_LIMIT && name->size <= 512) {
		if (!sound_groups)
			sound_groups = ht_create(256);
		ht_put(sound_groups, name->text, (void*)(intptr_t)(group + 1));
		nr_sound_groups++;
	}
	return group;
}

static float KiwiSoundEngine_GetGroupVolume(int group)
{
	int volume;
	return mixer_get_volume(group, &volume) ? volume / 100.0f : 0.0f;
}

static int KiwiSoundEngine_MillisecondsToSamples(int ms, int rate)
{
	int64_t samples = (int64_t)ms * rate / 1000;
	return samples < 0 ? 0 : samples > INT_MAX ? INT_MAX : samples;
}

static void KiwiSoundEngine_PreLink(void);

HLL_LIBRARY(KiwiSoundEngine,
	    HLL_EXPORT(_PreLink, KiwiSoundEngine_PreLink),
	    HLL_EXPORT(GetMixerNumof, mixer_get_numof),
	    HLL_EXPORT(GetMixerName, KiwiSoundEngine_GetMixerName),
	    HLL_EXPORT(GetMixerDefaultVolume, KiwiSoundEngine_GetMixerDefaultVolume),
	    HLL_EXPORT(GetMixerVolume, mixer_get_volume),
	    HLL_EXPORT(GetMixerMute, mixer_get_mute),
	    HLL_EXPORT(SetMixerName, KiwiSoundEngine_SetMixerName),
	    HLL_EXPORT(SetMixerVolume, mixer_set_volume),
	    HLL_EXPORT(SetMixerMute, mixer_set_mute),
	    HLL_EXPORT(_ModuleInit, KiwiSoundEngine_ModuleInit),
	    HLL_EXPORT(_ModuleFini, KiwiSoundEngine_ModuleFini),
	    HLL_EXPORT(GetFreeSeID, KiwiSoundEngine_GetFreeSeID),
	    HLL_EXPORT(IsExistSeID, KiwiSoundEngine_IsExistSeID),
	    HLL_EXPORT(SetSeParam, KiwiSoundEngine_SetSeParam),
	    HLL_EXPORT(PlaySe, KiwiSoundEngine_PlaySe),
	    HLL_EXPORT(StopSe, KiwiSoundEngine_StopSe),
	    HLL_EXPORT(IsPlaySe, KiwiSoundEngine_IsPlaySe),
	    HLL_EXPORT(GetFreeID, KiwiSoundEngine_GetFreeID),
	    HLL_EXPORT(IsExistID, KiwiSoundEngine_IsExistID),
	    HLL_EXPORT(Prepare, KiwiSoundEngine_Prepare),
	    HLL_EXPORT(Unprepare, KiwiSoundEngine_Unprepare),
	    HLL_EXPORT(Play, KiwiSoundEngine_Play),
	    HLL_EXPORT(Stop, KiwiSoundEngine_Stop),
	    HLL_EXPORT(IsPlay, KiwiSoundEngine_IsPlay),
	    HLL_EXPORT(SetLoopCount, KiwiSoundEngine_SetLoopCount),
	    HLL_EXPORT(GetLoopCount, KiwiSoundEngine_GetLoopCount),
	    HLL_EXPORT(Pause, KiwiSoundEngine_Pause),
	    HLL_EXPORT(Restart, KiwiSoundEngine_Restart),
	    HLL_EXPORT(IsPause, KiwiSoundEngine_IsPause),
	    HLL_EXPORT(Fade, KiwiSoundEngine_Fade),
	    HLL_EXPORT(StopFade, KiwiSoundEngine_StopFade),
	    HLL_EXPORT(IsFade, KiwiSoundEngine_IsFade),
	    HLL_EXPORT(Seek, KiwiSoundEngine_Seek),
	    HLL_EXPORT(GetPos, KiwiSoundEngine_GetPos),
	    HLL_EXPORT(GetLength, KiwiSoundEngine_GetLength),
	    HLL_EXPORT(GetGroupNum, KiwiSoundEngine_GetGroupNum),
	    HLL_EXPORT(IsExistFile, KiwiSoundEngine_IsExistFile),
	    HLL_EXPORT(GetLengthFromFile, KiwiSoundEngine_GetLengthFromFile),
	    HLL_EXPORT(GetGroupNumFromFile, KiwiSoundEngine_GetGroupNumFromFile),
	    HLL_EXPORT(GetGroupVolume, KiwiSoundEngine_GetGroupVolume),
	    HLL_EXPORT(MillisecondsToSamples, KiwiSoundEngine_MillisecondsToSamples),
	    HLL_EXPORT(SetGlobalFocus, KiwiSoundEngine_SetGlobalFocus),
	    HLL_EXPORT(Music_IsExist, bgm_exists),
	    HLL_EXPORT(Music_Prepare, bgm_prepare),
	    HLL_EXPORT(Music_Unprepare, bgm_unprepare),
	    HLL_EXPORT(Music_Play, bgm_play),
	    HLL_EXPORT(Music_Stop, bgm_stop),
	    HLL_EXPORT(Music_IsPlay, bgm_is_playing),
	    HLL_EXPORT(Music_SetLoopCount, bgm_set_loop_count),
	    HLL_EXPORT(Music_GetLoopCount, bgm_get_loop_count),
	    HLL_EXPORT(Music_SetLoopStartPos, bgm_set_loop_start_pos),
	    HLL_EXPORT(Music_SetLoopEndPos, bgm_set_loop_end_pos),
	    HLL_EXPORT(Music_Fade, bgm_fade),
	    HLL_EXPORT(Music_StopFade, bgm_stop_fade),
	    HLL_EXPORT(Music_IsFade, bgm_is_fading),
	    HLL_EXPORT(Music_Pause, bgm_pause),
	    HLL_EXPORT(Music_Restart, bgm_restart),
	    HLL_EXPORT(Music_IsPause, bgm_is_paused),
	    HLL_EXPORT(Music_GetPos, bgm_get_pos),
	    HLL_EXPORT(Music_GetLength, bgm_get_length),
	    HLL_EXPORT(Music_GetSamplePos, bgm_get_sample_pos),
	    HLL_EXPORT(Music_GetSampleLength, bgm_get_sample_length),
	    HLL_EXPORT(Music_Seek, bgm_seek),
	    //HLL_EXPORT(Music_MillisecondsToSamples, KiwiSoundEngine_Music_MillisecondsToSamples),
	    //HLL_EXPORT(Music_GetFormat, KiwiSoundEngine_Music_GetFormat),
	    HLL_EXPORT(Sound_IsExist, wav_exists),
	    HLL_EXPORT(Sound_Prepare, wav_prepare),
	    HLL_EXPORT(Sound_Unprepare, wav_unprepare),
	    HLL_EXPORT(Sound_Play, wav_play),
	    HLL_EXPORT(Sound_Stop, wav_stop),
	    HLL_EXPORT(Sound_IsPlay, wav_is_playing),
	    HLL_EXPORT(Sound_Fade, wav_fade),
	    HLL_EXPORT(Sound_StopFade, wav_stop_fade),
	    HLL_EXPORT(Sound_IsFade, wav_is_fading),
	    HLL_EXPORT(Sound_GetTimeLength, wav_get_time_length),
	    HLL_TODO_EXPORT(Sound_GetGroupNum, KiwiSoundEngine_Sound_GetGroupNum),
	    HLL_EXPORT(Sound_GetGroupNumFromDataNum, wav_get_group_num_from_data_num),
	    HLL_TODO_EXPORT(Sound_PrepareFromFile, KiwiSoundEngine_Sound_PrepareFromFile),
	    HLL_EXPORT(Sound_GetDataLength, wav_get_data_length),
	    HLL_EXPORT(GetMasterGroup, KiwiSoundEngine_GetMasterGroup),
	    HLL_EXPORT(GetBGMGroup, KiwiSoundEngine_GetBGMGroup),
	    HLL_EXPORT(GetSEGroup, KiwiSoundEngine_GetSEGroup),
	    HLL_EXPORT(GetVoiceGroup, KiwiSoundEngine_GetVoiceGroup),
	    HLL_EXPORT(GetGimicSEGroup, KiwiSoundEngine_GetGimicSEGroup),
	    HLL_EXPORT(GetBackVoiceGroup, KiwiSoundEngine_GetBackVoiceGroup)
	);

static void KiwiSoundEngine_PreLink(void)
{
	int libno = ain_get_library(ain, "KiwiSoundEngine");
	if (libno < 0)
		return;
	named_interface = ain_get_library_function(ain, libno, "SetSeParam") >= 0;
	if (ain_get_library_function(ain, libno, "Sound_Seek") >= 0) {
		static_library_replace(&lib_KiwiSoundEngine, "Music_IsExist", rance9_bgm_exists);
		static_library_replace(&lib_KiwiSoundEngine, "Music_Prepare", rance9_bgm_prepare);
		static_library_replace(&lib_KiwiSoundEngine, "Sound_IsExist", rance9_wav_exists);
		static_library_replace(&lib_KiwiSoundEngine, "Sound_Prepare", rance9_wav_prepare);
	}
}
