#ifndef MESSAGE_WINDOW_SAVE_INC
#define MESSAGE_WINDOW_SAVE_INC

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "../hll/iarray.h"

#define MW_SAVE_MAGIC 0x31574D52
#define MW_SAVE_MAX_STRING (1024u * 1024u)

static void mw_save_write_string(struct iarray_writer *w, struct string *s)
{
	iarray_write_string(w, s ? s : &EMPTY_STRING);
}

/*
 * Deliberately does not use iarray_read_string: it has no bound and no NUL
 * requirement here.  The temporary is freed on every path.
 */
static bool mw_save_read_string(struct iarray_reader *r, struct string **out)
{
	if (!r || !out || r->error)
		return false;
	uint8_t *bytes = malloc(MW_SAVE_MAX_STRING);
	if (!bytes)
		return false;
	size_t length = 0;
	for (;;) {
		if (iarray_end(r)) {
			free(bytes);
			return false;
		}
		int c = iarray_read(r);
		if (r->error) {
			free(bytes);
			return false;
		}
		if (c == 0)
			break;
		/* iarray_write_string packs a two-byte character into one int;
		 * the safety limit applies to decoded bytes rather than int cells. */
		int width = SJIS_2BYTE(c) ? 2 : 1;
		if (c < -128 || c > 65535 ||
				(width == 1 && c > 255) || (width == 2 && c < 256) ||
				(size_t)width > MW_SAVE_MAX_STRING - length) {
			free(bytes);
			return false;
		}
		bytes[length++] = (uint8_t)c;
		if (width == 2) bytes[length++] = (unsigned)c >> 8;
	}
	struct string *string = make_string((const char *)bytes, length);
	free(bytes);
	if (!string)
		return false;
	*out = string;
	return true;
}

static bool mw_save_read_i32(struct iarray_reader *r, int *out)
{
	if (!r || !out || r->error || iarray_end(r))
		return false;
	int value = iarray_read(r);
	if (r->error)
		return false;
	*out = value;
	return true;
}

static bool mw_save_read_range(struct iarray_reader *r, int minimum,
		int maximum, int *out)
{
	int value;
	if (!mw_save_read_i32(r, &value))
		return false;
	if (value < minimum || value > maximum)
		return false;
	*out = value;
	return true;
}

static bool mw_save_valid_font(const struct text_style *font)
{
	if (font->face > 65535)
		return false;
	if (!isfinite(font->size) || font->size < 1.0f || font->size > 512.0f)
		return false;
	int size = (int)font->size;
	if ((float)size != font->size)
		return false;
	if (!isfinite(font->bold_width) ||
			font->bold_width < 0.0f || font->bold_width > 16.0f)
		return false;
	float edge = font->edge_left;
	if (!isfinite(edge) || edge < 0.0f || edge > 16.0f)
		return false;
	if (font->edge_up != edge || font->edge_right != edge ||
			font->edge_down != edge)
		return false;
	return true;
}

static bool mw_save_valid_area(const Rectangle *area)
{
	return area->x >= -4096 && area->x <= 4096 &&
		area->y >= -4096 && area->y <= 4096 &&
		area->w >= 0 && area->w <= 4096 &&
		area->h >= 0 && area->h <= 4096;
}

static void mw_save_settings(struct iarray_writer *w, struct parts_message_window *m)
{
	if (!w || !m)
		return;

	iarray_write(w, MW_SAVE_MAGIC);
	mw_save_write_string(w, m->cg_name);
	mw_save_write_string(w, m->flat_name);
	mw_save_write_string(w, m->mark_cg_name);
	mw_save_write_string(w, m->mark_flat_name);
	mw_save_write_string(w, m->adv_name);
	mw_save_write_string(w, m->text.source);

	iarray_write_text_style(w, &m->font);
	iarray_write_rectangle(w, &m->area);
	iarray_write_point(w, &m->mark_pos);
	iarray_write(w, m->mark_z);
	iarray_write(w, m->loop_start);
	iarray_write(w, m->loop_count);
	iarray_write(w, m->loop_ms);
	iarray_write(w, m->text_origin);
	iarray_write(w, m->speed);
	iarray_write(w, m->tracking);
	iarray_write(w, m->leading);
	iarray_write(w, m->wait_frame);
	iarray_write(w, m->message_no);
	iarray_write(w, m->adv_version);
	iarray_write(w, m->adv_step);
	iarray_write(w, m->active ? 1 : 0);
	iarray_write(w, m->mark_show ? 1 : 0);
	iarray_write(w, m->inactive.r);
	iarray_write(w, m->inactive.g);
	iarray_write(w, m->inactive.b);
	iarray_write(w, (int)m->text.visible);

	uint64_t elapsed;
	memcpy(&elapsed, &m->text.elapsed_ms, sizeof(elapsed));
	iarray_write(w, (int)(uint32_t)elapsed);
	iarray_write(w, (int)(uint32_t)(elapsed >> 32));
}

static bool mw_read_settings(struct iarray_reader *r, struct parts_message_window *m,
		size_t *visible, double *elapsed)
{
	if (!r || !m || !visible || !elapsed || r->error)
		return false;

	int magic;
	if (!mw_save_read_i32(r, &magic) || magic != MW_SAVE_MAGIC)
		return false;

	if (!mw_save_read_string(r, &m->cg_name))
		return false;
	if (!mw_save_read_string(r, &m->flat_name))
		return false;
	if (!mw_save_read_string(r, &m->mark_cg_name))
		return false;
	if (!mw_save_read_string(r, &m->mark_flat_name))
		return false;
	if (!mw_save_read_string(r, &m->adv_name))
		return false;
	if (!mw_save_read_string(r, &m->text.source))
		return false;

	/* Validate raw color words before the legacy style reader narrows them. */
	if(r->size-r->pos<19)return false;
	struct iarray_reader font_check=*r;
	for(unsigned i=0;i<19;i++) {
		int word=iarray_read(&font_check);
		if(i>=8 && i<16 && (word<0 || word>255))return false;
	}
	iarray_read_text_style(r, &m->font);
	if (r->error)
		return false;
	m->font.font_size = NULL;
	if (!mw_save_valid_font(&m->font))
		return false;
	m->font.weight = FW_NORMAL;
	m->font.scale_x = 1.0f;
	m->font.space_scale_x = 1.0f;
	m->font.font_spacing=0;

	iarray_read_rectangle(r, &m->area);
	if (r->error || !mw_save_valid_area(&m->area))
		return false;

	iarray_read_point(r, &m->mark_pos);
	if (r->error)
		return false;
	if (m->mark_pos.x < -1000000 || m->mark_pos.x > 1000000 ||
			m->mark_pos.y < -1000000 || m->mark_pos.y > 1000000)
		return false;
	if (!mw_save_read_range(r, -1000000, 1000000, &m->mark_z))
		return false;

	if (!mw_save_read_range(r, 0, 1000000, &m->loop_start))
		return false;
	if (!mw_save_read_range(r, 0, 4096, &m->loop_count))
		return false;
	if (!mw_save_read_range(r, 0, 1000000, &m->loop_ms))
		return false;
	if (m->loop_count > 0 && m->loop_ms < 1)
		return false;

	if (!mw_save_read_range(r, 1, 9, &m->text_origin))
		return false;
	if (!mw_save_read_range(r, 0, 60000, &m->speed))
		return false;
	if (!mw_save_read_range(r, -512, 512, &m->tracking))
		return false;
	if (!mw_save_read_range(r, -512, 512, &m->leading))
		return false;
	if (!mw_save_read_range(r, 0, 1000000, &m->wait_frame))
		return false;
	if (!mw_save_read_i32(r, &m->message_no))
		return false;
	if (!mw_save_read_i32(r, &m->adv_version))
		return false;
	if (!mw_save_read_i32(r, &m->adv_step))
		return false;

	int active, mark_show;
	if (!mw_save_read_range(r, 0, 1, &active))
		return false;
	if (!mw_save_read_range(r, 0, 1, &mark_show))
		return false;
	m->active = active != 0;
	m->mark_show = mark_show != 0;

	int red, green, blue;
	if (!mw_save_read_range(r, 0, 255, &red))
		return false;
	if (!mw_save_read_range(r, 0, 255, &green))
		return false;
	if (!mw_save_read_range(r, 0, 255, &blue))
		return false;
	m->inactive = (SDL_Color){
		(uint8_t)red, (uint8_t)green, (uint8_t)blue, 255
	};

	int visible_value;
	if (!mw_save_read_range(r, 0, 2048, &visible_value))
		return false;

	int low, high;
	if (!mw_save_read_i32(r, &low) || !mw_save_read_i32(r, &high))
		return false;
	uint64_t bits = (uint32_t)low | ((uint64_t)(uint32_t)high << 32);
	double elapsed_value;
	memcpy(&elapsed_value, &bits, sizeof(elapsed_value));
	if (!isfinite(elapsed_value) || elapsed_value < 0.0)
		return false;
	if (r->error)
		return false;

	*visible = (size_t)visible_value;
	*elapsed = elapsed_value;
	return true;
}

#endif /* MESSAGE_WINDOW_SAVE_INC */
