#ifndef PARTS_TEXTURE_PARKING_INC
#define PARTS_TEXTURE_PARKING_INC

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*
 * GL-texture CPU parking for the main GL thread only. This file knows nothing
 * about Parts/VM/assets/strings; it only dereferences the stable Texture*
 * slots supplied by the caller.
 */
#ifndef PARTS_TEX_CAPTURE
#define PARTS_TEX_CAPTURE gfx_get_pixels
#endif
#ifndef PARTS_TEX_UPLOAD
#define PARTS_TEX_UPLOAD gfx_init_texture_with_pixels
#define PARTS_TEX_UPLOAD_INIT_DEFAULTS 1
#endif

struct parts_texture_park_slot {
	Texture *tex;
	int image; /* -1 for an original handle==0 slot */
	int w, h;
};

struct parts_texture_park_image {
	GLuint old_handle;
	int w, h;
	GLint min_filter, mag_filter, wrap_s, wrap_t;
	uint8_t *rgba;
	/* 1 when rgba is the texture's retained CPU source (owned through
	 * gfx_texture_park_source_*), 0 for a GPU readback. */
	int from_source;
};

struct parts_texture_park {
	size_t slot_count;
	size_t image_count;
	struct parts_texture_park_slot *slots;
	struct parts_texture_park_image *images;
	size_t bytes;
};

static void parts_texture_park_free(struct parts_texture_park *park)
{
	if (!park)
		return;
	for (size_t i = 0; i < park->image_count; i++) {
		if (park->images[i].from_source)
			gfx_texture_park_source_drop(park->images[i].rgba,
				(size_t)park->images[i].w * (size_t)park->images[i].h * 4u);
		else
			free(park->images[i].rgba);
	}
	free(park->images);
	free(park->slots);
	free(park);
}

static void parts_texture_restore_binding(GLint binding,
		const struct parts_texture_park *park)
{
	bool deleted = false;
	for (size_t i = 0; i < park->image_count; i++) {
		if ((GLuint)binding == park->images[i].old_handle) {
			deleted = true;
			break;
		}
	}
	glBindTexture(GL_TEXTURE_2D, deleted ? 0 : (GLuint)binding);
}

static bool parts_texture_suspend(Texture **slots, size_t count, size_t limit,
		struct parts_texture_park **out)
{
	if (!out || *out || count > 4096 || (count && !slots))
		return false;
	if (glGetError() != GL_NO_ERROR)
		return false;

	struct parts_texture_park *park = calloc(1, sizeof(*park));
	if (!park)
		return false;
	if (count) {
		park->slots = calloc(count, sizeof(*park->slots));
		park->images = calloc(count, sizeof(*park->images));
		if (!park->slots || !park->images) {
			parts_texture_park_free(park);
			return false;
		}
	}

	/* Validate every slot pointer and de-duplicate slot addresses first. */
	for (size_t i = 0; i < count; i++) {
		Texture *tex = slots[i];
		if (!tex) {
			parts_texture_park_free(park);
			return false;
		}
		bool duplicate = false;
		for (size_t j = 0; j < park->slot_count; j++) {
			if (park->slots[j].tex == tex) {
				duplicate = true;
				break;
			}
		}
		if (duplicate)
			continue;
		struct parts_texture_park_slot *slot = &park->slots[park->slot_count++];
		slot->tex = tex;
		slot->image = -1;
		slot->w = tex->w;
		slot->h = tex->h;
	}

	/* Build the unique-handle image table and validate sizes/budgets. */
	for (size_t i = 0; i < park->slot_count; i++) {
		struct parts_texture_park_slot *slot = &park->slots[i];
		Texture *tex = slot->tex;
		if (tex->handle == 0)
			continue;

		int image = -1;
		for (size_t j = 0; j < park->image_count; j++) {
			if (park->images[j].old_handle == tex->handle) {
				image = (int)j;
				break;
			}
		}
		if (image >= 0) {
			struct parts_texture_park_image *existing = &park->images[image];
			if (existing->w != tex->w || existing->h != tex->h) {
				parts_texture_park_free(park);
				return false;
			}
			slot->image = image;
			continue;
		}

		if (tex->w < 1 || tex->w > 8192 || tex->h < 1 || tex->h > 8192) {
			parts_texture_park_free(park);
			return false;
		}
		size_t bytes = (size_t)tex->w * (size_t)tex->h * 4;
		if (bytes > (size_t)64u * 1024u * 1024u ||
				bytes > limit - park->bytes) {
			parts_texture_park_free(park);
			return false;
		}

		struct parts_texture_park_image *img = &park->images[park->image_count];
		img->old_handle = tex->handle;
		img->w = tex->w;
		img->h = tex->h;
		slot->image = (int)park->image_count;
		if (slot->tex->park_source) {
			img->rgba = gfx_texture_park_source_take(slot->tex);
			img->from_source = img->rgba != NULL;
		}
		park->image_count++;
		park->bytes += bytes;
	}

	if (park->image_count == 0) {
		*out = park;
		return true;
	}

	GLint old_binding = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_binding);
	if (glGetError() != GL_NO_ERROR) {
		parts_texture_park_free(park);
		return false;
	}

	for (size_t i = 0; i < park->image_count; i++) {
		struct parts_texture_park_image *img = &park->images[i];
		glBindTexture(GL_TEXTURE_2D, img->old_handle);
		if (glGetError() != GL_NO_ERROR)
			goto capture_failed;

		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &img->min_filter);
		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &img->mag_filter);
		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &img->wrap_s);
		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &img->wrap_t);
		if (glGetError() != GL_NO_ERROR)
			goto capture_failed;

		if ((img->min_filter != GL_NEAREST && img->min_filter != GL_LINEAR) ||
				(img->mag_filter != GL_NEAREST && img->mag_filter != GL_LINEAR))
			goto capture_failed;

		if (img->rgba)
			continue; /* retained CPU source: no GPU readback */

		Texture capture = {
			.handle = img->old_handle,
			.w = img->w,
			.h = img->h,
		};
		void *rgba = PARTS_TEX_CAPTURE(&capture);
		if (!rgba || glGetError() != GL_NO_ERROR) {
			free(rgba);
			goto capture_failed;
		}
		img->rgba = rgba;
	}

	for (size_t i = 0; i < park->image_count; i++) {
		Texture texture = {
			.handle = park->images[i].old_handle,
			.w = park->images[i].w,
			.h = park->images[i].h,
		};
		gfx_delete_texture(&texture);
	}
	for (size_t i = 0; i < park->slot_count; i++)
		park->slots[i].tex->handle = 0;
	parts_texture_restore_binding(old_binding, park);
	*out = park;
	return true;

capture_failed:
	glBindTexture(GL_TEXTURE_2D, (GLuint)old_binding);
	parts_texture_park_free(park);
	return false;
}

static bool parts_texture_resume(struct parts_texture_park **parkp)
{
	if (!parkp || !*parkp)
		return true;
	struct parts_texture_park *park = *parkp;
	if (glGetError() != GL_NO_ERROR)
		return false;

	GLint old_binding = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_binding);
	if (glGetError() != GL_NO_ERROR)
		return false;

	for (size_t i = 0; i < park->slot_count; i++) {
		struct parts_texture_park_slot *slot = &park->slots[i];
		if (!slot->tex || slot->tex->handle != 0 ||
				slot->tex->w != slot->w || slot->tex->h != slot->h)
			return false;
	}

	Texture *temps = NULL;
	if (park->image_count) {
		temps = calloc(park->image_count, sizeof(*temps));
		if (!temps)
			return false;
	}

	for (size_t i = 0; i < park->image_count; i++) {
		struct parts_texture_park_image *img = &park->images[i];
		Texture tmp = {0};
		PARTS_TEX_UPLOAD(&tmp, img->w, img->h, img->rgba);
		temps[i] = tmp;
		if (!tmp.handle || glGetError() != GL_NO_ERROR)
			goto resume_failed;

#ifndef PARTS_TEX_UPLOAD_INIT_DEFAULTS
		/* Custom upload hooks need not leave this binding/default sampler. */
		glBindTexture(GL_TEXTURE_2D, tmp.handle);
		if (glGetError() != GL_NO_ERROR)
			goto resume_failed;
#endif
#define RESTORE_SAMPLER(parameter, field, initial) do { \
		if (restore_all || img->field != (initial)) { \
			glTexParameteri(GL_TEXTURE_2D, (parameter), img->field); \
			if (glGetError() != GL_NO_ERROR) goto resume_failed; \
		} \
	} while (0)
#ifdef PARTS_TEX_UPLOAD_INIT_DEFAULTS
		/* init_texture already set LINEAR/CLAMP and left tmp bound. */
		const bool restore_all = false;
#else
		const bool restore_all = true;
#endif
		RESTORE_SAMPLER(GL_TEXTURE_MIN_FILTER, min_filter, GL_LINEAR);
		RESTORE_SAMPLER(GL_TEXTURE_MAG_FILTER, mag_filter, GL_LINEAR);
		RESTORE_SAMPLER(GL_TEXTURE_WRAP_S, wrap_s, GL_CLAMP_TO_EDGE);
		RESTORE_SAMPLER(GL_TEXTURE_WRAP_T, wrap_t, GL_CLAMP_TO_EDGE);
#undef RESTORE_SAMPLER
	}

	for (size_t i = 0; i < park->slot_count; i++) {
		struct parts_texture_park_slot *slot = &park->slots[i];
		if (slot->image >= 0)
			slot->tex->handle = temps[slot->image].handle;
	}
	/* Give retained CPU sources back so a later suspend can park by pointer. */
	for (size_t i = 0; i < park->image_count; i++) {
		struct parts_texture_park_image *img = &park->images[i];
		if (!img->from_source || !img->rgba)
			continue;
		for (size_t s = 0; s < park->slot_count; s++) {
			struct parts_texture_park_slot *slot = &park->slots[s];
			if (slot->image == (int)i) {
				gfx_texture_park_source_give(slot->tex, img->rgba);
				img->rgba = NULL;
				break;
			}
		}
	}
	/* Names deleted at suspend may since have been reused by an unrelated
	 * texture. Resume restores the current binding, not the old-name filter. */
	glBindTexture(GL_TEXTURE_2D,(GLuint)old_binding);
	free(temps);
	parts_texture_park_free(park);
	*parkp = NULL;
	return true;

resume_failed:
	for (size_t i = 0; i < park->image_count; i++) {
		if (temps[i].handle)
			gfx_delete_texture(&temps[i]);
	}
	free(temps);
	glBindTexture(GL_TEXTURE_2D,(GLuint)old_binding);
	return false;
}

static void parts_texture_discard(struct parts_texture_park **parkp)
{
	if (!parkp || !*parkp)
		return;
	struct parts_texture_park *park = *parkp;
	*parkp = NULL;
	for (size_t i = 0; i < park->image_count; i++) {
		if (park->images[i].from_source)
			gfx_texture_park_source_drop(park->images[i].rgba,
				(size_t)park->images[i].w * (size_t)park->images[i].h * 4u);
		else
			free(park->images[i].rgba);
	}
	free(park->images);
	free(park->slots);
	free(park);
}

static size_t parts_texture_park_bytes(const struct parts_texture_park *park)
{
	return park ? park->bytes : 0;
}

#endif /* PARTS_TEXTURE_PARKING_INC */
