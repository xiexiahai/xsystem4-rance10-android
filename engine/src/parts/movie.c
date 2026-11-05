/* PartsEngine movie components share their render target with a hidden SACT sprite. */
#include <string.h>
#include "system4.h"
#include "system4/string.h"
#include "mixer.h"
#include "movie.h"
#include "parts.h"
#include "parts_internal.h"
#include "sact.h"
#include "sprite.h"
#include "vm.h"

static int clamp_byte(int value)
{
	return min(255, max(0, value));
}

static struct parts_movie *find_movie(int parts_no, int state)
{
	if (!parts_state_valid(state))
		return NULL;
	struct parts *parts = parts_try_get(parts_no);
	return parts && parts->states[state].type == PARTS_MOVIE
		? &parts->states[state].movie : NULL;
}

static bool movie_group_volume(int group, int *volume)
{
	int mute;
	if (!mixer_get_volume(group, volume) || !mixer_get_mute(group, &mute))
		return false;
	/* Streams already pass through the master mixer. */
	const char *name = mixer_get_name(group);
	if (name && (!strcmp(name, "Master") || !strcmp(name, "\x83\x7d\x83\x58\x83\x5e\x81\x5b")))
		*volume = 100;
	else if (mute)
		*volume = 0;
	return true;
}

void parts_movie_free(struct parts_movie *movie)
{
	if (movie->decoder)
		movie_free(movie->decoder);
	if (movie->filename)
		free_string(movie->filename);
	/* The sprite alone owns this texture. */
	movie->common.texture.handle = 0;
	if (movie->sprite_no >= 0)
		sact_SP_Delete(movie->sprite_no);
	movie->sprite_no = -1;
	movie->decoder = NULL;
	movie->filename = NULL;
	movie->started = movie->playing = movie->ended = false;
}

bool PE_CreatePartsMovie(int parts_no, struct string *filename, int sound_id,
		int sound_group, int y, int cb, int cr, int state)
{
	int volume;
	if (!parts_state_valid(state - 1) || !filename || !filename->size
			|| !movie_group_volume(sound_group, &volume))
		return false;
	struct movie_context *decoder = movie_load(filename->text);
	if (!decoder)
		return false;
	int width, height;
	if (!movie_get_dimensions(decoder, &width, &height) || width <= 0 || height <= 0) {
		movie_free(decoder);
		return false;
	}
	/* CDrawMovie passes Y/Cb/Cr, despite the original parameter names. */
	int r = clamp_byte(y + (int)(1.402f * (cr - 128)));
	int g = clamp_byte(y - (int)(0.344136f * (cb - 128) + 0.714136f * (cr - 128)));
	int b = clamp_byte(y + (int)(1.772f * (cb - 128)));
	struct string *name = string_dup(filename);
	if (!PE_init_parts_movie(parts_no, width, height, r, g, b, state)) {
		free_string(name);
		movie_free(decoder);
		return false;
	}
	struct parts_movie *movie = find_movie(parts_no, state - 1);
	movie->decoder = decoder;
	movie->filename = name;
	movie->sound_id = sound_id;
	movie->sound_group = sound_group;
	return true;
}

bool PE_PlayPartsMovie(int parts_no, int state)
{
	return PE_PlayPartsMovieAt(parts_no,0,state);
}

bool PE_PlayPartsMovieAt(int parts_no, int msec, int state)
{
	struct parts_movie *movie = find_movie(parts_no, state - 1);
	int volume;
	if (msec<0 || !movie || !movie->filename || !movie_group_volume(movie->sound_group, &volume))
		return false;
	if (movie->started || !movie->decoder) {
		struct movie_context *decoder = movie_load(movie->filename->text);
		if (!decoder)
			return false;
		if (movie->decoder)
			movie_free(movie->decoder);
		movie->decoder = decoder;
	}
	movie->playing = movie->ended = false;
	if (!movie_set_volume(movie->decoder, volume) || !movie_play_at(movie->decoder,msec)) {
		movie_free(movie->decoder);
		movie->decoder = NULL;
		return false;
	}
	movie->started = movie->playing = true;
	return true;
}

int PE_GetPartsMovieEndTime(int parts_no,int state)
{
	struct parts_movie *movie=find_movie(parts_no,state-1);
	return movie && movie->decoder?movie_get_duration(movie->decoder):0;
}

int PE_GetPartsMovieCurrentTime(int parts_no,int state)
{
	struct parts_movie *movie=find_movie(parts_no,state-1);
	return movie && movie->decoder?movie_get_position(movie->decoder):0;
}

bool PE_IsEndPartsMovie(int parts_no, int state)
{
	struct parts_movie *movie = find_movie(parts_no, state - 1);
	if (!movie)
		return false;
	if (movie->playing && movie_is_end(movie->decoder)) {
		movie->playing = false;
		movie->ended = true;
	}
	return movie->ended;
}

bool PE_ReleasePartsMovie(int parts_no, int state)
{
	struct parts_movie *movie = find_movie(parts_no, state - 1);
	if (!movie)
		return false;
	struct parts *parts = parts_try_get(parts_no);
	parts_state_reset(&parts->states[state - 1], PARTS_UNINITIALIZED);
	parts_dirty(parts);
	return true;
}

bool parts_movie_update(struct parts *parts, struct parts_movie *movie)
{
	if (!movie->playing || !movie->decoder)
		return false;
	int volume;
	struct sact_sprite *sprite = sact_try_get_sprite(movie->sprite_no);
	if (!sprite || !movie_group_volume(movie->sound_group, &volume)
			|| !movie_set_volume(movie->decoder, volume)
			|| !movie_draw(movie->decoder, sprite))
		VM_ERROR("Failed to update movie component %d", parts->no);
	movie->common.texture = *sprite_get_texture(sprite);
	if (movie_is_end(movie->decoder)) {
		movie->playing = false;
		movie->ended = true;
	}
	return true;
}
