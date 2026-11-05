/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XSYSTEM4_PARTS_WIDGET_H
#define XSYSTEM4_PARTS_WIDGET_H
#include "gfx/gfx.h"
#include "gfx/font.h"
struct parts;
struct parts_flat;
struct parts_textbox;
struct string;
struct iarray_writer;
struct iarray_reader;

enum { PARTS_WIDGET_BUTTON=0, PARTS_WIDGET_CHECKBOX=1, PARTS_WIDGET_VSCROLL=2,
    PARTS_WIDGET_HSCROLL=3, PARTS_WIDGET_PANEL=4,
    PARTS_WIDGET_TEXTBOX=5, PARTS_WIDGET_SPINBOX=6, PARTS_WIDGET_MULTITEXTBOX=7, PARTS_WIDGET_RADIO=9,
    PARTS_WIDGET_FORM=23, PARTS_WIDGET_LISTBOX=24 };
struct parts_widget {
    int type, width, height;
    struct parts_textbox *textbox;
    bool enabled, checked, button_mode, dirty, color_set;
    bool flat_backed,flat_enabled,flat_checked;
    bool flat_text_overlay; /* Explicit native SetButtonText, not the serialized fallback label. */
    bool panel_layout_only; /* PACT runtime layout marker: preserve children and color metadata. */
    SDL_Color color;
    struct text_style font;
    int char_space, line_space, text_origin;
    struct string *text, *cg_name, *flat_name;
    Texture images[20];
    bool scroll_art;
    struct parts_flat *scroll_flats[20];
    struct string *scroll_cg_names[20];
    /* Owned role prefixes, independent of the resolved resources above. */
    struct string *scroll_flat_override[4], *scroll_cg_override[4];
    int scroll_active[4];
    int total, view, pos, step, previous_size, next_size;
    bool scroll_pos_assigned; /* First explicit assignment initializes the listener once. */
    int pressed_region, hover_region, drag_offset;
    unsigned repeat_at;
    int *members;
    unsigned nr_members;
    int total_size_link,view_size_link;
    /* Typed extensions: serialized only for these new kinds, never old widgets. */
    struct { float value,def,min,max,increment; int decimals; } spin;
    struct { int min_w,min_h,max_w,max_h,caption_plus; SDL_Color caption;
        bool close_enabled; int close_phase; } form;
    struct { struct string **items; int count,selected,scroll,line_height,margin_x,margin_y; } list;
};
struct parts_widget *parts_widget_get(int no, int type);
void parts_widget_create(struct parts *p, int type);
void parts_widget_free(struct parts_widget *w);
bool parts_widget_set_cg(struct parts *p, struct string *name);
bool parts_widget_set_flat(struct parts *p,struct string *name);
bool parts_widget_build(struct parts *p);
void parts_textbox_init(struct parts *p);
void parts_textbox_free(struct parts_widget *w);
bool parts_textbox_set_skin(struct parts *p,struct string *name);
bool parts_textbox_build(struct parts *p);
bool parts_textbox_update(struct parts *p,int passed_time);
bool parts_textbox_set_text(struct parts *p,struct string *text);
bool parts_textbox_set_limit(struct parts *p,int limit);
bool parts_textbox_open(struct parts *p,bool open);
bool parts_textbox_is_open(struct parts *p);
void parts_textbox_mouse(struct parts *p,Point pos,bool drag,bool extend);
void parts_textbox_dirty(struct parts *p);
/* Role order: thumb, previous, next, background. Strings are borrowed.
 * A NULL override array preserves its current prefixes; an explicit array
 * replaces that whole set, with NULL/empty entries clearing individual roles.
 * Conflicting CG/FLAT prefixes or a missing normal asset leave all state intact. */
bool parts_widget_set_scroll_skin(struct parts *p,struct string *flat,struct string *cg,
    struct string *flat_override[4],struct string *cg_override[4]);
bool parts_widget_set_scroll_override(struct parts *p,bool flat,int role,struct string *name);
void parts_widget_scroll_art_free(struct parts_widget *w);
bool parts_widget_scroll_update(struct parts *p,int passed_time);
struct widget_scroll_item { struct parts_flat *flat; Texture *texture; Rectangle rect; };
void parts_widget_scroll_items(struct parts *p,struct widget_scroll_item items[4]);
bool parts_widget_spin_value(struct parts *p,float value);
void parts_widget_spin_refresh(struct parts *p);
void parts_widget_spin_select_all(struct parts *p);
void parts_listbox_insert(struct parts *p,int index,struct string *item);
void parts_listbox_set(struct parts *p,int index,struct string *item);
void parts_listbox_erase(struct parts *p,int index);
void parts_listbox_clear(struct parts *p);
void parts_listbox_select(struct parts *p,int index);
int parts_listbox_cursor_index(struct parts *p,Point cursor);
void parts_form_close(struct parts *p,bool cancel);

int parts_scroll_link_pos(struct parts *p,int axis);
float parts_scroll_link_alpha(struct parts *p);
void parts_set_scroll_link(struct parts *p,int kind,int no);
int parts_widget_scroll_size(struct parts *p,bool view);
void parts_widget_set_size_link(struct parts *p,bool view,int no);
void parts_scroll_links_save(struct iarray_writer *w,struct parts *p);
void parts_scroll_links_load(struct iarray_reader *r,struct parts *p,int version);
void parts_widgets_prepare(void);
bool parts_widget_visible(struct parts *p);
void parts_widget_check(struct parts *p, bool checked);
void parts_widget_scroll(struct parts *p, int pos);
void parts_widget_set_scroll_pos(struct parts *p, int pos);
float parts_widget_rate(struct parts_widget *w);
void parts_widget_set_rate(struct parts *p, float rate);
void parts_widget_mouse(struct parts *p, Point cursor, bool down, bool pressed, bool released);
void parts_widget_radio_add(struct parts *p, int child);
void parts_widget_save(struct iarray_writer *w, struct parts *p);
void parts_widget_load(struct iarray_reader *r, struct parts *p,int version);
void PE_SetFocus(int no);
int PE_GetFocusPartsNumber(void);
bool PE_IsFocus(int no);
void parts_widget_font_get(struct text_style *font, int *type,int *size,int *r,int *g,int *b,
    float *bold,int *er,int *eg,int *eb,float *edge);
void parts_widget_font_set(struct text_style *font, int type,int size,int r,int g,int b,
    float bold,int er,int eg,int eb,float edge);
#endif
