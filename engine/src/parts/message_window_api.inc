#ifndef PARTS_MESSAGE_WINDOW_API_INC
#define PARTS_MESSAGE_WINDOW_API_INC

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

/*
 * Mechanical typed wrappers for parts_message_window.h.  This file is included
 * at the end of message_window.c; all mw_* helpers are provided by that file.
 * No resource implementation lives here.
 */

static struct string *mw_api_string(struct string *s)
{
	return s ? string_dup(s) : string_ref(&EMPTY_STRING);
}

void PE_SetMessageWindowActive(int no, bool active)
{
	struct parts *owner = mw_owner(no);
	struct parts_message_window *m = parts_message_window_ensure(owner);
	m->active = active;
	parts_dirty(owner);
}

void PE_SetMessageWindowInactiveMultipleColor(int no, int r, int g, int b)
{
	if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
		ERROR("PE_SetMessageWindowInactiveMultipleColor: color out of range");
	struct parts *owner = mw_owner(no);
	struct parts_message_window *m = parts_message_window_ensure(owner);
	m->inactive = (SDL_Color){ (uint8_t)r, (uint8_t)g, (uint8_t)b, 255 };
	parts_dirty(owner);
}

int PE_GetMessageWindowInactiveMultipleColorR(int no)
{
	struct parts_message_window *m = mw_lookup(no);
	return m ? m->inactive.r : 255;
}

int PE_GetMessageWindowInactiveMultipleColorG(int no)
{
	struct parts_message_window *m = mw_lookup(no);
	return m ? m->inactive.g : 255;
}

int PE_GetMessageWindowInactiveMultipleColorB(int no)
{
	struct parts_message_window *m = mw_lookup(no);
	return m ? m->inactive.b : 255;
}

void PE_SetMessageWindowCGName(int no, struct string *name)
{
	mw_choose_image(no, false, false, name, 0, 0, 0);
}

struct string *PE_GetMessageWindowCGName(int no)
{
	struct parts_message_window *m = mw_lookup(no);
	return mw_api_string(m ? m->cg_name : NULL);
}

void PE_SetMessageWindowFlatName(int no, struct string *name)
{
	mw_choose_image(no, false, true, name, 0, 0, 0);
}

struct string *PE_GetMessageWindowFlatName(int no)
{
	struct parts_message_window *m = mw_lookup(no);
	return mw_api_string(m ? m->flat_name : NULL);
}

void PE_SetMessageWindowFlatShowWaitFrameNumber(int no, int frame)
{
	if (frame < 0 || frame > 1000000)
		ERROR("PE_SetMessageWindowFlatShowWaitFrameNumber: frame out of range");
	struct parts *owner = mw_owner(no);
	struct parts_message_window *m = parts_message_window_ensure(owner);
	m->wait_frame = frame;
	parts_dirty(owner);
}

int PE_GetMessageWindowFlatShowWaitFrameNumber(int no)
{
	struct parts_message_window *m = mw_lookup(no);
	return m ? m->wait_frame : 0;
}

bool PE_IsOverMessageWindowFlatShowWaitFrame(int no)
{
	struct parts_message_window *m = mw_lookup(no);
	if (!m || m->background.type != PARTS_FLAT ||
			!m->background.flat.flat)
		return true;
	struct parts_flat *f = &m->background.flat;
	int current = f->root_state
		? f->root_state->current_frame + f->pending_seek_delta : 0;
	return current >= m->wait_frame;
}

bool PE_BackMessageWindowFlatBeginFrame(int no)
{
	return mw_seek(no, 0);
}

bool PE_StepMessageWindowFlatFinalFrame(int no)
{
	struct parts_message_window *m = mw_lookup(no);
	if (!m || m->background.type != PARTS_FLAT ||
			!m->background.flat.flat)
		return false;
	return mw_seek(no, m->background.flat.end_frame);
}

void PE_SetKeyWaitCGName(int no, struct string *name, int start, int count, int ms)
{
	mw_choose_image(no, true, false, name, start, count, ms);
}

void PE_GetKeyWaitCGName(int no, struct string **name, int *start, int *count, int *ms)
{
	struct parts_message_window *m = mw_lookup(no);
	if (name) {
		struct string *copy = mw_api_string(m ? m->mark_cg_name : NULL);
		if (*name)
			free_string(*name);
		*name = copy;
	}
	if (start)
		*start = m ? m->loop_start : 0;
	if (count)
		*count = m ? m->loop_count : 0;
	if (ms)
		*ms = m ? m->loop_ms : 0;
}

void PE_SetKeyWaitFlatName(int no, struct string *name)
{
	mw_choose_image(no, true, true, name, 0, 0, 0);
}

struct string *PE_GetKeyWaitFlatName(int no)
{
	struct parts_message_window *m = mw_lookup(no);
	return mw_api_string(m ? m->mark_flat_name : NULL);
}

void PE_SetKeyWaitPos(int no, int x, int y, int z)
{
	if (x < -1000000 || x > 1000000 || y < -1000000 || y > 1000000 ||
			z < -1000000 || z > 1000000)
		ERROR("PE_SetKeyWaitPos: position out of range");
	struct parts *owner = mw_owner(no);
	struct parts_message_window *m = parts_message_window_ensure(owner);
	m->mark_pos = (Point){ x, y };
	m->mark_z = z;
	parts_dirty(owner);
}

int PE_GetKeyWaitPosX(int no)
{
	struct parts_message_window *m = mw_lookup(no);
	return m ? m->mark_pos.x : 0;
}

int PE_GetKeyWaitPosY(int no)
{
	struct parts_message_window *m = mw_lookup(no);
	return m ? m->mark_pos.y : 0;
}

int PE_GetKeyWaitPosZ(int no)
{
	struct parts_message_window *m = mw_lookup(no);
	return m ? m->mark_z : 0;
}

void PE_SetKeyWaitShow(int no, bool show)
{
	struct parts *owner = mw_owner(no);
	struct parts_message_window *m = parts_message_window_ensure(owner);
	m->mark_show = show;
	parts_dirty(owner);
}

bool PE_IsKeyWaitShow(int no)
{
	struct parts_message_window *m = mw_lookup(no);
	return m ? m->mark_show : false;
}

void PE_SetMessageWindowText(int no, struct string *text, int message,
		struct string *adv, int version, int step)
{
	mw_set_text(no, text, message, adv, version, step);
}

struct string *PE_GetMessageWindowText(int no)
{
	struct parts_message_window *m = mw_lookup(no);
	return mw_api_string(m ? m->text.source : NULL);
}

void PE_FixMessageWindowText(int no)
{
	mw_fix(no);
}

bool PE_IsFixedMessageWindowText(int no)
{
	struct parts_message_window *m = mw_lookup(no);
	return !m || m->text.visible == m->text.count;
}

void PE_SetMessageWindowTextArea(int no, int x, int y, int w, int h)
{
	if (x < -4096 || x > 4096 || y < -4096 || y > 4096 ||
			w < 0 || w > 4096 || h < 0 || h > 4096)
		ERROR("PE_SetMessageWindowTextArea: area out of range");
	struct parts *owner = mw_owner(no);
	struct parts_message_window *candidate = parts_message_window_ensure(owner);
	struct parts_message_window next = *candidate;
	next.area = (Rectangle){ x, y, w, h };
	mw_apply_settings(owner, &next);
}

void PE_GetMessageWindowTextArea(int no, int *x, int *y, int *w, int *h)
{
	struct parts_message_window *m = mw_lookup(no);
	Rectangle area = m ? m->area : (Rectangle){ 0, 0, 0, 0 };
	if (x) *x = area.x;
	if (y) *y = area.y;
	if (w) *w = area.w;
	if (h) *h = area.h;
}

void PE_SetMessageWindowTextOriginPosMode(int no, int origin)
{
	if (origin < 1 || origin > 9)
		ERROR("PE_SetMessageWindowTextOriginPosMode: origin out of range");
	struct parts *owner = mw_owner(no);
	struct parts_message_window *candidate = parts_message_window_ensure(owner);
	struct parts_message_window next = *candidate;
	next.text_origin = origin;
	mw_apply_settings(owner, &next);
}

int PE_GetMessageWindowTextOriginPosMode(int no)
{
	struct parts_message_window *m = mw_lookup(no);
	return m ? m->text_origin : 1;
}

void PE_SetMessageWindowTextFont(int no, int face, int size, int r, int g, int b,
		float bold, int er, int eg, int eb, float edge)
{
	if (face < 0 || face > 65535 || size < 1 || size > 512 ||
			r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255 ||
			er < 0 || er > 255 || eg < 0 || eg > 255 || eb < 0 || eb > 255 ||
			!isfinite(bold) || bold < 0.0f || bold > 16.0f ||
			!isfinite(edge) || edge < 0.0f || edge > 16.0f)
		ERROR("PE_SetMessageWindowTextFont: font parameter out of range");
	struct parts *owner = mw_owner(no);
	struct parts_message_window *candidate = parts_message_window_ensure(owner);
	struct parts_message_window next = *candidate;
	next.font.face = face;
	next.font.size = (float)size;
	next.font.color = (SDL_Color){ (uint8_t)r, (uint8_t)g, (uint8_t)b, 255 };
	next.font.bold_width = bold;
	next.font.weight = FW_NORMAL;
	next.font.edge_left = edge;
	next.font.edge_up = edge;
	next.font.edge_right = edge;
	next.font.edge_down = edge;
	next.font.edge_color = (SDL_Color){ (uint8_t)er, (uint8_t)eg,
		(uint8_t)eb, 255 };
	next.font.font_size = NULL;
	next.font.scale_x = 1.0f;
	next.font.space_scale_x = 1.0f;
	mw_apply_settings(owner, &next);
}

void PE_GetMessageWindowTextFont(int no, int *face, int *size, int *r, int *g,
		int *b, float *bold, int *er, int *eg, int *eb, float *edge)
{
	struct parts_message_window *m = mw_lookup(no);
	int fface = 0, fsize = 16, fr = 255, fg = 255, fb = 255;
	int fer = 0, feg = 0, feb = 0;
	float fbold = 0.0f, fedge = 0.0f;
	if (m) {
		fface = m->font.face;
		fsize = (int)m->font.size;
		fr = m->font.color.r;
		fg = m->font.color.g;
		fb = m->font.color.b;
		fbold = m->font.bold_width;
		fer = m->font.edge_color.r;
		feg = m->font.edge_color.g;
		feb = m->font.edge_color.b;
		fedge = m->font.edge_left;
	}
	if (face) *face = fface;
	if (size) *size = fsize;
	if (r) *r = fr;
	if (g) *g = fg;
	if (b) *b = fb;
	if (bold) *bold = fbold;
	if (er) *er = fer;
	if (eg) *eg = feg;
	if (eb) *eb = feb;
	if (edge) *edge = fedge;
}

void PE_SetMessageWindowTextSpeed(int no, int speed)
{
	if (speed < 0 || speed > 60000)
		ERROR("PE_SetMessageWindowTextSpeed: speed out of range");
	struct parts *owner = mw_owner(no);
	struct parts_message_window *candidate = parts_message_window_ensure(owner);
	struct parts_message_window next = *candidate;
	next.speed = speed;
	mw_apply_settings(owner, &next);
}

int PE_GetMessageWindowTextSpeed(int no)
{
	struct parts_message_window *m = mw_lookup(no);
	return m ? m->speed : 23;
}

void PE_SetMessageWindowTextSpace(int no, int tracking, int leading)
{
	if (tracking < -512 || tracking > 512 || leading < -512 || leading > 512)
		ERROR("PE_SetMessageWindowTextSpace: space out of range");
	struct parts *owner = mw_owner(no);
	struct parts_message_window *candidate = parts_message_window_ensure(owner);
	struct parts_message_window next = *candidate;
	next.tracking = tracking;
	next.leading = leading;
	mw_apply_settings(owner, &next);
}

void PE_GetMessageWindowTextSpace(int no, int *tracking, int *leading)
{
	struct parts_message_window *m = mw_lookup(no);
	if (tracking) *tracking = m ? m->tracking : 0;
	if (leading) *leading = m ? m->leading : 0;
}

#endif /* PARTS_MESSAGE_WINDOW_API_INC */
