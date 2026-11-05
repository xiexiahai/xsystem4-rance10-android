/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Included by widget_textbox.inc after the widget drawing helpers. */
static void textbox_skin_free(Texture images[27])
{for(int i=0;i<27;i++)gfx_delete_texture(&images[i]);}

static bool textbox_skin_image(Texture *out,struct string *prefix,const char *suffix)
{
    struct string *name=string_dup(prefix);char *s=utf2sjis(suffix,0);
    string_append_cstr(&name,s,strlen(s));free(s);
    struct cg *cg=asset_cg_load_by_name(name->text,NULL);free_string(name);
    if(!cg)return false;
    bool valid=cg->metrics.w>0 && cg->metrics.h>0 && cg->metrics.w<=4096 && cg->metrics.h<=4096;
    if(valid)gfx_init_texture_with_cg(out,cg);
    cg_free(cg);return valid && out->handle;
}

bool parts_textbox_set_skin(struct parts *p,struct string *name)
{
    if(!p || !p->widget || !p->widget->textbox || !name || name->size>4096)return false;
    struct parts_widget *w=p->widget;Texture next[27]={0};
    struct string *retained=string_dup(name);
    if(name->size) {
        if(!textbox_skin_image(&next[0],name,"／背景"))goto fail;
        const char *axes[2][2]={{"／横向滚动条","／横スクロールバー"},{"／纵向滚动条","／縦スクロールバー"}};
        const char *roles[2][2][4]={
            {{"／栏","／左按钮","／右按钮","／背景"},{"／バー","／左ボタン","／右ボタン","／背景"}},
            {{"／栏","／上按钮","／下按钮","／背景"},{"／バー","／上ボタン","／下ボタン","／背景"}}};
        const char *states[2][4]={{"／一般","／オン","／DOWN","／无效"},{"／通常","／オン","／ダウン","／無効"}};
        for(int axis=0;axis<2;axis++)for(int role=0;role<4;role++)for(int state=0;state<(role==3?1:4);state++) {
            int slot=1+axis*13+role*4+state;
            for(int localized=0;localized<2 && !next[slot].handle;localized++) {
                char suffix[256];snprintf(suffix,sizeof(suffix),"%s%s%s",axes[axis][localized],roles[axis][localized][role],role==3?"":states[localized][state]);
                textbox_skin_image(&next[slot],name,suffix);
            }
            /* A family may omit hover/down/disabled art, but normal roles
             * must exist so a partly installed skin cannot become live. */
            if(!next[slot].handle && state==0)goto fail;
        }
    }
    parts_textures_require_resident(p);
    textbox_skin_free(w->textbox->skin);memcpy(w->textbox->skin,next,sizeof(next));
    free_string(w->cg_name);w->cg_name=retained;
    if(next[0].handle){w->width=next[0].w;w->height=next[0].h;}
    parts_textbox_dirty(p);return true;
fail:
    textbox_skin_free(next);free_string(retained);return false;
}

static int textbox_bar_size(struct parts_textbox *m,int axis)
{
    Texture *track=&m->skin[1+axis*13+12];
    return track->handle?(axis?track->w:track->h):12;
}
static int textbox_inner_width(struct parts_widget *w)
{struct parts_textbox *m=w->textbox;return max(1,w->width-m->padding[2]-m->padding[3]-2-(m->show_v?textbox_bar_size(m,1):0));}
static int textbox_inner_height(struct parts_widget *w)
{struct parts_textbox *m=w->textbox;return max(1,w->height-m->padding[0]-m->padding[1]-2-(m->show_h?textbox_bar_size(m,0):0));}

static Texture *textbox_role(struct parts_textbox *m,int axis,int role,int state)
{
    int base=1+axis*13+role*4;
    Texture *image=&m->skin[base+(role==3?0:state)];return image->handle?image:&m->skin[base];
}
static struct widget_scroll_geometry textbox_bar_geometry(struct parts_widget *w,int axis,int total,int view,int pos)
{
    struct parts_textbox *m=w->textbox;
    Texture *previous=textbox_role(m,axis,1,0),*next=textbox_role(m,axis,2,0),*thumb=textbox_role(m,axis,0,0);
    int length=(axis?w->height:w->width)-2;
    if(axis?m->show_h:m->show_v)length-=textbox_bar_size(m,!axis);
    return widget_scroll_layout(length,axis?previous->h:previous->w,axis?next->h:next->w,total,view,pos,axis?thumb->h:thumb->w,true);
}
static void textbox_draw_bar(Texture *canvas,struct parts_widget *w,int axis,int total,int view,int pos,int state)
{
    struct parts_textbox *m=w->textbox;
    struct widget_scroll_geometry g=textbox_bar_geometry(w,axis,total,view,pos);
    int breadth=textbox_bar_size(m,axis),x=axis?w->width-1-breadth:1,y=axis?1:w->height-1-breadth;
    int offsets[4]={g.start,0,g.length-g.next,g.previous},lengths[4]={g.thumb,g.previous,g.next,g.track};
    /* Track first, then end buttons and thumb. */
    const int order[]={3,1,2,0};
    for(int i=0;i<4;i++) {
        int role=order[i];Texture *image=textbox_role(m,axis,role,state);
        draw_image(canvas,image,x+(axis?0:offsets[role]),y+(axis?offsets[role]:0),axis?breadth:lengths[role],axis?lengths[role]:breadth);
    }
}
