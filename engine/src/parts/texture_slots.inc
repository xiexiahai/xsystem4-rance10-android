#ifndef PARTS_TEXTURE_SLOTS_INC
#define PARTS_TEXTURE_SLOTS_INC

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* Collect the addresses of every Texture field owned by one synthetic/live
 * part.  This is deliberately a type-ownership walk, not a GL or asset probe:
 * handles are never inspected and aliases are not deduplicated.  The caller
 * owns the returned pointer array, not the Texture fields. */
static bool parts_texture_slots_add(Texture **slots, size_t *used,
		size_t limit, Texture *texture)
{
	if (*used >= limit)
		return false;
	if (slots)
		slots[*used] = texture;
	(*used)++;
	return true;
}

static bool parts_texture_slots_state(struct parts_state *state,
		Texture **slots, size_t *used, size_t limit)
{
	switch (state->type) {
	case PARTS_UNINITIALIZED:
	case PARTS_RECT_DETECTION:
	case PARTS_LAYOUT_BOX:
	case PARTS_CG:
	case PARTS_NUMERAL:
	case PARTS_CONSTRUCTION_PROCESS:
		return parts_texture_slots_add(slots, used, limit,
			&state->common.texture);
	case PARTS_TEXT: {
		if (!parts_texture_slots_add(slots, used, limit, &state->common.texture))
			return false;
		size_t lines = state->text.nr_lines;
		if (lines > 4096 || (lines && !state->text.lines))
			return false;
		for (size_t i = 0; i < lines; i++) {
			struct parts_text_line *line = &state->text.lines[i];
			if (line->nr_chars < 0 || line->nr_chars > 4096 ||
					(line->nr_chars && !line->chars))
				return false;
			for (int j = 0; j < line->nr_chars; j++)
				if (!parts_texture_slots_add(slots, used, limit,
						&line->chars[j].t))
					return false;
		}
		return true;
	}
	case PARTS_ANIMATION:
		if (!parts_texture_slots_add(slots, used, limit, &state->common.texture))
			return false;
		if (state->anim.nr_frames > 4096 ||
				(state->anim.nr_frames && !state->anim.frames))
			return false;
		for (unsigned i = 0; i < state->anim.nr_frames; i++)
			if (!parts_texture_slots_add(slots, used, limit,
					&state->anim.frames[i]))
				return false;
		return true;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		if (!parts_texture_slots_add(slots, used, limit, &state->common.texture))
			return false;
		return parts_texture_slots_add(slots, used, limit, &state->gauge.cg);
	case PARTS_FLAT:
		if (!parts_texture_slots_add(slots, used, limit, &state->common.texture))
			return false;
		if (state->flat.nr_libraries > 4096 ||
				(state->flat.nr_libraries && !state->flat.textures))
			return false;
		for (size_t i = 0; i < state->flat.nr_libraries; i++)
			if (!parts_texture_slots_add(slots, used, limit,
					&state->flat.textures[i]))
				return false;
		return true;
	case PARTS_FLASH:
	case PARTS_MOVIE:
	case PARTS_3DLAYER:
		return false;
	case PARTS_MESSAGE_WINDOW:
		return parts_texture_slots_add(slots,used,limit,&state->common.texture) &&
			parts_message_window_slots(state->window.model,slots,used,limit);
	default:
		return false;
	}
}

#include "textbox_internal.h"
static bool parts_texture_slots_widget(struct parts_widget *widget,
		Texture **slots, size_t *used, size_t limit)
{
	if (!widget)
		return true;
    if(widget->textbox) {
        for(size_t i=0;i<27;i++)
            if(!parts_texture_slots_add(slots,used,limit,&widget->textbox->skin[i]))return false;
    }
	for (size_t i = 0; i < 20; i++)
		if (!parts_texture_slots_add(slots, used, limit, &widget->images[i]))
			return false;
    for(size_t i=7;i<20;i++) if(widget->scroll_flats[i]) {
        struct parts_flat *f=widget->scroll_flats[i];
        if(!parts_texture_slots_add(slots,used,limit,&f->common.texture) ||
            f->nr_libraries>4096 || (f->nr_libraries && !f->textures))return false;
        for(size_t k=0;k<f->nr_libraries;k++)
            if(!parts_texture_slots_add(slots,used,limit,&f->textures[k]))return false;
    }
	return true;
}

static bool parts_texture_slots_collect(struct parts *parts, Texture ***out,
		size_t *count)
{
	if (!parts || !out || !count || *out)
		return false;

	size_t used = 0;
	for (int i = 0; i < PARTS_NR_STATES; i++)
		if (!parts_texture_slots_state(&parts->states[i], NULL, &used, 4096))
			return false;
	if (!parts_texture_slots_widget(parts->widget, NULL, &used, 4096))
		return false;

	if (used > SIZE_MAX / sizeof(Texture *))
		return false;
	Texture **slots = malloc(used * sizeof(*slots));
	if (!slots)
		return false;

	size_t filled = 0;
	for (int i = 0; i < PARTS_NR_STATES; i++)
		if (!parts_texture_slots_state(&parts->states[i], slots, &filled, used)) {
			free(slots);
			return false;
		}
	if (!parts_texture_slots_widget(parts->widget, slots, &filled, used)) {
		free(slots);
		return false;
	}
	if (filled != used) {
		free(slots);
		return false;
	}

	*out = slots;
	*count = used;
	return true;
}

#endif /* PARTS_TEXTURE_SLOTS_INC */
