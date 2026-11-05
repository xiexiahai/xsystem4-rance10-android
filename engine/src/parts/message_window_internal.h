#ifndef PARTS_MESSAGE_WINDOW_INTERNAL_H
#define PARTS_MESSAGE_WINDOW_INTERNAL_H
#include "parts_internal.h"
struct mw_text_plan;
struct mw_glyph {Texture texture;int x,y;size_t token_index;double due_ms;};
struct mw_content {
    struct string *source;
    struct mw_text_plan *plan;
    struct mw_glyph *glyphs;
    size_t count,visible;
    Texture canvas;
    int w,h;
    double elapsed_ms,total_ms;
};
struct parts_message_window {
    struct parts_state background,mark;
    struct string *cg_name,*flat_name,*mark_cg_name,*mark_flat_name,*adv_name;
    struct mw_content text;
    struct text_style font;
    Rectangle area;
    Point mark_pos;
    int mark_z,loop_start,loop_count,loop_ms;
    int text_origin,speed,tracking,leading,wait_frame,message_no,adv_version,adv_step;
    bool active,mark_show;
    SDL_Color inactive;
};
void parts_message_window_free(struct parts_message_window *m);
struct parts_message_window *parts_message_window_ensure(struct parts *p);
bool parts_message_window_update(struct parts *p,int elapsed);
bool parts_message_window_slots(struct parts_message_window *m,Texture **slots,size_t *used,size_t limit);
void parts_message_window_render(struct parts *p);
bool parts_flat_seek(struct parts_flat *f,int frame_no);
bool parts_message_window_set_image(int no,bool mark,bool flat,struct string *name,int start,int count,int ms);
bool parts_message_window_set_text(int no,struct string *text,int msg,struct string *adv,int version,int step);
void parts_message_window_save(struct iarray_writer *w,struct parts_message_window *m);
struct parts_message_window *parts_message_window_load(struct parts *owner,struct iarray_reader *r);
#endif
