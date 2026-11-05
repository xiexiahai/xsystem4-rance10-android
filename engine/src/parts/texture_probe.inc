#ifndef PARTS_TEXTURE_PROBE_INC
#define PARTS_TEXTURE_PROBE_INC

/*
 * Opt-in, main-thread round-trip probe. It is compiled into texture_lifecycle.c
 * but does nothing unless the process environment explicitly enables it.
 * This is intentionally not an HLL entry point and does not alter normal
 * Suspend/Resume policy.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

struct parts_probe_cg_meta {
	int no;
	int w, h;
	char *name;
	size_t name_len;
};

struct parts_probe_flat_meta {
	int w, h;
	int end_frame;
	int current_frame;
	double elapsed;
	size_t libraries;
	char *name;
	size_t name_len;
};

struct parts_probe_slot {
	Texture *tex;
	int image;
	int w, h;
};

struct parts_probe_image {
	int w, h;
	GLint min_filter, mag_filter, wrap_s, wrap_t;
	uint8_t *rgba;
	size_t bytes;
};

struct parts_probe_ref {
	const char *kind;
	int part_no;
	size_t slot_count;
	size_t image_count;
	struct parts_probe_slot *slots;
	struct parts_probe_image *images;
	enum parts_type state_type[PARTS_NR_STATES];
	struct parts_probe_cg_meta cg[PARTS_NR_STATES];
	struct parts_probe_flat_meta flat[PARTS_NR_STATES];
	Point local_pos, global_pos;
	uint8_t local_alpha, global_alpha;
	size_t part_bytes;
};

static bool parts_probe_recursion_guard;
static bool parts_probe_cg_done;
static bool parts_probe_flat_done;
static int parts_probe_parts[2];
static unsigned parts_probe_part_count;

static char *parts_probe_dup_name(const struct string *s, size_t *len)
{
	*len = 0;
	if (!s || s->size < 0)
		return NULL;
	*len = (size_t)s->size;
	char *copy = malloc(*len + 1);
	if (!copy)
		return NULL;
	if (*len)
		memcpy(copy, s->text, *len);
	copy[*len] = '\0';
	return copy;
}

static bool parts_probe_name_equal(const char *name, size_t len,
		const struct string *actual)
{
	if (!name)
		return !actual;
	if (!actual || actual->size < 0 || (size_t)actual->size != len)
		return false;
	if (len == 0)
		return true;
	return !memcmp(name, actual->text, len);
}

static bool parts_probe_part_seen(int no)
{
	for (unsigned i = 0; i < parts_probe_part_count; i++)
		if (parts_probe_parts[i] == no)
			return true;
	return false;
}

static void parts_probe_free(struct parts_probe_ref *ref)
{
	if (!ref)
		return;
	if (ref->images) {
		for (size_t i = 0; i < ref->image_count; i++)
			free(ref->images[i].rgba);
	}
	free(ref->images);
	free(ref->slots);
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		free(ref->cg[i].name);
		free(ref->flat[i].name);
	}
	free(ref);
}

static void parts_probe_fail(struct parts_probe_ref *ref, size_t slot,
		const char *what)
{
	const char *kind = ref ? ref->kind : "?";
	int part = ref ? ref->part_no : -1;
	parts_probe_free(ref);
	parts_probe_recursion_guard = false;
	ERROR("[texture-probe] FAIL kind=%s part=%d slot=%zu %s", kind, part, slot, what);
}

static bool parts_probe_meta_equal(struct parts_probe_ref *ref,
		struct parts *p)
{
	if (p->local.pos.x != ref->local_pos.x ||
			p->local.pos.y != ref->local_pos.y ||
			p->local.alpha != ref->local_alpha ||
			p->global.pos.x != ref->global_pos.x ||
			p->global.pos.y != ref->global_pos.y ||
			p->global.alpha != ref->global_alpha)
		return false;
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		struct parts_state *state = &p->states[i];
		if (state->type != ref->state_type[i])
			return false;
		if (state->type == PARTS_CG) {
			if (state->cg.no != ref->cg[i].no ||
					state->cg.common.w != ref->cg[i].w ||
					state->cg.common.h != ref->cg[i].h ||
					!parts_probe_name_equal(ref->cg[i].name,
						ref->cg[i].name_len, state->cg.name))
				return false;
		}
		if (state->type == PARTS_FLAT) {
			struct parts_flat *f = &state->flat;
			int current = f->root_state
				? f->root_state->current_frame + f->pending_seek_delta : -1;
			if (f->common.w != ref->flat[i].w ||
					f->common.h != ref->flat[i].h ||
					f->end_frame != ref->flat[i].end_frame ||
					current != ref->flat[i].current_frame ||
					f->elapsed_frames != ref->flat[i].elapsed ||
					(f->flat ? f->flat->nr_libraries : 0) != ref->flat[i].libraries ||
					!parts_probe_name_equal(ref->flat[i].name,
						ref->flat[i].name_len, f->name))
				return false;
		}
	}
	return true;
}

static bool parts_probe_check_live(struct parts_probe_ref *ref,
		struct parts *p, size_t *bad_slot)
{
	if (p->parked_textures)
		return false;
	GLuint *handles = calloc(ref->image_count ? ref->image_count : 1,
			sizeof(*handles));
	if (!handles)
		parts_probe_fail(ref, 0, "allocation while checking live handles");
	for (size_t i = 0; i < ref->slot_count; i++) {
		struct parts_probe_slot *slot = &ref->slots[i];
		if (!slot->tex) {
			free(handles);
			*bad_slot = i;
			return false;
		}
		if (slot->image < 0) {
			if (slot->tex->handle != 0 ||
					slot->tex->w != slot->w || slot->tex->h != slot->h) {
				free(handles);
				*bad_slot = i;
				return false;
			}
			continue;
		}
		if (!slot->tex->handle || !glIsTexture(slot->tex->handle) ||
				slot->tex->w != slot->w || slot->tex->h != slot->h) {
			free(handles);
			*bad_slot = i;
			return false;
		}
		if (handles[slot->image] == 0)
			handles[slot->image] = slot->tex->handle;
		else if (handles[slot->image] != slot->tex->handle) {
			free(handles);
			*bad_slot = i;
			return false;
		}
	}
	for (size_t i = 0; i < ref->image_count; i++) {
		if (!handles[i]) {
			free(handles);
			*bad_slot = ref->slot_count;
			return false;
		}
		for (size_t j = i + 1; j < ref->image_count; j++) {
			if (handles[i] == handles[j]) {
				free(handles);
				*bad_slot = ref->slot_count;
				return false;
			}
		}
	}
	for (size_t i = 0; i < ref->image_count; i++) {
		Texture *texture = NULL;
		size_t first_slot = ref->slot_count;
		for (size_t j = 0; j < ref->slot_count; j++) {
			if (ref->slots[j].image == (int)i) {
				texture = ref->slots[j].tex;
				first_slot = j;
				break;
			}
		}
		if (!texture) {
			free(handles);
			*bad_slot = ref->slot_count;
			return false;
		}
		glBindTexture(GL_TEXTURE_2D, texture->handle);
		if (glGetError() != GL_NO_ERROR) {
			free(handles);
			*bad_slot = first_slot;
			return false;
		}
		GLint value = 0;
		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &value);
		if (glGetError() != GL_NO_ERROR || value != ref->images[i].min_filter) {
			free(handles);
			*bad_slot = first_slot;
			return false;
		}
		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &value);
		if (glGetError() != GL_NO_ERROR || value != ref->images[i].mag_filter) {
			free(handles);
			*bad_slot = first_slot;
			return false;
		}
		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &value);
		if (glGetError() != GL_NO_ERROR || value != ref->images[i].wrap_s) {
			free(handles);
			*bad_slot = first_slot;
			return false;
		}
		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &value);
		if (glGetError() != GL_NO_ERROR || value != ref->images[i].wrap_t) {
			free(handles);
			*bad_slot = first_slot;
			return false;
		}
		uint8_t *pixels = gfx_get_pixels(texture);
		if (!pixels || glGetError() != GL_NO_ERROR ||
				memcmp(pixels, ref->images[i].rgba, ref->images[i].bytes)) {
			free(pixels);
			free(handles);
			*bad_slot = first_slot;
			return false;
		}
		free(pixels);
	}
	free(handles);
	if (!parts_probe_meta_equal(ref, p)) {
		*bad_slot = ref->slot_count;
		return false;
	}
	return true;
}

static bool parts_probe_check_reparked(struct parts_probe_ref *ref,
		struct parts *p, size_t *bad_slot)
{
	if (!p->parked_textures ||
			p->parked_textures->slot_count != ref->slot_count ||
			p->parked_textures->image_count != ref->image_count ||
			parts_texture_park_bytes(p->parked_textures) != ref->part_bytes) {
		*bad_slot = ref->slot_count;
		return false;
	}
	for (size_t i = 0; i < ref->slot_count; i++) {
		if (!ref->slots[i].tex || p->parked_textures->slots[i].tex != ref->slots[i].tex ||
				p->parked_textures->slots[i].image != ref->slots[i].image ||
				p->parked_textures->slots[i].w != ref->slots[i].w ||
				p->parked_textures->slots[i].h != ref->slots[i].h ||
				ref->slots[i].tex->handle != 0 ||
				ref->slots[i].tex->w != ref->slots[i].w ||
				ref->slots[i].tex->h != ref->slots[i].h) {
			*bad_slot = i;
			return false;
		}
	}
	for (size_t i = 0; i < ref->image_count; i++) {
		size_t first_slot = ref->slot_count;
		for (size_t j = 0; j < ref->slot_count; j++) {
			if (ref->slots[j].image == (int)i) {
				first_slot = j;
				break;
			}
		}
		struct parts_texture_park_image *img = &p->parked_textures->images[i];
		if (img->w != ref->images[i].w || img->h != ref->images[i].h ||
				img->min_filter != ref->images[i].min_filter ||
				img->mag_filter != ref->images[i].mag_filter ||
				img->wrap_s != ref->images[i].wrap_s ||
				img->wrap_t != ref->images[i].wrap_t ||
				!img->rgba ||
				memcmp(img->rgba, ref->images[i].rgba, ref->images[i].bytes)) {
			*bad_slot = first_slot;
			return false;
		}
	}
	if (!parts_probe_meta_equal(ref, p)) {
		*bad_slot = ref->slot_count;
		return false;
	}
	return true;
}

static void parts_textures_roundtrip_probe(struct parts *p)
{
	if (!getenv("XSYSTEM4_TEST_TEXTURE_ROUNDTRIP"))
		return;
	if (parts_probe_recursion_guard || !p || !p->parked_textures)
		return;

	bool has_flat = false;
	bool has_cg = false;
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		if (p->states[i].type == PARTS_FLAT && p->states[i].flat.flat)
			has_flat = true;
		if (p->states[i].type == PARTS_CG && p->states[i].cg.common.w > 0 &&
				p->states[i].cg.common.h > 0)
			has_cg = true;
	}
	const char *kind = has_flat ? "FLAT" : has_cg ? "CG" : NULL;
	if (!kind)
		return;
	if ((!strcmp(kind, "CG") && parts_probe_cg_done) ||
			(!strcmp(kind, "FLAT") && parts_probe_flat_done))
		return;
	if (parts_probe_part_seen(p->no) ||
			p->parked_textures->image_count == 0)
		return;

	size_t part_bytes = parts_texture_park_bytes(p->parked_textures);
	if (part_bytes > (size_t)32u * 1024u * 1024u) {
		NOTICE("[texture-probe] SKIP kind=%s part=%d bytes=%zu reason=over32MiB",
			kind, p->no, part_bytes);
		return;
	}

	struct parts_probe_ref *ref = calloc(1, sizeof(*ref));
	if (!ref) {
		parts_probe_recursion_guard = false;
		ERROR("[texture-probe] FAIL kind=%s part=%d slot=0 allocation", kind, p->no);
	}
	ref->kind = kind;
	ref->part_no = p->no;
	ref->part_bytes = part_bytes;
	ref->slot_count = p->parked_textures->slot_count;
	ref->image_count = p->parked_textures->image_count;
	ref->slots = calloc(ref->slot_count ? ref->slot_count : 1, sizeof(*ref->slots));
	ref->images = calloc(ref->image_count ? ref->image_count : 1, sizeof(*ref->images));
	if (!ref->slots || !ref->images)
		parts_probe_fail(ref, 0, "allocation");

	for (size_t i = 0; i < ref->slot_count; i++) {
		struct parts_texture_park_slot *slot = &p->parked_textures->slots[i];
		if (!slot->tex)
			parts_probe_fail(ref, i, "NULL parked slot");
		ref->slots[i].tex = slot->tex;
		ref->slots[i].image = slot->image;
		ref->slots[i].w = slot->w;
		ref->slots[i].h = slot->h;
		if (slot->image < -1 || slot->image >= (int)ref->image_count)
			parts_probe_fail(ref, i, "invalid image alias mapping");
	}
	for (size_t i = 0; i < ref->image_count; i++) {
		struct parts_texture_park_image *img = &p->parked_textures->images[i];
		ref->images[i].w = img->w;
		ref->images[i].h = img->h;
		ref->images[i].min_filter = img->min_filter;
		ref->images[i].mag_filter = img->mag_filter;
		ref->images[i].wrap_s = img->wrap_s;
		ref->images[i].wrap_t = img->wrap_t;
		ref->images[i].bytes = (size_t)img->w * (size_t)img->h * 4;
		if (!img->rgba || ref->images[i].bytes == 0)
			parts_probe_fail(ref, ref->slot_count, "missing parked RGBA");
		ref->images[i].rgba = malloc(ref->images[i].bytes);
		if (!ref->images[i].rgba)
			parts_probe_fail(ref, ref->slot_count, "allocation");
		memcpy(ref->images[i].rgba, img->rgba, ref->images[i].bytes);
	}

	ref->local_pos = p->local.pos;
	ref->global_pos = p->global.pos;
	ref->local_alpha = p->local.alpha;
	ref->global_alpha = p->global.alpha;
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		struct parts_state *state = &p->states[i];
		ref->state_type[i] = state->type;
		if (state->type == PARTS_CG) {
			ref->cg[i].no = state->cg.no;
			ref->cg[i].w = state->cg.common.w;
			ref->cg[i].h = state->cg.common.h;
			ref->cg[i].name = parts_probe_dup_name(state->cg.name,
					&ref->cg[i].name_len);
			if (state->cg.name && !ref->cg[i].name)
				parts_probe_fail(ref, ref->slot_count, "CG name allocation");
		} else if (state->type == PARTS_FLAT) {
			struct parts_flat *f = &state->flat;
			ref->flat[i].w = f->common.w;
			ref->flat[i].h = f->common.h;
			ref->flat[i].end_frame = f->end_frame;
			ref->flat[i].current_frame = f->root_state
				? f->root_state->current_frame + f->pending_seek_delta : -1;
			ref->flat[i].elapsed = f->elapsed_frames;
			ref->flat[i].libraries = f->flat ? f->flat->nr_libraries : 0;
			ref->flat[i].name = parts_probe_dup_name(f->name,
					&ref->flat[i].name_len);
			if (f->name && !ref->flat[i].name)
				parts_probe_fail(ref, ref->slot_count, "FLAT name allocation");
		}
	}

	size_t before_bytes = parked_bytes;
	unsigned before_parts = parked_parts;
	GLint previous_binding=0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D,&previous_binding);
	if (glGetError()!=GL_NO_ERROR)
		parts_probe_fail(ref,ref->slot_count,"capture binding");
	parts_probe_recursion_guard = true;
	parts_textures_require_resident(p);

	if (parked_bytes != before_bytes - part_bytes ||
			parked_parts != before_parts - 1 || p->parked_textures != NULL)
		parts_probe_fail(ref, ref->slot_count, "resume accounting");
	size_t bad_slot = 0;
	if (!parts_probe_check_live(ref, p, &bad_slot))
		parts_probe_fail(ref, bad_slot, "live texture/metadata mismatch");

	PE_SuspendTexture(p->no);
	if (!p->parked_textures)
		parts_probe_fail(ref, ref->slot_count, "re-park did not restore");
	if (parked_bytes != before_bytes || parked_parts != before_parts)
		parts_probe_fail(ref, ref->slot_count, "re-park accounting");
	if (!parts_probe_check_reparked(ref, p, &bad_slot))
		parts_probe_fail(ref, bad_slot, "re-parked texture/metadata mismatch");
	glBindTexture(GL_TEXTURE_2D,(GLuint)previous_binding);
	if (glGetError()!=GL_NO_ERROR)
		parts_probe_fail(ref,ref->slot_count,"restore binding");

	size_t images = ref->image_count;
	size_t slots = ref->slot_count;
	parts_probe_free(ref);
	parts_probe_recursion_guard = false;
	if (!strcmp(kind, "CG"))
		parts_probe_cg_done = true;
	else
		parts_probe_flat_done = true;
	if (parts_probe_part_count < 2)
		parts_probe_parts[parts_probe_part_count++] = p->no;
	NOTICE("[texture-probe] PASS kind=%s images=%zu bytes=%zu slots=%zu",
		kind, images, part_bytes, slots);
}

#endif /* PARTS_TEXTURE_PROBE_INC */
