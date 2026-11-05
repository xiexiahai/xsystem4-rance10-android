#include "scene-batch.h"
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
#include <limits.h>
#include <math.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/hashtable.h"
#include "system4/string.h"

#include "asset_manager.h"
#include "audio.h"
#include "vm/page.h"
#include "xsystem4.h"
#include "sact.h"
#include "sprite.h"
#include "parts.h"
#include "perf_window.h"
#include "parts_internal.h"
#include "numeral_layout.inc"
#include "gauge_layout.inc"
#include "parts_activity.h"
#include "reign.h"
#include "savedata.h"

struct parts_list parts_list = TAILQ_HEAD_INITIALIZER(parts_list);
static struct parts_list dirty_list = TAILQ_HEAD_INITIALIZER(dirty_list);
static struct hash_table *parts_table = NULL;

#define PARTS_PARAMS_INITIALIZER(Z) (struct parts_params) { \
	.z = Z, \
	.pos = { 0, 0 }, \
	.show = true, \
	.alpha = 255, \
	.scale = { 1.0f, 1.0f }, \
	.rotation = { 0.0f, 0.0f, 0.0f }, \
	.add_color = { 0, 0, 0, 0 }, \
	.multiply_color = { 255, 255, 255, 255 }, \
	.alpha_clipper_parts_no = 0 \
}

static struct parts_params root_params = PARTS_PARAMS_INITIALIZER(0);

struct parts_controller_stack ctrl_stack;
bool parts_multi_controller;

static void ctrl_stack_init(void);

static void parts_init(struct parts *parts)
{
	parts->sp.z = 1;
	parts->sp.has_pixel = true;
	parts->sp.has_alpha = true;
	parts->sp.render = parts_sprite_render;
	parts->sp.to_json = parts_sprite_to_json;
	parts->local = PARTS_PARAMS_INITIALIZER(1);
	parts->global = PARTS_PARAMS_INITIALIZER(1);
	parts->enable_speed_up = true;
	parts->delegate_index = -1;
	parts->want_save = true;
	parts->want_save_back_scene = true;
	parts->on_cursor_sound = -1;
	parts->normal_sound = -1;
	parts->on_click_sound = -1;
	parts->origin_mode = 1;
	parts->pending_parent = -1;
	parts->linked_to = -1;
	parts->linked_from = -1;
	TAILQ_INIT(&parts->children);
	TAILQ_INIT(&parts->motion);
}

static struct parts *parts_alloc(void)
{
	struct parts *parts = xcalloc(1, sizeof(struct parts));
	parts_init(parts);
	return parts;
}

struct parts *parts_alloc_back_scene(int parts_no)
{
	struct parts *parts = parts_alloc();
	parts->no = parts_no;
	parts->is_back_scene = true;
	parts->want_save = parts->want_save_back_scene = false;
	return parts;
}

void parts_component_dirty(struct parts *parts)
{
	if (parts->is_back_scene)
		return;
	if (parts->dirty)
		return;
	parts->dirty = true;
	TAILQ_INSERT_TAIL(&dirty_list, parts, dirty_list_entry);
}

static void dirty_list_remove(struct parts *parts)
{
	if (parts->dirty)
		TAILQ_REMOVE(&dirty_list, parts, dirty_list_entry);
}

static int parts_get_sprite_z(struct parts *parts)
{
	if (!parts_multi_controller)
		return parts->global.z;
	// The system overlay controller sorts above any in-stack controller.
	return PE_get_controller_index(parts->controller_no);
}

static int parts_get_sprite_z2(struct parts *parts)
{
	if (!parts_multi_controller)
		return 0;
	return parts->global.z;
}

static void parts_list_insert(struct parts *parts)
{
	int z = parts_get_sprite_z(parts);
	int z2 = parts_get_sprite_z2(parts);
	parts->sp.z = z;
	parts->sp.z2 = z2;
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		int pz = parts_get_sprite_z(p);
		int pz2 = parts_get_sprite_z2(p);
		if (pz > z || (pz == z && pz2 > z2)) {
			TAILQ_INSERT_BEFORE(p, parts, parts_list_entry);
			goto done;
		}
	}
	TAILQ_INSERT_TAIL(&parts_list, parts, parts_list_entry);
done:
	parts_engine_dirty();
	scene_register_sprite(&parts->sp);
}

static void parts_list_remove(struct parts *parts)
{
	TAILQ_REMOVE(&parts_list, parts, parts_list_entry);
	scene_unregister_sprite(&parts->sp);
}

void parts_list_resort(struct parts *parts)
{
	// TODO: this could be optimized
	parts_list_remove(parts);
	parts_list_insert(parts);
}

struct parts *parts_try_get(int parts_no)
{
	return ht_get_int(parts_table, parts_no, NULL);
}

struct parts *parts_get(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (slot->value)
		return slot->value;

	struct parts *parts = parts_alloc();
	parts->no = parts_no;
	parts->controller_no = ctrl_stack.active;
	slot->value = parts;
	parts_list_insert(parts);
	return parts;
}

bool parts_exists(int parts_no)
{
	return !!ht_get_int(parts_table, parts_no, NULL);
}

static void parts_state_free(struct parts_state *state)
{
	parts_textures_require_state(state);
	parts_flat_async_cancel_state(state);
	switch (state->type) {
	case PARTS_UNINITIALIZED:
	case PARTS_RECT_DETECTION:
	case PARTS_LAYOUT_BOX:
		break;
	case PARTS_CG:
		gfx_delete_texture(&state->common.texture);
		if (state->cg.name)
			free_string(state->cg.name);
		break;
	case PARTS_TEXT:
		parts_text_free(&state->text);
		break;
	case PARTS_ANIMATION:
		for (unsigned i = 0; i < state->anim.nr_frames; i++) {
			gfx_delete_texture(&state->anim.frames[i]);
		}
		free(state->anim.frames);
		if (state->anim.cg_name)
			free_string(state->anim.cg_name);
		break;
	case PARTS_NUMERAL:
		gfx_delete_texture(&state->common.texture);
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		gfx_delete_texture(&state->common.texture);
		gfx_delete_texture(&state->gauge.cg);
		if (state->gauge.cg_name) free_string(state->gauge.cg_name);
		break;
	case PARTS_CONSTRUCTION_PROCESS:
		gfx_delete_texture(&state->common.texture);
		parts_clear_construction_process(&state->cproc);
		break;
	case PARTS_FLASH:
		parts_flash_free(&state->flash);
		break;
	case PARTS_FLAT:
		parts_flat_free(&state->flat);
		break;
	case PARTS_MOVIE:
		parts_movie_free(&state->movie);
		break;
	case PARTS_3DLAYER:
		state->common.texture.handle = 0;
		if (state->layer3d.plugin >= 0)
			ReignEngine_ReleasePlugin(state->layer3d.plugin);
		if (state->layer3d.sprite_no >= 0)
			sact_SP_Delete(state->layer3d.sprite_no);
		break;
	case PARTS_MESSAGE_WINDOW:
		parts_message_window_free(state->window.model);
		break;
	}
	memset(state, 0, sizeof(struct parts_state));
}

static void parts_free_user_component(struct parts *p)
{
	if(p->user_component_name) free_string(p->user_component_name);
	for(int i=0;i<p->nr_user_component_data;i++) {
		free_string(p->user_component_data[i].key);free_string(p->user_component_data[i].value);
	}
	free(p->user_component_data);
}

void PE_SetUserComponentName(int number,struct string *name)
{
	struct parts *p=parts_get(number);
	if(p->user_component_name) free_string(p->user_component_name);
	p->user_component_name=string_ref(name);
}
struct string *PE_GetUserComponentName(int number)
{
	struct parts *p=parts_get(number);
	return string_ref(p->user_component_name?p->user_component_name:&EMPTY_STRING);
}
void PE_SetUserComponentData(int number,struct string *key,struct string *value)
{
	struct parts *p=parts_get(number);int i;
	for(i=0;i<p->nr_user_component_data;i++) if(!strcmp(p->user_component_data[i].key->text,key->text)) break;
	if(i==p->nr_user_component_data) {
		p->user_component_data=xrealloc(p->user_component_data,++p->nr_user_component_data*sizeof(*p->user_component_data));
		p->user_component_data[i].key=string_ref(key);p->user_component_data[i].value=NULL;
	}
	if(p->user_component_data[i].value) free_string(p->user_component_data[i].value);
	p->user_component_data[i].value=string_ref(value);
}
struct string *PE_GetUserComponentData(int number,struct string *key)
{
	struct parts *p=parts_get(number);
	for(int i=0;i<p->nr_user_component_data;i++) if(!strcmp(p->user_component_data[i].key->text,key->text))
		return string_ref(p->user_component_data[i].value);
	return string_ref(&EMPTY_STRING);
}

void parts_free_back_scene(struct parts *parts)
{
	parts_free_user_component(parts);
	if (parts->comment) free_string(parts->comment);
	scene_unregister_sprite(&parts->sp);
	parts_widget_free(parts->widget);
	for (int i = 0; i < PARTS_NR_STATES; i++)
		parts_state_free(&parts->states[i]);
	free(parts);
}

static struct text_style default_text_style = {
	.face = FONT_GOTHIC,
	.size = 16.0f,
	.bold_width = 0.0f,
	.weight = FW_NORMAL,
	.edge_left = 0.0f,
	.edge_up = 0.0f,
	.edge_right = 0.0f,
	.edge_down = 0.0f,
	.color = { .r = 255, .g = 255, .b = 255, .a = 255 },
	.edge_color = { .r = 0, .g = 0, .b = 0, .a = 255 },
	.scale_x = 1.0f,
	.space_scale_x = 1.0f,
	.font_spacing = 0.0f,
	.font_size = NULL
};

void parts_state_reset(struct parts_state *state, enum parts_type type)
{
	parts_state_free(state);
	state->type = type;
	switch (type) {
	case PARTS_TEXT:
		state->text.ts = default_text_style;
		break;
	case PARTS_NUMERAL:
		state->num.length = 1;
		state->num.font_no = -1;
		break;
	case PARTS_CONSTRUCTION_PROCESS:
		TAILQ_INIT(&state->cproc.ops);
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		state->gauge.cg_no = -1;
		state->gauge.rate = 0.0f;
		state->gauge.numerator = 0.0f;
		state->gauge.denominator = 1.0f;
		state->gauge.reverse = false;
		break;
	case PARTS_3DLAYER:
		state->layer3d.plugin = -1;
		state->layer3d.sprite_no = -1;
		break;
	case PARTS_FLAT:
		state->flat.fps_rate = 1.0f;
		break;
	case PARTS_UNINITIALIZED:
	case PARTS_CG:
	case PARTS_ANIMATION:
	case PARTS_FLASH:
	case PARTS_RECT_DETECTION:
	case PARTS_MESSAGE_WINDOW:
		break;
	case PARTS_MOVIE:
		state->movie.sprite_no = -1;
		break;
	case PARTS_LAYOUT_BOX:
		state->layout_box.layout_type = PARTS_LAYOUT_VERTICAL;
		state->layout_box.align = 1;
		break;
	}
}

struct parts_cg *parts_get_cg(struct parts *parts, int state)
{
	parts_textures_require_resident(parts);
	if (parts->states[state].type != PARTS_CG) {
		parts_state_reset(&parts->states[state], PARTS_CG);
	}
	return &parts->states[state].cg;
}

struct parts_text *parts_get_text(struct parts *parts, int state)
{
	parts_textures_require_resident(parts);
	if (parts->states[state].type != PARTS_TEXT) {
		parts_state_reset(&parts->states[state], PARTS_TEXT);
	}
	return &parts->states[state].text;
}

struct parts_animation *parts_get_animation(struct parts *parts, int state)
{
	parts_textures_require_resident(parts);
	if (parts->states[state].type != PARTS_ANIMATION) {
		parts_state_reset(&parts->states[state], PARTS_ANIMATION);
	}
	return &parts->states[state].anim;
}

struct parts_numeral *parts_get_numeral(struct parts *parts, int state)
{
	parts_textures_require_resident(parts);
	if (parts->states[state].type != PARTS_NUMERAL) {
		parts_state_reset(&parts->states[state], PARTS_NUMERAL);
	}
	return &parts->states[state].num;
}

struct parts_gauge *parts_get_hgauge(struct parts *parts, int state)
{
	parts_textures_require_resident(parts);
	if (parts->states[state].type != PARTS_HGAUGE) {
		parts_state_reset(&parts->states[state], PARTS_HGAUGE);
	}
	return &parts->states[state].gauge;
}

struct parts_gauge *parts_get_vgauge(struct parts *parts, int state)
{
	parts_textures_require_resident(parts);
	if (parts->states[state].type != PARTS_VGAUGE) {
		parts_state_reset(&parts->states[state], PARTS_VGAUGE);
	}
	return &parts->states[state].gauge;
}

struct parts_construction_process *parts_get_construction_process(struct parts *parts, int state)
{
	parts_textures_require_resident(parts);
	if (parts->states[state].type != PARTS_CONSTRUCTION_PROCESS) {
		parts_state_reset(&parts->states[state], PARTS_CONSTRUCTION_PROCESS);
	}
	return &parts->states[state].cproc;
}

struct parts_flash *parts_get_flash(struct parts *parts, int state)
{
	parts_textures_require_resident(parts);
	if (parts->states[state].type != PARTS_FLASH) {
		parts_state_reset(&parts->states[state], PARTS_FLASH);
	}
	return &parts->states[state].flash;
}

struct parts_flat *parts_get_flat(struct parts *parts, int state)
{
	parts_textures_require_resident(parts);
	if (parts->states[state].type != PARTS_FLAT) {
		parts_state_reset(&parts->states[state], PARTS_FLAT);
	}
	return &parts->states[state].flat;
}

struct parts_movie *parts_get_movie(struct parts *parts, int state)
{
	parts_textures_require_resident(parts);
	if (parts->states[state].type != PARTS_MOVIE) {
		parts_state_reset(&parts->states[state], PARTS_MOVIE);
	}
	return &parts->states[state].movie;
}

struct parts_layout_box *parts_get_layout_box(struct parts *parts)
{
	parts_textures_require_resident(parts);
	if (parts->states[0].type != PARTS_LAYOUT_BOX) {
		parts_state_reset(&parts->states[0], PARTS_LAYOUT_BOX);
		parts_recalculate_hitbox(parts);
	}
	return &parts->states[0].layout_box;
}

struct parts_3dlayer *parts_get_3dlayer(struct parts *parts, int state)
{
	parts_textures_require_resident(parts);
	if (parts->states[state].type != PARTS_3DLAYER) {
		parts_state_reset(&parts->states[state], PARTS_3DLAYER);
	}
	return &parts->states[state].layer3d;
}

static Point calculate_offset(int mode, int w, int h)
{
	switch (mode) {
	case 1:  return (Point) {    0, 0    }; // top-left
	case 2:  return (Point) { -w/2, 0    }; // top-center
	case 3:  return (Point) {   -w, 0    }; // top-right
	case 4:  return (Point) {    0, -h/2 }; // middle-left
	case 5:  return (Point) { -w/2, -h/2 }; // middle-center
	case 6:  return (Point) {   -w, -h/2 }; // middle-right
	case 7:  return (Point) {    0, -h   }; // bottom-left
	case 8:  return (Point) { -w/2, -h   }; // bottom-center
	case 9:  return (Point) {   -w, -h   }; // bottom-right
	default: return (Point) { mode, (3*h)/4 }; // why...
	}
}

/*
 * Should be called when:
 *   - position (parts->pos) changes
 *   - width or height changes
 *   - origin mode changes
 */
static void parts_common_recalculate_hitbox(struct parts *parts, struct parts_common *common)
{

	if (common->surface_area.w || common->surface_area.h) {
		common->origin_offset = calculate_offset(parts->origin_mode,
				common->surface_area.w, common->surface_area.h);
		Rectangle r = { 0, 0, common->w, common->h };
		SDL_IntersectRect(&r, &common->surface_area, &common->hitbox);
		common->origin_offset.x -= common->surface_area.x;
		common->origin_offset.y -= common->surface_area.y;
		common->hitbox.x += parts->local.pos.x + common->origin_offset.x;
		common->hitbox.y += parts->local.pos.y + common->origin_offset.y;
	} else {
		common->origin_offset = calculate_offset(parts->origin_mode, common->w, common->h);
		common->hitbox = (Rectangle) {
			.x = parts->local.pos.x + common->origin_offset.x,
			.y = parts->local.pos.y + common->origin_offset.y,
			.w = common->w,
			.h = common->h,
		};
	}
}

void parts_recalculate_hitbox(struct parts *parts)
{
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		parts_common_recalculate_hitbox(parts, &parts->states[i].common);
	}
}

Point parts_child_origin(struct parts *parts)
{
	Point pos=parts->global.pos;
	struct parts_state *s=&parts->states[0];
	if (s->type==PARTS_LAYOUT_BOX && s->layout_box.layout_type==PARTS_LAYOUT_FREE) {
		pos.x+=s->common.origin_offset.x;
		pos.y+=s->common.origin_offset.y;
	}
	return pos;
}

static void parts_update_global_pos(struct parts *parts, Point parent_pos)
{
	parts->global.pos = (Point) {
		parent_pos.x + parts->local.pos.x - parts_scroll_link_pos(parts,0),
		parent_pos.y + parts->local.pos.y - parts_scroll_link_pos(parts,1)
	};

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_pos(child, parts_child_origin(parts));
	}
}

#include "relative_position.inc"

void parts_set_pos(struct parts *parts, Point pos)
{
	parts->position_is_ratio=false;
	parts->local.pos.x = pos.x;
	parts->local.pos.y = pos.y;
	parts_recalculate_hitbox(parts);
	parts_update_global_pos(parts, parts->parent ? parts_child_origin(parts->parent) : root_params.pos);
	parts_dirty(parts);
}

bool parts_set_ratio_position(struct parts *p,bool enabled,double x,double y)
{
	struct parts_relative_position raw;
	if(!parts_relative_set(&raw,enabled,x,y)) return false;
	p->position_is_ratio=enabled;p->raw_position_x=x;p->raw_position_y=y;
	parts_component_dirty(p);return true;
}

static void parts_resolve_ratio_position(struct parts *p)
{
	if(!p->position_is_ratio) return;
	int w=config.view_width,h=config.view_height;bool have_w=false,have_h=false;
	for(struct parts *a=p->parent;a && (!have_w || !have_h);a=a->parent) {
		/* LayoutBox dimensions are measured from its children: using them as
		 * their own ratio basis creates a cyclic geometry dependency. */
		struct parts_state *s=&a->states[a->state];
		if(s->type==PARTS_LAYOUT_BOX) continue;
		if(!have_w && s->common.w>0){w=s->common.w;have_w=true;}
		if(!have_h && s->common.h>0){h=s->common.h;have_h=true;}
	}
	struct parts_relative_position raw={true,p->raw_position_x,p->raw_position_y};
	int x,y;if(!parts_relative_resolve(&raw,w,h,&x,&y)) ERROR("Invalid proportional Parts coordinate");
	if(x!=p->local.pos.x || y!=p->local.pos.y) {
		parts_set_pos(p,(Point){x,y});p->position_is_ratio=true;
	}
}

void parts_set_global_pos(Point pos)
{
	root_params.pos = pos;
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		parts_update_global_pos(parts, root_params.pos);
	}
	parts_engine_dirty();
}

static void parts_update_global_z(struct parts *parts, int parent_z)
{
	parts->global.z = parent_z + parts->local.z;
	parts_list_resort(parts);

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_z(child, parts->global.z);
	}
}

void parts_set_z(struct parts *parts, int z)
{
	if (parts->local.z == z)
		return;

	parts->local.z = z;
	parts_update_global_z(parts, parts->parent ? parts->parent->global.z : 0);
	parts_dirty(parts);
}

static void parts_update_global_show(struct parts *parts, bool parent_show)
{
	parts->global.show = parent_show && parts->local.show;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_show(child, parts->global.show);
	}
}

void parts_set_show(struct parts *parts, bool show)
{
	if (parts->local.show == show)
		return;

	parts->local.show = show;
	parts_update_global_show(parts, parts->parent ? parts->parent->global.show : true);
	/* Automatic layouts measure only explicitly visible children. */
	for (struct parts *p = parts->parent; p; p = p->parent)
		if (p->states[0].type == PARTS_LAYOUT_BOX)
			parts_component_dirty(p);
	parts_dirty(parts);
}

static void parts_update_global_alpha(struct parts *parts, int parent_alpha)
{
	parts->global.alpha = parent_alpha * (parts->local.alpha / 255.0f) * parts_scroll_link_alpha(parts);

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_alpha(child, parts->global.alpha);
	}
}

void parts_set_alpha(struct parts *parts, int alpha)
{
	parts->local.alpha = max(0, min(255, alpha));
	parts_update_global_alpha(parts, parts->parent ? parts->parent->global.alpha : 255);
	parts_dirty(parts);
}

static void parts_update_global_add_color(struct parts *parts, SDL_Color parent_color)
{
	parts->global.add_color = (SDL_Color) {
		parent_color.r * (parts->local.add_color.r / 255.0f),
		parent_color.g * (parts->local.add_color.g / 255.0f),
		parent_color.b * (parts->local.add_color.b / 255.0f),
		0
	};

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_add_color(child, parts->global.add_color);
	}
}

void parts_set_add_color(struct parts *parts, SDL_Color color)
{
	parts->local.add_color = color;
	parts_update_global_add_color(parts, parts->parent ? parts->parent->global.add_color
			: (SDL_Color){0,0,0,0});
	parts_dirty(parts);
}

static void parts_update_global_multiply_color(struct parts *parts, SDL_Color parent_color)
{
	parts->global.multiply_color = (SDL_Color) {
		parent_color.r * (parts->local.multiply_color.r / 255.0f),
		parent_color.g * (parts->local.multiply_color.g / 255.0f),
		parent_color.b * (parts->local.multiply_color.b / 255.0f),
		255
	};

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_multiply_color(child, parts->global.multiply_color);
	}
}

void parts_set_multiply_color(struct parts *parts, SDL_Color color)
{
	parts->local.multiply_color = color;
	parts_update_global_multiply_color(parts, parts->parent ? parts->parent->global.multiply_color
			: (SDL_Color){255,255,255,255});
	parts_dirty(parts);
}

void parts_set_origin_mode(struct parts *parts, int origin_mode)
{
	parts->origin_mode = origin_mode;
	parts_recalculate_hitbox(parts);
	parts_dirty(parts);
}

static void parts_update_global_scale_x(struct parts *parts, float parent_scale_x)
{
	parts->global.scale.x = parent_scale_x * parts->local.scale.x;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_scale_x(child, parts->global.scale.x);
	}
}

void parts_set_scale_x(struct parts *parts, float mag)
{
	parts->local.scale.x = mag;
	parts_recalculate_hitbox(parts);
	parts_update_global_scale_x(parts, parts->parent ? parts->parent->global.scale.x : 1.0f);
	parts_dirty(parts);
}

static void parts_update_global_scale_y(struct parts *parts, float parent_scale_y)
{
	parts->global.scale.y = parent_scale_y * parts->local.scale.y;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_scale_y(child, parts->global.scale.y);
	}
}

void parts_set_scale_y(struct parts *parts, float mag)
{
	parts->local.scale.y = mag;
	parts_recalculate_hitbox(parts);
	parts_update_global_scale_y(parts, parts->parent ? parts->parent->global.scale.y : 1.0f);
	parts_dirty(parts);
}

static void parts_update_global_rotate_x(struct parts *parts, float parent_rot_x)
{
	parts->global.rotation.x = parent_rot_x + parts->local.rotation.x;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_rotate_x(child, parts->global.rotation.x);
	}
}

void parts_set_rotation_x(struct parts *parts, float rot)
{
	parts->local.rotation.x = rot;
	parts_update_global_rotate_x(parts, parts->parent ? parts->parent->global.rotation.x : 0.0f);
	parts_dirty(parts);
}

static void parts_update_global_rotate_y(struct parts *parts, float parent_rot_y)
{
	parts->global.rotation.y = parent_rot_y + parts->local.rotation.y;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_rotate_y(child, parts->global.rotation.y);
	}
}

void parts_set_rotation_y(struct parts *parts, float rot)
{
	parts->local.rotation.y = rot;
	parts_update_global_rotate_y(parts, parts->parent ? parts->parent->global.rotation.y : 0.0f);
	parts_dirty(parts);
}

static void parts_update_global_rotate_z(struct parts *parts, float parent_rot_z)
{
	parts->global.rotation.z = parent_rot_z + parts->local.rotation.z;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_rotate_z(child, parts->global.rotation.z);
	}
}

void parts_set_rotation_z(struct parts *parts, float rot)
{
	parts->local.rotation.z = rot;
	parts_update_global_rotate_z(parts, parts->parent ? parts->parent->global.rotation.z : 0.0f);
	parts_dirty(parts);
}

void parts_flat_async_content_changed(struct parts *parts,struct parts_common *common)
{
	// Later state-specific content wins; generic transforms do not cancel.
	// Private/widget/prepared surfaces are not live ownership slots.
	for (int i=0;i<PARTS_NR_STATES;i++)
		if (common==&parts->states[i].common) {
			parts_flat_async_cancel_state(&parts->states[i]);
			break;
		}
}

void parts_set_dims(struct parts *parts, struct parts_common *common, int w, int h)
{
	parts_flat_async_content_changed(parts,common);
	bool changed=common->w!=w || common->h!=h;
	common->w = w;
	common->h = h;
	parts_common_recalculate_hitbox(parts, common);
	if (changed) for (struct parts *p=parts->parent;p;p=p->parent)
		if (p->states[0].type==PARTS_LAYOUT_BOX) parts_component_dirty(p);
}

bool _parts_cg_set(struct parts *parts, struct parts_cg *parts_cg, struct cg *cg, int cg_no,
		struct string *name)
{
	if (!cg) {
		if (name) free_string(name);
		return false;
	}
	parts_textures_require_common(parts,&parts_cg->common);
	Texture next={0};
	GLenum prior=glGetError();
	if (prior!=GL_NO_ERROR) {
		WARNING("CG: previous GL error before texture upload: %u",(unsigned)prior);
		for (int i=0;i<16 && glGetError()!=GL_NO_ERROR;i++) {}
	}
	gfx_init_texture_with_cg(&next,cg);
	GLenum error=glGetError();
	if (error!=GL_NO_ERROR || !next.handle) {
		WARNING("CG upload failed: prior=%u error=%u",(unsigned)prior,(unsigned)error);
		gfx_delete_texture(&next);cg_free(cg);if (name) free_string(name);return false;
	}
	gfx_delete_texture(&parts_cg->common.texture);
	parts_cg->common.texture=next;
	parts_set_dims(parts, &parts_cg->common, cg->metrics.w, cg->metrics.h);
	parts_cg->no = cg_no;
	if (parts_cg->name)
		free_string(parts_cg->name);
	parts_cg->name = name;
	parts_dirty(parts);
	cg_free(cg);
	return true;
}

void parts_cg_publish_state(struct parts *parts,int state,struct parts_cg *next)
{
	struct parts_state *slot=&parts->states[state];
	Point origin=slot->type==PARTS_CG ? slot->common.origin_offset : (Point){0};
	Rectangle area=slot->type==PARTS_CG ? slot->common.surface_area : (Rectangle){0};
	parts_state_reset(slot,PARTS_CG);
	slot->cg=*next;memset(next,0,sizeof(*next));
	slot->common.origin_offset=origin;slot->common.surface_area=area;
	parts_set_dims(parts,&slot->common,slot->common.w,slot->common.h);
	parts_dirty(parts);
}

bool parts_cg_set_by_index(struct parts *parts, struct parts_cg *cg, int cg_no)
{
	assert(cg_no);
	return _parts_cg_set(parts, cg, asset_cg_load(cg_no), cg_no, NULL);
}

bool parts_cg_set(struct parts *parts, struct parts_cg *parts_cg, struct string *cg_name)
{
	assert(cg_name && *(cg_name->text) != '\0');
	int no=0;
	struct cg *cg;
	if (!strncmp(cg_name->text, "<save>", 6)) {
		char *path = savedir_path(cg_name->text + 6);
		cg = cg_load_file(path);
		no = 0;
		free(path);
	} else {
		cg = asset_cg_load_by_name(cg_name->text, &no);
	}
	return _parts_cg_set(parts, parts_cg, cg, no, string_dup(cg_name));
}

struct parts_numeral_font *parts_numeral_fonts = NULL;
int parts_nr_numeral_fonts = 0;

static void parts_numeral_font_clear_textures(struct parts_numeral_font *font)
{
	for (int i = 0; i < 12; i++) {
		if (font->cg[i].handle)
			gfx_delete_texture(&font->cg[i]);
	}
	memset(font->cg, 0, sizeof(font->cg));
}

void parts_numeral_font_fini(struct parts_numeral_font *font)
{
	parts_numeral_font_clear_textures(font);
	if (font->cg_name) free_string(font->cg_name);
	font->cg_name = NULL;
}

#include "numeral_text.inc"

void parts_numeral_font_init(struct parts_numeral_font *font)
{
	memset(font->cg, 0, sizeof(font->cg));
	if (font->type == PARTS_NUMERAL_FONT_TEXT) {
		if (!parts_numeral_text_font_init(font))
			parts_numeral_font_clear_textures(font);
	} else if (font->type == PARTS_NUMERAL_FONT_SEPARATE) {
		for (int i = 0; i < 12; i++) {
			struct cg *cg = asset_cg_load(font->cg_no + i);
			if (!cg) {
				font->cg[i].handle = 0;
				continue;
			}
			gfx_init_texture_with_cg(&font->cg[i], cg);
			cg_free(cg);
		}
	} else if (font->type == PARTS_NUMERAL_FONT_SEPARATE2) {
		for (int i = 0; i < 12; i++) {
			if (font->width[i] < 0)
				continue;
			struct cg *cg;
			if (font->cg_name) {
				struct string *name = string_format(font->cg_name, (union vm_value){.i=i}, STRFMT_INT);
				cg = asset_cg_load_by_name(name->text, NULL);
				free_string(name);
			} else cg = asset_cg_load(font->width[i]);
			if (!cg) {
				font->cg[i].handle = 0;
				continue;
			}
			gfx_init_texture_with_cg(&font->cg[i], cg);
			cg_free(cg);
		}
	} else if (font->type == PARTS_NUMERAL_FONT_COMBINED) {
		struct cg_metrics metrics;
		if (!(font->cg_name ? asset_cg_get_metrics_by_name(font->cg_name->text, &metrics)
				: asset_cg_get_metrics(font->cg_no, &metrics)))
			return;
		if (metrics.w < 1 || metrics.w > 4096 || metrics.h < 1 || metrics.h > 4096)
			return;

		for (int i = 0; i < 12; i++) {
			if (font->width[i] < 0 || font->width[i] > 4096)
				return;
		}

		struct cg *cg = font->cg_name ? asset_cg_load_by_name(font->cg_name->text, NULL)
				: asset_cg_load(font->cg_no);
		if (!cg)
			return;
		Texture sheet = {0};
		gfx_init_texture_with_cg(&sheet, cg);
		if (!sheet.handle) {
			cg_free(cg);
			return;
		}
		if (sheet.w < 1 || sheet.w > 4096 || sheet.h < 1 || sheet.h > 4096) {
			gfx_delete_texture(&sheet);
			cg_free(cg);
			return;
		}

		int64_t x = 0;
		bool ok = true;
		/* Linked sheets may contain only a prefix of the declared glyphs.
		 * Keep the declared offset even for missing cells; never reinterpret a
		 * partial cell or reuse slot zero as a different digit. */
		for (int i = 0; i < 12; i++) {
			int w = font->width[i];
			if (!w)
				continue;
			if (x <= sheet.w - w) {
				gfx_init_texture_blank(&font->cg[i], w, sheet.h);
				if (!font->cg[i].handle) { ok = false; break; }
				gfx_copy_with_alpha_map(&font->cg[i], 0, 0, &sheet,
					(int)x, 0, w, sheet.h);
			}
			x += w;
		}
		gfx_delete_texture(&sheet);
		cg_free(cg);
		if (!ok)
			parts_numeral_font_clear_textures(font);
	}
}

static bool parts_numeral_font_has_digit(struct parts_numeral_font *font)
{
	for (int i=0;i<10;i++)
		if (font->cg[i].handle) return true;
	return false;
}

static int parts_numeral_finish_separate(int font_no)
{
	struct parts_numeral_font *font=&parts_numeral_fonts[font_no];
	if (parts_numeral_font_has_digit(font)) return font_no;
	parts_numeral_font_fini(font);
	memset(font,0,sizeof(*font));
	parts_nr_numeral_fonts--;
	return -1;
}

static int parts_load_numeral_font_separate(int cg_no)
{
	if (cg_no<0 || cg_no>INT_MAX-11) return -1;
	// find existing font
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		if (parts_numeral_fonts[i].type == PARTS_NUMERAL_FONT_SEPARATE
				&& parts_numeral_fonts[i].cg_no == cg_no)
			return parts_numeral_font_has_digit(&parts_numeral_fonts[i]) ? i : -1;
	}

	// load new font
	int font_no = parts_nr_numeral_fonts++;
	parts_numeral_fonts = xrealloc_array(parts_numeral_fonts, font_no, font_no + 1,
			sizeof(struct parts_numeral_font));
	struct parts_numeral_font *font = &parts_numeral_fonts[font_no];
	font->cg_no = cg_no;
	font->type = PARTS_NUMERAL_FONT_SEPARATE;
	parts_numeral_font_init(font);
	return parts_numeral_finish_separate(font_no);
}

static int parts_load_numeral_font_separate_string(struct string *cg_name)
{
	// convert name to CG indices
	int indices[12];
	for (int i = 0; i < 12; i++) {
		struct string *name = string_format(cg_name, (union vm_value){.i = i}, STRFMT_INT);
		if (!asset_exists_by_name(ASSET_CG, name->text, &indices[i])) {
			WARNING("numeral cg doesn't exist: %s", display_sjis0(name->text));
			indices[i] = -1;
		}
		free_string(name);
	}

	// find existing font
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		if (font->type != PARTS_NUMERAL_FONT_SEPARATE2 || !font->cg_name ||
				strcmp(font->cg_name->text, cg_name->text))
			continue;
		bool not_match = false;
		for (int d = 0; d < 12; d++) {
			if (font->width[d] != indices[d]) {
				not_match = true;
				break;
			}
		}
		if (not_match)
			continue;
		return parts_numeral_font_has_digit(font) ? i : -1;
	}

	// load new font
	int font_no = parts_nr_numeral_fonts++;
	parts_numeral_fonts = xrealloc_array(parts_numeral_fonts, font_no, font_no + 1,
			sizeof(struct parts_numeral_font));
	struct parts_numeral_font *font = &parts_numeral_fonts[font_no];
	font->cg_name = string_dup(cg_name);
	font->cg_no = indices[0];
	font->type = PARTS_NUMERAL_FONT_SEPARATE2;
	memcpy(font->width, indices, sizeof(int) * 12);
	parts_numeral_font_init(font);
	return parts_numeral_finish_separate(font_no);
}

static int parts_load_numeral_font_combined(int cg_no, struct string *cg_name, int w[12])
{
	int64_t digit_width=0;
	for (int i=0;i<12;i++) {
		if (w[i]<0 || w[i]>4096) return -1;
		if (i<10) digit_width+=w[i];
	}
	if (!digit_width) return -1;
	// find existing font
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		if (font->type != PARTS_NUMERAL_FONT_COMBINED ||
				(cg_name ? !font->cg_name || strcmp(font->cg_name->text, cg_name->text)
				: font->cg_name || font->cg_no != cg_no))
			continue;
		if (parts_numeral_widths_equal(w, font->width))
			return parts_numeral_font_has_digit(font) ? i : -1;
	}

	/* Check the sheet before allocating a new cache entry. */
	struct cg_metrics metrics;
	if (!(cg_name ? asset_cg_get_metrics_by_name(cg_name->text, &metrics)
			: asset_cg_get_metrics(cg_no, &metrics)) ||
			metrics.w < 1 || metrics.w > 4096 ||
			metrics.h < 1 || metrics.h > 4096)
		return -1;

	// load new font
	int font_no = parts_nr_numeral_fonts++;
	parts_numeral_fonts = xrealloc_array(parts_numeral_fonts, font_no, font_no + 1,
			sizeof(struct parts_numeral_font));
	struct parts_numeral_font *font = &parts_numeral_fonts[font_no];
	font->cg_no = cg_no;
	font->type = PARTS_NUMERAL_FONT_COMBINED;
	font->cg_name = cg_name ? string_dup(cg_name) : NULL;
	memcpy(font->width, w, sizeof(int) * 12);
	parts_numeral_font_init(font);

	bool valid = parts_numeral_font_has_digit(font);
	for (int i = 0; i < 12; i++)
		if (font->width[i] < 0 || font->width[i] > 4096)
			valid = false;
	/* A partial sheet is useful when the requested value only uses present
	 * cells (e.g. a one-cell hyphen rendered through digit zero). Missing
	 * main digits, minus and comma all fail at actual rendering, below. */
	if (!valid) {
		parts_numeral_font_fini(font);
		memset(font, 0, sizeof(*font));
		parts_nr_numeral_fonts--;
		return -1;
	}
	return font_no;
}

static bool parts_numeral_update(struct parts *parts, struct parts_numeral *num)
{
	parts_textures_require_common(parts,&num->common);
	if (!num->have_num) {
		parts_flat_async_content_changed(parts,&num->common);
		return true;
	}
	// Preserve deferred configuration: scripts may set a value before a font.
	if (num->font_no < 0) {
		parts_flat_async_content_changed(parts,&num->common);
		return true;
	}
	if (num->font_no >= parts_nr_numeral_fonts)
		return false;

	uint8_t chars[32];
	int nr_chars = 0;
	if (!parts_numeral_build_chars(num->num, num->length, num->show_comma != 0,
			chars, &nr_chars))
		return false;

	struct parts_numeral_font *font = &parts_numeral_fonts[num->font_no];
	if (font->type == PARTS_NUMERAL_FONT_TEXT)
		return parts_numeral_text_update(parts, num, font, chars, nr_chars);

	// Validate every actually needed glyph before touching the old texture.
	int64_t width = 0;
	int height = 0;
	for (int i = 0; i < nr_chars; i++) {
		Texture *ch = &font->cg[chars[i]];
		if (!ch->handle)
			return false;
		if (ch->w < 0 || ch->h < 1 || ch->h > 4096)
			return false;
		width += ch->w;
		height = max(height, ch->h);
	}
	/* Space applies only between adjacent characters. Negative spacing is
	 * allowed as long as the resulting width and draw positions stay in
	 * bounds. */
	width += (int64_t)(nr_chars - 1) * num->space;
	if (width < 1 || width > 4096 || height < 1 || height > 4096)
		return false;

	int64_t pos = 0;
	for (int i = 0; i < nr_chars; i++) {
		Texture *ch = &font->cg[chars[i]];
		if (pos < 0 || pos + ch->w > width)
			return false;
		pos += ch->w;
		if (i + 1 < nr_chars)
			pos += num->space;
	}
	if (pos != width)
		return false;

	Texture next = {0};
	gfx_init_texture_rgba(&next, (int)width, height, (SDL_Color){0, 0, 0, 0});
	if (!next.handle)
		return false;

	int x = 0;
	for (int i = 0; i < nr_chars; i++) {
		Texture *ch = &font->cg[chars[i]];
		gfx_copy_with_alpha_map(&next, x, 0, ch, 0, 0, ch->w, ch->h);
		x += ch->w;
		if (i + 1 < nr_chars)
			x += num->space;
	}

	gfx_delete_texture(&num->common.texture);
	num->common.texture = next;
	parts_set_dims(parts, &num->common, (int)width, height);
	parts_dirty(parts);
	return true;
}

bool parts_numeral_set_number(struct parts *parts, struct parts_numeral *num, int n)
{
	bool old_have_num = num->have_num;
	int old_num = num->num;
	num->have_num = true;
	num->num = n;
	if (!parts_numeral_update(parts, num)) {
		num->have_num = old_have_num;
		num->num = old_num;
		return false;
	}
	return true;
}

static bool parts_numeral_apply_font(struct parts *parts,
		struct parts_numeral *num, int font_no)
{
	if (font_no < 0)
		return false;
	int old_font_no = num->font_no;
	num->font_no = font_no;
	if (num->have_num && !parts_numeral_update(parts, num)) {
		num->font_no = old_font_no;
		return false;
	}
	if (!num->have_num) parts_flat_async_content_changed(parts,&num->common);
	return true;
}

void parts_set_state(struct parts *parts, enum parts_state_type state)
{
	if (parts->lock_input_state)
		return;
	while (state > PARTS_STATE_DEFAULT && parts->states[state].type == PARTS_UNINITIALIZED)
		state--;
	if (parts->state != state) {
		parts->state = state;
		parts_dirty(parts);
	}
}

void parts_set_surface_area(struct parts *parts, struct parts_common *common, int x, int y, int w, int h)
{
	parts_textures_require_common(parts,common);
	parts_flat_async_content_changed(parts,common);
	if (x < 0 || y < 0)
		return;
	common->surface_area = (Rectangle) { x, y, w, h };
	parts_common_recalculate_hitbox(parts, common);
}

static bool parts_animation_update(struct parts_animation *anim, int passed_time)
{
	if (passed_time <= 0 || !anim->nr_frames)
		return false;

	const unsigned elapsed = anim->elapsed + passed_time;
	const unsigned frame_diff = elapsed / anim->frame_time;
	const unsigned remainder = elapsed % anim->frame_time;

	if (frame_diff > 0) {
		anim->elapsed = remainder;
		anim->current_frame = (anim->current_frame + frame_diff) % anim->nr_frames;
		anim->common.texture = anim->frames[anim->current_frame];
		return true;
	} else {
		anim->elapsed = elapsed;
		return false;
	}
}

static void parts_update_loop(struct parts *parts, int passed_time)
{
	if (parts->parked_textures) return;
	bool dirty = false;
	switch (parts->states[parts->state].type) {
	case PARTS_ANIMATION:
		dirty = parts_animation_update(&parts->states[parts->state].anim, passed_time);
		break;
	case PARTS_FLASH:
		dirty = parts_flash_update(&parts->states[parts->state].flash, passed_time);
		break;
	case PARTS_FLAT:
		dirty = parts_flat_update(&parts->states[parts->state].flat, passed_time);
		break;
	case PARTS_MOVIE:
		dirty = parts_movie_update(parts, &parts->states[parts->state].movie);
		break;
	case PARTS_MESSAGE_WINDOW:
		dirty = parts_message_window_update(parts, passed_time);
		break;
	default:
		break;
	}
	if(parts_widget_scroll_update(parts,passed_time))dirty=true;
    if(parts_textbox_update(parts,passed_time))dirty=true;
	if (dirty)
		parts_dirty(parts);
}

static void parts_update_animation(int passed_time)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		parts_update_loop(parts, passed_time);
	}
}
static void parts_update_animation12(int base_time, int scaled_time)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		int passed_time = parts->enable_speed_up ? scaled_time : base_time;
		parts_update_loop(parts, passed_time);
	}
}


void parts_release(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return;
	parts_textures_discard(parts);
	parts_input_reset_drag(parts);
	parts_msg_release_parts(parts_no);
	PE_ActivityPartReleased(parts_no);
	parts_widget_free(parts->widget);
	parts_clear_motion(parts);
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		parts_state_free(&parts->states[i]);
	}

	// break parent/child relationships
	while (!TAILQ_EMPTY(&parts->children)) {
		struct parts *child = TAILQ_FIRST(&parts->children);
		TAILQ_REMOVE(&parts->children, child, child_list_entry);
		child->parent = NULL;
		parts_component_dirty(child);
	}
	if (parts->parent) {
		TAILQ_REMOVE(&parts->parent->children, parts, child_list_entry);
		parts->parent = NULL;
	}

	parts_list_remove(parts);
	dirty_list_remove(parts);
	parts_free_user_component(parts);
	if (parts->comment) free_string(parts->comment);
	free(parts);
	/* Released monotonic IDs must not remain as historical lookup slots. */
	ht_remove_int(parts_table, parts_no);
	parts_engine_dirty();
}

void parts_release_all(void)
{
	parts_flat_async_reset();
	PE_ClearBackScene();
	PE_ActivityReset();
	while (!TAILQ_EMPTY(&parts_list)) {
		struct parts *parts = TAILQ_FIRST(&parts_list);
		parts_release(parts->no);
	}

	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		parts_numeral_font_fini(font);
	}

	free(parts_numeral_fonts);
	parts_numeral_fonts = NULL;
	parts_nr_numeral_fonts = 0;
}

static bool parts_engine_initialized = false;

void PE_enable_multi_controller(void)
{
	if (parts_multi_controller)
		return;
	assert(!parts_engine_initialized);
	parts_multi_controller = true;
}

bool PE_Init(void)
{
	if (parts_engine_initialized)
		return true;
	// XXX: Oyako Rankan doesn't call ChipmunkSpriteEngine.Init
	sact_init(16, CHIPMUNK_SPRITE_ENGINE);
	parts_table = ht_create(1024);
	parts_render_init();
	parts_debug_init();
	ctrl_stack_init();
	parts_engine_initialized = true;
	return true;
}

void PE_Reset(void)
{
	PE_SetMessageWindowRates(1.f, 1.f);
	parts_input_reset();
	PE_ReleaseAllParts();
	PE_ReleaseMessage();
	ctrl_stack_init();
	sact_ModuleFini();
}

static bool parts_has_dirty_parent(struct parts *parts)
{
	for (struct parts *parent = parts->parent; parent; parent = parent->parent) {
		if (parent->dirty)
			return true;
	}
	return false;
}

static void parts_combine_params(struct parts_params *parent, struct parts_params *child,
		struct parts_params *out)
{
	out->z = parent->z + child->z;
	out->pos = (Point) { parent->pos.x + child->pos.x, parent->pos.y + child->pos.y };
	out->show = parent->show && child->show;
	out->alpha = parent->alpha * (child->alpha / 255.0f);
	out->scale.x = parent->scale.x * child->scale.x;
	out->scale.y = parent->scale.y * child->scale.y;
	out->rotation.x = parent->rotation.x + child->rotation.x;
	out->rotation.y = parent->rotation.y + child->rotation.y;
	out->rotation.z = parent->rotation.z + child->rotation.z;
	out->add_color.r = parent->add_color.r * (child->add_color.r / 255.0f);
	out->add_color.g = parent->add_color.g * (child->add_color.g / 255.0f);
	out->add_color.b = parent->add_color.b * (child->add_color.b / 255.0f);
	out->multiply_color.r = parent->multiply_color.r * (child->multiply_color.r / 255.0f);
	out->multiply_color.g = parent->multiply_color.g * (child->multiply_color.g / 255.0f);
	out->multiply_color.b = parent->multiply_color.b * (child->multiply_color.b / 255.0f);
	out->alpha_clipper_parts_no = child->alpha_clipper_parts_no
			? child->alpha_clipper_parts_no : parent->alpha_clipper_parts_no;
}

static void parts_update_component(struct parts *parts)
{
	struct parts_params parent=parts->parent?parts->parent->global:root_params;
	if (parts->parent) parent.pos=parts_child_origin(parts->parent);
	struct parts_params effective=parts->local;
	effective.pos.x-=parts_scroll_link_pos(parts,0);effective.pos.y-=parts_scroll_link_pos(parts,1);
	parts_combine_params(&parent,&effective,&parts->global);
	parts->global.alpha=parent.alpha * (parts->local.alpha/255.f) * parts_scroll_link_alpha(parts);
	if (parts_get_sprite_z(parts) != parts->sp.z
			|| parts_get_sprite_z2(parts) != parts->sp.z2) {
		parts_list_resort(parts);
	}

	if (parts->dirty) {
		TAILQ_REMOVE(&dirty_list, parts, dirty_list_entry);
		parts->dirty = false;
	}

	parts_do_layout(parts);

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_component(child);
	}
}

/* A source may be a sibling changed by input rather than an owner setter.
 * Resolve from base coordinates once, walking parents before their children.
 * Preserve the normal transform path for trees with no scroll links. */
static void parts_refresh_scroll_links(struct parts *p,bool inherited)
{
	bool affected=inherited || p->scroll_pos_link[0] || p->scroll_pos_link[1] || p->scroll_alpha_link;
	if(affected) {
		Point parent=p->parent?parts_child_origin(p->parent):root_params.pos;
		Point pos={parent.x+p->local.pos.x-parts_scroll_link_pos(p,0),
			parent.y+p->local.pos.y-parts_scroll_link_pos(p,1)};
		int parent_alpha=p->parent?p->parent->global.alpha:root_params.alpha;
		int alpha=parent_alpha*(p->local.alpha/255.f)*parts_scroll_link_alpha(p);
		if(pos.x!=p->global.pos.x || pos.y!=p->global.pos.y || alpha!=p->global.alpha) {
			p->global.pos=pos;p->global.alpha=alpha;parts_dirty(p);
		}
	}
	struct parts *child;
	PARTS_FOREACH_CHILD(child,p) parts_refresh_scroll_links(child,affected);
}

static void parts_set_controller_tree(struct parts *p, int controller)
{
	if (p->controller_no != controller) {
		p->controller_no = controller;
		parts_list_resort(p);
	}
	struct parts *child;
	TAILQ_FOREACH(child, &p->children, child_list_entry)
		parts_set_controller_tree(child, controller);
}

void PE_UpdateComponent(possibly_unused int passed_time)
{
	struct perf_window_scope perf_scope = perf_window_begin(PERF_WINDOW_UPDATE, 0, 0);
	parts_widgets_prepare();
	// After loading a save, the game script may compute a negative time delta
	// because it stores an absolute system.GetTime() value in the save data,
	// which is meaningless across process restarts.
	if (passed_time < 0)
		passed_time = 0;
	while (!TAILQ_EMPTY(&dirty_list)) {
		// pop parts object from dirty list
		struct parts *parts = TAILQ_FIRST(&dirty_list);
		TAILQ_REMOVE(&dirty_list, parts, dirty_list_entry);
		parts->dirty = false;

		// update parent
		struct parts *parent;
		if (parts->pending_parent >= 0 && (parent = parts_try_get(parts->pending_parent))) {
			if (parts->parent) {
				TAILQ_REMOVE(&parts->parent->children, parts, child_list_entry);
			}
			parts->parent = parent;
			TAILQ_INSERT_TAIL(&parent->children, parts, child_list_entry);
			if (ain && ain->version >= 11)
				parts_set_controller_tree(parts, parent->controller_no);

			// if parent is layout box, mark it dirty so that it can re-layout its children
			if (parent->states[0].type == PARTS_LAYOUT_BOX)
				parts_component_dirty(parent);
		}
		// TODO: should the child be orphaned if it already has a parent and an invalid
		//       parent no is given?
		parts->pending_parent = -1;

		// don't do anything here if parent is dirty
		if (parts_has_dirty_parent(parts))
			continue;

		// update child params
		parts_update_component(parts);
	}
	struct parts *ratio_part;
	bool has_scroll_links=false;
	PARTS_LIST_FOREACH(ratio_part) {
		parts_resolve_ratio_position(ratio_part);
		has_scroll_links=has_scroll_links || ratio_part->scroll_pos_link[0] ||
			ratio_part->scroll_pos_link[1] || ratio_part->scroll_alpha_link;
	}
	/* Link sources can be siblings and can change without dirtying an owner.
	 * Refresh after layout/ratio resolution without mutating base coordinates. */
	if(has_scroll_links) PARTS_LIST_FOREACH(ratio_part)
		if(!ratio_part->parent) parts_refresh_scroll_links(ratio_part,false);
	perf_window_end(perf_scope);
}

bool parts_message_window_show = true;
float parts_message_window_mul_rate = 1.f;
float parts_message_window_alpha_rate = 1.f;

void PE_SetMessageWindowRates(float mul, float alpha)
{
	float next_mul = isfinite(mul) ? fmaxf(0.f, fminf(mul, 1.f)) : 1.f;
	float next_alpha = isfinite(alpha) ? fmaxf(0.f, fminf(alpha, 1.f)) : 1.f;
	if (parts_message_window_mul_rate != next_mul || parts_message_window_alpha_rate != next_alpha)
		parts_engine_dirty();
	parts_message_window_mul_rate = next_mul;
	parts_message_window_alpha_rate = next_alpha;
}

void PE_Update(int passed_time, bool message_window_show)
{
	PE_SetMessageWindowRates(1.f, 1.f);
	parts_flat_async_pump();
	parts_message_window_show = message_window_show;
	PE_UpdateComponent(passed_time);
	audio_update();
	parts_update_animation(passed_time);
	PE_UpdateInputState(passed_time);
	parts_probe_update();
	parts_render_update();
}
void PE_Update12(int base_time, int scaled_time, bool message_window_show)
{
	if (base_time < 0)
		base_time = 0;
	if (scaled_time < 0)
		scaled_time = 0;
	parts_flat_async_pump();
	parts_message_window_show = message_window_show;
	PE_UpdateComponent(base_time);
	audio_update();
	parts_update_animation12(base_time, scaled_time);
	PE_UpdateInputState(base_time);
	parts_probe_update();
	parts_render_update();
}


void PE_UpdateParts(int passed_time, possibly_unused bool is_skip, bool message_window_show)
{
	PE_SetMessageWindowRates(1.f, 1.f);
	parts_flat_async_pump();
	parts_message_window_show = message_window_show;
	audio_update();
	parts_update_animation(passed_time);
	parts_render_update();
}
void PE_SetEnableSpeedUp(int parts_no, bool enable)
{ parts_get(parts_no)->enable_speed_up = enable; }
bool PE_IsEnableSpeedUp(int parts_no)
{ struct parts *parts = parts_try_get(parts_no); return parts ? parts->enable_speed_up : false; }

void PE_SetDelegateIndex(int parts_no, int delegate_index)
{
	parts_get(parts_no)->delegate_index = delegate_index;
	parts_msg_bind_delegate(parts_no,delegate_index);
}

int PE_GetDelegateIndex(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? parts->delegate_index : -1;
}

bool PE_SetPartsCG(int parts_no, struct string *cg_name, int sprite_deform, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	if (!cg_name || *(cg_name->text) == '\0') {
		parts->sprite_deform = sprite_deform;
		parts_state_reset(&parts->states[state], PARTS_CG);
		parts_dirty(parts);
		return true;
	}

	struct parts_cg next={0};
	if (!parts_cg_set(parts,&next,cg_name)) return false;
	parts->sprite_deform=sprite_deform;parts_cg_publish_state(parts,state,&next);return true;
}

bool PE_SetPartsCG_by_index(int parts_no, int cg_no, int sprite_deform, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	if (!cg_no) {
		parts->sprite_deform = sprite_deform;
		parts_state_reset(&parts->states[state], PARTS_CG);
		parts_dirty(parts);
		return true;
	}

	struct parts_cg next={0};
	if (!parts_cg_set_by_index(parts,&next,cg_no)) return false;
	parts->sprite_deform=sprite_deform;parts_cg_publish_state(parts,state,&next);return true;
}

// XXX: Rance Quest
bool PE_SetPartsCG_by_string_index(int parts_no, struct string *cg_name,
		int sprite_deform, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	if (!cg_name) {
		parts->sprite_deform=sprite_deform;
		parts_state_reset(&parts->states[state], PARTS_CG);
		parts_dirty(parts);
		return true;
	}

	struct parts_cg next={0};
	int cg_no = atoi(cg_name->text);
	if (cg_no) {
		if (!parts_cg_set_by_index(parts, &next, cg_no))
			return false;
		next.name=string_ref(cg_name);
		parts->sprite_deform=sprite_deform;parts_cg_publish_state(parts,state,&next);
		return true;
	} else if (!strncmp(cg_name->text, "<save>SaveData\\", 15)) {
		char *path = savedir_path(cg_name->text + 15);
		bool result = _parts_cg_set(parts, &next, cg_load_file(path), 0, string_ref(cg_name));
		free(path);
		if (result) {parts->sprite_deform=sprite_deform;parts_cg_publish_state(parts,state,&next);}
		return result;
	} else {
		VM_ERROR("Invalid CG name: %s", display_sjis0(cg_name->text));
	}
}

void PE_GetPartsCGName(int parts_no, struct string **cg_name, int state)
{
	if (!parts_state_valid(--state))
		return;
	if (!cg_name) return;
	struct parts *parts=parts_try_get(parts_no);
	struct parts_state *slot=parts ? &parts->states[state] : NULL;
	struct string *name=slot && slot->type==PARTS_CG ? slot->cg.name : NULL;
	struct string *copy=name ? string_dup(name) : string_ref(&EMPTY_STRING);
	if (*cg_name) free_string(*cg_name);
	*cg_name=copy;
}

bool PE_SetPartsCGSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_cg *cg = parts_get_cg(parts, state);
	parts_set_surface_area(parts, &cg->common, x, y, w, h);
	return true;
}

void PE_GetPartsCGSurfaceArea(int parts_no, int *x, int *y, int *w, int *h, int state)
{
	if (!parts_state_valid(--state))
		return;

	struct parts *parts=parts_try_get(parts_no);
	Rectangle area=parts && parts->states[state].type==PARTS_CG ? parts->states[state].common.surface_area : (Rectangle){0};
	*x=area.x;*y=area.y;*w=area.w;*h=area.h;
}

int PE_GetPartsCGNumber(int parts_no, int state)
{
	if (!parts_state_valid(--state)) {
		return false;
	}

	struct parts *parts=parts_try_get(parts_no);
	return parts && parts->states[state].type==PARTS_CG ? parts->states[state].cg.no : 0;
}

static bool _parts_animation_set_cg(struct parts *parts, struct parts_animation *anim,
		int start_no, int nr_frames, int frame_time,
		struct cg *(*load_cg)(int no, void *data), void *data)
{
	int w = 0, h = 0;
	Texture *frames = xcalloc(nr_frames, sizeof(Texture));
	for (int i = 0; i < nr_frames; i++) {
		struct cg *cg = load_cg(start_no + i, data);
		if (!cg) {
			for (int j = 0; j < i; j++) {
				gfx_delete_texture(&frames[j]);
			}
			free(frames);
			return false;
		}
		gfx_init_texture_with_cg(&frames[i], cg);
		w = max(w, cg->metrics.w);
		h = max(h, cg->metrics.h);
		cg_free(cg);
	}

	parts_set_dims(parts, &anim->common, w, h);
	free(anim->frames);
	anim->start_no = start_no;
	anim->frames = frames;
	anim->nr_frames = nr_frames;
	anim->frame_time = frame_time;
	anim->elapsed = 0;
	anim->current_frame = 0;
	anim->common.texture = frames[0];
	return true;
}

static struct cg *load_loop_cg_by_index(int no, void *_)
{
	return asset_cg_load(no);
}

bool parts_animation_set_cg_by_index(struct parts *parts, struct parts_animation *anim,
		int cg_no, int nr_frames, int frame_time)
{
	return _parts_animation_set_cg(parts, anim, cg_no, nr_frames, frame_time,
			load_loop_cg_by_index, NULL);
}

bool PE_SetLoopCG_by_index(int parts_no, int cg_no, int nr_frames, int frame_time, int state)
{
	if (!parts_state_valid(--state))
		return false;
	if (nr_frames <= 0 || nr_frames > 10000) {
		WARNING("Invalid frame count: %d", nr_frames);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	struct parts_animation *anim = parts_get_animation(parts, state);
	return parts_animation_set_cg_by_index(parts, anim, cg_no, nr_frames, frame_time);
}

static struct cg *load_loop_cg_by_name(int no, void *data)
{
	int unused_no;
	struct string *cg_name = string_format((struct string*)data, (union vm_value){.i=no}, STRFMT_INT);
	struct cg *cg = asset_cg_load_by_name(cg_name->text, &unused_no);
	free_string(cg_name);
	return cg;
}

bool parts_animation_set_cg(struct parts *parts, struct parts_animation *anim,
		struct string *cg_name, int start_no, int nr_frames, int frame_time)
{
	bool r = _parts_animation_set_cg(parts, anim, start_no, nr_frames, frame_time,
			load_loop_cg_by_name, cg_name);
	if (r) {
		if (anim->cg_name)
			free_string(anim->cg_name);
		anim->cg_name = string_dup(cg_name);
	}
	return r;
}

bool PE_SetLoopCG(int parts_no, struct string *cg_name, int start_no, int nr_frames,
	int frame_time, int state)
{
	if (!parts_state_valid(--state))
		return false;
	if (nr_frames <= 0 || nr_frames > 10000) {
		WARNING("Invalid frame count: %d", nr_frames);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	struct parts_animation *anim = parts_get_animation(parts, state);
	return parts_animation_set_cg(parts, anim, cg_name, start_no, nr_frames, frame_time);
}

bool PE_SetLoopCGSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state)) {
		WARNING("Invalid parts state: %d", state);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	struct parts_animation *anim = parts_get_animation(parts, state);
	parts_set_surface_area(parts, &anim->common, x, y, w, h);
	return true;
}

static bool parts_gauge_prepare_cg(struct cg *cg, Texture *cg_tex, Texture *texture)
{
	if (!cg || !cg->pixels)
		return false;
	if (cg->metrics.w < 1 || cg->metrics.w > 4096 ||
			cg->metrics.h < 1 || cg->metrics.h > 4096)
		return false;

	Texture local_cg = {0};
	Texture local_texture = {0};
	gfx_init_texture_with_cg(&local_cg, cg);
	if (!local_cg.handle)
		return false;
	if (local_cg.w < 1 || local_cg.w > 4096 ||
			local_cg.h < 1 || local_cg.h > 4096) {
		gfx_delete_texture(&local_cg);
		return false;
	}
	gfx_init_texture_rgba(&local_texture, local_cg.w, local_cg.h,
			(SDL_Color){0, 0, 0, 255});
	if (!local_texture.handle) {
		gfx_delete_texture(&local_cg);
		return false;
	}
	gfx_copy_with_alpha_map(&local_texture, 0, 0, &local_cg, 0, 0,
			local_cg.w, local_cg.h);
	*cg_tex = local_cg;
	*texture = local_texture;
	return true;
}

static void parts_gauge_install(struct parts *parts, struct parts_gauge *g,
		Texture cg_tex, Texture texture)
{
	GLuint old_cg = g->cg.handle;
	GLuint old_texture = g->common.texture.handle;
	if (old_cg)
		gfx_delete_texture(&g->cg);
	if (old_texture && old_texture != old_cg)
		gfx_delete_texture(&g->common.texture);
	g->cg = cg_tex;
	g->common.texture = texture;
	parts_set_dims(parts, &g->common, cg_tex.w, cg_tex.h);
	parts_dirty(parts);
}

static bool _parts_set_gauge_cg(struct parts *parts, struct parts_gauge *g, struct cg *cg)
{
	Texture cg_tex = {0}, texture = {0};
	if (!parts_gauge_prepare_cg(cg, &cg_tex, &texture))
		return false;
	parts_gauge_install(parts, g, cg_tex, texture);
	return true;
}

bool parts_gauge_set_cg(struct parts *parts, struct parts_gauge *g, struct string *cg_name)
{
	int cg_no;
	struct cg *cg = asset_cg_load_by_name(cg_name->text, &cg_no);
	if (!cg)
		return false;
	bool ok = _parts_set_gauge_cg(parts, g, cg);
	cg_free(cg);
	if (!ok)
		return false;
	struct string *name_copy = string_dup(cg_name);
	if (g->cg_name) free_string(g->cg_name);
	g->cg_name = name_copy;
	g->cg_no = cg_no;
	return true;
}

bool parts_gauge_set_cg_by_index(struct parts *parts, struct parts_gauge *g, int cg_no)
{
	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return false;
	bool ok = _parts_set_gauge_cg(parts, g, cg);
	cg_free(cg);
	if (!ok)
		return false;
	if (g->cg_name) free_string(g->cg_name);
	g->cg_name = NULL;
	g->cg_no = cg_no;
	return true;
}

static bool parts_hgauge_paint(Texture *texture, Texture *source, float rate,
		bool reverse)
{
	if (!texture || !source || !texture->handle || !source->handle)
		return false;
	if (source->w < 1 || source->w > 4096 ||
			source->h < 1 || source->h > 4096)
		return false;
	if (texture->w != source->w || texture->h != source->h)
		return false;

	int x, pixels;
	if (!parts_hgauge_window(source->w, rate, reverse, &x, &pixels))
		return false;

	if (pixels > 0)
		gfx_copy_with_alpha_map(texture, x, 0, source, x, 0,
				pixels, source->h);
	if (pixels < source->w) {
		if (reverse)
			gfx_fill_amap(texture, 0, 0, source->w - pixels,
					source->h, 0);
		else
			gfx_fill_amap(texture, x + pixels, 0,
					source->w - pixels, source->h, 0);
	}
	return true;
}

static bool parts_hgauge_set_cg_common(struct parts *parts, int parts_no, int state,
		struct cg *cg, int cg_no, struct string *cg_name)
{
	Texture cg_tex = {0}, texture = {0};
	if (!parts_gauge_prepare_cg(cg, &cg_tex, &texture))
		return false;

	struct parts_state *st = parts ? &parts->states[state] : NULL;
	float rate = st && st->type == PARTS_HGAUGE ? st->gauge.rate : 0.0f;
	bool reverse = st && st->type == PARTS_HGAUGE ? st->gauge.reverse : false;
	if (!parts_hgauge_paint(&texture, &cg_tex, rate, reverse)) {
		gfx_delete_texture(&texture);
		gfx_delete_texture(&cg_tex);
		return false;
	}

	if (!parts) parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	parts_gauge_install(parts, g, cg_tex, texture);
	struct string *name_copy = cg_name ? string_dup(cg_name) : NULL;
	if (g->cg_name) free_string(g->cg_name);
	g->cg_name = name_copy;
	g->cg_no = cg_no;
	g->rate = rate;
	g->reverse = reverse;
	return true;
}

bool PE_SetHGaugeCG(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;

	int cg_no;
	struct cg *cg = asset_cg_load_by_name(cg_name->text, &cg_no);
	if (!cg)
		return false;
	struct parts *parts = parts_try_get(parts_no);
	bool ok = parts_hgauge_set_cg_common(parts, parts_no, state, cg, cg_no, cg_name);
	cg_free(cg);
	return ok;
}

bool PE_SetHGaugeCG_by_index(int parts_no, int cg_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return false;
	struct parts *parts = parts_try_get(parts_no);
	bool ok = parts_hgauge_set_cg_common(parts, parts_no, state, cg, cg_no, NULL);
	cg_free(cg);
	return ok;
}

bool PE_SetVGaugeCG(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	return parts_gauge_set_cg(parts, g, cg_name);
}

bool PE_SetVGaugeCG_by_index(int parts_no, int cg_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	return parts_gauge_set_cg_by_index(parts, g, cg_no);
}

bool parts_hgauge_set_rate(struct parts *parts, struct parts_gauge *g, float rate)
{
	parts_textures_require_common(parts,&g->common);
	if (!parts_hgauge_paint(&g->common.texture, &g->cg, rate, g->reverse))
		return false;
	parts_flat_async_content_changed(parts,&g->common);
	parts_dirty(parts);
	g->rate = rate;
	return true;
}

void parts_vgauge_set_rate(struct parts *parts, struct parts_gauge *g, float rate)
{
	parts_textures_require_common(parts,&g->common);
	if (!g->common.texture.handle) {
		WARNING("VGauge texture uninitialized");
		return;
	}
	int pixels = rate * g->cg.h;
	parts_flat_async_content_changed(parts,&g->common);
	gfx_copy_with_alpha_map(&g->common.texture, 0, pixels, &g->cg, 0, pixels, g->cg.w, g->cg.h - pixels);
	gfx_fill_amap(&g->common.texture, 0, 0, g->cg.w, pixels, 0);
	parts_dirty(parts);
	g->rate = rate;
}

bool PE_SetHGaugeRate(int parts_no, float numerator, float denominator, int state)
{
	if (!parts_state_valid(--state))
		return false;
	if (!isfinite(numerator) || !isfinite(denominator) ||
			denominator <= 0.0f || numerator < 0.0f ||
			numerator > denominator)
		return false;

	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_HGAUGE)
		return false;
	struct parts_gauge *g = &parts->states[state].gauge;
	if (!parts_hgauge_set_rate(parts, g, numerator / denominator)) return false;
	g->numerator = numerator;
	g->denominator = denominator;
	return true;
}

bool PE_SetHGaugeRate_int(int parts_no, int numerator, int denominator, int state)
{
	return PE_SetHGaugeRate(parts_no, numerator, denominator, state);
}

bool PE_SetHGaugeReverse(int parts_no, bool reverse, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_HGAUGE)
		return false;
	struct parts_gauge *g = &parts->states[state].gauge;

	bool old_reverse = g->reverse;
	g->reverse = reverse;
	if (!parts_hgauge_set_rate(parts, g, g->rate)) {
		g->reverse = old_reverse;
		return false;
	}
	return true;
}

static struct parts_gauge *parts_hgauge_existing(int parts_no, int state)
{
	if (!parts_state_valid(--state)) return NULL;
	struct parts *p=parts_try_get(parts_no);
	return p && p->states[state].type==PARTS_HGAUGE ? &p->states[state].gauge : NULL;
}

bool PE_IsHGaugeReverse(int parts_no,int state)
{
	struct parts_gauge *g=parts_hgauge_existing(parts_no,state);
	return g && g->reverse;
}

float PE_GetHGaugeNumerator(int parts_no,int state)
{
	struct parts_gauge *g=parts_hgauge_existing(parts_no,state);
	return g ? g->numerator : 0.f;
}

float PE_GetHGaugeDenominator(int parts_no,int state)
{
	struct parts_gauge *g=parts_hgauge_existing(parts_no,state);
	return g ? g->denominator : 0.f;
}

bool PE_SetVGaugeRate(int parts_no, float numerator, float denominator, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	parts_vgauge_set_rate(parts, g, (float)numerator/(float)denominator);
	return true;
}

bool PE_SetVGaugeRate_int(int parts_no, int numerator, int denominator, int state)
{
	return PE_SetVGaugeRate(parts_no, numerator, denominator, state);
}

bool PE_SetHGaugeSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	parts_set_surface_area(parts, &g->common, x, y, w, h);
	return true;
}

bool PE_SetVGaugeSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	parts_set_surface_area(parts, &g->common, x, y, w, h);
	return true;
}

bool PE_SetNumeralCG(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;
	int font_no = parts_load_numeral_font_separate_string(cg_name);
	if (font_no<0) return false;
	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *n = parts_get_numeral(parts, state);
	return parts_numeral_apply_font(parts, n, font_no);
}

bool PE_SetNumeralCG_by_index(int parts_no, int cg_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	int font_no = parts_load_numeral_font_separate(cg_no);
	if (font_no<0) return false;
	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *n = parts_get_numeral(parts, state);
	return parts_numeral_apply_font(parts, n, font_no);
}

bool PE_SetNumeralLinkedCGNumberWidthWidthList_by_index(int parts_no, int cg_no,
		int w0, int w1, int w2, int w3, int w4, int w5, int w6, int w7, int w8,
		int w9, int w_minus, int w_comma, int state)
{
	if (!parts_state_valid(--state))
		return false;

	if (!asset_exists(ASSET_CG, cg_no))
		return false;

	int w[12] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w_minus, w_comma };
	int font_no = parts_load_numeral_font_combined(cg_no, NULL, w);
	if (font_no<0) return false;
	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *n = parts_get_numeral(parts, state);
	return parts_numeral_apply_font(parts, n, font_no);
}

bool PE_SetNumeralLinkedCGNumberWidthWidthList(int parts_no, struct string *cg_name,
		int w0, int w1, int w2, int w3, int w4, int w5, int w6, int w7, int w8,
		int w9, int w_minus, int w_comma, int state)
{
	if (!parts_state_valid(--state))
		return false;

	int no;
	if (!asset_exists_by_name(ASSET_CG, cg_name->text, &no))
		return false;

	int w[12] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w_minus, w_comma };
	int font_no = parts_load_numeral_font_combined(no, cg_name, w);
	if (font_no<0) return false;
	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *n = parts_get_numeral(parts, state);
	return parts_numeral_apply_font(parts, n, font_no);
}

bool PE_SetNumeralNumber(int parts_no, int n, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *numeral = parts_get_numeral(parts, state);
	return parts_numeral_set_number(parts, numeral, n);
}

/* Read the committed integer model without materializing parked textures or
 * converting a different Parts state. Missing states follow the existing
 * scalar getter zero policy (for example PE_GetPartsCGNumber). */
int PE_GetNumeralNumber(int parts_no, int state)
{
	if (state < 1 || state > PARTS_NR_STATES)
		return 0;
	struct parts *parts = parts_try_get(parts_no);
	return parts && parts->states[state-1].type == PARTS_NUMERAL
		? parts->states[state-1].num.num : 0;
}

bool PE_SetNumeralShowComma(int parts_no, bool show_comma, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *num = parts_get_numeral(parts, state);
	if (num->show_comma == show_comma)
		return true;

	int old_show_comma = num->show_comma;
	num->show_comma = show_comma;
	if (!parts_numeral_update(parts, num)) {
		num->show_comma = old_show_comma;
		return false;
	}
	return true;
}

bool PE_SetNumeralSpace(int parts_no, int space, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *num = parts_get_numeral(parts, state);
	if (num->space == space)
		return true;

	int old_space = num->space;
	num->space = space;
	if (!parts_numeral_update(parts, num)) {
		num->space = old_space;
		return false;
	}
	return true;
}

bool PE_SetNumeralLength(int parts_no, int length, int state)
{
	if (!parts_state_valid(--state))
		return false;

	if (length > 24)
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *num = parts_get_numeral(parts, state);
	if (num->length == length)
		return true;

	int old_length = num->length;
	bool old_have_num = num->have_num;
	num->length = max(1, length);
	num->have_num = true;
	if (!parts_numeral_update(parts, num)) {
		num->length = old_length;
		num->have_num = old_have_num;
		return false;
	}
	return true;
}

bool PE_SetNumeralSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *n = parts_get_numeral(parts, state);
	parts_set_surface_area(parts, &n->common, x, y, w, h);
	return true;
}

void PE_ReleaseParts(int parts_no)
{
	if (getenv("XSYSTEM4_TRACE_INPUT"))
		sys_message("[parts-release] parts=%d\n", parts_no);
	parts_release(parts_no);
}

void PE_ReleaseAllParts(void)
{
	parts_release_all();
}

void PE_ReleaseAllPartsWithoutSystem(void)
{
	// FIXME: what's the difference?
	parts_release_all();
}

void PE_ReleaseAllWithoutSystem(struct page **erase_number_list)
{
	// Release all parts not belonging to the system overlay controller
	struct parts *parts = TAILQ_FIRST(&parts_list);
	while (parts) {
		struct parts *next = TAILQ_NEXT(parts, parts_list_entry);
		if (parts->controller_no != PARTS_CONTROLLER_SYSTEM_OVERLAY) {
			*erase_number_list = array_pushback(*erase_number_list,
					(union vm_value){.i = parts->no}, AIN_ARRAY_INT, -1);
			parts_release(parts->no);
		}
		parts = next;
	}

	// Drop all normal controllers and add a fresh default one.
	ctrl_stack.nr_controllers = 0;
	PE_AddController(-1);
}

void PE_SetPos(int parts_no, int x, int y)
{
	parts_set_pos(parts_get(parts_no), (Point){ x, y });
}

void PE_SetZ(int parts_no, int z)
{
	parts_set_z(parts_get(parts_no), z);
}

void PE_SetShow(int parts_no, bool show)
{
	parts_set_show(parts_get(parts_no), show);
}

bool PE_controller_visible(int id)
{
	if (!parts_multi_controller)
		return true;
	if (id == PARTS_CONTROLLER_SYSTEM_OVERLAY)
		return !ctrl_stack.system_overlay_hidden;
	int index = PE_get_controller_index(id);
	return index < 0 || !ctrl_stack.hidden[index];
}

void PE_SetComponentShow(int no, bool show)
{
	int index = parts_multi_controller ? PE_get_controller_index(no) : -1;
	if (index < 0) {
		PE_SetShow(no, show);
		return;
	}
	if (no == PARTS_CONTROLLER_SYSTEM_OVERLAY)
		ctrl_stack.system_overlay_hidden = !show;
	else
		ctrl_stack.hidden[index] = !show;
	parts_engine_dirty();
}

bool PE_IsComponentShow(int no)
{
	if (parts_multi_controller && PE_get_controller_index(no) >= 0)
		return PE_controller_visible(no);
	return PE_GetPartsShow(no);
}

void PE_SetAlpha(int parts_no, int alpha)
{
	parts_set_alpha(parts_get(parts_no), alpha);
}

void PE_SetPartsDrawFilter(int parts_no, int draw_filter)
{
	parts_get(parts_no)->draw_filter = draw_filter;
}

void PE_SetAddColor(int parts_no, int r, int g, int b)
{
	SDL_Color add_color = {
		min(255, max(0, r)),
		min(255, max(0, g)),
		min(255, max(0, b)),
		255
	};
	parts_set_add_color(parts_get(parts_no), add_color);
}

void PE_SetMultiplyColor(int parts_no, int r, int g, int b)
{
	SDL_Color multiply_color = {
		min(255, max(0, r)),
		min(255, max(0, g)),
		min(255, max(0, b)),
		255
	};
	parts_set_multiply_color(parts_get(parts_no), multiply_color);
}

int PE_GetPartsX(int parts_no)
{
	return parts_get(parts_no)->local.pos.x;
}

int PE_GetPartsY(int parts_no)
{
	return parts_get(parts_no)->local.pos.y;
}

int PE_GetPartsWidth(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *p=parts_get(parts_no);
	parts_widget_build(p);
	return p->states[state].common.w;
}

int PE_GetPartsHeight(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *p=parts_get(parts_no);
	parts_widget_build(p);
	return p->states[state].common.h;
}

int PE_GetPartsUpperLeftPosX(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_get(parts_no);
	int x = parts->states[state].common.hitbox.x;
	if (parts->parent)
		x += parts_child_origin(parts->parent).x;
	return x;
}

int PE_GetPartsUpperLeftPosY(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_get(parts_no);
	int y = parts->states[state].common.hitbox.y;
	if (parts->parent)
		y += parts_child_origin(parts->parent).y;
	return y;
}

int PE_GetPartsZ(int parts_no)
{
	return parts_get(parts_no)->local.z;
}

bool PE_GetPartsShow(int parts_no)
{
	return parts_get(parts_no)->local.show;
}

int PE_GetPartsAlpha(int parts_no)
{
	return parts_get(parts_no)->local.alpha;
}

int PE_GetPartsDrawFilter(int parts_no)
{
	return parts_get(parts_no)->draw_filter;
}

void PE_GetAddColor(int parts_no, int *r, int *g, int *b)
{
	struct parts *parts = parts_get(parts_no);
	*r = parts->local.add_color.r;
	*g = parts->local.add_color.g;
	*b = parts->local.add_color.b;
}

void PE_GetMultiplyColor(int parts_no, int *r, int *g, int *b)
{
	struct parts *parts = parts_get(parts_no);
	*r = parts->local.multiply_color.r;
	*g = parts->local.multiply_color.g;
	*b = parts->local.multiply_color.b;
}

void PE_SetPartsOriginPosMode(int parts_no, int origin_pos_mode)
{
	struct parts *parts = parts_get(parts_no);
	parts_set_origin_mode(parts, origin_pos_mode);
	parts_dirty(parts);
}

int PE_GetPartsOriginPosMode(int parts_no)
{
	return parts_get(parts_no)->origin_mode;
}

void PE_SetParentPartsNumber(int parts_no, int parent_parts_no)
{
	struct parts *parts = parts_get(parts_no);
	parts->pending_parent = parent_parts_no;
	parts_component_dirty(parts);
}

void PE_SetParentPartsNumber12(int parts_no, int parent_parts_no)
{
	if (PE_get_controller_index(parent_parts_no) < 0) {
		PE_SetParentPartsNumber(parts_no, parent_parts_no);
		return;
	}
	struct parts *p = parts_get(parts_no);
	if (p->parent) {
		TAILQ_REMOVE(&p->parent->children, p, child_list_entry);
		p->parent = NULL;
	}
	p->pending_parent = -1;
	parts_set_controller_tree(p, parent_parts_no);
	parts_component_dirty(p);
}

int PE_GetParentPartsNumber(int parts_no)
{
	struct parts *parts = parts_get(parts_no);
	if (parts->parent)
		return parts->parent->no;
	return -1;
}

int PE_GetParentPartsNumber12(int parts_no)
{
	// Zero is the modern no-parent / whole-screen-listener sentinel, even
	// if an earlier missing-part lookup accidentally materialized part zero.
	if (!parts_no)
		return 0;
	// Modern controllers are virtual roots. Layer-erasing observers walk the
	// parent chain to this ID before releasing their script-owned activities.
	// Returning zero for every root leaves those objects pointing at dead parts.
	if (PE_get_controller_index(parts_no) >= 0)
		return 0;
	struct parts *p = parts_try_get(parts_no);
	if (!p)
		return 0;
	if (p->pending_parent >= 0 && (parts_try_get(p->pending_parent) ||
			PE_get_controller_index(p->pending_parent) >= 0))
		return p->pending_parent;
	return p->parent ? p->parent->no : p->controller_no;
}

bool PE_SetPartsGroupNumber(possibly_unused int PartsNumber, possibly_unused int GroupNumber)
{
	UNIMPLEMENTED("(%d, %d)", PartsNumber, GroupNumber);
	return true;
}

void PE_SetPartsMessageWindowShowLink(possibly_unused int parts_no, bool message_window_show_link)
{
	struct parts *parts = parts_get(parts_no);
	parts->message_window = message_window_show_link;
}

bool PE_GetPartsMessageWindowShowLink(int parts_no)
{
	return parts_get(parts_no)->message_window;
}

void PE_SetPartsMagX(int parts_no, float scale_x)
{
	struct parts *parts = parts_get(parts_no);
	parts_set_scale_x(parts, scale_x);
}

float PE_GetPartsMagX(int parts_no)
{
	return parts_get(parts_no)->local.scale.x;
}

void PE_SetPartsMagY(int parts_no, float scale_y)
{
	struct parts *parts = parts_get(parts_no);
	parts_set_scale_y(parts, scale_y);
}

float PE_GetPartsMagY(int parts_no)
{
	return parts_get(parts_no)->local.scale.y;
}

void PE_SetPartsRotateX(int parts_no, float rot_x)
{
	parts_set_rotation_x(parts_get(parts_no), rot_x);
}

void PE_SetPartsRotateY(int parts_no, float rot_y)
{
	parts_set_rotation_y(parts_get(parts_no), rot_y);
}

void PE_SetPartsRotateZ(int parts_no, float rot_z)
{
	parts_set_rotation_z(parts_get(parts_no), rot_z);
}

float PE_GetPartsRotateZ(int parts_no)
{
	return parts_get(parts_no)->local.rotation.z;
}

void PE_SetPartsAlphaClipperPartsNumber(int parts_no, int alpha_clipper_parts_no)
{
	struct parts *parts = parts_get(parts_no);
	parts->local.alpha_clipper_parts_no = alpha_clipper_parts_no;
	parts_component_dirty(parts);
	parts_dirty(parts);
}

void PE_SetPartsPixelDecide(int parts_no, bool pixel_decide)
{
	//UNIMPLEMENTED("(%d, %s)", parts_no, pixel_decide ? "true" : "false");
}

bool PE_SetThumbnailReductionSize(int reduction_size)
{
	UNIMPLEMENTED("(%d)", reduction_size);
	return true;
}

bool PE_SetThumbnailMode(bool mode)
{
	UNIMPLEMENTED("(%s)", mode ? "true" : "false");
	return true;
}

bool PE_save_thumbnail(struct string *filename, int reduction_factor)
{
	if (!filename || !filename->size)
		return false;
	if (reduction_factor < 1)
		reduction_factor = 1;

	Texture *src = gfx_main_surface();
	if (!src || !src->handle || src->w < 1 || src->h < 1)
		return false;
	/* Modern PartsEngine takes a target pixel width. Older engines use an
	 * integer reduction factor; keep that separate compatibility contract. */
	int w = ain && ain->version>=11 ? min(src->w,reduction_factor) : max(1,src->w/reduction_factor);
	int h = ain && ain->version>=11 ? max(1,(int)((int64_t)src->h*w/src->w)) : max(1,src->h/reduction_factor);

	// Downscale by repeatedly halving until we are within a factor of two of
	// the target size, then do the final stretch. This avoids the aliasing
	// that a single bilinear minification would otherwise produce.
	Texture tmp = {0}, *cur = src;
	bool have_tmp = false;
	while (cur->w / 2 > w && cur->h / 2 > h) {
		Texture next = {0};
		gfx_init_texture_blank(&next, cur->w / 2, cur->h / 2);
		if (!next.handle) {
			if (have_tmp) gfx_delete_texture(&tmp);
			return false;
		}
		gfx_copy_stretch_with_alpha_map(&next, 0, 0, next.w, next.h, cur, 0, 0, cur->w, cur->h);
		if (have_tmp)
			gfx_delete_texture(&tmp);
		tmp = next;
		cur = &tmp;
		have_tmp = true;
	}

	Texture dst = {0};
	gfx_init_texture_blank(&dst, w, h);
	if (!dst.handle) {
		if (have_tmp) gfx_delete_texture(&tmp);
		return false;
	}
	gfx_copy_stretch_with_alpha_map(&dst, 0, 0, w, h, cur, 0, 0, cur->w, cur->h);
	if (have_tmp)
		gfx_delete_texture(&tmp);

	char *path = savedir_path(filename->text);
	struct save_file output;
	bool ok = begin_save_file(&output, path);
	if (ok) {
		struct cg image = {
			.type = ALCG_UNKNOWN,
			.metrics = {.w=w,.h=h,.bpp=24,.has_pixel=true,.has_alpha=true,
				.pixel_pitch=w*3,.alpha_pitch=1},
			.pixels = gfx_get_pixels(&dst)
		};
		ok = end_save_file(&output, image.pixels && cg_write(&image, ALCG_QNT, output.file));
		free(image.pixels);
	}
	free(path);
	gfx_delete_texture(&dst);
	return ok;
}

void PE_SetInputState(int parts_no, int state)
{
	if (!parts_state_valid(--state)) {
		WARNING("invalid input state: %d", state);
		return;
	}
	parts_set_state(parts_get(parts_no), state);
}

int PE_GetInputState(int parts_no)
{
	return parts_get(parts_no)->state + 1;
}

void PE_SetComponentType(int parts_no, int type, int state)
{
	if (!parts_state_valid(--state))
		return;
	struct parts *parts = parts_get(parts_no);
	parts_flat_async_cancel_state(&parts->states[state]);
	enum parts_type pt = PARTS_UNINITIALIZED;
	if ((type>=0 && type<=7) || type==9 || type==PARTS_WIDGET_FORM || type==PARTS_WIDGET_LISTBOX) {
		for (int i=0;i<PARTS_NR_STATES;i++) parts_flat_async_cancel_state(&parts->states[i]);
		parts_widget_create(parts,type);
		return;
	}
	/* Parked Texture slots must be restored before freeing their widget owner. */
	parts_textures_require_resident(parts);
	if (parts->widget) { parts_widget_free(parts->widget); parts->widget=NULL; }
	if(type==19) {
		if(state!=0) VM_ERROR("Message window requires state 1");
		parts_message_window_ensure(parts);
		return;
	}
	switch (type) {
	case 8:  pt = PARTS_LAYOUT_BOX; break;
	case 11: pt = PARTS_CG; break;
	case 12: pt = PARTS_ANIMATION; break;
	case 13: pt = PARTS_TEXT; break;
	case 14: pt = PARTS_HGAUGE; break;
	case 15: pt = PARTS_VGAUGE; break;
	case 16: pt = PARTS_NUMERAL; break;
	case 17: pt = PARTS_RECT_DETECTION; break;
	case 18: pt = PARTS_CONSTRUCTION_PROCESS; break;
	case 20: pt = PARTS_FLAT; break;
	case 21: pt = PARTS_3DLAYER; break;
	case 22: pt = PARTS_MOVIE; break;
	default:
		VM_ERROR("unknown component type %d", type);
	}
	if (parts->states[state].type != pt)
		parts_state_reset(&parts->states[state], pt);
}

int PE_GetComponentType(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return -1;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return -1;
	if (parts->widget)
		return parts->widget->type;

	switch (parts->states[state].type) {
	case PARTS_LAYOUT_BOX: return 8;
	case PARTS_UNINITIALIZED:  // defaluts to CG
	case PARTS_CG:
		return 11;
	case PARTS_ANIMATION: return 12;
	case PARTS_TEXT: return 13;
	case PARTS_HGAUGE: return 14;
	case PARTS_VGAUGE: return 15;
	case PARTS_NUMERAL: return 16;
	case PARTS_RECT_DETECTION: return 17;
	case PARTS_CONSTRUCTION_PROCESS: return 18;
	case PARTS_MESSAGE_WINDOW: return 19;
	case PARTS_FLAT: return 20;
	case PARTS_3DLAYER: return 21;
	case PARTS_MOVIE: return 22;
	case PARTS_FLASH:
		break;
	}
	VM_ERROR("unsupported component type %d", parts->states[state].type);
}

bool PE_SetPartsRectangleDetectionSize(int parts_no, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return false;
	if (parts->states[state].type != PARTS_RECT_DETECTION)
		parts_state_reset(&parts->states[state], PARTS_RECT_DETECTION);
	parts_set_dims(parts, &parts->states[state].common, w, h);
	return true;
}

bool PE_SetPartsCGDetectionSize(int parts_no, struct string *cg_name, int state)
{
	UNIMPLEMENTED("(%d, %s, %d)", parts_no, display_sjis0(cg_name->text), state);
	return false;
}

int PE_GetFreeNumber(void)
{
	// NOTE: per GUIEngine.dll (Rance 01)
	static int first_free = 1000001000;
	while (PE_IsExist(first_free) || PE_get_controller_index(first_free) >= 0) {
		first_free++;
	}
	// XXX: the ID is incremented even if the parts is not created
	return first_free++;
}

bool PE_IsExist(int parts_no)
{
	return !!ht_get_int(parts_table, parts_no, NULL);
}

static void ctrl_stack_init(void)
{
	memset(&ctrl_stack, 0, sizeof(ctrl_stack));
	// Add initial default controller
	PE_AddController(-1);
}

static void controllers_update_sprite_order(void)
{
    if (!parts_multi_controller)
        return;

    struct parts *parts;
    size_t count = 0;
    PARTS_LIST_FOREACH(parts) count++;

    struct scene_z2_update *updates = NULL;
    bool batch = count <= (size_t)-1 / sizeof(*updates);
    if (batch && count) {
        updates = malloc(count * sizeof(*updates));
        if (!updates)
            batch = false;
    }

    if (batch) {
        size_t i = 0;
        PARTS_LIST_FOREACH(parts) {
            updates[i].sp = &parts->sp;
            updates[i].z = parts_get_sprite_z(parts);
            updates[i].z2 = parts_get_sprite_z2(parts);
            i++;
        }
        scene_set_sprites_z2_batch(updates, count);
        free(updates);
    } else {
        PARTS_LIST_FOREACH(parts) {
            scene_set_sprite_z2(&parts->sp,
                parts_get_sprite_z(parts),
                parts_get_sprite_z2(parts));
        }
    }
    parts_engine_dirty();
}

// Insert after the given controller ID (-1 uses the active controller).
// The game's AddTopLayer passes GetControllerID(length - 1), not -1.
int PE_AddController(int id)
{
	if (ctrl_stack.nr_controllers >= PARTS_CONTROLLER_STACK_MAX)
		VM_ERROR("controller stack overflow");
	int index = 0;
	if (ctrl_stack.nr_controllers) {
		if (id == -1)
			id = ctrl_stack.active;
		if (id == PARTS_CONTROLLER_SYSTEM_OVERLAY)
			index = ctrl_stack.nr_controllers;
		else {
			index = PE_get_controller_index(id);
			if (index < 0)
				VM_ERROR("Invalid controller number: %d", id);
			index++;
		}
	}
	if (ctrl_stack.next_id == PARTS_CONTROLLER_SYSTEM_OVERLAY)
		ctrl_stack.next_id++;
	if (ctrl_stack.next_id == INT_MAX)
		VM_ERROR("controller ID overflow");
	// AIN11+ exposes controllers in the same parent-number namespace as parts.
	// Share the system allocator so a layer cannot alias a script/user part.
	int no = ain && ain->version >= 11 ? PE_GetFreeNumber() : ctrl_stack.next_id++;
	if (no >= ctrl_stack.next_id)
		ctrl_stack.next_id = no + 1;
	memmove(&ctrl_stack.ids[index + 1], &ctrl_stack.ids[index],
			(ctrl_stack.nr_controllers - index) * sizeof(int));
	memmove(&ctrl_stack.hidden[index + 1], &ctrl_stack.hidden[index],
			(ctrl_stack.nr_controllers - index) * sizeof(bool));
	ctrl_stack.ids[index] = no;
	ctrl_stack.hidden[index] = false;
	ctrl_stack.nr_controllers++;
	ctrl_stack.active = no;
	controllers_update_sprite_order();
	return no;
}

// Release this controller's parts and return their delegate indices.
void PE_RemoveController(struct page **erase_number_list, int id)
{
	if (id == -1)
		id = ctrl_stack.active;
	int index = PE_get_controller_index(id);
	if (index < 0 || id == PARTS_CONTROLLER_SYSTEM_OVERLAY)
		VM_ERROR("Invalid controller number: %d", id);

	/* Modern GetParent already exposes pending real parents. Layer-erasing
	 * observers therefore see the new ancestry; commit its matching ownership
	 * before deciding which parts this old controller is allowed to destroy. */
	if (ain && ain->version >= 11)
		PE_UpdateComponent(0);

	// Collect and release parts belonging to this controller
	struct parts *p = TAILQ_FIRST(&parts_list);
	while (p) {
		struct parts *next = TAILQ_NEXT(p, parts_list_entry);
		if (p->controller_no == id) {
			*erase_number_list = array_pushback(*erase_number_list,
					(union vm_value){.i = p->delegate_index}, AIN_ARRAY_INT, -1);
			parts_release(p->no);
		}
		p = next;
	}

	ctrl_stack.nr_controllers--;
	memmove(&ctrl_stack.ids[index], &ctrl_stack.ids[index + 1],
			(ctrl_stack.nr_controllers - index) * sizeof(int));
	memmove(&ctrl_stack.hidden[index], &ctrl_stack.hidden[index + 1],
			(ctrl_stack.nr_controllers - index) * sizeof(bool));
	if (getenv("XSYSTEM4_TRACE_INPUT"))
		sys_message("[controller] remove id=%d\n", id);
	if (ctrl_stack.nr_controllers == 0) {
		PE_AddController(-1);
	} else if (ctrl_stack.active == id) {
		ctrl_stack.active = ctrl_stack.ids[max(0, index - 1)];
	}
	controllers_update_sprite_order();
	// Native destruction can invalidate a cached activity even when the
	// script's layer observer could not find its filtered-out root part.
	parts_reconcile_message_window_activities();
}

void PE_set_active_controller(int controller_no)
{
	if (controller_no == PARTS_CONTROLLER_SYSTEM_OVERLAY ||
			PE_get_controller_index(controller_no) >= 0)
		ctrl_stack.active = controller_no;
	else
		VM_ERROR("Invalid controller number: %d", controller_no);
}

int PE_get_active_controller(void)
{
	return ctrl_stack.active;
}

int PE_get_system_controller(void)
{
	return PARTS_CONTROLLER_SYSTEM_OVERLAY;
}

int PE_get_nr_controllers(void)
{
	return ctrl_stack.nr_controllers;
}

int PE_get_controller_index(int id)
{
	if (id == PARTS_CONTROLLER_SYSTEM_OVERLAY)
		return PARTS_CONTROLLER_SYSTEM_OVERLAY;
	for (int i = 0; i < ctrl_stack.nr_controllers; i++) {
		if (ctrl_stack.ids[i] == id)
			return i;
	}
	return -1;
}

int PE_get_controller_id(int index)
{
	return index >= 0 && index < ctrl_stack.nr_controllers ? ctrl_stack.ids[index] : -1;
}

void PE_parts_set_want_save(int parts_no, bool want_save)
{
	parts_get(parts_no)->want_save = want_save;
}

void PE_SetWantSaveBackScene(int parts_no, bool enable)
{
	parts_get(parts_no)->want_save_back_scene = enable;
}

bool PE_IsWantSaveBackScene(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts && parts->want_save_back_scene;
}

float PE_parts_get_absolute_x(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? (float)parts->global.pos.x : 0.0f;
}

float PE_parts_get_absolute_y(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? (float)parts->global.pos.y : 0.0f;
}

int PE_parts_get_absolute_z(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? parts->global.z : 0;
}

static int parts_max_z(struct parts *p)
{
	int z=p->global.z;
	struct parts *child;
	PARTS_FOREACH_CHILD(child,p) z=max(z,parts_max_z(child));
	return z;
}

int PE_parts_get_absolute_max_z(int parts_no)
{
	PE_UpdateComponent(0);
	struct parts *p=parts_try_get(parts_no);
	return p?parts_max_z(p):0;
}

void PE_parts_set_lock_input_state(int parts_no, bool lock)
{
	parts_get(parts_no)->lock_input_state = lock;
}

bool PE_init_parts_movie(int parts_no, int width, int height, int bg_r, int bg_g, int bg_b, int state)
{
	if (!parts_state_valid(--state) || width <= 0 || height <= 0)
		return false;

	struct parts *parts = parts_get(parts_no);

	int sp_no = sact_SP_GetUnuseNum(0);
	struct sact_sprite *sp = sact_create_sprite(sp_no, width, height, bg_r, bg_g, bg_b, 255);
	if (!sp)
		return false;
	struct parts_movie *movie = parts_get_movie(parts, state);
	parts_movie_free(movie);
	// The movie frames are composited by the parts engine via parts_render(),
	// so hide the bound sprite from the scene to avoid double-drawing.
	sprite_set_show(sp, false);

	struct texture *tex = sprite_get_texture(sp);
	movie->sprite_no = sp_no;
	movie->common.texture = *tex; // XXX: textures normally shouldn't be copied like this...
	parts_set_dims(parts, &movie->common, width, height);
	parts_dirty(parts);
	return true;
}

int PE_get_movie_sprite(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return -1;

	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_MOVIE)
		return -1;

	return parts->states[state].movie.sprite_no;
}

bool parts_3dlayer_create_plugin(struct parts_3dlayer *l)
{
	if (l->plugin >= 0)
		return false;

	int handle = ReignEngine_create_plugin(RE_SEAL_PLUGIN);
	if (handle < 0)
		return false;

	int sp_no = sact_SP_GetUnuseNum(0);
	struct sact_sprite *sp = sact_create_sprite(sp_no, 1, 1, 0, 0, 0, 255);
	if (!sp) {
		ReignEngine_ReleasePlugin(handle);
		return false;
	}

	if (!ReignEngine_BindPlugin(handle, sp_no)) {
		sact_SP_Delete(sp_no);
		ReignEngine_ReleasePlugin(handle);
		return false;
	}
	// The 3D content is composited by the parts engine via parts_render(), so
	// hide the bound sprite from the scene to avoid double-drawing.
	sprite_set_show(sp, false);

	l->plugin = handle;
	l->sprite_no = sp_no;
	return true;
}

bool PE_CreateParts3DLayerPluginID(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_get(parts_no);
	return parts_3dlayer_create_plugin(parts_get_3dlayer(parts, state));
}

int PE_GetParts3DLayerPluginID(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return -1;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_3DLAYER)
		return -1;
	return parts->states[state].layer3d.plugin;
}

bool PE_ReleaseParts3DLayerPluginID(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_3DLAYER)
		return false;
	struct parts_3dlayer *l = &parts->states[state].layer3d;
	if (l->plugin < 0)
		return false;
	ReignEngine_ReleasePlugin(l->plugin);
	l->plugin = -1;
	if (l->sprite_no >= 0) {
		sact_SP_Delete(l->sprite_no);
		l->sprite_no = -1;
	}
	return true;
}
