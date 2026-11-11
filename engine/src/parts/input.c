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

#include "system4.h"
const char *PE_DebugPartName(int number);
void vm_trace_callchain_for(unsigned ms);
bool PE_IsActivityOpen(const char *prefix);

#include "asset_manager.h"
#include "parts.h"
#include "audio.h"
#include "system4/archive.h"
#include "system4/string.h"
#include "input.h"
#include "system4/ain.h"
#include "vm.h"
#include "xsystem4.h"
#include "parts_internal.h"
#include "../ain12_drag_capability.inc"
#include "swipe_gesture.h"

// true between calls to BeginClick and EndClick
bool parts_began_click = false;
// Multi-controller activities nest their input loops while running callbacks.
// Ending the child menu must leave its suspended parent's loop enabled.
static unsigned input_depth;

// the mouse position at last update
static Point parts_prev_pos = {0};
// true when the left mouse button was down last update
static bool prev_clicking = false;
// the last (fully) clicked parts number
static int clicked_parts = 0;
// the current (partially) clicked parts number
static int click_down_parts = 0;
static int click_miss_sound=-1;
// A whole-screen listener is independent of the hit part, but a captured
// gesture must not turn into a click when the pointer is released later.
static bool whole_click_cancelled;

// drag state
static struct parts *drag_parts = NULL;
static bool is_dragging = false;
static Point drag_initial_pos;
static Point drag_start_cursor;
static struct parts *drop_target = NULL;
static int swipe_parts_no;
static struct parts_swipe_gesture swipe_gesture;

static int *sound_slot(struct parts *p,int state)
{
	switch (state) {
	case 1: return &p->normal_sound;
	case 2: return &p->on_cursor_sound;
	case 3: return &p->on_click_sound;
	default: VM_ERROR("Invalid widget sound state %d",state);
	}
}

void PE_SetPartsSoundName(int no,struct string *name,int state)
{
	int id=-1;
	if (name->size && !asset_exists_by_name(ASSET_SOUND,name->text,&id))
		VM_ERROR("Missing widget sound name");
	*sound_slot(parts_get(no),state)=id;
}

void PE_GetPartsSoundName(int no,struct string **out,int state)
{
	int id=*sound_slot(parts_get(no),state);
	struct archive_data *data=id>=0?asset_get(ASSET_SOUND,id):NULL;
	free_string(*out);
	*out=data?cstr_to_string(data->name):string_ref(&EMPTY_STRING);
	if (data) archive_free_data(data);
}

void PE_PlayPartsSound(int no,int state)
{
	int id=*sound_slot(parts_get(no),state);
	if (id>=0) audio_play_sound(id);
}

/* Per native input call only: no cached metadata/heap pointers cross VM work.
 * Most parts fail the cheap guards. Resolve schema once lazily for the first
 * fully transparent passive CG, then query only its live native/script route.
 */
struct parts_input_drag_schema {
    bool checked, known;
    int global, manager, functions;
};
static bool parts_invisible_passive_cg(struct parts *p, struct parts_input_drag_schema *schema)
{
    if (!ain || ain->version < 11 || p->states[0].type != PARTS_CG ||
            p->global.alpha || p->widget || p->clickable || p->draggable ||
            p->wheelable || p->swipe_type || (is_dragging && drag_parts == p))
        return false;
    if (!schema->checked) {
        schema->checked = true;
        schema->known = parts_drag_r10_schema(&schema->global, &schema->manager, &schema->functions);
    }
    /* Unknown schemas and explicit script drag routes preserve prior behavior.
     * Transparent rectangle detection remains input geometry, not decoration. */
    return schema->known && (p->delegate_index < 0 ||
        !parts_drag_enabled_in_schema(p, schema->global, schema->manager, schema->functions));
}

static bool input_allowed(struct parts *p)
{
	// Modern pages fade their container to zero without destroying child hit
	// regions. Those children must not intercept the newly exposed page.
	// A part's own zero alpha remains valid for intentional invisible hot spots.
	if (ain && ain->version>=11 && p->parent && !p->parent->global.alpha)
		return false;
	return !p->parked_textures && !p->input_disabled && parts_widget_visible(p) && (!p->widget || p->widget->enabled)
		&& (!parts_multi_controller || p->controller_no==ctrl_stack.active
			|| (p->controller_no==PARTS_CONTROLLER_SYSTEM_OVERLAY && p->clickable));
}

int PE_GetFocusPartsNumber(void)
{
	struct parts *p;
	PARTS_LIST_FOREACH(p) if (p->has_focus && input_allowed(p)
			&& (!parts_multi_controller || p->controller_no==ctrl_stack.active)) return p->no;
	return 0;
}
bool PE_IsFocus(int no) { return no && PE_GetFocusPartsNumber()==no; }
void PE_SetFocus(int no)
{
	struct parts *target=no?parts_try_get(no):NULL;
	if (no && (!target || !input_allowed(target))) return;
	int controller=target?target->controller_no:ctrl_stack.active;
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		if (parts_multi_controller && p->controller_no!=controller) continue;
		if (p->has_focus && p!=target) {
			p->has_focus=false; parts_textbox_open(p,false);parts_textbox_dirty(p);parts_msg_push(p,PARTS_MSG_LOST_FOCUS,"");
		}
	}
	if (target && !target->has_focus) {
		target->has_focus=true; parts_textbox_dirty(target);parts_msg_push(target,PARTS_MSG_FOCUS,"");
	}
}

static bool parts_hittest(struct parts *parts, int state, Point pos)
{
	if (parts->parked_textures) return false;
	bool hit;
	if (parts->states[state].type==PARTS_FLAT && !parts->widget
			&& parts_flat_hittest(parts,state,pos,&hit)) return hit;
	Rectangle hitbox = parts->states[state].common.hitbox;
	hitbox.x-=parts_scroll_link_pos(parts,0);hitbox.y-=parts_scroll_link_pos(parts,1);
	if (parts->parent) {
		Point origin=parts_child_origin(parts->parent);
		hitbox.x += origin.x;
		hitbox.y += origin.y;
	}
	return SDL_PointInRect(&pos, &hitbox);
}

static void drag_state_reset(void)
{
	drag_parts = NULL;
	is_dragging = false;
	drop_target = NULL;
}
static void swipe_message(void *data,enum parts_swipe_event event,int sx,int sy,int x,int y,bool inertia)
{
    struct parts *p=parts_try_get(swipe_parts_no);if(!p)return;
    if(event==PARTS_SWIPE_BEGIN) {
        whole_click_cancelled=true;
        struct parts *pressed=parts_try_get(click_down_parts);
        if(pressed && pressed->widget){pressed->widget->pressed_region=-1;pressed->widget->dirty=true;}
        clicked_parts=click_down_parts=0;drag_state_reset();
        parts_msg_push(p,PARTS_MSG_SWIPE_BEGIN,"");
    } else if(event==PARTS_SWIPE_UPDATE)parts_msg_push(p,PARTS_MSG_SWIPING,"iiiib",sx,sy,x,y,inertia);
    else parts_msg_push(p,PARTS_MSG_SWIPE_END,"");
}
void PE_StopSwipe(void)
{parts_swipe_stop(&swipe_gesture,swipe_message,NULL);swipe_parts_no=0;}

void parts_input_reset_drag(struct parts *parts)
{
    if(parts->is_back_scene)return;
    if(parts->no==swipe_parts_no){parts_swipe_reset(&swipe_gesture);swipe_parts_no=0;}
	if (clicked_parts==parts->no) clicked_parts=0;
	if (click_down_parts==parts->no) click_down_parts=0;
	if (parts == drag_parts)
		drag_state_reset();
	else if (parts == drop_target)
		drop_target = NULL;
}
static void update_swipe(Point pos,bool down,int passed)
{
    if(!parts_began_click){parts_swipe_reset(&swipe_gesture);swipe_parts_no=0;return;}
    if(down && !prev_clicking) {
        PE_StopSwipe();struct parts *p;
        PARTS_LIST_FOREACH_REVERSE(p)if(p->swipe_type>=1 && p->swipe_type<=3 && input_allowed(p) &&
            parts_hittest(p,PARTS_STATE_DEFAULT,pos)) {
            swipe_parts_no=p->no;parts_swipe_begin(&swipe_gesture,p->swipe_type,pos.x,pos.y);break;
        }
    }
    struct parts *p=swipe_parts_no?parts_try_get(swipe_parts_no):NULL;
    if(swipe_parts_no && (!p || !input_allowed(p))){parts_swipe_reset(&swipe_gesture);swipe_parts_no=0;return;}
    if(p)parts_swipe_update(&swipe_gesture,pos.x,pos.y,down,passed,swipe_message,NULL);
    if(!swipe_gesture.type)swipe_parts_no=0;
}

static void parts_update_mouse(struct parts *parts, Point cur_pos, bool cur_clicking,
		int passed_time, bool *hover_consumed, bool *click_consumed,
        struct parts_input_drag_schema *schema)
{
	// Measured layout boxes are geometry containers. Their extent must not
	// block visible controls in another group unless explicitly clickable.
	if (!input_allowed(parts) || parts_invisible_passive_cg(parts,schema) ||
            (!parts->clickable && parts->states[0].type==PARTS_LAYOUT_BOX)) {
		parts->is_hovered=false;
		if (parts->widget && (parts->widget->hover_region!=-1 || parts->widget->pressed_region!=-1)) {
			parts->widget->hover_region=-1;
			parts->widget->pressed_region=-1;
			parts->widget->dirty=true;
		}
		if(!input_allowed(parts) || parts->no!=swipe_parts_no)parts_input_reset_drag(parts);
		parts_set_state(parts,PARTS_STATE_DEFAULT);
		return;
	}
	// Always use DEFAULT state hitbox regardless of the current display state.
	bool is_hovered = parts_hittest(parts, PARTS_STATE_DEFAULT, cur_pos)
		&& !*hover_consumed;

	bool was_hovered = parts->is_hovered;
	parts->is_hovered = is_hovered;

	// !pass_cursor parts consume the cursor for parts behind them
	if (is_hovered && !parts->pass_cursor)
		*hover_consumed = true;

	if (parts->linked_from >= 0 && is_hovered != was_hovered) {
		parts_dirty(parts_get(parts->linked_from));
	}

	if (!parts_began_click)
		return;
	if (parts->no!=click_down_parts)
		parts_widget_mouse(parts,cur_pos,false,false,false);

	if (is_hovered && !was_hovered) {
		parts_msg_push(parts, PARTS_MSG_MOUSE_ENTER, "ii", cur_pos.x, cur_pos.y);
		parts->hover_time = 0;
	}
	if (!is_hovered && was_hovered) {
		parts_msg_push(parts, PARTS_MSG_MOUSE_LEAVE, "ii", cur_pos.x, cur_pos.y);
	}

	if (is_hovered) {
		// MOUSE_ON message fires every frame while hovering
		parts_msg_push(parts, PARTS_MSG_MOUSE_ON,
				"iii", cur_pos.x, cur_pos.y, parts->hover_time);
		parts->hover_time += passed_time;

		// MOUSE_MOVE message fires when cursor moves while hovering
		if (cur_pos.x != parts_prev_pos.x || cur_pos.y != parts_prev_pos.y) {
			parts_msg_push(parts, PARTS_MSG_MOUSE_MOVE, "ii", cur_pos.x, cur_pos.y);
		}
	}

	if (!is_hovered) {
		if (parts->no==click_down_parts)
			parts_widget_mouse(parts,cur_pos,cur_clicking,false,prev_clicking&&!cur_clicking);
		parts_set_state(parts, PARTS_STATE_DEFAULT);
		return;
	}

	bool click_eligible = parts->clickable || !parts->pass_cursor;
	if (!click_eligible || *click_consumed) {
		if (!was_hovered)
			PE_PlayPartsSound(parts->no,2);
		parts_set_state(parts, PARTS_STATE_HOVERED);
		return;
	}

	// click down: first eligible part captures the click
	if (cur_clicking && !prev_clicking) {
		if(getenv("XSYSTEM4_TRACE_INPUT")){
			fprintf(stderr,"[drag-capture] down=%d clickable=%d draggable=%d pass=%d ctrl=%d\n",parts->no,parts->clickable,parts->draggable,parts->pass_cursor,parts->controller_no);
			int depth=0;for(struct parts *p=parts->parent;p && depth++<12;p=p->parent)
				fprintf(stderr,"[drag-capture] parent=%d clickable=%d draggable=%d pass=%d delegate=%d\n",p->no,p->clickable,p->draggable,p->pass_cursor,p->delegate_index);
		}
		click_down_parts = parts->no;
		*click_consumed = true;
		PE_SetFocus(parts->no);
		parts_widget_mouse(parts,cur_pos,true,true,false);

		drag_parts = parts;
		drag_initial_pos = parts->local.pos;
		drag_start_cursor = cur_pos;

		// KEY_TRIGGER message fires on press transition
		parts_msg_push(parts, PARTS_MSG_KEY_TRIGGER, "i", VK_LBUTTON);
	}

	// KEY_DOWN message fires every frame while held (not first frame)
	if (prev_clicking && cur_clicking && click_down_parts == parts->no) {
		parts_widget_mouse(parts,cur_pos,true,false,false);
		parts_msg_push(parts, PARTS_MSG_KEY_DOWN, "i", VK_LBUTTON);
	}

	if (cur_clicking && click_down_parts == parts->no) {
		parts_set_state(parts, PARTS_STATE_CLICKED);
	} else {
		if (!was_hovered) {
			PE_PlayPartsSound(parts->no,2);
		}
		parts_set_state(parts, PARTS_STATE_HOVERED);
	}

	// click event: only if the click down event had same parts number
	if (parts->clickable && !whole_click_cancelled && !is_dragging
			&& prev_clicking && !cur_clicking
			&& click_down_parts == parts->no) {
		parts_widget_mouse(parts,cur_pos,false,false,true);
		PE_PlayPartsSound(parts->no,3);
		clicked_parts = parts->no;
		if (strstr(PE_DebugPartName(parts->no), "Battle") ||
				strstr(PE_DebugPartName(parts->no), "Quest"))
			PE_RecordNavigationClick(parts->no, PE_DebugPartName(parts->no));
		if (getenv("XSYSTEM4_TRACE_INPUT"))
			sys_message("[parts-click] parts=%d delegate=%d controller=%d name=%s\n",
				parts->no, parts->delegate_index, parts->controller_no, PE_DebugPartName(parts->no));

		parts_msg_push(parts, PARTS_MSG_MOUSE_CLICK,
				"iii", cur_pos.x, cur_pos.y, VK_LBUTTON);
		parts_msg_push(parts, PARTS_MSG_KEY_UP, "i", VK_LBUTTON);
		parts_msg_push(parts, PARTS_MSG_KEY_PRESS, "i", VK_LBUTTON);
	}
}

static void update_keyboard(void)
{
	static bool previous[VK_NR_KEYCODES];
	static unsigned repeat[VK_NR_KEYCODES];
	unsigned now=SDL_GetTicks();
	struct parts *focus=parts_try_get(PE_GetFocusPartsNumber());
	for (int key=1;key<VK_NR_KEYCODES;key++) {
		bool down=key_is_down(key);
		if (key==VK_LBUTTON) { previous[key]=down; continue; }
		if (parts_began_click && down && !previous[key]) {
			parts_msg_push(NULL,PARTS_MSG_KEY_TRIGGER,"i",key);
			parts_msg_push(NULL,PARTS_MSG_KEY_PRESS,"i",key);
			if (focus) {
				parts_msg_push(focus,PARTS_MSG_KEY_TRIGGER,"i",key);
				parts_msg_push(focus,PARTS_MSG_KEY_PRESS,"i",key);
				if (focus->widget && focus->widget->type==1 && (key==VK_SPACE || key==VK_RETURN))
					parts_widget_check(focus,!focus->widget->checked || focus->widget->button_mode);
				if (focus->widget && focus->widget->type==3 && (key==VK_LEFT || key==VK_RIGHT))
					parts_widget_scroll(focus,focus->widget->pos+(key==VK_LEFT?-focus->widget->step:focus->widget->step));
			}
			repeat[key]=now+400;
		} else if (parts_began_click && down && previous[key]) {
			parts_msg_push(NULL,PARTS_MSG_KEY_DOWN,"i",key);
			if (focus) parts_msg_push(focus,PARTS_MSG_KEY_DOWN,"i",key);
			if (SDL_TICKS_PASSED(now,repeat[key])) {
				parts_msg_push(NULL,PARTS_MSG_KEY_PRESS,"i",key);
				if (focus) parts_msg_push(focus,PARTS_MSG_KEY_PRESS,"i",key);
				repeat[key]=now+75;
			}
		} else if (parts_began_click && !down && previous[key]) {
			parts_msg_push(NULL,PARTS_MSG_KEY_UP,"i",key);
			if (focus) parts_msg_push(focus,PARTS_MSG_KEY_UP,"i",key);
		}
		previous[key]=down;
	}
	int forward,back; mouse_get_wheel(&forward,&back);
	if (parts_began_click && (forward || back)) {
		parts_msg_push(NULL,PARTS_MSG_MOUSE_WHEEL,"ii",forward,back);
		struct parts *focus=parts_try_get(PE_GetFocusPartsNumber());
		if (focus && focus->wheelable) parts_msg_push(focus,PARTS_MSG_MOUSE_WHEEL,"ii",forward,back);
		mouse_clear_wheel();
	}
}



/* Diagnostic: scan which parts sit under a game-space point at scheduled
 * times. Format: XSYSTEM4_TEST_HITSCAN="sec:x:y;sec:x:y". */
static void diag_test_hitscan(void)
{
	static bool init;
	static struct { Uint32 at; int x, y; } items[32];
	static int count, index;
	if (!init) {
		init = true;
		const char *cfg = getenv("XSYSTEM4_TEST_HITSCAN");
		if (cfg) {
			char *copy = strdup(cfg);
			Uint32 start = SDL_GetTicks();
			for (char *it = strtok(copy, ";,"); it; it = strtok(NULL, ";,")) {
				double sec; int x, y;
				if (count >= 32 || sscanf(it, "%lf:%d:%d", &sec, &x, &y) != 3)
					continue;
				items[count].at = start + (Uint32)(sec * 1000);
				items[count].x = x;
				items[count].y = y;
				count++;
			}
			free(copy);
			if (count)
				sys_message("[test-scan] armed %d scan(s)\n", count);
		}
	}
	if (index >= count || SDL_GetTicks() < items[index].at)
		return;
	Point pt;
	pt.x = items[index].x;
	pt.y = items[index].y;
	sys_message("[test-scan] point (%d,%d)\n", pt.x, pt.y);
	struct parts *hp;
	int shown = 0;
	PARTS_LIST_FOREACH_REVERSE(hp) {
		if (!parts_hittest(hp, PARTS_STATE_DEFAULT, pt))
			continue;
		sys_message("[test-scan] parts=%d ctrl=%d clickable=%d allowed=%d delegate=%d z=%d\n",
			hp->no, hp->controller_no, hp->clickable, input_allowed(hp) ? 1 : 0,
			hp->delegate_index, hp->sp.z);
		if (++shown >= 8)
			break;
	}
	index++;
}

/* Diagnostic: click a part by number at scheduled times, bypassing pointer
 * hit-testing. Format: XSYSTEM4_TEST_PART_CLICK="sec:part;sec:part". */
static void diag_test_part_click(void)
{
	static bool init;
	static struct { Uint32 at; int no; } items[64];
	static int count, index;
	if (!init) {
		init = true;
		const char *cfg = getenv("XSYSTEM4_TEST_PART_CLICK");
		if (cfg) {
			char *copy = strdup(cfg);
			Uint32 start = SDL_GetTicks();
			for (char *it = strtok(copy, ";,"); it; it = strtok(NULL, ";,")) {
				double sec; int no;
				if (count >= 64 || sscanf(it, "%lf:%d", &sec, &no) != 2)
					continue;
				items[count].at = start + (Uint32)(sec * 1000);
				items[count].no = no;
				count++;
			}
			free(copy);
			if (count)
				sys_message("[test-part] armed %d click(s)\n", count);
		}
	}
	if (index >= count || SDL_GetTicks() < items[index].at)
		return;
	int no = items[index++].no;
	bool force = getenv("XSYSTEM4_TEST_FORCE") != NULL;
	struct parts *p = parts_try_get(no);
	if (!p || (!force && !input_allowed(p))) {
		sys_message("[test-part] parts=%d %s\n", no, p ? "blocked" : "missing");
		return;
	}
	clicked_parts = p->no;
	parts_msg_push(p, PARTS_MSG_MOUSE_CLICK, "iii", 0, 0, VK_LBUTTON);
	parts_msg_push(p, PARTS_MSG_KEY_UP, "i", VK_LBUTTON);
	parts_msg_push(p, PARTS_MSG_KEY_PRESS, "i", VK_LBUTTON);
	sys_message("[test-part] clicked parts=%d delegate=%d\n", p->no, p->delegate_index);
}

void PE_UpdateInputState(int passed_time)
{
	diag_test_part_click();
	diag_test_hitscan();
    if (parts_multi_controller && !input_depth && !parts_began_click) {
        unsigned waiting=vm_parts_active_input_scopes();
        if (waiting) {
            // VM restores and nested activity exits can outlive the native
            // counter. Recover only bytecode-proven entered input loops.
            input_begin_mouse_sampling();input_depth=waiting;parts_began_click=true;
            NOTICE("[parts-input] recovered %u active wait scope(s)",waiting);
        }
    }
    struct parts_input_drag_schema schema = {0};
	if (input_next_mouse_sample()) {
		parts_swipe_reset(&swipe_gesture);swipe_parts_no=0;
		prev_clicking = false;
		clicked_parts = click_down_parts = 0;
		drag_state_reset();
		whole_click_cancelled = true;
	}
	update_keyboard();
	Point cur_pos;
	bool cur_clicking = key_is_down(VK_LBUTTON);
	mouse_get_pos(&cur_pos.x, &cur_pos.y);
	if (cur_clicking && !prev_clicking && !parts_began_click) {
		sys_message("[input] click while input scope closed (depth=%u)\n", input_depth);
	}
	if (cur_clicking && !prev_clicking) {
		whole_click_cancelled = false;
		if (getenv("XSYSTEM4_TRACE_INPUT")) {
			struct parts *hp;
			int shown = 0;
			PARTS_LIST_FOREACH_REVERSE(hp) {
				if (!parts_hittest(hp, PARTS_STATE_DEFAULT, cur_pos))
					continue;
				sys_message("[hit-scan] parts=%d z=%d z2=%d ctrl=%d clickable=%d allowed=%d delegate=%d "
					"alpha=%d show=%d disabled=%d mw=%d parent=%d palpha=%d pshow=%d name=%s\n",
					hp->no, hp->sp.z, hp->sp.z2, hp->controller_no,
					hp->clickable, input_allowed(hp) ? 1 : 0, hp->delegate_index,
					hp->global.alpha, hp->global.show ? 1 : 0, hp->input_disabled ? 1 : 0,
					hp->message_window ? 1 : 0,
					hp->parent ? hp->parent->no : 0,
					hp->parent ? hp->parent->global.alpha : 255,
					hp->parent ? (hp->parent->global.show ? 1 : 0) : 1,
					PE_DebugPartName(hp->no));
				if (++shown >= 10)
					break;
			}
		}
	}
	update_swipe(cur_pos,cur_clicking,passed_time);

	bool hover_consumed = false;
	bool click_consumed = false;
	struct parts *parts;
	// Iterate front-to-back (highest z first) for proper cursor consumption
	PARTS_LIST_FOREACH_REVERSE(parts) {
		parts_update_mouse(parts, cur_pos, cur_clicking, passed_time,
				&hover_consumed, &click_consumed, &schema);
	}

	// Drag movement processing
	if (drag_parts && cur_clicking) {
		bool cursor_moved = (cur_pos.x != parts_prev_pos.x ||
				cur_pos.y != parts_prev_pos.y);
		// Modern scripts can handle drag motion themselves without enabling
		// native auto-movement (e.g. PartyCard's shadow/preview animation).
		// A small slop keeps ordinary button taps from becoming manual drags.
		int dx=cur_pos.x-drag_start_cursor.x,dy=cur_pos.y-drag_start_cursor.y;
		bool manual=ain && ain->version>=11 &&
			(is_dragging || parts_ain12_manual_drag_enabled(drag_parts));
		bool dragging=drag_parts->draggable || (manual && (is_dragging || dx*dx+dy*dy>=16));
		if (cursor_moved && dragging) {
			Point new_pos = {
				drag_initial_pos.x + (cur_pos.x - drag_start_cursor.x),
				drag_initial_pos.y + (cur_pos.y - drag_start_cursor.y)
			};
			if(drag_parts->draggable)parts_set_pos(drag_parts, new_pos);
			if (!is_dragging) {
				parts_msg_push(drag_parts, PARTS_MSG_DRAG_BEGIN,
					ain && ain->version>=11 ? "i" : "", VK_LBUTTON);
			}
			parts_msg_push(drag_parts, PARTS_MSG_DRAGGING,
					ain && ain->version>=11 ? "iiiii" : "iiii",
					drag_start_cursor.x, drag_start_cursor.y, cur_pos.x, cur_pos.y, VK_LBUTTON);
			is_dragging = true;
			whole_click_cancelled = true;
		}

		// Drop target tracking
		if (cursor_moved && is_dragging) {
			struct parts *new_drop = NULL;
			PARTS_LIST_FOREACH_REVERSE(parts) {
				if (parts == drag_parts || !input_allowed(parts) || parts_invisible_passive_cg(parts,&schema))
					continue;
				if (parts_hittest(parts, PARTS_STATE_DEFAULT, cur_pos)) {
					new_drop = parts;
					if (!parts->pass_cursor)
						break;
				}
			}
			if (new_drop != drop_target) {
				if (drop_target) {
					parts_msg_push(drop_target, PARTS_MSG_DROP_LEAVE,
							"i", drag_parts->no);
				}
				if (new_drop) {
					parts_msg_push(new_drop, PARTS_MSG_DROP_ENTER,
							"i", drag_parts->no);
				}
				drop_target = new_drop;
			} else if (drop_target) {
				parts_msg_push(drop_target, PARTS_MSG_DROP_ON,
						"iii", drag_parts->no, cur_pos.x, cur_pos.y);
			}
		}
	}

	// Release handling
	if (prev_clicking && !cur_clicking) {
		if(getenv("XSYSTEM4_TRACE_INPUT"))fprintf(stderr,"[drag-capture] up source=%d dragging=%d target=%d\n",drag_parts?drag_parts->no:0,is_dragging,drop_target?drop_target->no:0);
		// Parts 0 is the script's whole-screen function set. A background
		// click has no clickable part and still needs to reach that listener.
		if (parts_began_click && !whole_click_cancelled)
			parts_msg_push(NULL, PARTS_MSG_MOUSE_CLICK,
					"iii", cur_pos.x, cur_pos.y, VK_LBUTTON);
		// Modern source cleanup consults the drop result (e.g. whether a card
		// was accepted by another slot), so deliver that result before DragEnd.
		if (ain && ain->version>=11 && drop_target && drag_parts)
			parts_msg_push(drop_target, PARTS_MSG_DROPPED,
					"iii", drag_parts->no, cur_pos.x, cur_pos.y);
		if (is_dragging && drag_parts && (drag_parts->draggable || (ain && ain->version>=11))) {
			parts_msg_push(drag_parts, PARTS_MSG_DRAG_END,
				ain && ain->version>=11 ? "ib" : "", VK_LBUTTON, false);
		}
		if (drop_target && drag_parts) {
			if (!ain || ain->version<11)
				parts_msg_push(drop_target, PARTS_MSG_DROPPED,
						"iii", drag_parts->no, cur_pos.x, cur_pos.y);
			parts_msg_push(drop_target, PARTS_MSG_DROP_LEAVE, "i", drag_parts->no);
		}
		drag_state_reset();

		if (!click_down_parts) {
			if(click_miss_sound>=0) audio_play_sound(click_miss_sound);
		}
		click_down_parts = 0;
	}

	prev_clicking = cur_clicking;
	parts_prev_pos = cur_pos;
}

void PE_SetPassCursor(int parts_no, bool pass)
{
	parts_get(parts_no)->pass_cursor = !!pass;
}

bool PE_GetPartsPassCursor(int parts_no)
{
	return parts_get(parts_no)->pass_cursor;
}

void PE_SetClickable(int parts_no, bool clickable)
{
	parts_get(parts_no)->clickable = !!clickable;
}

bool PE_GetPartsClickable(int parts_no)
{
	return parts_get(parts_no)->clickable;
}

void PE_SetPartsGroupDecideOnCursor(possibly_unused int group_no, possibly_unused bool decide_on_cursor)
{
	UNIMPLEMENTED("(%d, %s)", group_no, decide_on_cursor ? "true" : "false");
}

void PE_SetPartsGroupDecideClick(possibly_unused int group_no, possibly_unused bool decide_click)
{
	UNIMPLEMENTED("(%d, %s)", group_no, decide_click ? "true" : "false");
}

void PE_SetOnCursorShowLinkPartsNumber(int parts_no, int link_parts_no)
{
	struct parts *parts = parts_get(parts_no);
	struct parts *link_parts = parts_get(link_parts_no);
	parts->linked_to = link_parts_no;
	link_parts->linked_from = parts_no;
}

int PE_GetOnCursorShowLinkPartsNumber(int parts_no)
{
	return parts_get(parts_no)->linked_to;
}

bool PE_SetPartsOnCursorSoundNumber(int parts_no, int sound_no)
{
	if (!asset_exists(ASSET_SOUND, sound_no)) {
		WARNING("Invalid sound number: %d", sound_no);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	parts->on_cursor_sound = sound_no;
	return true;
}

bool PE_SetPartsClickSoundNumber(int parts_no, int sound_no)
{
	if (!asset_exists(ASSET_SOUND, sound_no)) {
		WARNING("Invalid sound number: %d", sound_no);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	parts->on_click_sound = sound_no;
	return true;
}

void PE_SetClickMissSoundName(struct string *name)
{
	int id=-1;
	if(name->size && !asset_exists_by_name(ASSET_SOUND,name->text,&id)) VM_ERROR("Missing click-miss sound");
	click_miss_sound=id;
}
struct string *PE_GetClickMissSoundName(void)
{
	struct archive_data *data=click_miss_sound>=0?asset_get(ASSET_SOUND,click_miss_sound):NULL;
	struct string *name=data?cstr_to_string(data->name):string_ref(&EMPTY_STRING);
	if(data) archive_free_data(data);
	return name;
}
bool PE_SetClickMissSoundNumber(int sound_no)
{
	click_miss_sound=sound_no>0?sound_no:-1;
	return true;
}

void PE_BeginInput(void)
{
	if (input_depth == UINT_MAX)
		VM_ERROR("Parts input nesting overflow");
	input_depth = parts_multi_controller ? input_depth + 1 : 1;
	input_begin_mouse_sampling();
	parts_began_click = true;
	if (getenv("XSYSTEM4_TRACE_INPUT"))
		fprintf(stderr,"[parts-input] Begin controller=%d depth=%u\n",ctrl_stack.active,input_depth);
}

void parts_input_reset(void)
{
	parts_swipe_reset(&swipe_gesture);swipe_parts_no=0;
	input_end_mouse_sampling();
	input_depth = 0;
	parts_began_click = false;
	prev_clicking = false;
	whole_click_cancelled = true;
	clicked_parts = 0;
	click_down_parts = 0;
	drag_state_reset();
}

void PE_EndInput(void)
{
	PE_StopSwipe();
	clicked_parts = 0;
	click_down_parts = 0;
	drag_state_reset();
	if (input_depth > 1)
		input_depth--;
	else
		parts_input_reset();
	if (getenv("XSYSTEM4_TRACE_INPUT"))
		fprintf(stderr,"[parts-input] End controller=%d depth=%u\n",ctrl_stack.active,input_depth);
}

void PE_SetDrag(int parts_no, bool enable)
{
	parts_get(parts_no)->draggable = !!enable;
}
bool PE_GetPartsDrag(int no){return parts_get(no)->draggable;}
void PE_SetSwipeType(int no,int type)
{
    if(type<0 || type>4)VM_ERROR("Invalid swipe type: %d",type);
    if(no==swipe_parts_no)PE_StopSwipe();parts_get(no)->swipe_type=type;
}
int PE_GetSwipeType(int no){return parts_get(no)->swipe_type;}

int PE_GetClickPartsNumber(void)
{
	return clicked_parts;
}

bool PE_IsCursorIn(int parts_no, int mouse_x, int mouse_y, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return false;

	Point mouse_pos = { mouse_x, mouse_y };
	return parts_hittest(parts, state, mouse_pos);
}
