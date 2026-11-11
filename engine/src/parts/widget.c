/* Copyright (C) 2026 xsystem4 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <math.h>
#include <float.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/cg.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "asset_manager.h"
#include "parts.h"
#include "input.h"
#include "xsystem4.h"
#include "vm.h"
#include "parts_internal.h"
#include "textbox_internal.h"
#include "../hll/iarray.h"
#include "widget_scroll_geometry.h"
#include "widget_skin_candidates.h"

enum { IMAGE_BODY=0, IMAGE_CHECK=4, IMAGE_THUMB=7, IMAGE_PREVIOUS=11,
    IMAGE_NEXT=15, IMAGE_BACKGROUND=19 };
static const char *states[] = {"／通常", "／オン", "／ダウン", "／無効"};

void parts_widget_font_set(struct text_style *f,int type,int size,int r,int g,int b,
    float bold,int er,int eg,int eb,float edge)
{
    *f=(struct text_style){.face=type,.size=max(1,min(512,size)),.weight=FW_NORMAL,
        .bold_width=isfinite(bold)?max(0.f,min(16.f,bold)):0.f,
        .edge_left=edge,.edge_right=edge,.edge_up=edge,.edge_down=edge,
        .color={r,g,b,255},.edge_color={er,eg,eb,255},.scale_x=1.f,.space_scale_x=1.f};
}
void parts_widget_font_get(struct text_style *f,int *type,int *size,int *r,int *g,int *b,
    float *bold,int *er,int *eg,int *eb,float *edge)
{
    *type=f->face; *size=f->size; *r=f->color.r; *g=f->color.g; *b=f->color.b;
    *bold=f->bold_width; *er=f->edge_color.r; *eg=f->edge_color.g; *eb=f->edge_color.b;
    *edge=f->edge_left;
}
void parts_widget_free(struct parts_widget *w)
{
    if (!w) return;
    parts_textbox_free(w);
    parts_widget_scroll_art_free(w);
    for(int role=0;role<4;role++) {
        if(w->scroll_flat_override[role])free_string(w->scroll_flat_override[role]);
        if(w->scroll_cg_override[role])free_string(w->scroll_cg_override[role]);
    }
    for (unsigned i=0;i<20;i++) gfx_delete_texture(&w->images[i]);
    for(int i=0;i<w->list.count;i++)free_string(w->list.items[i]);
    free(w->list.items);
    free_string(w->text); free_string(w->cg_name);free_string(w->flat_name); free(w->members); free(w);
}
static void widget_create(struct parts *p,int type,bool reset_states)
{
    parts_textures_require_resident(p);
    if (p->widget && p->widget->type==type) return;
    parts_widget_free(p->widget);
    struct parts_widget *w=p->widget=xcalloc(1,sizeof(*w));
    w->type=type; w->enabled=true; w->dirty=true; w->width=80; w->height=24;
    w->color=(SDL_Color){200,200,200,255}; w->text_origin=5;
    w->text=string_ref(&EMPTY_STRING); w->cg_name=string_ref(&EMPTY_STRING);
    w->flat_name=string_ref(&EMPTY_STRING);
    w->total=100; w->view=1; w->step=1; w->pressed_region=-1; w->hover_region=-1;
    for(int i=0;i<4;i++)w->scroll_active[i]=-1;
    parts_widget_font_set(&w->font,0,22,0,0,0,0,0,0,0,0);
    if(reset_states)for (int i=0;i<PARTS_NR_STATES;i++) parts_state_reset(&p->states[i],PARTS_CG);
    p->clickable=type!=PARTS_WIDGET_RADIO && type!=PARTS_WIDGET_PANEL;
    p->pass_cursor=type==PARTS_WIDGET_RADIO;
    if(type==5 || type==7 || type==PARTS_WIDGET_SPINBOX)parts_textbox_init(p);
    if(type==PARTS_WIDGET_SPINBOX) {
        w->spin.min=-FLT_MAX;w->spin.max=FLT_MAX;w->spin.increment=1;
        w->textbox->padding[3]+=14;parts_widget_spin_refresh(p);
    }
    if(type==PARTS_WIDGET_FORM) {
        w->width=320;w->height=240;w->form.min_w=50;w->form.min_h=24;
        w->form.max_w=w->form.max_h=2000;w->form.caption=(SDL_Color){50,50,50,255};
        w->color=(SDL_Color){120,120,120,255};w->font.color=(SDL_Color){255,255,255,255};
    }
    if(type==PARTS_WIDGET_LISTBOX) {
        w->width=120;w->height=100;w->list.line_height=26;w->list.selected=-1;
    }
    parts_component_dirty(p);
}
void parts_widget_create(struct parts *p,int type)
{widget_create(p,type,true);}
struct parts_widget *parts_widget_get(int no,int type)
{
    struct parts *p=parts_get(no);
    parts_widget_create(p,type);
    return p->widget;
}
static bool image_load(Texture *t,struct string *prefix,const char *suffix)
{
    struct string *name=string_dup(prefix);
    char *encoded=utf2sjis(suffix,0);
    string_append_cstr(&name,encoded,strlen(encoded)); free(encoded);
    struct cg *cg=asset_cg_load_by_name(name->text,NULL); free_string(name);
    if (!cg) return false;
    gfx_init_texture_with_cg(t,cg); cg_free(cg); return true;
}
static struct string *button_skin_name(struct string *prefix,struct widget_skin_candidate c)
{
    const char *local[]={"／一般","／オン","／DOWN","／无效"};
    const char *japanese[]={"／通常","／オン","／ダウン","／無効"};
    struct string *name=string_dup(prefix);
    if(c.checked){char *mark=utf2sjis(c.localized?"／检查":"／チェック",0);string_append_cstr(&name,mark,strlen(mark));free(mark);}
    char *suffix=utf2sjis(c.localized?local[c.state]:japanese[c.state],0);
    string_append_cstr(&name,suffix,strlen(suffix));free(suffix);return name;
}
static bool button_image_load(Texture *out,struct string *prefix,bool checked,int state)
{
    struct widget_skin_candidate candidates[8];size_t count=widget_skin_candidates(checked,state,candidates);
    for(size_t i=0;i<count;i++) {
        struct string *name=button_skin_name(prefix,candidates[i]);
        struct cg *cg=asset_cg_load_by_name(name->text,NULL);free_string(name);
        if(!cg)continue;
        gfx_init_texture_with_cg(out,cg);cg_free(cg);return out->handle!=0;
    }
    return false;
}
static struct string *button_flat_name(struct string *prefix,bool checked,int state)
{
    struct widget_skin_candidate candidates[8];size_t count=widget_skin_candidates(checked,state,candidates);
    for(size_t i=0;i<count;i++) {
        struct string *name=button_skin_name(prefix,candidates[i]);
        if(PE_ExistsFlatFile(name))return name;
        free_string(name);
    }
    return NULL;
}

bool parts_widget_set_cg(struct parts *p,struct string *name)
{
    if (!p || !p->widget || !name) return false;
    if(p->widget->type==2 || p->widget->type==3)
        return parts_widget_set_scroll_skin(p,&EMPTY_STRING,name,NULL,NULL);
    parts_textures_require_resident(p);
    struct parts_widget *w=p->widget;
    if(w->textbox) {
        return parts_textbox_set_skin(p,name);
    }
    Texture images[20]={0};
    if (name->size) {
        if (w->type==0 || w->type==1) {
            for(int i=0;i<4;i++)button_image_load(&images[IMAGE_BODY+i],name,false,i);
            if(w->type==1)for(int i=0;i<3;i++)button_image_load(&images[IMAGE_CHECK+i],name,true,i);
        } else if (w->type==2 || w->type==3) {
            const char *roles[]={"／バー",w->type==2?"／上按钮":"／左按钮",w->type==2?"／下按钮":"／右按钮"};
            for (int j=0;j<3;j++) for (int i=0;i<4;i++) {
                char suffix[128]; snprintf(suffix,sizeof(suffix),"%s%s",roles[j],states[i]);
                image_load(&images[IMAGE_THUMB+j*4+i],name,suffix);
            }
            image_load(&images[IMAGE_BACKGROUND],name,"／背景");
        }
        bool ok=w->type<2 ? !!images[0].handle : !!images[IMAGE_THUMB].handle;
        if (!ok) {
            for (int i=0;i<20;i++) gfx_delete_texture(&images[i]);
            return false;
        }
    }
    if(w->flat_backed) {
        w->flat_backed=false;
        for(int i=0;i<3;i++) parts_state_reset(&p->states[i],PARTS_CG);
    }
    parts_widget_scroll_art_free(w);
    free_string(w->flat_name);w->flat_name=string_ref(&EMPTY_STRING);
    for (int i=0;i<20;i++) { gfx_delete_texture(&w->images[i]); w->images[i]=images[i]; }
    free_string(w->cg_name); w->cg_name=string_dup(name); w->dirty=true;
    // With named images the native widgets use their intrinsic button size;
    // the serialized dimensions describe the unskinned fallback.
    if (w->type<2 && images[0].handle) { w->width=images[0].w; w->height=images[0].h; }
    parts_component_dirty(p); parts_dirty(p); return true;
}
static int scroll_max(struct parts_widget *w) { return max(0,w->total-w->view); }
float parts_widget_rate(struct parts_widget *w)
{
    int limit=scroll_max(w); return limit ? (float)w->pos/limit : 0.f;
}
void parts_widget_scroll(struct parts *p,int pos)
{
    struct parts_widget *w=p->widget;
    int value=max(0,min(scroll_max(w),pos));
    if (value==w->pos) return;
    w->pos=value; w->dirty=true; parts_dirty(p);
    if (!p->is_back_scene) parts_msg_push(p,PARTS_MSG_SCROLL,"ii",value,w->total);
}
void parts_widget_set_rate(struct parts *p,float rate)
{
    if (!isfinite(rate)) return;
    parts_widget_scroll(p,(int)lroundf(max(0.f,min(1.f,rate))*scroll_max(p->widget)));
}
void parts_widget_set_scroll_pos(struct parts *p,int pos)
{
    struct parts_widget *w=p->widget;
    int previous=w->pos;
    bool first=!w->scroll_pos_assigned;
    w->scroll_pos_assigned=true;
    parts_widget_scroll(p,pos);
    // Short content still needs its first zero-position notification. Later
    // assignments of the current value are often a listener echo; repeating
    // that event would keep the script's message-draining loop alive forever.
    if (first && w->pos==previous && !p->is_back_scene)
        parts_msg_push(p,PARTS_MSG_SCROLL,"ii",w->pos,w->total);
}
static void set_checked(struct parts *p,bool checked)
{
    struct parts_widget *w=p->widget;
    if (w->checked==checked) return;
    w->checked=checked; w->dirty=true; parts_dirty(p); scene_dirty();
    if (!p->is_back_scene) parts_msg_push(p,PARTS_MSG_CHANGED_FLAG,"b",checked);
}
void parts_widget_check(struct parts *p,bool checked)
{
    if (!p || !p->widget || p->widget->type!=1) return;
    if (checked && !p->is_back_scene) {
        struct parts *group;
        PARTS_LIST_FOREACH(group) {
            struct parts_widget *g=group->widget;
            if (!g || g->type!=9) continue;
            bool found=false;
            for (unsigned i=0;i<g->nr_members;i++) if(g->members[i]==p->no) found=true;
            if (!found) continue;
            for (unsigned i=0;i<g->nr_members;i++) {
                struct parts *other=parts_try_get(g->members[i]);
                if (other && other!=p && other->widget && other->widget->type==1) set_checked(other,false);
            }
        }
    }
    set_checked(p,checked);
}
void parts_widget_radio_add(struct parts *p,int child)
{
    struct parts_widget *w=p->widget;
    for (unsigned i=0;i<w->nr_members;i++) if(w->members[i]==child) return;
    w->members=xrealloc(w->members,(w->nr_members+1)*sizeof(int));
    w->members[w->nr_members++]=child;
}
bool parts_widget_visible(struct parts *p)
{
    if (!p->is_back_scene && !PE_controller_visible(p->controller_no)) return false;
    for (struct parts *q=p;q;q=q->parent) {
        if (!q->global.show) return false;
        if (!q->checkbox_show_link) continue;
        struct parts *check=q->is_back_scene?parts_back_scene_get(q->checkbox_show_link):parts_try_get(q->checkbox_show_link);
        /* An absent or non-checkbox source leaves the owner's visibility
         * unchanged. The linked part can acquire a checkbox later. */
        if (check && check->widget && check->widget->type==PARTS_WIDGET_CHECKBOX &&
            !check->widget->checked) return false;
    }
    return true;
}
static struct widget_scroll_geometry geometry(struct parts_widget *w)
{
    int intrinsic=w->type==2?w->images[IMAGE_THUMB].h:w->images[IMAGE_THUMB].w;
    if(w->scroll_flats[IMAGE_THUMB]) intrinsic=w->type==2?
        w->scroll_flats[IMAGE_THUMB]->common.h:w->scroll_flats[IMAGE_THUMB]->common.w;
    return widget_scroll_layout(w->type==2?w->height:w->width,w->previous_size,w->next_size,
        w->total,w->view,w->pos,intrinsic,w->type==2);
}
#include "widget_scroll.inc"
#include "widget_spin_form.inc"
static void draw_image(Texture *dst,Texture *src,int x,int y,int w,int h)
{
    if (src->handle && w>0 && h>0)
        gfx_copy_stretch_alpha_blend(dst,x,y,w,h,src,0,0,src->w,src->h);
}
static void fill_opaque(Texture *dst,int x,int y,int w,int h,int r,int g,int b)
{
    gfx_fill(dst,x,y,w,h,r,g,b);
    gfx_fill_amap(dst,x,y,w,h,255);
}
static Texture *state_image(struct parts_widget *w,int base,int state)
{
    Texture *t=&w->images[base+state];
    return t->handle?t:&w->images[base];
}
static int text_width(struct parts_widget *w,int *lines)
{
    int width=0; *lines=0; const char *s=w->text->text;
    while (*s) {
        const char *end=strchr(s,'\n'); size_t len=end?(size_t)(end-s):strlen(s);
        struct string *line=make_string(s,len);
        width=max(width,(int)ceilf(gfx_size_text(&w->font,line->text)));
        free_string(line); (*lines)++;
        if (!end) break;
        s=end+1;
    }
    return width;
}
static void draw_text(Texture *dst,struct parts_widget *w,int x,int y,int width,int height,bool blend)
{
    const char *s=w->text->text; int lines; text_width(w,&lines);
    int line_height=text_style_height(&w->font)+w->line_space;
    int all_height=lines?lines*line_height-w->line_space:0;
    int row=(w->text_origin-1)/3;
    y+=row==1?(height-all_height)/2:row==2?height-all_height:0;
    while (*s) {
        const char *end=strchr(s,'\n'); size_t len=end?(size_t)(end-s):strlen(s);
        struct string *line=make_string(s,len);
        int tw=ceilf(gfx_size_text(&w->font,line->text));
        int col=(w->text_origin-1)%3;
        int dx=x+(col==1?(width-tw)/2:col==2?width-tw:0);
        gfx_render_textf(dst,dx,y,line->text,&w->font,blend);
        free_string(line); y+=line_height;
        if (!end) break;
        s=end+1;
    }
}
#include "widget_textbox.inc"
#include "widget_form_listbox.inc"

bool parts_widget_set_flat(struct parts *p,struct string *name)
{
    if (!p || !p->widget || !name) return false;
    parts_textures_require_resident(p);
    struct parts_widget *w=p->widget;
    if(w && (w->type==2 || w->type==3))return parts_widget_set_scroll_skin(p,name,&EMPTY_STRING,NULL,NULL);
    if(!w || w->type>1 || !name->size) return false;
    // Refuse a missing skin before changing the current prefix or cached state.
    // Decoding and materialization remain the responsibility of the FLAT setter.
    bool checked=w->type==1 && w->checked && w->enabled;
    for(int state=0;state<3;state++) {
        struct string *selected=button_flat_name(name,checked,w->enabled?state:3);
        if(!selected)return false;
        free_string(selected);
    }
    free_string(w->flat_name);w->flat_name=string_dup(name);
    w->flat_backed=true;w->flat_enabled=!w->enabled;w->flat_checked=!w->checked;
    w->dirty=true;return parts_widget_build(p);
}
bool parts_widget_build(struct parts *p)
{
    if(p->parked_textures) return true;
    struct parts_widget *w=p->widget;
    if (!w || !w->dirty) return true;
    if(w->textbox)return parts_textbox_build(p);
    if(w->flat_backed) {
        if(w->flat_enabled!=w->enabled || w->flat_checked!=w->checked) {
            struct string *selected[3]={0};
            bool checked=w->type==1 && w->checked && w->enabled;
            for(int state=0;state<3;state++) {
                selected[state]=button_flat_name(w->flat_name,checked,w->enabled?state:3);
                if(!selected[state]){for(int j=0;j<3;j++)if(selected[j])free_string(selected[j]);return false;}
            }
            bool ok=true;
            for(int state=0;state<3;state++) {
                if(ok)ok=PE_SetPartsFlat(p->no,selected[state],state+1);
                free_string(selected[state]);
            }
            if(!ok)return false;
            w->width=p->states[0].common.w;w->height=p->states[0].common.h;w->flat_enabled=w->enabled;w->flat_checked=w->checked;
        }
        w->font.font_spacing=w->char_space;
        bool text_active=(w->type==1 && !w->button_mode) || w->flat_text_overlay;
        int lines=0,tw=text_active?text_width(w,&lines):0,width=w->width,height=w->height;
        bool label=w->type==1 && !w->button_mode && tw>0;
        if(label){width+=tw+4;height=max(height,(int)(text_style_height(&w->font)*lines));}
        if(width<1 || width>8192 || height<1 || height>4096)return false;
        for(int state=0;state<3;state++) {
            struct parts_common *c=&p->states[state].common;gfx_delete_texture(&c->texture);
            if(text_active && w->text->size) {
                gfx_init_texture_rgba(&c->texture,width,height,(SDL_Color){0,0,0,0});
                int origin=w->text_origin;if(label)w->text_origin=4;
                draw_text(&c->texture,w,label?w->width+4:2,0,label?tw:max(1,width-4),height,false);
                w->text_origin=origin;
            }
            parts_set_dims(p,c,width,height);
        }
        w->dirty=false;parts_component_dirty(p);parts_dirty(p);return true;
    }
    if (w->type==9) { w->dirty=false; return true; }
    w->width=max(1,min(4096,w->width)); w->height=max(1,min(4096,w->height));
    w->font.font_spacing=w->char_space;
    int lines=0,tw=w->type<2?text_width(w,&lines):0;
    int width=w->width,height=w->height;
    bool label=w->type==1 && !w->button_mode && tw>0;
    if (label) { width+=tw+4; height=max(height,(int)(text_style_height(&w->font)*lines)); }
    if (width>8192 || height>4096) return false;
    for (int state=0;state<3;state++) {
        struct parts_common *common=&p->states[state].common;
        gfx_delete_texture(&common->texture);
        gfx_init_texture_rgba(&common->texture,width,height,(SDL_Color){0,0,0,0});
        if(w->type==PARTS_WIDGET_FORM) {
            int caption=min(height,24+w->form.caption_plus);
            fill_opaque(&common->texture,0,0,width,height,w->color.r,w->color.g,w->color.b);
            fill_opaque(&common->texture,0,0,width,caption,w->form.caption.r,w->form.caption.g,w->form.caption.b);
            int origin=w->text_origin;w->text_origin=4;
            draw_text(&common->texture,w,4,0,max(1,width-8),caption,false);w->text_origin=origin;
            if(w->form.close_enabled)parts_form_draw_close(&common->texture,w,caption);
        } else if(w->type==PARTS_WIDGET_LISTBOX) {
            parts_listbox_draw(&common->texture,w);
        } else if (w->type==PARTS_WIDGET_PANEL) {
            if(!w->panel_layout_only) {
                gfx_fill(&common->texture,0,0,width,height,w->color.r,w->color.g,w->color.b);
                gfx_fill_amap(&common->texture,0,0,width,height,w->color.a);
            }
        } else if (w->type<2) {
            int visual=w->enabled?state:3;
            Texture *image=state_image(w,IMAGE_BODY,visual);
            if (w->type==1 && w->checked && visual<3) image=state_image(w,IMAGE_CHECK,visual);
            if (image->handle) {
                if (w->color_set) {
                    // The font-colour skin is a frame with a transparent centre.
                    // SetColor supplies its background, preserving the frame RGB.
                    fill_opaque(&common->texture,0,0,w->width,w->height,w->color.r,w->color.g,w->color.b);
                    draw_image(&common->texture,image,0,0,w->width,w->height);
                } else gfx_copy_stretch_with_alpha_map(&common->texture,0,0,w->width,w->height,image,0,0,image->w,image->h);
            }
            else {
                int shade=w->enabled?(state==2?-32:state==1?16:0):-64;
                fill_opaque(&common->texture,0,0,w->width,w->height,
                    max(0,min(255,w->color.r+shade)),max(0,min(255,w->color.g+shade)),max(0,min(255,w->color.b+shade)));
                if (w->type==1 && w->checked)
                    gfx_fill(&common->texture,4,4,max(1,w->width-8),max(1,w->height-8),32,80,32);
            }
            int origin=w->text_origin;
            if (label) w->text_origin=4;
            draw_text(&common->texture,w,label?w->width+4:2,0,label?tw:max(1,width-4),height,!label);
            w->text_origin=origin;
        } else {
            struct widget_scroll_geometry g=geometry(w);
            Texture *back=&w->images[IMAGE_BACKGROUND];
            if (back->handle) gfx_copy_stretch_with_alpha_map(&common->texture,0,0,width,height,back,0,0,back->w,back->h);
            else fill_opaque(&common->texture,0,0,width,height,80,80,80);
            const int bases[]={IMAGE_PREVIOUS,IMAGE_NEXT,IMAGE_THUMB};
            const int starts[]={0,g.length-g.next,g.start};
            const int lengths[]={g.previous,g.next,g.thumb};
            for (int i=0;i<3;i++) {
                int region=i+1;
                int visual=!scroll_max(w)?3:(w->pressed_region==region?2:w->hover_region==region?1:0);
                Texture *image=state_image(w,bases[i],visual);
                int x=w->type==2?0:starts[i],y=w->type==2?starts[i]:0;
                int dx=w->type==2?width:lengths[i],dy=w->type==2?lengths[i]:height;
                if (image->handle) draw_image(&common->texture,image,x,y,dx,dy);
                else if (lengths[i]>0) fill_opaque(&common->texture,x,y,dx,dy,170,170,170);
            }
        }
        parts_set_dims(p,common,width,height);
    }
    w->dirty=false;
    if (p->parent && p->parent->states[0].type==PARTS_LAYOUT_BOX) parts_component_dirty(p->parent);
    parts_component_dirty(p); parts_dirty(p); return true;
}
#include "scroll_links.inc"

void parts_widgets_prepare(void)
{
    struct parts *p;
    PARTS_LIST_FOREACH(p) parts_form_listbox_update(p);
    PARTS_LIST_FOREACH(p) if (p->widget && p->widget->dirty) parts_widget_build(p);
    parts_widget_prepare_size_links();
    PARTS_LIST_FOREACH(p) if (p->widget && p->widget->dirty) parts_widget_build(p);
}
void parts_widget_mouse(struct parts *p,Point cursor,bool down,bool pressed,bool released)
{
    struct parts_widget *w=p->widget;
    if (!w || !w->enabled) return;
    if(w->type==PARTS_WIDGET_FORM) {parts_form_mouse(p,cursor,down,pressed,released);return;}
    if(w->type==PARTS_WIDGET_LISTBOX) {
        if(released && p->is_hovered) {
            int index=parts_listbox_cursor_index(p,cursor);
            if(index>=0)parts_listbox_select(p,index);
        }
        return;
    }
    if(w->type==PARTS_WIDGET_SPINBOX && parts_widget_spin_mouse(p,cursor,down,pressed))return;
    if(w->textbox) {
        if(pressed || (down && w->pressed_region==3))parts_textbox_mouse(p,cursor,!pressed,key_is_down(VK_SHIFT));
        if(pressed)w->pressed_region=3;
        if(!down)w->pressed_region=-1;
        return;
    }
    if (w->type<2) {
        if (released && p->is_hovered && w->type==1 && (!w->checked || !w->button_mode))
            parts_widget_check(p,!w->checked);
        return;
    }
    if (w->type>3) return;
    struct widget_scroll_geometry g=geometry(w);
    float scale=w->type==2?p->global.scale.y:p->global.scale.x;
    if (fabsf(scale)<0.0001f) return;
    struct parts_common *c=&p->states[0].common;
    int coordinate=w->type==2 ? cursor.y-p->global.pos.y-c->origin_offset.y : cursor.x-p->global.pos.x-c->origin_offset.x;
    coordinate=(int)lroundf(coordinate/scale);
    int region=coordinate<g.previous?1:coordinate>=g.length-g.next?2:
        coordinate>=g.start && coordinate<g.start+g.thumb?3:0;
    int hover=p->is_hovered?region:-1;
    if (hover!=w->hover_region) { w->hover_region=hover; w->dirty=true; }
    if (pressed) {
        w->pressed_region=region; w->repeat_at=SDL_GetTicks()+350; w->dirty=true;
        if (region==3) w->drag_offset=coordinate-g.start;
        else if (region==1 || region==2) parts_widget_scroll(p,w->pos+(region==1?-w->step:w->step));
        else parts_widget_scroll(p,w->pos+(coordinate<g.start?-max(1,w->view):max(1,w->view)));
    }
    if (down && w->pressed_region==3 && g.travel>0)
        parts_widget_set_rate(p,(float)(coordinate-g.previous-w->drag_offset)/g.travel);
    if (down && (w->pressed_region==1 || w->pressed_region==2)
            && SDL_TICKS_PASSED(SDL_GetTicks(),w->repeat_at)) {
        parts_widget_scroll(p,w->pos+(w->pressed_region==1?-w->step:w->step));
        w->repeat_at=SDL_GetTicks()+75;
    }
    if (!down && w->pressed_region!=-1) { w->pressed_region=-1; w->dirty=true; }
    if (w->dirty) parts_dirty(p);
}

void parts_widget_save(struct iarray_writer *out,struct parts *p)
{
    struct parts_widget *w=p->widget;
    iarray_write(out,p->has_focus); iarray_write(out,p->checkbox_show_link);
    iarray_write(out,p->normal_sound);
    iarray_write(out,w?w->type:-1);
    if (!w) return;
    iarray_write(out,w->flat_backed);
    if(w->flat_backed){iarray_write(out,w->flat_text_overlay);iarray_write_string(out,w->flat_name);}
    int values[]={w->width,w->height,w->enabled,w->checked,w->button_mode,w->color.r,w->color.g,w->color.b,
        w->char_space,w->line_space,w->text_origin,w->total,w->view,w->pos,w->step,w->previous_size,w->next_size};
    for (unsigned i=0;i<sizeof(values)/sizeof(*values);i++) iarray_write(out,values[i]);
    iarray_write(out,w->color.a);
    iarray_write(out,w->panel_layout_only);
    iarray_write_text_style(out,&w->font); iarray_write_string(out,w->text); iarray_write_string(out,w->scroll_art?&EMPTY_STRING:w->cg_name);
    iarray_write(out,w->color_set);
    iarray_write(out,w->nr_members);
    for (unsigned i=0;i<w->nr_members;i++) iarray_write(out,w->members[i]);
    parts_widget_spin_form_save(out,w);
    parts_listbox_save(out,w);
}
void parts_widget_load(struct iarray_reader *in,struct parts *p,int version)
{
    p->has_focus=iarray_read(in); p->checkbox_show_link=iarray_read(in);
    p->normal_sound=iarray_read(in);
    int type=iarray_read(in);
    if (type==-1) return;
    if (type!=0 && type!=1 && type!=2 && type!=3 && type!=4 && type!=5 && type!=6 && type!=7 && type!=9 && type!=PARTS_WIDGET_FORM && type!=PARTS_WIDGET_LISTBOX) { in->error=1; return; }
    if(type==PARTS_WIDGET_LISTBOX && version<28){in->error=1;return;}
    int flat=version>=17?iarray_read(in):0;
    if(flat<0 || flat>1 || (flat && type>1)){in->error=1;return;}
    int overlay=flat?iarray_read(in):0;
    if(overlay<0 || overlay>1){in->error=1;return;}
    struct string *flat_name=flat?iarray_read_string(in):NULL;
    if(flat)for(int i=0;i<3;i++)if(p->states[i].type!=PARTS_FLAT || !p->states[i].flat.flat){free_string(flat_name);in->error=1;return;}
    // The constructor's input defaults must not overwrite saved caller settings.
    bool clickable=p->clickable,pass_cursor=p->pass_cursor;
    widget_create(p,type,!flat); struct parts_widget *w=p->widget;
    if(flat){free_string(w->flat_name);w->flat_name=flat_name;w->flat_backed=true;w->flat_text_overlay=overlay;}
    p->clickable=clickable; p->pass_cursor=pass_cursor;
    int values[17]; for (int i=0;i<17;i++) values[i]=iarray_read(in);
    int alpha=version>=23?iarray_read(in):255;
    if(alpha<0 || alpha>255){in->error=1;return;}
    int layout_only=version>=23?iarray_read(in):0;
    if(layout_only<0 || layout_only>1 || (layout_only && type!=PARTS_WIDGET_PANEL)){in->error=1;return;}
    w->panel_layout_only=layout_only;
    w->width=values[0]; w->height=values[1]; w->enabled=values[2]; w->checked=values[3]; w->button_mode=values[4];
    w->color=(SDL_Color){values[5],values[6],values[7],alpha}; w->char_space=values[8]; w->line_space=values[9];
    w->text_origin=values[10]; w->total=values[11]; w->view=values[12]; w->pos=values[13]; w->step=values[14];
    w->previous_size=values[15]; w->next_size=values[16];
    iarray_read_text_style(in,&w->font);
    free_string(w->text); w->text=iarray_read_string(in);
    struct string *name=iarray_read_string(in);
    w->color_set=iarray_read(in);
    int members=iarray_read(in);
    if (members<0 || members>4096 || (unsigned)members>in->size-in->pos || w->width<1 || w->width>4096
            || w->height<1 || w->height>4096 || w->total<0 || w->view<0 || w->pos<0 || w->pos>scroll_max(w)
            || w->text_origin<1 || w->text_origin>9 || !isfinite(w->font.size) || w->font.size<1 || w->font.size>512) {
        in->error=1; free_string(name); return;
    }
    for (int i=0;i<members;i++) parts_widget_radio_add(p,iarray_read(in));
    if(!parts_widget_spin_form_load(in,w,version) || !parts_listbox_load(in,w)){free_string(name);return;}
    if (in->error || (flat ? name->size!=0 : !parts_widget_set_cg(p,name))) in->error=1;
    if(flat){w->flat_enabled=w->enabled;w->flat_checked=w->checked;}
    w->width=values[0]; w->height=values[1];
    if (!in->error && !parts_widget_build(p)) in->error=1;
    free_string(name);
}
