/* Copyright (C) 2023 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include <math.h>
#include <limits.h>
#include "system4.h"
#include "system4/string.h"
#include "system4/cg.h"
#include "system4/archive.h"

#include "vm/page.h"
#include "vm/heap.h"
#include "asset_manager.h"
#include "parts.h"
#include "parts_activity.h"
#include "reign.h"
#include "parts_internal.h"
#include "message_window_internal.h"
#include "textbox_internal.h"
#include "../hll/iarray.h"

#include <zlib.h>
#include "parts_texture_pixels.h"
#include "rance10_diagnostics.h"

#define CURRENT_SAVE_VERSION 29

static void save_parts_double(struct iarray_writer *w,double value)
{
	uint64_t bits;memcpy(&bits,&value,sizeof(bits));
	iarray_write(w,(uint32_t)bits);iarray_write(w,(uint32_t)(bits>>32));
}
static double load_parts_double(struct iarray_reader *r)
{
	uint64_t bits=(uint32_t)iarray_read(r);bits|=(uint64_t)(uint32_t)iarray_read(r)<<32;
	double value;memcpy(&value,&bits,sizeof(value));return value;
}

static void save_parts_params(struct iarray_writer *w, struct parts_params *params)
{
	iarray_write(w, params->z);
	iarray_write_point(w, &params->pos);
	iarray_write(w, params->show);
	iarray_write(w, params->alpha);
	iarray_write_float(w, params->scale.x);
	iarray_write_float(w, params->scale.y);
	iarray_write_float(w, params->rotation.x);
	iarray_write_float(w, params->rotation.y);
	iarray_write_float(w, params->rotation.z);
	iarray_write_color(w, &params->add_color);
	iarray_write_color(w, &params->multiply_color);
	iarray_write(w, params->alpha_clipper_parts_no);
}

static void load_parts_params(struct iarray_reader *r, struct parts_params *params, int version)
{
	params->z = iarray_read(r);
	iarray_read_point(r, &params->pos);
	params->show = iarray_read(r);
	params->alpha = iarray_read(r);
	params->scale.x = iarray_read_float(r);
	params->scale.y = iarray_read_float(r);
	params->rotation.x = iarray_read_float(r);
	params->rotation.y = iarray_read_float(r);
	params->rotation.z = iarray_read_float(r);
	iarray_read_color(r, &params->add_color);
	iarray_read_color(r, &params->multiply_color);
	if (version >= 7)
		params->alpha_clipper_parts_no = iarray_read(r);
}

static void save_parts_cg(struct iarray_writer *w, struct parts_cg *cg)
{
	iarray_write(w, cg->no);
	iarray_write_string_or_null(w, cg->name);
}

static void load_parts_cg(struct iarray_reader *r, struct parts *parts, struct parts_cg *cg)
{
	int no = iarray_read(r);
	struct string *name = iarray_read_string_or_null(r);
	if (name) {
		parts_cg_set(parts, cg, name);
		free_string(name);
	} else if (no) {
		parts_cg_set_by_index(parts, cg, no);
	}
}

static void save_parts_text(struct iarray_writer *w, struct parts_text *text)
{
	iarray_write(w, text->line_space);
	iarray_write_text_style(w, &text->ts);
	iarray_write(w, text->nr_lines);
	for (unsigned i = 0; i < text->nr_lines; i++) {
		struct string *s = parts_text_line_get(&text->lines[i]);
		iarray_write_string(w, s);
		free_string(s);
	}
}

static void load_parts_text(struct iarray_reader *r, struct parts *parts,
		struct parts_text *text)
{
	// FIXME: this won't accurately restore the text state if the text style
	//        varies per-character
	text->line_space = iarray_read(r);
	iarray_read_text_style(r, &text->ts);

	text->nr_lines = 0;
	text->lines = NULL;
	text->cursor.x = 0;
	text->cursor.y = 0;

	int nr_lines = iarray_read(r);
	for (int i = 0; i < nr_lines; i++) {
		struct string *line = iarray_read_string(r);
		if (i > 0) {
			struct string *nl = make_string("\n", 1);
			parts_text_append(parts, text, nl);
			free_string(nl);
		}
		parts_text_append(parts, text, line);
		free_string(line);
	}
}

static void save_parts_animation(struct iarray_writer *w, struct parts_animation *anim)
{
	iarray_write_string_or_null(w, anim->cg_name);
	iarray_write(w, anim->start_no);
	iarray_write(w, anim->nr_frames);
	iarray_write(w, anim->frame_time);
	iarray_write(w, anim->elapsed);
	iarray_write(w, anim->current_frame);
}

static void load_parts_animation(struct iarray_reader *r, struct parts *parts,
		struct parts_animation *anim)
{
	struct string *cg_name = iarray_read_string_or_null(r);
	int start_no = iarray_read(r);
	int nr_frames = iarray_read(r);
	int frame_time = iarray_read(r);
	bool loaded;

	if (cg_name) {
		loaded = parts_animation_set_cg(parts, anim, cg_name, start_no, nr_frames, frame_time);
		free_string(cg_name);
	} else {
		loaded = parts_animation_set_cg_by_index(parts, anim, start_no, nr_frames, frame_time);
	}

	anim->elapsed = iarray_read(r);
	anim->current_frame = iarray_read(r);
	if (!loaded || anim->elapsed >= anim->frame_time || anim->current_frame >= anim->nr_frames) {
		r->error = 1;
		return;
	}
	anim->common.texture = anim->frames[anim->current_frame];
}

static void save_parts_numeral(struct iarray_writer *w, struct parts_numeral *num)
{
	iarray_write(w, num->have_num);
	iarray_write(w, num->num);
	iarray_write(w, num->space);
	iarray_write(w, num->show_comma);
	iarray_write(w, num->length);
	iarray_write(w, num->font_no);
}

static void load_parts_numeral(struct iarray_reader *r, struct parts *parts,
		struct parts_numeral *num)
{
	num->have_num = iarray_read(r);
	num->num = iarray_read(r);
	num->space = iarray_read(r);
	num->show_comma = iarray_read(r);
	num->length = iarray_read(r);
	num->font_no = iarray_read(r);
	if (num->have_num && (num->font_no < 0 || num->font_no >= parts_nr_numeral_fonts)) {
		r->error = 1;
		return;
	}

	if (num->have_num && !parts_numeral_set_number(parts, num, num->num))
		r->error = 1;
}

static void save_parts_gauge(struct iarray_writer *w, struct parts_gauge *gauge,
		bool vert)
{
	iarray_write(w, gauge->cg_no);
	iarray_write_float(w, gauge->rate);
	iarray_write(w, vert ? 0 : (gauge->reverse ? 1 : 0));
	if (!vert) {
		iarray_write_float(w,gauge->numerator);
		iarray_write_float(w,gauge->denominator);
	}
	iarray_write_string_or_null(w, gauge->cg_name);
}

static void load_parts_gauge(struct iarray_reader *r, struct parts *parts,
		struct parts_gauge *gauge, bool vert, int version)
{
	gauge->cg_no = iarray_read(r);
	gauge->rate = iarray_read_float(r);
	gauge->numerator = gauge->rate;
	gauge->denominator = 1.0f;
	gauge->reverse = false;
	if (version >= 13) {
		int reverse = iarray_read(r);
		if (reverse != 0 && reverse != 1)
			r->error = 1;
		else
			gauge->reverse = reverse != 0;
		if (vert && reverse) r->error=1;
		if (!vert) {
			gauge->numerator=iarray_read_float(r);
			gauge->denominator=iarray_read_float(r);
			if (!isfinite(gauge->numerator) || !isfinite(gauge->denominator) ||
				gauge->denominator<=0 || gauge->numerator<0 || gauge->numerator>gauge->denominator ||
				fabsf(gauge->rate-gauge->numerator/gauge->denominator)>0.000001f)
				r->error=1;
		}
	}
	struct string *cg_name = version >= 21 ? iarray_read_string_or_null(r) : NULL;
	if (vert)
		gauge->reverse = false;
	if (!isfinite(gauge->rate) || gauge->rate < 0.0f || gauge->rate > 1.0f)
		r->error = 1;
	if (gauge->cg_no < -1) r->error=1;
	if (cg_name && (cg_name->size == 0 || strlen(cg_name->text) != cg_name->size)) r->error = 1;
	if (r->error) {
		if (cg_name) free_string(cg_name);
		return;
	}

	if (cg_name || gauge->cg_no >= 0) {
		bool ok = cg_name ? parts_gauge_set_cg(parts, gauge, cg_name)
				: parts_gauge_set_cg_by_index(parts, gauge, gauge->cg_no);
		if (cg_name) free_string(cg_name);
		if (!ok) {
			r->error = 1;
			return;
		}
		if (vert) {
			parts_vgauge_set_rate(parts, gauge, gauge->rate);
		} else if (!parts_hgauge_set_rate(parts, gauge, gauge->rate)) {
			r->error = 1;
		}
	}
}

static void save_parts_cp_op(struct iarray_writer *w, struct parts_cp_op *op)
{
	iarray_write(w, op->type);
	switch (op->type) {
	case PARTS_CP_CREATE:
	case PARTS_CP_CREATE_PIXEL_ONLY:
		iarray_write(w, op->create.w);
		iarray_write(w, op->create.h);
		break;
	case PARTS_CP_CG:
		iarray_write_string(w, op->cg.name);
		break;
	case PARTS_CP_FILL:
	case PARTS_CP_FILL_ALPHA_COLOR:
	case PARTS_CP_FILL_AMAP:
	case PARTS_CP_FILL_WITH_ALPHA:
	case PARTS_CP_DRAW_RECT:
		iarray_write(w, op->fill.x);
		iarray_write(w, op->fill.y);
		iarray_write(w, op->fill.w);
		iarray_write(w, op->fill.h);
		iarray_write(w, op->fill.r);
		iarray_write(w, op->fill.g);
		iarray_write(w, op->fill.b);
		iarray_write(w, op->fill.a);
		break;
	case PARTS_CP_MULTIPLY_AMAP_GRADATION_UD:
		iarray_write(w, op->amap_grad.x);
		iarray_write(w, op->amap_grad.y);
		iarray_write(w, op->amap_grad.w);
		iarray_write(w, op->amap_grad.h);
		iarray_write(w, op->amap_grad.up_a);
		iarray_write(w, op->amap_grad.down_a);
		break;
	case PARTS_CP_DRAW_CUT_CG:
	case PARTS_CP_COPY_CUT_CG:
	case PARTS_CP_ALPHA_BLEND_CUT_CG:
	case PARTS_CP_ONLY_ALPHA_CUT_CG:
		iarray_write_string(w, op->cut_cg.cg_name);
		iarray_write(w, op->cut_cg.dx);
		iarray_write(w, op->cut_cg.dy);
		iarray_write(w, op->cut_cg.dw);
		iarray_write(w, op->cut_cg.dh);
		iarray_write(w, op->cut_cg.sx);
		iarray_write(w, op->cut_cg.sy);
		iarray_write(w, op->cut_cg.sw);
		iarray_write(w, op->cut_cg.sh);
		iarray_write(w, op->cut_cg.interp_type);
		break;
	case PARTS_CP_DRAW_TEXT:
	case PARTS_CP_COPY_TEXT:
	case PARTS_CP_ALPHA_BLEND_TEXT:
		iarray_write_string(w, op->text.text);
		iarray_write(w, op->text.x);
		iarray_write(w, op->text.y);
		iarray_write(w, op->text.line_space);
		iarray_write_text_style(w, &op->text.style);
		break;
	case PARTS_CP_GRAY_FILTER:
		iarray_write(w, op->filter.x);
		iarray_write(w, op->filter.y);
		iarray_write(w, op->filter.w);
		iarray_write(w, op->filter.h);
		iarray_write(w, op->filter.full_size);
		break;
	}
}

static struct string *load_cp_cg_name(struct iarray_reader *r, int version)
{
	if (version >= 5)
		return iarray_read_string(r);
	// In version 4 and earlier, the CG name was stored as an integer ID.
	// Convert it to a string name by looking up the asset.
	int cg_no = iarray_read(r);
	if (cg_no <= 0)
		return string_ref(&EMPTY_STRING);
	struct archive_data *data = asset_get(ASSET_CG, cg_no);
	if (!data)
		return string_ref(&EMPTY_STRING);
	struct string *name = cstr_to_string(data->name);
	archive_free_data(data);
	return name;
}

static struct parts_cp_op *load_parts_cp_op(struct iarray_reader *r, int version)
{
	struct parts_cp_op *op = xcalloc(1, sizeof(struct parts_cp_op));
	op->type = iarray_read(r);
	if (op->type < 0 || op->type >= PARTS_NR_CP_TYPES ||
			(op->type == PARTS_CP_MULTIPLY_AMAP_GRADATION_UD && version <= 20) ||
			(op->type == PARTS_CP_ALPHA_BLEND_TEXT && version <= 21)) {
		r->error = 1; free(op); return NULL;
	}
	switch (op->type) {
	case PARTS_CP_CREATE:
	case PARTS_CP_CREATE_PIXEL_ONLY:
		op->create.w = iarray_read(r);
		op->create.h = iarray_read(r);
		break;
	case PARTS_CP_CG:
		op->cg.name = load_cp_cg_name(r, version);
		break;
	case PARTS_CP_FILL:
	case PARTS_CP_FILL_ALPHA_COLOR:
	case PARTS_CP_FILL_AMAP:
	case PARTS_CP_FILL_WITH_ALPHA:
	case PARTS_CP_DRAW_RECT:
		op->fill.x = iarray_read(r);
		op->fill.y = iarray_read(r);
		op->fill.w = iarray_read(r);
		op->fill.h = iarray_read(r);
		op->fill.r = iarray_read(r);
		op->fill.g = iarray_read(r);
		op->fill.b = iarray_read(r);
		op->fill.a = iarray_read(r);
		break;
	case PARTS_CP_DRAW_CUT_CG:
	case PARTS_CP_COPY_CUT_CG:
	case PARTS_CP_ALPHA_BLEND_CUT_CG:
	case PARTS_CP_ONLY_ALPHA_CUT_CG:
		op->cut_cg.cg_name = load_cp_cg_name(r, version);
		op->cut_cg.dx = iarray_read(r);
		op->cut_cg.dy = iarray_read(r);
		op->cut_cg.dw = iarray_read(r);
		op->cut_cg.dh = iarray_read(r);
		op->cut_cg.sx = iarray_read(r);
		op->cut_cg.sy = iarray_read(r);
		op->cut_cg.sw = iarray_read(r);
		op->cut_cg.sh = iarray_read(r);
		op->cut_cg.interp_type = iarray_read(r);
		break;
	case PARTS_CP_MULTIPLY_AMAP_GRADATION_UD:
		op->amap_grad.x = iarray_read(r);
		op->amap_grad.y = iarray_read(r);
		op->amap_grad.w = iarray_read(r);
		op->amap_grad.h = iarray_read(r);
		op->amap_grad.up_a = iarray_read(r);
		op->amap_grad.down_a = iarray_read(r);
		if (!parts_cp_amap_gradation_valid(op->amap_grad.x, op->amap_grad.y,
				op->amap_grad.w, op->amap_grad.h, op->amap_grad.up_a, op->amap_grad.down_a))
			r->error = 1;
		break;
	case PARTS_CP_DRAW_TEXT:
	case PARTS_CP_COPY_TEXT:
	case PARTS_CP_ALPHA_BLEND_TEXT:
		op->text.text = iarray_read_string(r);
		op->text.x = iarray_read(r);
		op->text.y = iarray_read(r);
		op->text.line_space = iarray_read(r);
		iarray_read_text_style(r, &op->text.style);
		break;
	case PARTS_CP_GRAY_FILTER:
		op->filter.x = iarray_read(r);
		op->filter.y = iarray_read(r);
		op->filter.w = iarray_read(r);
		op->filter.h = iarray_read(r);
		op->filter.full_size = !!iarray_read(r);
		break;
	}
	if (r->error) {
		parts_cp_op_free(op);
		return NULL;
	}
	return op;
}


#define CP_PIXELS_MAGIC 0x43505031
#define CP_PIXELS_LIMIT (64u*1024u*1024u)
static bool save_construction_pixels(struct iarray_writer *w,Texture *texture,struct parts *owner,bool needed)
{
 if(!needed){iarray_write(w,CP_PIXELS_MAGIC);iarray_write(w,0);return true;}
 struct parts_texture_pixels image;
 int got=parts_copy_texture_pixels(owner,texture,&image);
 if(got<0)return false;
 iarray_write(w,CP_PIXELS_MAGIC);iarray_write(w,got?1:2);
 if(!got)return true;
 size_t bytes=(size_t)image.w*image.h*4;
 uLongf size=compressBound(bytes);uint8_t *compressed=malloc(size);
 if(!compressed){free(image.rgba);return false;}
 int status=compress2(compressed,&size,image.rgba,bytes,Z_BEST_SPEED);
 if(status!=Z_OK||size>CP_PIXELS_LIMIT){free(compressed);free(image.rgba);return false;}
 iarray_write(w,image.w);iarray_write(w,image.h);
 iarray_write(w,image.min_filter);iarray_write(w,image.mag_filter);
 iarray_write(w,image.wrap_s);iarray_write(w,image.wrap_t);
 iarray_write(w,(int)bytes);iarray_write(w,(int)size);
 iarray_write(w,(int)(uint32_t)crc32(0,image.rgba,bytes));
 for(size_t i=0;i<size;i+=4) {
  uint32_t word=0;for(unsigned j=0;j<4&&i+j<size;j++)word|=(uint32_t)compressed[i+j]<<(8*j);
  iarray_write(w,(int32_t)word);
 }
 free(compressed);free(image.rgba);return true;
}
static bool cp_filter(int v,bool min)
{
 return v==GL_NEAREST||v==GL_LINEAR||(min&&v>=GL_NEAREST_MIPMAP_NEAREST&&v<=GL_LINEAR_MIPMAP_LINEAR);
}
static bool cp_wrap(int v){return v==GL_REPEAT||v==GL_CLAMP_TO_EDGE||v==GL_MIRRORED_REPEAT;}
static int load_construction_pixels(struct iarray_reader *r,Texture *texture)
{
 if(iarray_read(r)!=CP_PIXELS_MAGIC)return -1;
 int present=iarray_read(r);if(r->error||present<0||present>2)return -1;
 if(!present||present==2)return present;
 int w=iarray_read(r),h=iarray_read(r),min=iarray_read(r),mag=iarray_read(r);
 int sw=iarray_read(r),tw=iarray_read(r),bytes=iarray_read(r),packed=iarray_read(r);
 uint32_t crc=(uint32_t)iarray_read(r);
 if(r->error||w<1||h<1||w>8192||h>8192||bytes<1||packed<1||
    (size_t)w*h*4>CP_PIXELS_LIMIT||(size_t)bytes!=(size_t)w*h*4||
    (unsigned)packed>CP_PIXELS_LIMIT||!cp_filter(min,true)||!cp_filter(mag,false)||
    !cp_wrap(sw)||!cp_wrap(tw)||((size_t)packed+3)/4>r->size-r->pos)return -1;
 uint8_t *data=malloc(packed),*raw=malloc(bytes);
 if(!data||!raw){free(data);free(raw);return -1;}
 for(int i=0;i<packed;i+=4) {
  uint32_t word=(uint32_t)iarray_read(r);
  for(int j=0;j<4&&i+j<packed;j++)data[i+j]=(uint8_t)(word>>(8*j));
 }
 z_stream stream={0};
 stream.next_in=data;stream.avail_in=(uInt)packed;
 stream.next_out=raw;stream.avail_out=(uInt)bytes;
 bool ok=false;
 if(!r->error&&inflateInit(&stream)==Z_OK) {
  int status=inflate(&stream,Z_FINISH);
  ok=status==Z_STREAM_END&&stream.total_in==(uLong)packed&&
   stream.total_out==(uLong)bytes&&(uint32_t)crc32(0,raw,bytes)==crc;
  inflateEnd(&stream);
 }
 free(data);
 if(ok) {
  gfx_delete_texture(texture);gfx_init_texture_with_pixels(texture,w,h,raw);
  if(texture->handle) {
   GLint binding;glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);glBindTexture(GL_TEXTURE_2D,texture->handle);
   glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,min);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,mag);
   glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,sw);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,tw);
   if(min>=GL_NEAREST_MIPMAP_NEAREST&&min<=GL_LINEAR_MIPMAP_LINEAR)glGenerateMipmap(GL_TEXTURE_2D);
   glBindTexture(GL_TEXTURE_2D,(GLuint)binding);
  }
  ok=texture->handle&&glGetError()==GL_NO_ERROR;
 }
 free(raw);return ok?1:-1;
}

static bool save_parts_construction_process(struct iarray_writer *w,
		struct parts_construction_process *cproc,struct parts *owner,int version)
{
	unsigned ops_count_pos = iarray_writer_pos(w);
	iarray_write(w, 0); // size of ops list

	unsigned ops_count = 0;
	struct parts_cp_op *op;
	TAILQ_FOREACH(op, &cproc->ops, entry) {
		save_parts_cp_op(w, op);
		ops_count++;
	}

	iarray_write_at(w, ops_count_pos, ops_count);
	return version<29 || save_construction_pixels(w, &cproc->common.texture,owner,cproc->needs_pixel_snapshot);
}

static void load_parts_construction_process(struct iarray_reader *r, struct parts *parts,
		struct parts_construction_process *cproc, int version)
{
	int ops_count = iarray_read(r);
	if (r->error || ops_count < 0 || (unsigned)ops_count > r->size - r->pos) {
		r->error = 1;
		return;
	}
	for (int i = 0; i < ops_count; i++) {
		struct parts_cp_op *op = load_parts_cp_op(r, version);
		if (r->error || !op) {
			if (op) parts_cp_op_free(op);
			parts_clear_construction_process(cproc);
			r->error = 1;
			return;
		}
		parts_add_cp_op(cproc, op);
	}
	int mode = version >= 29 ? load_construction_pixels(r, &cproc->common.texture) : 0;
	bool restored = mode >= 0;
	if (restored && mode == 0) restored = parts_build_construction_process(parts, cproc);
	cproc->needs_pixel_snapshot = mode > 0;
	if (r->error || !restored) {
		parts_clear_construction_process(cproc);
		r->error = 1;
	}
}

static void save_parts_flash(struct iarray_writer *w, struct parts_flash *flash)
{
	iarray_write_string_or_null(w, flash->name);
	iarray_write(w, flash->stopped);
	iarray_write(w, flash->current_frame);
}

static void load_parts_flash(struct iarray_reader *r, struct parts *parts,
		struct parts_flash *flash)
{
	struct string *name = iarray_read_string_or_null(r);
	parts_flash_load(parts, flash, name);
	free_string(name);
	flash->stopped = !!iarray_read(r);
	parts_flash_seek(flash, iarray_read(r));
}

static void save_parts_flat(struct iarray_writer *w, struct parts_flat *flat)
{
	iarray_write_string_or_null(w, flat->name);
	iarray_write_float(w, flat->fps_rate);
	int count = 0;
	for (size_t i = 0; flat->cg_names && i < flat->nr_libraries; i++) {
		if (flat->cg_names[i])
			count++;
	}
	iarray_write(w, count);
	for (size_t i = 0; flat->cg_names && i < flat->nr_libraries; i++) {
		if (!flat->cg_names[i])
			continue;
		iarray_write(w, i);
		iarray_write_string(w, flat->cg_names[i]);
	}
	parts_flat_save_state(flat, w);
}

static void load_parts_flat(struct iarray_reader *r, struct parts *parts,
		struct parts_flat *flat, int version)
{
	struct string *name = iarray_read_string_or_null(r);
	bool loaded = parts_flat_load(parts, flat, name);
	if (name && !loaded)
		r->error = 1;
	if (name)
		free_string(name);
	if (version >= 8) {
		float rate = iarray_read_float(r);
		if (isfinite(rate) && rate >= 0)
			flat->fps_rate = rate;
	}
	if (version >= 9) {
		int count = iarray_read(r);
		if (count < 0 || count > 100000) {
			r->error = 1;
			return;
		}
		for (int i = 0; i < count; i++) {
			int index = iarray_read(r);
			struct string *cg_name = iarray_read_string(r);
			if (!parts_flat_replace_cg(flat, index, cg_name)) {
				WARNING("Failed to restore flat CG replacement at index %d", index);
				r->error = 1;
			}
			free_string(cg_name);
		}
	}
	if (version >= 10)
		parts_flat_load_state(flat, r);
	else
		flat->needs_advance = true;
}

/* Version15 flag1 stores private scrollbar resources. Version25 flag2 adds
 * the eight role prefixes; an ordinary scrollbar retains the flag1 layout. */
static void save_scroll_widget(struct iarray_writer *w,struct parts *p)
{
    struct parts_widget *v=p->widget;
    bool overrides=false;
    if(v)for(int i=0;i<4;i++)
        if((v->scroll_flat_override[i] && v->scroll_flat_override[i]->size) ||
           (v->scroll_cg_override[i] && v->scroll_cg_override[i]->size))overrides=true;
    iarray_write(w,overrides?2:v && v->scroll_art?1:0);
    if(!v || (!v->scroll_art && !overrides))return;
    iarray_write_string(w,v->flat_name);iarray_write_string(w,v->cg_name);
    if(overrides)for(int i=0;i<4;i++) {
        iarray_write_string(w,v->scroll_flat_override[i]?v->scroll_flat_override[i]:&EMPTY_STRING);
        iarray_write_string(w,v->scroll_cg_override[i]?v->scroll_cg_override[i]:&EMPTY_STRING);
    }
    iarray_write(w,v->hover_region);iarray_write(w,v->pressed_region);iarray_write(w,v->drag_offset);
    for(int i=0;i<4;i++)iarray_write(w,v->scroll_active[i]);
    for(int i=7;i<20;i++) {
        iarray_write(w,v->scroll_flats[i]?2:v->scroll_cg_names[i]?1:0);
        if(v->scroll_flats[i])save_parts_flat(w,v->scroll_flats[i]);
        else if(v->scroll_cg_names[i])iarray_write_string(w,v->scroll_cg_names[i]);
    }
}
static void load_scroll_widget(struct iarray_reader *r,struct parts *p,int version)
{
    int flag=iarray_read(r);if(flag==0)return;
    struct parts_widget *v=p->widget;
    if((flag!=1 && !(version>=25 && flag==2)) || !v || (v->type!=2 && v->type!=3)){r->error=1;return;}
    free_string(v->flat_name);v->flat_name=iarray_read_string(r);
    free_string(v->cg_name);v->cg_name=iarray_read_string(r);
    for(int i=0;i<4;i++) {
        if(v->scroll_flat_override[i])free_string(v->scroll_flat_override[i]);
        if(v->scroll_cg_override[i])free_string(v->scroll_cg_override[i]);
        v->scroll_flat_override[i]=flag==2?iarray_read_string(r):string_ref(&EMPTY_STRING);
        v->scroll_cg_override[i]=flag==2?iarray_read_string(r):string_ref(&EMPTY_STRING);
        if(v->scroll_flat_override[i]->size && v->scroll_cg_override[i]->size)r->error=1;
    }
    if(r->error)return;
    v->hover_region=iarray_read(r);v->pressed_region=iarray_read(r);v->drag_offset=iarray_read(r);
    if(v->hover_region < -1 || v->hover_region>3 || v->pressed_region < -1 || v->pressed_region>3 ||
        v->drag_offset<0 || v->drag_offset>4096){r->error=1;return;}
    for(int i=0;i<4;i++) {
        v->scroll_active[i]=iarray_read(r);
        const int bases[]={7,11,15,19};int value=v->scroll_active[i];
        if(value!=-1 && (value<bases[i] || value>bases[i]+(i==3?0:3)))r->error=1;
    }
    if(r->error)return;
    for(int i=0;i<20;i++)gfx_delete_texture(&v->images[i]);
    for(int i=7;i<20 && !r->error;i++) {
        int kind=iarray_read(r);
        if(kind==2) {
            v->scroll_flats[i]=xcalloc(1,sizeof(struct parts_flat));v->scroll_flats[i]->fps_rate=1.f;
            load_parts_flat(r,p,v->scroll_flats[i],version);
        } else if(kind==1) {
            struct string *name=iarray_read_string(r);
            struct cg *cg=r->error?NULL:asset_cg_load_by_name(name->text,NULL);
            if(!cg){free_string(name);r->error=1;break;}
            gfx_init_texture_with_cg(&v->images[i],cg);cg_free(cg);v->scroll_cg_names[i]=name;
            if(!v->images[i].handle)r->error=1;
        } else if(kind!=0)r->error=1;
    }
    /* Flag2 can carry pending prefixes without a base or any drawable role. */
    v->scroll_art=v->flat_name->size || v->cg_name->size;
    for(int i=7;i<20;i++)if(v->scroll_flats[i] || v->images[i].handle)v->scroll_art=true;
    v->repeat_at=SDL_GetTicks()+350;v->dirty=true;
    if(!r->error && !parts_widget_build(p))r->error=1;
}

#include "textbox_save.inc"

static void save_parts_3dlayer(struct iarray_writer *w, struct parts_3dlayer *l)
{
	struct RE_plugin *plugin = l->plugin >= 0 ? RE_get_plugin(l->plugin) : NULL;
	iarray_write(w, !!plugin);
	if (plugin)
		RE_plugin_serialize(plugin, w);
}

static void load_parts_3dlayer(struct iarray_reader *r, struct parts_3dlayer *l,
		int version)
{
	if (!iarray_read(r))
		return;
	if (!parts_3dlayer_create_plugin(l))
		VM_ERROR("cannot create 3D layer plugin");
	RE_plugin_deserialize(RE_get_plugin(l->plugin), r, version);
}

static void save_parts_layout_box(struct iarray_writer *w, struct parts_layout_box *lb)
{
	iarray_write(w, lb->layout_type);
	iarray_write(w, lb->wrap);
	iarray_write(w, lb->wrap_size);
	iarray_write(w, lb->align);
	iarray_write(w, lb->padding_top);
	iarray_write(w, lb->padding_bottom);
	iarray_write(w, lb->padding_left);
	iarray_write(w, lb->padding_right);
}

static void load_parts_layout_box(struct iarray_reader *r, struct parts_layout_box *lb)
{
	lb->layout_type = iarray_read(r);
	lb->wrap = iarray_read(r);
	lb->wrap_size = iarray_read(r);
	lb->align = iarray_read(r);
	lb->padding_top = iarray_read(r);
	lb->padding_bottom = iarray_read(r);
	lb->padding_left = iarray_read(r);
	lb->padding_right = iarray_read(r);
}

static bool save_parts_state(struct iarray_writer *w, struct parts_state *state,struct parts *owner,int version)
{
	iarray_write(w, state->type);
	iarray_write(w, state->common.w);
	iarray_write(w, state->common.h);
	iarray_write_point(w, &state->common.origin_offset);
	iarray_write_rectangle(w, &state->common.hitbox);
	iarray_write_rectangle(w, &state->common.surface_area);
	switch (state->type) {
	case PARTS_MESSAGE_WINDOW:
		parts_message_window_save(w,state->window.model);
		break;
	case PARTS_UNINITIALIZED:
	case PARTS_MOVIE:
	case PARTS_RECT_DETECTION:
		break;
	case PARTS_3DLAYER:
		save_parts_3dlayer(w, &state->layer3d);
		break;
	case PARTS_CG:
		save_parts_cg(w, &state->cg);
		break;
	case PARTS_TEXT:
		save_parts_text(w, &state->text);
		break;
	case PARTS_ANIMATION:
		save_parts_animation(w, &state->anim);
		break;
	case PARTS_NUMERAL:
		save_parts_numeral(w, &state->num);
		break;
	case PARTS_HGAUGE:
		save_parts_gauge(w, &state->gauge, false);
		break;
	case PARTS_VGAUGE:
		save_parts_gauge(w, &state->gauge, true);
		break;
	case PARTS_CONSTRUCTION_PROCESS:
		if (!save_parts_construction_process(w, &state->cproc,owner,version)) return false;
		break;
	case PARTS_FLASH:
		save_parts_flash(w, &state->flash);
		break;
	case PARTS_FLAT:
		save_parts_flat(w, &state->flat);
		break;
	case PARTS_LAYOUT_BOX:
		save_parts_layout_box(w, &state->layout_box);
		break;
	}
	return true;
}

static void load_parts_state(struct iarray_reader *r, struct parts *parts,
		struct parts_state *state, int version)
{
	int type = iarray_read(r);
	if (type < 0 || type >= PARTS_NR_TYPES || (type==PARTS_MESSAGE_WINDOW && (version<14 || state!=&parts->states[0])) || (parts->is_back_scene && type == PARTS_3DLAYER)) {
		r->error = 1;
		return;
	}
	parts_state_reset(state, type);
	state->common.w = iarray_read(r);
	state->common.h = iarray_read(r);
	iarray_read_point(r, &state->common.origin_offset);
	iarray_read_rectangle(r, &state->common.hitbox);
	iarray_read_rectangle(r, &state->common.surface_area);
	switch (state->type) {
	case PARTS_MESSAGE_WINDOW:
		state->window.model=parts_message_window_load(parts,r);
		if(state->window.model) parts_set_dims(parts,&state->common,state->window.model->background.common.w,state->window.model->background.common.h);
		break;
	case PARTS_UNINITIALIZED:
	case PARTS_MOVIE:
	case PARTS_RECT_DETECTION:
		break;
	case PARTS_3DLAYER:
		if (version > 5)
			load_parts_3dlayer(r, &state->layer3d, version);
		break;
	case PARTS_CG:
		load_parts_cg(r, parts, &state->cg);
		break;
	case PARTS_TEXT:
		load_parts_text(r, parts, &state->text);
		break;
	case PARTS_ANIMATION:
		load_parts_animation(r, parts, &state->anim);
		break;
	case PARTS_NUMERAL:
		load_parts_numeral(r, parts, &state->num);
		break;
	case PARTS_HGAUGE:
		load_parts_gauge(r, parts, &state->gauge, false, version);
		break;
	case PARTS_VGAUGE:
		load_parts_gauge(r, parts, &state->gauge, true, version);
		break;
	case PARTS_CONSTRUCTION_PROCESS:
		load_parts_construction_process(r, parts, &state->cproc, version);
		break;
	case PARTS_FLASH:
		load_parts_flash(r, parts, &state->flash);
		break;
	case PARTS_FLAT:
		load_parts_flat(r, parts, &state->flat, version);
		break;
	case PARTS_LAYOUT_BOX:
		load_parts_layout_box(r, &state->layout_box);
		break;
	}
}

static void save_parts_motion(struct iarray_writer *w, struct parts_motion *motion)
{
	iarray_write(w, motion->type);
	switch (motion->type) {
	case PARTS_MOTION_POS:
		iarray_write(w, motion->begin.x);
		iarray_write(w, motion->begin.y);
		iarray_write(w, motion->end.x);
		iarray_write(w, motion->end.y);
		break;
	case PARTS_MOTION_ALPHA:
	case PARTS_MOTION_CG:
	case PARTS_MOTION_NUMERAL_NUMBER:
		iarray_write(w, motion->begin.i);
		iarray_write(w, motion->end.i);
		break;
	case PARTS_MOTION_HGAUGE_RATE:
	case PARTS_MOTION_VGAUGE_RATE:
	case PARTS_MOTION_MAG_X:
	case PARTS_MOTION_MAG_Y:
	case PARTS_MOTION_ROTATE_X:
	case PARTS_MOTION_ROTATE_Y:
	case PARTS_MOTION_ROTATE_Z:
		iarray_write_float(w, motion->begin.f);
		iarray_write_float(w, motion->end.f);
		break;
	case PARTS_MOTION_VIBRATION_SIZE:
		iarray_write(w, motion->begin.x);
		iarray_write(w, motion->begin.y);
		break;
	}
	iarray_write(w, motion->begin_time);
	iarray_write(w, motion->end_time);
}

static struct parts_motion *load_parts_motion(struct iarray_reader *r)
{
	struct parts_motion *motion = xcalloc(1, sizeof(struct parts_motion));
	motion->type = iarray_read(r);
	switch (motion->type) {
	case PARTS_MOTION_POS:
		motion->begin.x = iarray_read(r);
		motion->begin.y = iarray_read(r);
		motion->end.x = iarray_read(r);
		motion->end.y = iarray_read(r);
		break;
	case PARTS_MOTION_ALPHA:
	case PARTS_MOTION_CG:
	case PARTS_MOTION_NUMERAL_NUMBER:
		motion->begin.i = iarray_read(r);
		motion->end.i = iarray_read(r);
		break;
	case PARTS_MOTION_HGAUGE_RATE:
	case PARTS_MOTION_VGAUGE_RATE:
	case PARTS_MOTION_MAG_X:
	case PARTS_MOTION_MAG_Y:
	case PARTS_MOTION_ROTATE_X:
	case PARTS_MOTION_ROTATE_Y:
	case PARTS_MOTION_ROTATE_Z:
		motion->begin.f = iarray_read_float(r);
		motion->end.f = iarray_read_float(r);
		break;
	case PARTS_MOTION_VIBRATION_SIZE:
		motion->begin.x = iarray_read(r);
		motion->begin.y = iarray_read(r);
		break;
	}
	motion->begin_time = iarray_read(r);
	motion->end_time = iarray_read(r);
	return motion;
}

static bool save_parts(struct iarray_writer *w, struct parts *parts,int version)
{
	iarray_write(w, parts->no);
	iarray_write(w, parts->state);
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		if (!save_parts_state(w, &parts->states[i],parts,version)) return false;
	}

	save_parts_params(w, &parts->local);
	save_parts_params(w, &parts->global);
	iarray_write(w, parts->parent ? parts->parent->no : -1);
	iarray_write(w, parts->delegate_index);
	iarray_write(w, parts->sprite_deform);
	iarray_write(w, parts->clickable);
	iarray_write(w, parts->on_cursor_sound);
	iarray_write(w, parts->on_click_sound);
	iarray_write(w, parts->origin_mode);
	// XXX: no need to save pending parent since we called UpdateComponent first
	iarray_write(w, parts->linked_to);
	iarray_write(w, parts->linked_from);
	iarray_write(w, parts->draw_filter);
	iarray_write(w, parts->message_window);
	iarray_write(w, parts->controller_no);
	iarray_write(w, parts->pass_cursor);
	iarray_write(w, parts->lock_input_state);
	iarray_write(w, parts->margin_top);
	iarray_write(w, parts->margin_bottom);
	iarray_write(w, parts->margin_left);
	iarray_write(w, parts->margin_right);
	iarray_write(w, parts->draggable);

	unsigned motion_count_pos = iarray_writer_pos(w);
	iarray_write(w, 0); // size of motion list

	unsigned motion_count = 0;
	struct parts_motion *motion;
	TAILQ_FOREACH(motion, &parts->motion, entry) {
		save_parts_motion(w, motion);
		motion_count++;
	}

	iarray_write_at(w, motion_count_pos, motion_count);
	parts_widget_save(w,parts);
	iarray_write(w,parts->position_is_ratio);
	save_parts_double(w,parts->raw_position_x);save_parts_double(w,parts->raw_position_y);
    save_scroll_widget(w,parts);
    save_textbox_widget(w,parts);
    iarray_write(w,parts->swipe_type);
    iarray_write(w,parts->widget && parts->widget->scroll_pos_assigned);
	iarray_write(w, parts->enable_speed_up ? 1 : 0);
	parts_scroll_links_save(w,parts);
	return true;
}

static struct parts *load_parts(struct iarray_reader *r, int version, bool back_scene)
{
	int no = iarray_read(r);
	struct parts *parts = back_scene ? parts_alloc_back_scene(no) : parts_get(no);
	parts->state = iarray_read(r);
	if (!parts_state_valid(parts->state)) {
		r->error = 1;
		return parts;
	}
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		load_parts_state(r, parts, &parts->states[i], version);
		if (r->error)
			return parts;
	}

	load_parts_params(r, &parts->local, version);
	load_parts_params(r, &parts->global, version);
	parts->pending_parent = iarray_read(r);
	parts->delegate_index = iarray_read(r);
	parts->sprite_deform = iarray_read(r);
	parts->clickable = iarray_read(r);
	parts->on_cursor_sound = iarray_read(r);
	parts->on_click_sound = iarray_read(r);
	parts->origin_mode = iarray_read(r);
	parts->linked_to = iarray_read(r);
	parts->linked_from = iarray_read(r);
	parts->draw_filter = iarray_read(r);
	if (version > 0)
		parts->message_window = iarray_read(r);
	if (version > 2 && version < 7)
		parts->local.alpha_clipper_parts_no = iarray_read(r);
	if (version > 3 || (version == 3 && parts_multi_controller)) {
		parts->controller_no = iarray_read(r);
		parts->pass_cursor = iarray_read(r);
		parts->lock_input_state = iarray_read(r);
		parts->margin_top = iarray_read(r);
		parts->margin_bottom = iarray_read(r);
		parts->margin_left = iarray_read(r);
		parts->margin_right = iarray_read(r);
		parts->draggable = iarray_read(r);
	}

	int motion_count = iarray_read(r);
	if (motion_count < 0 || (unsigned)motion_count > r->size - r->pos) {
		r->error = 1;
		return parts;
	}
	for (int i = 0; i < motion_count; i++) {
		struct parts_motion *motion = load_parts_motion(r);
		if (back_scene)
			free(motion); // A historical frame must not change the live motion clock.
		else
			parts_add_motion(parts, motion);
	}
	if (version>=12)
		parts_widget_load(r,parts,version);
	if(version>=14) {
		int flag=iarray_read(r);double x=load_parts_double(r),y=load_parts_double(r);
		if(flag<0 || flag>1 || !isfinite(x) || !isfinite(y) || fabs(x)>10000000 || fabs(y)>10000000)r->error=1;
		parts->position_is_ratio=flag;parts->raw_position_x=x;parts->raw_position_y=y;
	}

    if(version>=15)load_scroll_widget(r,parts,version);
    if(version>=16)load_textbox_widget(r,parts);
    parts->swipe_type=0;
    if(version>=18){int type=iarray_read(r);if(type<0 || type>4)r->error=1;else parts->swipe_type=type;}
    if(version>=19) {
        int assigned=iarray_read(r);
        if(assigned<0 || assigned>1 || (assigned && !parts->widget))r->error=1;
        else if(parts->widget)parts->widget->scroll_pos_assigned=assigned;
    } else if(parts->widget) {
        // Restored older controls already have their position and listener.
        parts->widget->scroll_pos_assigned=true;
    }
	parts->enable_speed_up = true;
	if (version >= 20) {
		int enable_speed_up = iarray_read(r);
		if (enable_speed_up < 0 || enable_speed_up > 1)
			r->error = 1;
		else
			parts->enable_speed_up = enable_speed_up;
	}
	parts_scroll_links_load(r,parts,version);
	if (!back_scene) {
		parts_list_resort(parts);
		parts_component_dirty(parts);
	}
	parts_recalculate_hitbox(parts);
	return parts;
}

static void save_numeral_fonts(struct iarray_writer *w)
{
	iarray_write(w, parts_nr_numeral_fonts);
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		iarray_write(w, font->type);
		iarray_write(w, font->cg_no);
		for (int i = 0; i < 12; i++) {
			iarray_write(w, font->width[i]);
		}
		iarray_write_string_or_null(w, font->cg_name);
		if (font->type == PARTS_NUMERAL_FONT_TEXT)
			iarray_write_text_style(w, &font->text_style);
	}
}

static void load_numeral_fonts(struct iarray_reader *r, int version)
{
	parts_nr_numeral_fonts = iarray_read(r);
	if (parts_nr_numeral_fonts < 0 || parts_nr_numeral_fonts > 4096
			|| (unsigned)parts_nr_numeral_fonts > (r->size - r->pos) / 14) {
		parts_nr_numeral_fonts = 0;
		r->error = 1;
		return;
	}
	parts_numeral_fonts = xcalloc(parts_nr_numeral_fonts, sizeof(struct parts_numeral_font));
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		font->type = iarray_read(r);
		font->cg_no = iarray_read(r);
		for (int i = 0; i < 12; i++) {
			font->width[i] = iarray_read(r);
		}
		font->cg_name = version >= 21 ? iarray_read_string_or_null(r) : NULL;
		if (font->type == PARTS_NUMERAL_FONT_TEXT && version >= 26) {
			iarray_read_text_style(r, &font->text_style);
			font->text_style.font_size = NULL;
			if (font->cg_name || !parts_numeral_text_style_valid(&font->text_style)) {
				r->error = 1;
				return;
			}
		}
		if (font->type < PARTS_NUMERAL_FONT_SEPARATE ||
				font->type > (version >= 26 ? PARTS_NUMERAL_FONT_TEXT : PARTS_NUMERAL_FONT_SEPARATE2) ||
				(font->cg_name && (font->type == PARTS_NUMERAL_FONT_SEPARATE ||
				 !font->cg_name->size || strlen(font->cg_name->text) != font->cg_name->size))) {
			r->error = 1;
			return;
		}
		parts_numeral_font_init(font);
		if (font->type == PARTS_NUMERAL_FONT_TEXT) {
			for (int j=0;j<12;j++) if (!font->cg[j].handle) r->error = 1;
		}
		if (font->cg_name || font->type == PARTS_NUMERAL_FONT_COMBINED) {
			bool digit = false;
			for (int j=0;j<10;j++) if (font->cg[j].handle) digit = true;
			if (!digit) r->error = 1;
		}
	}
}

/* v21 resource directory only preflights newly named gauge/numeral inputs.
 * It is not a general transaction or validation pass for the XPE body. */
static void save_named_resource(struct iarray_writer *w, struct string *name, unsigned *count)
{
	iarray_write_string(w, name);
	(*count)++;
}

static bool parts_save_back_scene_allowed(struct parts *p)
{
	if (!p->want_save_back_scene)
		return false;
	// Modern system buttons are composite parts. Register() excludes their
	// root from the historical background; its visual children must follow it.
	if (ain && ain->version >= 11)
		for (struct parts *parent = p->parent; parent; parent = parent->parent)
			if (!parent->want_save_back_scene)
				return false;
	return true;
}

static void save_named_resources(struct iarray_writer *w, bool save_hidden, bool back_scene)
{
	unsigned pos = iarray_writer_pos(w), count = 0;
	iarray_write(w, 0);
	for (int i=0;i<parts_nr_numeral_fonts;i++) {
		struct parts_numeral_font *f = &parts_numeral_fonts[i];
		if (!f->cg_name) continue;
		if (f->type == PARTS_NUMERAL_FONT_SEPARATE2) {
			for (int j=0;j<12;j++) if (f->width[j] >= 0) {
				struct string *name = string_format(f->cg_name, (union vm_value){.i=j}, STRFMT_INT);
				save_named_resource(w, name, &count);
				free_string(name);
			}
		} else save_named_resource(w, f->cg_name, &count);
	}
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		if (!save_hidden && (!p->global.show || !PE_controller_visible(p->controller_no))) continue;
		if (back_scene ? !parts_save_back_scene_allowed(p) : !p->want_save) continue;
		for (int i=0;i<PARTS_NR_STATES;i++) {
			struct parts_state *st = &p->states[i];
			if ((st->type == PARTS_HGAUGE || st->type == PARTS_VGAUGE) && st->gauge.cg_name)
				save_named_resource(w, st->gauge.cg_name, &count);
		}
	}
	iarray_write_at(w, pos, count);
}

static bool preflight_named_resources(struct iarray_reader *r, int version)
{
	if (version < 21) return true;
	int count = iarray_read(r);
	if (count < 0 || count > 65536 || (unsigned)count > r->size-r->pos) return false;
	for (int i=0;i<count && !r->error;i++) {
		struct string *name = iarray_read_string(r);
		bool valid = !r->error && name->size > 0 && name->size <= 4096 && strlen(name->text) == name->size;
		struct cg *cg = valid ? asset_cg_load_by_name(name->text, NULL) : NULL;
		free_string(name);
		if (!cg) return false;
		bool dimensions = cg->metrics.w > 0 && cg->metrics.w <= 4096 && cg->metrics.h > 0 && cg->metrics.h <= 4096;
		cg_free(cg);
		if (!dimensions) return false;
	}
	return !r->error;
}

static bool parts_engine_save(struct page **buffer, bool save_hidden, bool back_scene)
{
	bool trace_save = r10_diagnostic_enabled("XSYSTEM4_DIAG_SAVE");
	Uint32 start_ms = trace_save ? SDL_GetTicks() : 0;
	if (PE_IsExistWorkerThread()) {
		WARNING("Cannot save Parts while asynchronous resources are pending");
		return false;
	}
	// get parts into clean state first
	PE_UpdateComponent(0);
	Uint32 updated_ms = trace_save ? SDL_GetTicks() : 0;

	/* Keep the existing wire format unless a selected object has pixels
	 * whose command history was discarded. Ordinary CG/retained-command
	 * snapshots neither read back pixels nor require a format upgrade. */
	int version = 28;
	struct parts *candidate;
	PARTS_LIST_FOREACH(candidate) {
		if ((!save_hidden && (!candidate->global.show ||
				!PE_controller_visible(candidate->controller_no))) ||
				(back_scene ? !parts_save_back_scene_allowed(candidate) : !candidate->want_save))
			continue;
		for (int state = 0; state < PARTS_NR_STATES; state++)
			if (candidate->states[state].type == PARTS_CONSTRUCTION_PROCESS &&
					candidate->states[state].cproc.needs_pixel_snapshot)
				version = 29;
	}

	struct iarray_writer w;
	iarray_init_writer(&w, back_scene ? "XBS" : "XPE");
	iarray_write(&w, version);
	save_named_resources(&w, save_hidden, back_scene);
	if (!back_scene) parts_activity_save(&w, save_hidden);
	if (CURRENT_SAVE_VERSION > 1)
		save_numeral_fonts(&w);

	iarray_write(&w, ctrl_stack.active);
	iarray_write(&w, ctrl_stack.nr_controllers);
	iarray_write(&w, ctrl_stack.next_id);
	for (int i = 0; i < ctrl_stack.nr_controllers; i++)
		iarray_write(&w, ctrl_stack.ids[i]);
	for (int i = 0; i < ctrl_stack.nr_controllers; i++)
		iarray_write(&w, ctrl_stack.hidden[i]);
	iarray_write(&w, ctrl_stack.system_overlay_hidden);

	unsigned count_pos = iarray_writer_pos(&w);
	iarray_write(&w, 0); // size of parts list
	Uint32 header_ms = trace_save ? SDL_GetTicks() : 0;

	unsigned count = 0;
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		if (!save_hidden && (!parts->global.show || !PE_controller_visible(parts->controller_no)))
			continue;
		if (back_scene ? !parts_save_back_scene_allowed(parts) : !parts->want_save)
			continue;
		if (!save_parts(&w, parts,version)) {
			iarray_free_writer(&w);
			return false;
		}
		count++;
	}

	iarray_write_at(&w, count_pos, count);
	unsigned words = iarray_writer_pos(&w);
	Uint32 serialized_ms = trace_save ? SDL_GetTicks() : 0;

	if (*buffer) {
		delete_page_vars(*buffer);
		free_page(*buffer);
	}
	*buffer = iarray_to_page(&w);;
	iarray_free_writer(&w);
	if (trace_save) {
		Uint32 end_ms = SDL_GetTicks();
		NOTICE("[parts-save-perf] back_scene=%d save_hidden=%d version=%d parts=%u words=%u update_ms=%u header_ms=%u serialize_ms=%u page_ms=%u total_ms=%u",
			back_scene,save_hidden,version,count,words,(unsigned)(updated_ms-start_ms),
			(unsigned)(header_ms-updated_ms),(unsigned)(serialized_ms-header_ms),
			(unsigned)(end_ms-serialized_ms),(unsigned)(end_ms-start_ms));
	}
	return true;

}

bool PE_SaveWithoutHideParts(struct page **buffer)
{
	return parts_engine_save(buffer, false, false);
}

bool PE_Save(struct page **buffer)
{
	return parts_engine_save(buffer, true, false);
}

bool PE_SaveBackScene(struct page **buffer)
{
	// Capture independently owned serialized parts, including hidden states.
	// The separate header prevents loading this subset as a complete game UI.
	return parts_engine_save(buffer, true, true);
}

static struct {
	unsigned count;
	struct parts **parts;
} back_scene;

struct parts *parts_back_scene_get(int no)
{
	for (unsigned i = 0; i < back_scene.count; i++)
		if (back_scene.parts[i]->no == no)
			return back_scene.parts[i];
	return NULL;
}

void PE_ClearBackScene(void)
{
	for (unsigned i = 0; i < back_scene.count; i++)
		parts_free_back_scene(back_scene.parts[i]);
	free(back_scene.parts);
	back_scene.parts = NULL;
	back_scene.count = 0;
}

bool PE_LoadBackScene(struct page **buffer)
{
	if (PE_IsExistWorkerThread()) return false;
	if (!buffer || !*buffer || (*buffer)->type != ARRAY_PAGE)
		return false;
	struct iarray_reader r;
	if (!iarray_init_reader(&r, *buffer, "XBS"))
		return false;
	int version = iarray_read(&r);
	if (version < 7 || version > CURRENT_SAVE_VERSION)
		return false;
	if (!preflight_named_resources(&r, version)) return false;

	// Numeral textures are materialized using the captured font table, then
	// own their pixels. Never replace the current game's table or controllers.
	struct parts_numeral_font *live_fonts = parts_numeral_fonts;
	int live_font_count = parts_nr_numeral_fonts;
	parts_numeral_fonts = NULL;
	parts_nr_numeral_fonts = 0;
	load_numeral_fonts(&r, version);
	iarray_read(&r); // captured active controller
	int controllers = iarray_read(&r);
	int ids[PARTS_CONTROLLER_STACK_MAX];
	bool hidden[PARTS_CONTROLLER_STACK_MAX] = {0};
	bool overlay_hidden = false;
	if (controllers <= 0 || controllers > PARTS_CONTROLLER_STACK_MAX)
		r.error = 1;
	if (version >= 8) {
		iarray_read(&r); // next controller ID
		for (int i = 0; i < controllers && !r.error; i++)
			ids[i] = iarray_read(&r);
	} else {
		for (int i = 0; i < controllers && !r.error; i++)
			ids[i] = i;
	}
	if (version >= 12) {
		for (int i = 0; i < controllers && !r.error; i++) {
			int value = iarray_read(&r);
			if (value < 0 || value > 1) r.error = 1;
			hidden[i] = value;
		}
		int value = iarray_read(&r);
		if (value < 0 || value > 1) r.error = 1;
		overlay_hidden = value;
	}
	int count = iarray_read(&r);
	if (count < 0 || count > 4096 || (unsigned)count > r.size - r.pos)
		r.error = 1;
	struct parts **candidate = r.error ? NULL : xcalloc(count, sizeof(*candidate));
	unsigned loaded = 0;
	for (int i = 0; i < count && !r.error; i++) {
		struct parts *p = load_parts(&r, version, true);
		candidate[loaded++] = p;
		for (unsigned j = 0; j + 1 < loaded; j++)
			if (candidate[j]->no == p->no)
				r.error = 1;
		p->sp.z = p->global.z;
		p->sp.z2 = 0;
		if (parts_multi_controller) {
			p->sp.z = -1;
			p->sp.z2 = p->global.z;
			for (int j = 0; j < controllers; j++)
				if (ids[j] == p->controller_no) {
					p->sp.z = j;
					p->global.show = p->global.show && !hidden[j];
				}
			if (p->controller_no == PARTS_CONTROLLER_SYSTEM_OVERLAY) {
				p->global.show = p->global.show && !overlay_hidden;
			}
		}
	}
	for (int i = 0; i < parts_nr_numeral_fonts; i++)
		parts_numeral_font_fini(&parts_numeral_fonts[i]);
	free(parts_numeral_fonts);
	parts_numeral_fonts = live_fonts;
	parts_nr_numeral_fonts = live_font_count;
	if (r.error || r.pos != r.size) {
		for (unsigned i = 0; i < loaded; i++)
			parts_free_back_scene(candidate[i]);
		free(candidate);
		WARNING("invalid back-scene data");
		return false;
	}
	// Commit only a completely decoded scene; failure preserves the old view.
	PE_ClearBackScene();
	back_scene.parts = candidate;
	back_scene.count = loaded;
	for (unsigned i = 0; i < loaded; i++)
		scene_register_sprite(&candidate[i]->sp);
	scene_dirty();
	return true;
}

void PE_SetFontColorForBackScene(int no, int r, int g, int b, int state)
{
	struct parts *p = parts_back_scene_get(no);
	if (!p || !parts_state_valid(--state) || p->states[state].type != PARTS_TEXT)
		return;
	p->states[state].text.ts.color = (SDL_Color){r, g, b, 255};
	parts_text_rerender(p, &p->states[state].text);
	parts_dirty(p);
}

void PE_SetMulColorForBackScene(int no, int r, int g, int b)
{
	struct parts *p = parts_back_scene_get(no);
	if (!p)
		return;
	p->global.multiply_color = (SDL_Color){r, g, b, 255};
	parts_dirty(p);
}

void PE_SetAlphaForBackScene(int no, int alpha)
{
	struct parts *p = parts_back_scene_get(no);
	if (!p)
		return;
	p->global.alpha = max(0, min(255, alpha));
	parts_dirty(p);
}

void PE_SetShowForBackScene(int no, bool show)
{
	struct parts *p = parts_back_scene_get(no);
	if (!p)
		return;
	p->global.show = show;
	parts_dirty(p);
}

/* PC resume saves retain the script's window objects even when their native
 * PartsEngine image cannot be imported. A subsequent XPE save can consequently
 * contain an empty placeholder while m_Exist is still true. Let the original
 * window manager recreate only those missing surfaces on its post-load Update;
 * do not reset valid (including deliberately hidden) windows or game progress. */
static struct page *message_restore_page(int slot, enum page_type type, int index, int min_vars)
{
	if (!page_index_valid(slot)) return NULL;
	struct page *p = heap[slot].page;
	return p && p->type == type && p->index == index && p->nr_vars >= min_vars ? p : NULL;
}

static void reconcile_rance03_message_windows(void)
{
	if (!ain || !heap_size) return;
	int method = ain_get_function(ain, "CMessageWindow@GetPartsNumber");
	if (method < 0 || ain->functions[method].crc != 395728508) return;
	int global = ain_get_global(ain, "g_MessageWindowManager");
	int manager_type = ain_get_struct(ain, "CMessageWindowManager");
	int window_type = ain_get_struct(ain, "CMessageWindow");
	if (global < 0 || manager_type < 0 || window_type < 0) return;
	if (ain->globals[global].type.data != AIN_STRUCT
			|| ain->globals[global].type.struc != manager_type) return;
	struct ain_struct *ws = &ain->structures[window_type];
	struct ain_struct *ms = &ain->structures[manager_type];
	if (ws->nr_members != 6 || ms->nr_members != 4
			|| strcmp(ws->members[0].name, "m_MessageWindowNumber")
			|| strcmp(ws->members[1].name, "m_Exist")
			|| ws->members[0].type.data != AIN_INT
			|| ws->members[1].type.data != AIN_BOOL
			|| strcmp(ms->members[0].name, "m_MessageWindowList")
			|| ms->members[0].type.data != AIN_ARRAY_STRUCT
			|| ms->members[0].type.struc != window_type) return;
	struct page *g = message_restore_page(0, GLOBAL_PAGE, 0, global + 1);
	if (!g) return;
	struct page *manager = message_restore_page(g->values[global].i, STRUCT_PAGE, manager_type, 4);
	if (!manager) return;
	struct page *list = message_restore_page(manager->values[0].i, ARRAY_PAGE, AIN_ARRAY_STRUCT, 0);
	if (!list || list->array.rank != 1 || list->array.struct_type != window_type) return;
	for (int i = 0; i < list->nr_vars; i++) {
		struct page *window = message_restore_page(list->values[i].i, STRUCT_PAGE, window_type, 6);
		if (!window || !window->values[1].i) continue;
		int number = window->values[0].i;
		if (number < 0 || number > INT_MAX - 1000000000) continue;
		struct parts *p = parts_try_get(1000000000 + number);
		bool missing = !p;
		if (p) {
			missing = true;
			for (int state = 0; state < PARTS_NR_STATES; state++)
				if (p->states[state].type != PARTS_UNINITIALIZED) missing = false;
		}
		if (missing) window->values[1].i = 0;
	}
}

#include "../ain12_message_restore.inc"

void parts_reconcile_message_window_activities(void)
{
	unsigned changed = reconcile_rance10_message_windows();
	if (changed)
		NOTICE("[message-lifecycle] invalidated %u stale window caches", changed);
}

bool PE_Load(struct page **buffer)
{
	if (!(*buffer)) {
		WARNING("savedata array is empty");
		return false;
	}

	struct iarray_reader r;
	if (!iarray_init_reader(&r, *buffer, "XPE")) {
		WARNING("unrecognized savedata magic");
		reconcile_rance03_message_windows();
		reconcile_rance10_message_windows();
		return false;
	}
	int version = iarray_read(&r);
	if (version > CURRENT_SAVE_VERSION) {
		WARNING("unrecognized savedata version");
		return false;
	}

	if (!preflight_named_resources(&r, version)) return false;
	struct parts_activity_snapshot *activity_snapshot = NULL;
	if (version >= 24 && !parts_activity_read(&r, &activity_snapshot)) return false;
	parts_release_all();
	memset(ctrl_stack.hidden, 0, sizeof(ctrl_stack.hidden));
	ctrl_stack.system_overlay_hidden = false;

	if (version > 1)
		load_numeral_fonts(&r, version);

	if (version > 3 || (version == 3 && parts_multi_controller)) {
		ctrl_stack.active = iarray_read(&r);
		ctrl_stack.nr_controllers = iarray_read(&r);
		if (ctrl_stack.nr_controllers <= 0 || ctrl_stack.nr_controllers > PARTS_CONTROLLER_STACK_MAX) {
			WARNING("invalid controller count");
			goto failed;
		}
		ctrl_stack.next_id = version >= 8 ? iarray_read(&r) : ctrl_stack.nr_controllers;
		for (int i = 0; i < ctrl_stack.nr_controllers; i++) {
			int id = version >= 8 ? iarray_read(&r) : i;
			if (id < 0 || id == PARTS_CONTROLLER_SYSTEM_OVERLAY || id >= ctrl_stack.next_id) {
				WARNING("invalid controller ID");
				goto failed;
			}
			for (int j = 0; j < i; j++) {
				if (ctrl_stack.ids[j] == id) {
					WARNING("duplicate controller ID");
					goto failed;
				}
			}
			ctrl_stack.ids[i] = id;
		}
		if (PE_get_controller_index(ctrl_stack.active) < 0) {
			WARNING("invalid active controller");
			goto failed;
		}
		if (version >= 12) {
			for (int i = 0; i < ctrl_stack.nr_controllers; i++) {
				int value = iarray_read(&r);
				if (value < 0 || value > 1) goto failed;
				ctrl_stack.hidden[i] = value;
			}
			int value = iarray_read(&r);
			if (value < 0 || value > 1) goto failed;
			ctrl_stack.system_overlay_hidden = value;
		}
	}

	int nr_parts = iarray_read(&r);
	if (nr_parts < 0) {
		WARNING("invalid parts count");
		goto failed;
	}

	for (int i = 0; i < nr_parts; i++) {
		load_parts(&r, version, false);
		if (r.error) {
			goto failed;
		}
	}

	if (r.error || (version >= 24 && r.pos != r.size)) goto failed;
	if (activity_snapshot && !parts_activity_install(activity_snapshot)) goto failed;
	bool restored_activities = activity_snapshot != NULL;
	parts_activity_snapshot_free(activity_snapshot);
	parts_engine_clean();
	if (restored_activities) parts_activity_restore_reference_compat();
	reconcile_rance03_message_windows();
	reconcile_rance10_message_windows();
	return true;
failed:
	parts_activity_snapshot_free(activity_snapshot);
	return false;
}
