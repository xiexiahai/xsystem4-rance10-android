/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Included by widget.c. UTF-8 editing stays separate from legacy game text. */
struct textbox_glyph {size_t start,end;int x,y,w;bool composing;char game[8];};
struct textbox_layout {
    struct textbox_glyph *glyphs;size_t count;
    int width,height,line_height,caret_x,caret_y;
};
static struct parts_textbox *textbox_owner;
static void textbox_commit_input(const char *text);
static void textbox_edit_input(const char *text,int start,int length);
static void textbox_key_input(int code);
#include "textbox_skin.inc"

void parts_textbox_dirty(struct parts *p)
{
    if(!p || !p->widget || !p->widget->textbox)return;
    p->widget->dirty=true;parts_component_dirty(p);parts_dirty(p);
}
static void textbox_sync(struct parts *p)
{
    struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;
    char *legacy=utf2sjis(m->buffer.text,0);
    bool changed=strcmp(w->text->text,legacy)!=0;
    free_string(w->text);w->text=cstr_to_string(legacy);free(legacy);
    if(w->type==PARTS_WIDGET_SPINBOX) {
        char *end;errno=0;float value=strtof(m->buffer.text,&end);
        bool numeric=*m->buffer.text && !*end && !errno && isfinite(value);
        if(numeric) {
            float parsed=value;value=max(w->spin.min,min(w->spin.max,value));
            changed=value!=w->spin.value;w->spin.value=value;
            if(value!=parsed)parts_widget_spin_refresh(p);
        } else changed=false;
    }
    if(changed && !p->is_back_scene)parts_msg_push(p,PARTS_MSG_CHANGED_TEXT,"");
    m->blink_ms=0;m->keep_caret_visible=true;parts_textbox_dirty(p);
}
void parts_textbox_init(struct parts *p)
{
    struct parts_textbox *m=p->widget->textbox=xcalloc(1,sizeof(*m));
    m->owner_no=p->no;
    if(!tb_init(&m->buffer,2000,p->widget->type==7))VM_ERROR("Cannot allocate textbox model");
    for(int i=0;i<4;i++)m->padding[i]=4;
    m->bg=(SDL_Color){255,255,255,255};m->readonly_bg=(SDL_Color){150,150,150,255};
    m->frame=(SDL_Color){100,100,100,255};m->selection=(SDL_Color){200,200,200,255};
    p->widget->width=160;p->widget->height=p->widget->type==7?60:28;
}
static bool textbox_release_ime(struct parts_textbox *m)
{
    if(textbox_owner!=m)return false;
    if(input_release_text_handlers(textbox_commit_input,textbox_edit_input,textbox_key_input))SDL_StopTextInput();
    textbox_owner=NULL;m->editing[0]=0;m->drag_axis=0;return true;
}
void parts_textbox_free(struct parts_widget *w)
{
    if(!w || !w->textbox)return;
    textbox_release_ime(w->textbox);tb_free(&w->textbox->buffer);
    for(int i=0;i<27;i++)gfx_delete_texture(&w->textbox->skin[i]);
    free(w->textbox);w->textbox=NULL;
}
bool parts_textbox_set_text(struct parts *p,struct string *text)
{
    if(!p || !p->widget || !p->widget->textbox || !text || text->size>262144 || memchr(text->text,0,text->size))return false;
    char *utf=sjis2utf(text->text,text->size);
    bool ok=tb_set(&p->widget->textbox->buffer,utf);free(utf);
    if(ok){p->widget->textbox->editing[0]=0;textbox_sync(p);}return ok;
}
bool parts_textbox_set_limit(struct parts *p,int limit)
{
    if(!p || !p->widget || !p->widget->textbox || limit<0 || limit>65536)return false;
    if(!tb_set_limit(&p->widget->textbox->buffer,limit))return false;
    textbox_sync(p);return true;
}
static struct parts *textbox_active_part(void)
{
    if(!textbox_owner)return NULL;
    struct parts *p=parts_try_get(textbox_owner->owner_no);
    return p && p->widget && p->widget->textbox==textbox_owner && !textbox_owner->readonly &&
        p->widget->enabled && !p->parked_textures && !p->is_back_scene && parts_widget_visible(p) && PE_IsFocus(p->no)?p:NULL;
}
bool parts_textbox_is_open(struct parts *p)
{
    return p && p->widget && p->widget->textbox==textbox_owner && textbox_active_part()==p &&
        input_owns_text_handlers(textbox_commit_input,textbox_edit_input,textbox_key_input) && SDL_IsTextInputActive();
}
bool parts_textbox_open(struct parts *p,bool open)
{
    if(!p || !p->widget || !p->widget->textbox)return false;
    struct parts_textbox *m=p->widget->textbox;
    if(!open){textbox_release_ime(m);
        if(p->widget->type==PARTS_WIDGET_SPINBOX) {
            if(!m->buffer.len)parts_widget_spin_value(p,p->widget->spin.def);
            parts_widget_spin_refresh(p);
        }
        parts_textbox_dirty(p);return true;}
    if(m->readonly || !p->widget->enabled || p->is_back_scene || p->parked_textures || !parts_widget_visible(p))return false;
    PE_SetFocus(p->no);if(!PE_IsFocus(p->no))return false;
    if(textbox_owner && textbox_owner!=m)textbox_release_ime(textbox_owner);
    if(!input_claim_text_handlers(textbox_commit_input,textbox_edit_input,textbox_key_input))return false;
    textbox_owner=m;
    int ww=config.view_width,hh=config.view_height;
    SDL_Window *window=SDL_GL_GetCurrentWindow();
    if(window)SDL_GetWindowSize(window,&ww,&hh);
    SDL_Rect rect={(int)((float)p->global.pos.x*ww/config.view_width),
        (int)((float)p->global.pos.y*hh/config.view_height),
        max(1,(int)((float)p->widget->width*ww/config.view_width)),
        max(1,(int)((float)p->widget->height*hh/config.view_height))};
    SDL_SetTextInputRect(&rect);SDL_StartTextInput();m->blink_ms=0;parts_textbox_dirty(p);return true;
}
static void textbox_commit_input(const char *text)
{
    struct parts *p=textbox_active_part();if(!p)return;
    struct parts_textbox *m=p->widget->textbox;
    if(tb_insert(&m->buffer,text)){m->editing[0]=0;textbox_sync(p);}
}
static void textbox_edit_input(const char *text,int start,int length)
{
    struct parts *p=textbox_active_part();if(!p)return;
    struct parts_textbox *m=p->widget->textbox;char *normal=NULL;size_t len,count;
    if(!tb_normalize(text,m->buffer.multiline,&normal,&len,&count))return;
    if(len<sizeof(m->editing)) {
        memcpy(m->editing,normal,len+1);m->editing_start=max(0,min((int)count,start));
        m->editing_length=max(0,min((int)count-m->editing_start,length));parts_textbox_dirty(p);
    }
    free(normal);
}
static struct textbox_layout textbox_layout(struct parts *p,int inner_width)
{
    struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;
    struct textbox_utf8 *b=&m->buffer;
    size_t edit_len=strlen(m->editing),len=b->len+edit_len;
    char *text=xmalloc(len+1);memcpy(text,b->text,b->cursor);memcpy(text+b->cursor,m->editing,edit_len);
    memcpy(text+b->cursor+edit_len,b->text+b->cursor,b->len-b->cursor+1);
    size_t caret=b->cursor;
    for(int i=0;i<m->editing_start && caret<b->cursor+edit_len;i++)caret=tb_next(text,len,caret);
    struct textbox_layout l={.line_height=max(1,text_style_height(&w->font)+w->line_space)};
    l.glyphs=xcalloc(tb_count(text,len)+1,sizeof(*l.glyphs));
    int x=0,y=0;bool caret_found=false;
    for(size_t i=0;i<len;) {
        size_t end=tb_next(text,len,i);char one[5]={0};memcpy(one,text+i,end-i);
        char *game=utf2sjis(one,0);
        int advance=text[i]=='\n'?0:max(1,(int)ceilf(gfx_size_text(&w->font,game))+w->char_space);
        if(b->multiline && !m->show_h && x>0 && x+advance>inner_width){x=0;y+=l.line_height;}
        if(i==caret){l.caret_x=x;l.caret_y=y;caret_found=true;}
        struct textbox_glyph *g=&l.glyphs[l.count++];
        g->start=i<b->cursor?i:i>=b->cursor+edit_len?i-edit_len:b->cursor;
        g->end=i<b->cursor?end:i>=b->cursor+edit_len?end-edit_len:b->cursor;
        g->composing=i>=b->cursor && i<b->cursor+edit_len;
        g->x=x;g->y=y;g->w=advance;snprintf(g->game,sizeof(g->game),"%s",game);free(game);
        if(text[i]=='\n'){x=0;y+=l.line_height;}else x+=advance;
        l.width=max(l.width,x);i=end;
    }
    if(!caret_found){l.caret_x=x;l.caret_y=y;}
    l.height=y+l.line_height;free(text);return l;
}
static size_t textbox_hit(struct textbox_layout *l,int x,int y,size_t end)
{
    if(y<0)return 0;
    size_t hit=end;bool row=false;
    for(size_t i=0;i<l->count;i++) {
        struct textbox_glyph *g=&l->glyphs[i];
        if(g->y<=y && y<g->y+l->line_height) {
            row=true;if(x<g->x+max(1,g->w)/2)return g->start;hit=g->end;
        } else if(row)break;
    }
    return hit;
}
static void textbox_key_input(int code)
{
    struct parts *p=textbox_active_part();if(!p)return;
    struct parts_textbox *m=p->widget->textbox;struct textbox_utf8 *b=&m->buffer;
    if(m->editing[0])return;
    if(p->widget->type==PARTS_WIDGET_SPINBOX && (code==VK_UP || code==VK_DOWN)) {
        double next=(double)p->widget->spin.value+(code==VK_UP?1.:-1.)*p->widget->spin.increment;
        parts_widget_spin_value(p,(float)max(p->widget->spin.min,min(p->widget->spin.max,next)));return;
    }
    bool shift=key_is_down(VK_SHIFT),ctrl=key_is_down(VK_CONTROL);
    if(ctrl && code==VK_A)tb_select_all(b);
    else if(code==VK_LEFT || code==VK_RIGHT) {
        if(!shift && b->cursor!=b->anchor)b->cursor=b->anchor=code==VK_LEFT?min(b->cursor,b->anchor):max(b->cursor,b->anchor);
        else tb_move(b,code==VK_LEFT?-1:1,shift);
    } else if(code==VK_HOME || code==VK_END) {
        if(ctrl){b->cursor=code==VK_HOME?0:b->len;if(!shift)b->anchor=b->cursor;}
        else tb_home(b,code==VK_END,shift);
    } else if(code==VK_UP || code==VK_DOWN) {
        int inner=textbox_inner_width(p->widget);
        struct textbox_layout l=textbox_layout(p,inner);
        b->cursor=textbox_hit(&l,l.caret_x,l.caret_y+(code==VK_UP?-l.line_height:l.line_height),b->len);
        if(!shift)b->anchor=b->cursor;
        free(l.glyphs);
    } else if(code==VK_BACK || code==VK_DELETE)tb_erase(b,code==VK_BACK);
    else if(code==VK_RETURN && b->multiline)tb_insert(b,"\n");
    else if(code==VK_RETURN || code==VK_ESCAPE){parts_textbox_open(p,false);return;}
    else return;
    textbox_sync(p);
}
void parts_textbox_mouse(struct parts *p,Point pos,bool drag,bool extend)
{
    struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;
    if(!m || p->is_back_scene)return;
    float sx=p->global.scale.x,sy=p->global.scale.y;if(fabsf(sx)<0.0001f || fabsf(sy)<0.0001f)return;
    float a=-p->local.rotation.z*0.017453292519943295f,dx=pos.x-p->global.pos.x,dy=pos.y-p->global.pos.y;
    Point origin=p->states[0].common.origin_offset;
    int x=(int)lroundf((cosf(a)*dx-sinf(a)*dy)/sx)-origin.x;
    int y=(int)lroundf((sinf(a)*dx+cosf(a)*dy)/sy)-origin.y;
    int iw=textbox_inner_width(w);
    int ih=textbox_inner_height(w);
    struct textbox_layout l=textbox_layout(p,iw);
    if(!drag)m->drag_axis=m->show_v && x>=w->width-1-textbox_bar_size(m,1)?2:m->show_h && y>=w->height-1-textbox_bar_size(m,0)?1:0;
    if(m->drag_axis) {
        int axis=m->drag_axis-1,total=axis?l.height:l.width,view=axis?ih:iw;
        int *scroll=axis?&m->scroll_y:&m->scroll_x,coordinate=(axis?y:x)-1;
        struct widget_scroll_geometry g=textbox_bar_geometry(w,axis,total,view,*scroll);
        if(!drag && (coordinate<g.previous || coordinate>=g.length-g.next)) {
            *scroll=max(0,min(max(0,total-view),*scroll+(coordinate<g.previous?-1:1)*l.line_height));m->drag_axis=0;
        } else {
            if(!drag)m->drag_offset=coordinate>=g.start && coordinate<g.start+g.thumb?coordinate-g.start:g.thumb/2;
            *scroll=g.travel?(int)((int64_t)max(0,min(g.travel,coordinate-g.previous-m->drag_offset))*max(0,total-view)/g.travel):0;
        }
    }
    else {
        m->editing[0]=0;
        m->buffer.cursor=textbox_hit(&l,x-m->padding[2]-1+m->scroll_x,y-m->padding[0]-1+m->scroll_y,m->buffer.len);
        if(!drag && !extend)m->buffer.anchor=m->buffer.cursor;
        m->keep_caret_visible=true;m->blink_ms=0;
        if(!m->readonly)parts_textbox_open(p,true);
    }
    free(l.glyphs);parts_textbox_dirty(p);
}
bool parts_textbox_update(struct parts *p,int passed_time)
{
    if(!p->widget || !p->widget->textbox || p->is_back_scene)return false;
    struct parts_textbox *m=p->widget->textbox;
    if(textbox_owner==m && (!textbox_active_part() || !input_owns_text_handlers(textbox_commit_input,textbox_edit_input,textbox_key_input))) {
        textbox_release_ime(m);if(p->widget->type==PARTS_WIDGET_SPINBOX)parts_widget_spin_refresh(p);parts_textbox_dirty(p);
    }
    if(!p->has_focus || m->readonly)return false;
    unsigned old=m->blink_ms/500;m->blink_ms=(m->blink_ms+(unsigned)max(0,passed_time))%1000;
    if(old!=m->blink_ms/500){parts_textbox_dirty(p);return true;}return false;
}
bool parts_textbox_build(struct parts *p)
{
    struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;
    if(!m || w->width<1 || w->height<1 || w->width>4096 || w->height>4096)return false;
    int iw=textbox_inner_width(w);
    int ih=textbox_inner_height(w);
    struct textbox_layout l=textbox_layout(p,iw);
    int maxx=m->show_h?max(0,l.width-iw):0,maxy=max(0,l.height-ih);
    if(m->keep_caret_visible) {
        if(l.caret_x<m->scroll_x)m->scroll_x=l.caret_x;
        if(l.caret_x+2>m->scroll_x+iw)m->scroll_x=l.caret_x+2-iw;
        if(l.caret_y<m->scroll_y)m->scroll_y=l.caret_y;
        if(l.caret_y+l.line_height>m->scroll_y+ih)m->scroll_y=l.caret_y+l.line_height-ih;
    }
    m->scroll_x=max(0,min(maxx,m->scroll_x));m->scroll_y=max(0,min(maxy,m->scroll_y));m->keep_caret_visible=false;
    SDL_Color bg=m->readonly?m->readonly_bg:m->bg;
    Texture content={0},canvas={0};gfx_init_texture_rgba(&content,iw,ih,bg);gfx_init_texture_rgba(&canvas,w->width,w->height,bg);
    if(m->skin[0].handle) {
        draw_image(&canvas,&m->skin[0],0,0,w->width,w->height);
        gfx_copy_with_alpha_map(&content,0,0,&canvas,1+m->padding[2],1+m->padding[0],min(iw,w->width-1),min(ih,w->height-1));
    }
    size_t lo=min(m->buffer.cursor,m->buffer.anchor),hi=max(m->buffer.cursor,m->buffer.anchor);
    for(size_t i=0;i<l.count;i++) {
        struct textbox_glyph *g=&l.glyphs[i];int x=g->x-m->scroll_x,y=g->y-m->scroll_y;
        if(y+l.line_height<0 || y>=ih || x+g->w<0 || x>=iw)continue;
        if(!g->composing && g->start>=lo && g->start<hi)
            fill_opaque(&content,x,y,max(3,g->w),l.line_height,m->selection.r,m->selection.g,m->selection.b);
        if(g->w)gfx_render_textf(&content,x,y,g->game,&w->font,true);
        if(g->composing)fill_opaque(&content,x,y+l.line_height-2,max(1,g->w),1,w->font.color.r,w->font.color.g,w->font.color.b);
    }
    if(p->has_focus && !m->readonly && m->blink_ms<500)
        fill_opaque(&content,l.caret_x-m->scroll_x,l.caret_y-m->scroll_y,1,l.line_height,w->font.color.r,w->font.color.g,w->font.color.b);
    int dx=1+m->padding[2],dy=1+m->padding[0];
    if(dx<w->width-1 && dy<w->height-1)
        gfx_copy_with_alpha_map(&canvas,dx,dy,&content,0,0,min(iw,w->width-1-dx),min(ih,w->height-1-dy));
    if(!m->skin[0].handle) {
    fill_opaque(&canvas,0,0,w->width,1,m->frame.r,m->frame.g,m->frame.b);
    fill_opaque(&canvas,0,w->height-1,w->width,1,m->frame.r,m->frame.g,m->frame.b);
    fill_opaque(&canvas,0,0,1,w->height,m->frame.r,m->frame.g,m->frame.b);
    fill_opaque(&canvas,w->width-1,0,1,w->height,m->frame.r,m->frame.g,m->frame.b);
    }
    if(w->type==PARTS_WIDGET_SPINBOX)parts_widget_spin_arrows(&canvas,w);
    if(!m->skin[0].handle && m->show_v && w->width>13 && w->height>2) {
        int track=w->height-2,thumb=min(track,max(8,(int)((int64_t)track*ih/max(ih,l.height))));
        fill_opaque(&canvas,w->width-13,1,12,track,210,210,210);
        int y=maxy?(int)((int64_t)(track-thumb)*m->scroll_y/maxy):0;
        fill_opaque(&canvas,w->width-12,1+y,10,thumb,100,100,100);
    }
    if(!m->skin[0].handle && m->show_h && w->height>13 && w->width>2) {
        int track=w->width-2,thumb=min(track,max(8,(int)((int64_t)track*iw/max(iw,l.width))));
        fill_opaque(&canvas,1,w->height-13,track,12,210,210,210);
        int x=maxx?(int)((int64_t)(track-thumb)*m->scroll_x/maxx):0;
        fill_opaque(&canvas,1+x,w->height-12,thumb,10,100,100,100);
    }
    for(int state=0;state<3;state++) {
        struct parts_common *c=&p->states[state].common;gfx_delete_texture(&c->texture);
        gfx_init_texture_rgba(&c->texture,w->width,w->height,bg);
        gfx_copy_with_alpha_map(&c->texture,0,0,&canvas,0,0,w->width,w->height);
        if(m->skin[0].handle) {
            int visual=w->enabled?state:3;
            if(m->show_v)textbox_draw_bar(&c->texture,w,1,l.height,ih,m->scroll_y,visual);
            if(m->show_h)textbox_draw_bar(&c->texture,w,0,l.width,iw,m->scroll_x,visual);
        }
        parts_set_dims(p,c,w->width,w->height);
    }
    gfx_delete_texture(&content);gfx_delete_texture(&canvas);free(l.glyphs);
    w->dirty=false;parts_component_dirty(p);parts_dirty(p);return true;
}
