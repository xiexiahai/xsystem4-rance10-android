/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TEXTBOX_SAVE_INC
#define TEXTBOX_SAVE_INC

#include "system4/utfsjis.h"

/* Per-part tail used by save.c after the scroll tail.  The root caller writes
 * format 16 and only calls the loader for version >= 16. */

static void save_textbox_widget(struct iarray_writer *w, struct parts *p)
{
    struct parts_widget *widget = p->widget;
    struct parts_textbox *tb = widget ? widget->textbox : NULL;
    iarray_write(w, tb ? 1 : 0);
    if (!tb)
        return;

    iarray_write(w, tb->readonly ? 1 : 0);
    iarray_write(w, tb->show_h ? 1 : 0);
    iarray_write(w, tb->show_v ? 1 : 0);
    for (int i = 0; i < 4; i++)
        iarray_write(w, tb->padding[i]);
    iarray_write(w, tb->scroll_x);
    iarray_write(w, tb->scroll_y);
    iarray_write_color(w, &tb->bg);
    iarray_write_color(w, &tb->readonly_bg);
    iarray_write_color(w, &tb->frame);
    iarray_write_color(w, &tb->selection);
    iarray_write(w, (int)tb->buffer.limit);
    iarray_write(w, (int)tb->buffer.cursor);
    iarray_write(w, (int)tb->buffer.anchor);

    /* Raw UTF-8 bytes have their own bounded encoding. Generic iarray strings
     * interpret bytes as legacy two-byte code points and cannot be used here. */
    iarray_write(w,(int)tb->buffer.len);
    for(size_t i=0;i<tb->buffer.len;i++)iarray_write(w,(unsigned char)tb->buffer.text[i]);
    iarray_write(w, (int)tb->blink_ms);
}

static bool load_textbox_widget(struct iarray_reader *r, struct parts *p)
{
    if (r->error)
        return false;
    int flag = iarray_read(r);
    if (flag < 0 || flag > 1) {
        r->error = 1;
        return false;
    }
    struct parts_widget *widget = p->widget;
    struct parts_textbox *tb = widget ? widget->textbox : NULL;
    if (!!flag != !!tb) {
        r->error = 1;
        return false;
    }
    if (!flag)
        return true;
    if (!widget || !tb || (widget->type != PARTS_WIDGET_TEXTBOX &&
            widget->type != PARTS_WIDGET_MULTITEXTBOX && widget->type != PARTS_WIDGET_SPINBOX)) {
        r->error = 1;
        return false;
    }

    int ro = iarray_read(r), sh = iarray_read(r), sv = iarray_read(r);
    if (ro < 0 || ro > 1 || sh < 0 || sh > 1 || sv < 0 || sv > 1) {
        r->error = 1;
        return false;
    }
    int padding[4];
    for (int i = 0; i < 4; i++) {
        padding[i] = iarray_read(r);
        if (padding[i] < 0 || padding[i] > 4096) {
            r->error = 1;
            return false;
        }
    }
    int scroll_x = iarray_read(r), scroll_y = iarray_read(r);
    if (scroll_x < 0 || scroll_x > 10000000 || scroll_y < 0 || scroll_y > 10000000) {
        r->error = 1;
        return false;
    }
    SDL_Color bg, readonly_bg, frame, selection;
    SDL_Color *colors[4] = { &bg, &readonly_bg, &frame, &selection };
    for (int i = 0; i < 4; i++) {
        int c[4];
        for (int k = 0; k < 4; k++) {
            c[k] = iarray_read(r);
            if (c[k] < 0 || c[k] > 255) {
                r->error = 1;
                return false;
            }
        }
        *colors[i] = (SDL_Color){ c[0], c[1], c[2], c[3] };
    }
    int limit = iarray_read(r), cursor = iarray_read(r), anchor = iarray_read(r);
    if (limit < 0 || limit > 65536 || cursor < 0 || anchor < 0) {
        r->error = 1;
        return false;
    }
    int byte_count=iarray_read(r);
    if(r->error || byte_count<0 || byte_count>262144 || cursor>byte_count || anchor>byte_count ||
            (size_t)byte_count>r->size-r->pos){r->error=1;return false;}
    char *bytes=xmalloc((size_t)byte_count+1);
    for(int i=0;i<byte_count;i++) {
        int value=iarray_read(r);
        if(value<1 || value>255){free(bytes);r->error=1;return false;}
        bytes[i]=(char)value;
    }
    bytes[byte_count]=0;
    struct string *text=make_string(bytes,byte_count);free(bytes);
    bool multiline = widget->type == PARTS_WIDGET_MULTITEXTBOX;
    char *normalized = NULL;
    size_t norm_len = 0, scalars = 0;
    bool boundaries = true;
    size_t offsets[2] = { (size_t)cursor, (size_t)anchor };
    for (int i = 0; i < 2; i++) {
        size_t off = offsets[i];
        bool ok = off == 0 || off == text->size;
        if (!ok && off < text->size) {
            size_t start = tb_prev(text->text, text->size, off);
            size_t n = tb_utf8_seq((const unsigned char *)text->text + start,
                    text->size - start);
            ok = n != 0 && start + n == off;
        }
        if (!ok)
            boundaries = false;
    }
    if (!boundaries ||
            !tb_normalize(text->text, multiline, &normalized, &norm_len, &scalars) ||
            norm_len != text->size || memcmp(normalized, text->text, text->size) ||
            scalars > (size_t)limit) {
        free(normalized);
        free_string(text);
        r->error = 1;
        return false;
    }
    int blink = iarray_read(r);
    if (r->error || blink < 0 || blink > 999) {
        free(normalized);
        free_string(text);
        r->error = 1;
        return false;
    }

    char *sjis = utf2sjis(normalized, 0);
    if (!sjis) {
        free(normalized);
        free_string(text);
        r->error = 1;
        return false;
    }
    struct string *legacy = cstr_to_string(sjis);
    free(sjis);
    if (!legacy) {
        free(normalized);
        free_string(text);
        r->error = 1;
        return false;
    }

    struct textbox_utf8 tmp = {
        .text = normalized,
        .len = norm_len,
        .cursor = (size_t)cursor,
        .anchor = (size_t)anchor,
        .limit = (size_t)limit,
        .multiline = multiline,
    };
    tb_free(&tb->buffer);
    tb->buffer = tmp;
    tb->readonly = ro != 0;
    tb->show_h = sh != 0;
    tb->show_v = sv != 0;
    for (int i = 0; i < 4; i++)
        tb->padding[i] = padding[i];
    tb->scroll_x = scroll_x;
    tb->scroll_y = scroll_y;
    tb->bg = bg;
    tb->readonly_bg = readonly_bg;
    tb->frame = frame;
    tb->selection = selection;
    tb->blink_ms = (unsigned)blink;
    tb->keep_caret_visible = false;
    free_string(widget->text);
    widget->text = legacy;
    widget->dirty = true;
    free_string(text);
    if (!parts_widget_build(p))
        r->error = 1;
    return !r->error;
}

#endif
