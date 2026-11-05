/* Composite message-window ownership, candidate text layout and frame clock. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include "system4.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "system4/flat.h"
#include "xsystem4.h"
#include "parts.h"
#include "parts_message_window.h"
#include "message_window_internal.h"
#include "../hll/iarray.h"
#include "message_text_plan.inc"
#include "texture_slots.inc"
static unsigned live_windows;
bool parts_message_windows_exist(void) {return live_windows!=0;}

static void mw_content_free(struct mw_content *c) {
    for(size_t i=0;i<c->count;i++)gfx_delete_texture(&c->glyphs[i].texture);
    free(c->glyphs);gfx_delete_texture(&c->canvas);
    if(c->plan){mw_text_plan_free(c->plan);free(c->plan);}
    if(c->source)free_string(c->source);
    memset(c,0,sizeof(*c));
}
void parts_message_window_free(struct parts_message_window *m) {
    if(!m)return;
    parts_state_reset(&m->background,PARTS_UNINITIALIZED);
    parts_state_reset(&m->mark,PARTS_UNINITIALIZED);
    mw_content_free(&m->text);
    struct string *names[]={m->cg_name,m->flat_name,m->mark_cg_name,m->mark_flat_name,m->adv_name};
    for(size_t i=0;i<sizeof(names)/sizeof(*names);i++)if(names[i])free_string(names[i]);
    free(m);live_windows--;
}
struct parts_message_window *parts_message_window_ensure(struct parts *p) {
    parts_textures_require_resident(p);
    if(p->states[0].type!=PARTS_MESSAGE_WINDOW)parts_state_reset(&p->states[0],PARTS_MESSAGE_WINDOW);
    struct parts_message_window *m=p->states[0].window.model;
    if(m)return m;
    m=calloc(1,sizeof(*m));if(!m)ERROR("Cannot allocate message window");
    m->cg_name=string_ref(&EMPTY_STRING);m->flat_name=string_ref(&EMPTY_STRING);
    m->mark_cg_name=string_ref(&EMPTY_STRING);m->mark_flat_name=string_ref(&EMPTY_STRING);
    m->adv_name=string_ref(&EMPTY_STRING);m->text.source=string_ref(&EMPTY_STRING);
    m->font=(struct text_style){.face=0,.size=16,.weight=FW_NORMAL,.color={255,255,255,255},.edge_color={0,0,0,255},.scale_x=1,.space_scale_x=1};
    m->text_origin=1;m->speed=23;m->message_no=-1;m->inactive=(SDL_Color){255,255,255,255};
    p->states[0].window.model=m;live_windows++;return m;
}
static struct parts_message_window *mw_lookup(int no) {
    struct parts *p=parts_try_get(no);
    return p && p->states[0].type==PARTS_MESSAGE_WINDOW?p->states[0].window.model:NULL;
}
static struct parts *mw_owner(int no) {
    struct parts *p=parts_get(no);parts_message_window_ensure(p);return p;
}
static struct mw_text_style mw_base(const struct parts_message_window *m) {
    return (struct mw_text_style){.face=m->font.face,.size=m->font.size,.bold=m->font.bold_width,.edge=m->font.edge_left,
      .r=m->font.color.r,.g=m->font.color.g,.b=m->font.color.b,.er=m->font.edge_color.r,.eg=m->font.edge_color.g,.eb=m->font.edge_color.b,
      .tracking=m->tracking,.leading=m->leading,.delay_ms=m->speed};
}
static struct text_style mw_font(const struct mw_text_style *s) {
    return (struct text_style){.face=s->face,.size=s->size,.bold_width=s->bold,.weight=FW_NORMAL,
      .edge_left=s->edge,.edge_up=s->edge,.edge_right=s->edge,.edge_down=s->edge,.color={s->r,s->g,s->b,255},
      .edge_color={s->er,s->eg,s->eb,255},.scale_x=1,.space_scale_x=1};
}
static bool mw_paint(struct mw_content *c,size_t visible) {
    if(visible>c->count)return false;
    for(size_t i=c->visible;i<visible;i++) {
        struct mw_glyph *g=&c->glyphs[i];
        const struct mw_text_token *token=&c->plan->tokens[g->token_index];
        struct text_style font=mw_font(&token->style);
        int edge=(int)ceilf(font.edge_left+font.bold_width);
        /* Render directly: copying whole transparent glyph rectangles would
         * erase previously painted outlines when tracking is negative. */
        gfx_render_textf(&c->canvas,g->x+edge,g->y+edge,(char*)token->bytes,&font,false);
        if(glGetError()!=GL_NO_ERROR)return false;
    }
    c->visible=visible;return true;
}
static bool mw_build(const struct parts_message_window *settings,const struct string *source,const struct mw_content *old,struct mw_content *out) {
    memset(out,0,sizeof(*out));
    if(!source || source->size<0 || source->size>1024*1024)return false;
    out->plan=calloc(1,sizeof(*out->plan));if(!out->plan)return false;
    const char *stage="parse";
    struct mw_text_style base=mw_base(settings);
    if(!mw_text_parse(source->text,source->size,sys_text_is_gbk,&base,out->plan))goto fail;
    stage="glyph-count";
    if(out->plan->glyph_count>2048)goto fail;
    out->source=string_dup(source);
    int bgw=settings->background.common.w,bgh=settings->background.common.h;
    stage="text-area";
    out->w=settings->area.w?settings->area.w:(bgw>0?bgw:config.view_width)-settings->area.x;
    out->h=settings->area.h?settings->area.h:(bgh>0?bgh:config.view_height)-settings->area.y;
    if(out->w<1 || out->h<1 || out->w>4096 || out->h>4096)goto fail;
    if(!out->plan->glyph_count)return true;
    out->glyphs=calloc(out->plan->glyph_count,sizeof(*out->glyphs));if(!out->glyphs)goto fail;
    int x=0,y=0,line_h=0,line_leading=settings->leading,maxw=0;size_t bytes=0;
    for(size_t i=0;i<out->plan->count;i++) {
        const struct mw_text_token *t=&out->plan->tokens[i];
        if(t->newline){maxw=max(maxw,x);x=0;y+=max(1,line_h+line_leading);line_h=0;line_leading=settings->leading;continue;}
        struct text_style font=mw_font(&t->style);int edge=(int)ceilf(font.edge_left+font.bold_width);
        int advance=(int)ceilf(gfx_size_char(&font,t->bytes));
        int w=max(1,advance+edge*2),h=max(1,text_style_height(&font)+edge*2);
        stage="glyph-budget";
        if(w>2048 || h>2048 || (size_t)w*h*4>64u*1024u*1024u-bytes)goto fail;
        bytes+=(size_t)w*h*4;
        if(x>0 && x+advance>out->w){maxw=max(maxw,x);x=0;y+=max(1,line_h+line_leading);line_h=0;}
        struct mw_glyph *g=&out->glyphs[out->count++];g->x=x;g->y=y;g->token_index=i;
        gfx_init_texture_rgba(&g->texture,w,h,(SDL_Color){0,0,0,0});
        stage="glyph-texture";
        if(!g->texture.handle || glGetError()!=GL_NO_ERROR)goto fail;
        gfx_render_textf(&g->texture,edge,edge,(char*)t->bytes,&font,false);
        stage="glyph-render";
        if(glGetError()!=GL_NO_ERROR)goto fail;
        out->total_ms+=t->style.delay_ms;g->due_ms=out->total_ms;
        x+=max(1,advance+t->style.tracking);line_h=max(line_h,h);line_leading=t->style.leading;
    }
    maxw=max(maxw,x);int block_h=y+line_h;
    int col=(settings->text_origin-1)%3,row=(settings->text_origin-1)/3;
    int dx=(out->w-maxw)*col/2,dy=(out->h-block_h)*row/2;
    for(size_t i=0;i<out->count;i++){out->glyphs[i].x+=dx;out->glyphs[i].y+=dy;}
    gfx_init_texture_rgba(&out->canvas,out->w,out->h,(SDL_Color){0,0,0,0});
    stage="text-canvas";
    if(!out->canvas.handle || glGetError()!=GL_NO_ERROR)goto fail;
    bool prefix=old && old->source && source->size>=old->source->size && !memcmp(source->text,old->source->text,old->source->size);
    size_t visible=prefix?min(old->visible,out->count):0;
    out->elapsed_ms=prefix?fmin(old->elapsed_ms,out->total_ms):0;
    if(visible)out->elapsed_ms=fmax(out->elapsed_ms,out->glyphs[visible-1].due_ms);
    while(visible<out->count && out->glyphs[visible].due_ms<=out->elapsed_ms)visible++;
    stage="text-paint";
    if(!mw_paint(out,visible))goto fail;
    return true;
fail:
    WARNING("[message-layout] stage=%s bytes=%d glyphs=%zu area=%dx%d font=%g face=%d",stage,source->size,out->plan?out->plan->glyph_count:0,out->w,out->h,(double)base.size,base.face);
    mw_content_free(out);return false;
}
static void mw_sync_dims(struct parts *p) {
    struct parts_message_window *m=mw_lookup(p->no);
    parts_set_dims(p,&p->states[0].common,m->background.common.w,m->background.common.h);
    parts_dirty(p);
}
static void mw_apply_settings(struct parts *p,const struct parts_message_window *candidate) {
    struct parts_message_window *m=parts_message_window_ensure(p);struct mw_content next;
    if(!mw_build(candidate,m->text.source,&m->text,&next))ERROR("Cannot construct message text layout before publication");
    mw_content_free(&m->text);*m=*candidate;m->text=next;mw_sync_dims(p);
}
bool parts_message_window_set_text(int no,struct string *text,int msg,struct string *adv,int version,int step) {
    struct parts *p=mw_owner(no);struct parts_message_window *m=parts_message_window_ensure(p);struct mw_content next;
    if(!text || !adv || !mw_build(m,text,&m->text,&next))return false;
    struct string *adv_copy=string_dup(adv);mw_content_free(&m->text);m->text=next;
    free_string(m->adv_name);m->adv_name=adv_copy;m->message_no=msg;m->adv_version=version;m->adv_step=step;parts_dirty(p);return true;
}
static void mw_set_text(int no,struct string *text,int msg,struct string *adv,int version,int step) {
    if(!parts_message_window_set_text(no,text,msg,adv,version,step))ERROR("Invalid message text or unsupported control/layout budget");
}
static void mw_fix(int no) {
    struct parts_message_window *m=mw_lookup(no);if(!m)return;
    struct parts *p=mw_owner(no);m->text.elapsed_ms=m->text.total_ms;
    if(!mw_paint(&m->text,m->text.count))ERROR("Message text GL completion failed");
    parts_dirty(p);
}
static bool mw_name_valid(const struct string *s){return s && s->size>=0 && s->size<=1024*1024 && strlen(s->text)==(size_t)s->size;}
static bool mw_image(struct parts *p,const struct string *cg,const struct string *flat,int start,int count,int ms,struct parts_state *out) {
    memset(out,0,sizeof(*out));
    if(flat->size){out->type=PARTS_FLAT;return parts_flat_load(p,&out->flat,(struct string*)flat);}
    if(!cg->size)return true;
    if(count){out->type=PARTS_ANIMATION;return parts_animation_set_cg(p,&out->anim,(struct string*)cg,start,count,ms);}
    out->type=PARTS_CG;return parts_cg_set(p,&out->cg,(struct string*)cg);
}
bool parts_message_window_set_image(int no,bool mark,bool flat,struct string *name,int start,int count,int ms) {
    if(!mw_name_valid(name) || start<0 || count<0 || count>4096 || ms<0 || (count && ms<1))return false;
    struct parts *p=mw_owner(no);struct parts_message_window *m=parts_message_window_ensure(p);
    struct string **field=mark?(flat?&m->mark_flat_name:&m->mark_cg_name):(flat?&m->flat_name:&m->cg_name);
    struct string *copy=string_dup(name);struct parts_message_window candidate=*m;
    if(mark){if(flat)candidate.mark_flat_name=copy;else{candidate.mark_cg_name=copy;candidate.loop_start=start;candidate.loop_count=count;candidate.loop_ms=ms;}}
    else{if(flat)candidate.flat_name=copy;else candidate.cg_name=copy;}
    struct parts_state image;
    bool ok=mw_image(p,mark?candidate.mark_cg_name:candidate.cg_name,mark?candidate.mark_flat_name:candidate.flat_name,
      mark?candidate.loop_start:0,mark?candidate.loop_count:0,mark?candidate.loop_ms:0,&image);
    struct mw_content text={0};
    if(ok && !mark){candidate.background=image;ok=mw_build(&candidate,m->text.source,&m->text,&text);}
    if(!ok){parts_state_reset(&image,PARTS_UNINITIALIZED);free_string(copy);return false;}
    struct parts_state *target=mark?&m->mark:&m->background;
    parts_state_reset(target,PARTS_UNINITIALIZED);*target=image;
    free_string(*field);*field=copy;
    if(mark){if(!flat){m->loop_start=start;m->loop_count=count;m->loop_ms=ms;}m->mark.common.origin_offset=(Point){0,0};}
    else{mw_content_free(&m->text);m->text=text;mw_sync_dims(p);}
    parts_dirty(p);return true;
}
static void mw_choose_image(int no,bool mark,bool flat,struct string *name,int start,int count,int ms) {
    if(!parts_message_window_set_image(no,mark,flat,name,start,count,ms))ERROR("Invalid message window image or resource/layout failure");
}
static bool mw_seek(int no,int frame) {
    struct parts_message_window *m=mw_lookup(no);if(!m || m->background.type!=PARTS_FLAT)return false;
    struct parts *p=mw_owner(no);bool ok=parts_flat_seek(&m->background.flat,frame);if(ok)parts_dirty(p);return ok;
}
bool parts_message_window_update(struct parts *p,int elapsed) {
    struct parts_message_window *m=mw_lookup(p->no);if(!m || elapsed<=0 || !p->global.show || !parts_message_window_show || !PE_controller_visible(p->controller_no))return false;
    bool dirty=false;
    if(m->background.type==PARTS_FLAT)dirty|=parts_flat_update(&m->background.flat,elapsed);
    if(m->mark_show && m->mark.type==PARTS_FLAT)dirty|=parts_flat_update(&m->mark.flat,elapsed);
    if(m->mark_show && m->mark.type==PARTS_ANIMATION && m->mark.anim.nr_frames && m->mark.anim.frame_time) {
        struct parts_animation *a=&m->mark.anim;uint64_t t=(uint64_t)a->elapsed+(unsigned)elapsed;
        unsigned frames=t/a->frame_time;a->elapsed=t%a->frame_time;
        if(frames){a->current_frame=(a->current_frame+frames)%a->nr_frames;a->common.texture=a->frames[a->current_frame];dirty=true;}
    }
    m->text.elapsed_ms=fmin(m->text.total_ms,m->text.elapsed_ms+elapsed);
    size_t n=m->text.visible;while(n<m->text.count && m->text.glyphs[n].due_ms<=m->text.elapsed_ms)n++;
    if(n!=m->text.visible){if(!mw_paint(&m->text,n))ERROR("Message text reveal GL publication failed");dirty=true;}
    return dirty;
}
bool parts_message_window_slots(struct parts_message_window *m,Texture **slots,size_t *used,size_t limit) {
    if(!m)return true;
    if(!parts_texture_slots_state(&m->background,slots,used,limit) || !parts_texture_slots_state(&m->mark,slots,used,limit) ||
      !parts_texture_slots_add(slots,used,limit,&m->text.canvas))return false;
    for(size_t i=0;i<m->text.count;i++)if(!parts_texture_slots_add(slots,used,limit,&m->text.glyphs[i].texture))return false;
    return true;
}
#include "message_window_save.inc"
static void mw_save_image_state(struct iarray_writer *w,struct parts_state *s) {
    iarray_write(w,s->type);
    if(s->type==PARTS_FLAT){iarray_write_float(w,s->flat.fps_rate);parts_flat_save_state(&s->flat,w);}
    if(s->type==PARTS_ANIMATION){iarray_write(w,s->anim.elapsed);iarray_write(w,s->anim.current_frame);}
}
static bool mw_read_image_state(struct iarray_reader *r,struct parts_state *s) {
    if(iarray_read(r)!=s->type || r->error)return false;
    if(s->type==PARTS_FLAT) {
        float rate=iarray_read_float(r);
        if(!isfinite(rate) || rate<0 || rate>1000)return false;
        s->flat.fps_rate=rate;
        if(!parts_flat_load_state(&s->flat,r))return false;
    }
    if(s->type==PARTS_ANIMATION) {
        int elapsed=iarray_read(r),frame=iarray_read(r);
        if(elapsed<0 || frame<0 || (unsigned)elapsed>=s->anim.frame_time || (unsigned)frame>=s->anim.nr_frames)return false;
        s->anim.elapsed=elapsed;s->anim.current_frame=frame;s->anim.common.texture=s->anim.frames[frame];
    }
    return !r->error;
}
void parts_message_window_save(struct iarray_writer *w,struct parts_message_window *m) {
    mw_save_settings(w,m);mw_save_image_state(w,&m->background);mw_save_image_state(w,&m->mark);
}
struct parts_message_window *parts_message_window_load(struct parts *owner,struct iarray_reader *r) {
    struct parts_message_window *m=calloc(1,sizeof(*m));if(!m){r->error=1;return NULL;}live_windows++;
    size_t visible=0;double elapsed=0;
    if(!mw_read_settings(r,m,&visible,&elapsed))goto fail;
    if(!mw_image(owner,m->cg_name,m->flat_name,0,0,0,&m->background) ||
       !mw_image(owner,m->mark_cg_name,m->mark_flat_name,m->loop_start,m->loop_count,m->loop_ms,&m->mark))goto fail;
    m->mark.common.origin_offset=(Point){0,0};
    if(!mw_read_image_state(r,&m->background) || !mw_read_image_state(r,&m->mark))goto fail;
    struct mw_content next;
    if(!mw_build(m,m->text.source,NULL,&next))goto fail;
    if(visible>next.count || visible<next.visible || elapsed>next.total_ms ||
       (visible && elapsed<next.glyphs[visible-1].due_ms) ||
       (visible<next.count && elapsed>=next.glyphs[visible].due_ms)) {mw_content_free(&next);goto fail;}
    if(!mw_paint(&next,visible)){mw_content_free(&next);goto fail;}
    next.elapsed_ms=elapsed;mw_content_free(&m->text);m->text=next;
    return m;
fail:r->error=1;parts_message_window_free(m);return NULL;
}
#include "message_window_api.inc"
