/* SPDX-License-Identifier: GPL-2.0-or-later */
static void listbox_dirty(struct parts *p)
{p->widget->dirty=true;parts_component_dirty(p);parts_dirty(p);}

void parts_listbox_insert(struct parts *p,int index,struct string *item)
{
    struct parts_widget *w=p->widget;
    if(!item || item->size>262144 || w->list.count>=4096)return;
    index=max(0,min(w->list.count,index));
    struct string *copy=string_dup(item);
    w->list.items=xrealloc(w->list.items,(w->list.count+1)*sizeof(*w->list.items));
    memmove(w->list.items+index+1,w->list.items+index,(w->list.count-index)*sizeof(*w->list.items));
    w->list.items[index]=copy;w->list.count++;listbox_dirty(p);
}
void parts_listbox_set(struct parts *p,int index,struct string *item)
{
    struct parts_widget *w=p->widget;
    if(index<0 || index>=w->list.count || !item || item->size>262144)return;
    struct string *copy=string_dup(item);free_string(w->list.items[index]);
    w->list.items[index]=copy;listbox_dirty(p);
}
void parts_listbox_erase(struct parts *p,int index)
{
    struct parts_widget *w=p->widget;
    if(index<0 || index>=w->list.count)return;
    free_string(w->list.items[index]);
    memmove(w->list.items+index,w->list.items+index+1,(w->list.count-index-1)*sizeof(*w->list.items));
    w->list.count--;w->list.selected=-1;listbox_dirty(p);
}
void parts_listbox_clear(struct parts *p)
{
    struct parts_widget *w=p->widget;
    for(int i=0;i<w->list.count;i++)free_string(w->list.items[i]);
    free(w->list.items);w->list.items=NULL;w->list.count=0;
    w->list.selected=-1;w->list.scroll=0;listbox_dirty(p);
}
void parts_listbox_select(struct parts *p,int index)
{
    struct parts_widget *w=p->widget;
    w->list.selected=max(-1,min(w->list.count-1,index));listbox_dirty(p);
    /* Selecting the same index is observable in the native ListBox API. */
    if(!p->is_back_scene)parts_msg_push(p,PARTS_MSG_LIST_SELECTED,"i",w->list.selected);
}
static bool widget_cursor_local(struct parts *p,Point cursor,int *x,int *y)
{
    float sx=p->global.scale.x,sy=p->global.scale.y;
    if(fabsf(sx)<.0001f || fabsf(sy)<.0001f)return false;
    float a=-p->local.rotation.z*.017453292519943295f,dx=cursor.x-p->global.pos.x,dy=cursor.y-p->global.pos.y;
    Point origin=p->states[0].common.origin_offset;
    *x=(int)floorf((cosf(a)*dx-sinf(a)*dy)/sx)-origin.x;
    *y=(int)floorf((sinf(a)*dx+cosf(a)*dy)/sy)-origin.y;
    return true;
}
int parts_listbox_cursor_index(struct parts *p,Point cursor)
{
    int x,y;struct parts_widget *w=p->widget;
    if(!w || !widget_cursor_local(p,cursor,&x,&y) || x<w->list.margin_x ||
        x>=w->width-w->list.margin_x || y<w->list.margin_y || y>=w->height-w->list.margin_y)return -1;
    int row=w->list.scroll+(y-w->list.margin_y)/max(1,w->list.line_height);
    return row>=0 && row<w->list.count?row:-1;
}
void parts_form_close(struct parts *p,bool cancel)
{p->widget->form.close_phase=cancel?0:1;}
static void parts_form_listbox_update(struct parts *p)
{
    struct parts_widget *w=p->widget;if(!w || p->is_back_scene || p->parked_textures)return;
    if(w->type==PARTS_WIDGET_FORM) {
        if(w->form.close_phase==1) {
            w->form.close_phase=2;parts_msg_push(p,PARTS_MSG_FORM_CLOSING,"");
        } else if(w->form.close_phase==2) {
            w->form.close_phase=0;PE_SetShow(p->no,false);
        }
    } else if(w->type==PARTS_WIDGET_LISTBOX) {
        int rows=max(1,(w->height-2*w->list.margin_y)/max(1,w->list.line_height));
        int scroll=max(0,min(max(0,w->list.count-rows),w->list.scroll));
        if(scroll!=w->list.scroll){w->list.scroll=scroll;listbox_dirty(p);}
    }
}
static void parts_form_draw_close(Texture *dst,struct parts_widget *w,int caption)
{
    int size=min(20,min(caption,w->width)),x=w->width-size;
    fill_opaque(dst,x,0,size,size,w->pressed_region==6?90:150,50,50);
    for(int i=4;i<size-4;i++) {
        fill_opaque(dst,x+i,i,1,1,255,255,255);
        fill_opaque(dst,x+size-1-i,i,1,1,255,255,255);
    }
}
static void parts_form_mouse(struct parts *p,Point cursor,bool down,bool pressed,bool released)
{
    struct parts_widget *w=p->widget;int x,y;
    if(!w->form.close_enabled || !widget_cursor_local(p,cursor,&x,&y))return;
    int size=min(20,min(24+w->form.caption_plus,w->width));
    bool inside=x>=w->width-size && x<w->width && y>=0 && y<size;
    if(pressed && inside){w->pressed_region=6;listbox_dirty(p);}
    if(released && w->pressed_region==6 && inside)parts_form_close(p,false);
    if(!down && w->pressed_region==6){w->pressed_region=-1;listbox_dirty(p);}
}
static void parts_listbox_draw(Texture *dst,struct parts_widget *w)
{
    fill_opaque(dst,0,0,w->width,w->height,255,255,255);
    int width=max(1,w->width-2*w->list.margin_x),inner=max(1,w->height-2*w->list.margin_y);
    Texture content={0};gfx_init_texture_rgba(&content,width,inner,(SDL_Color){255,255,255,255});
    for(int i=max(0,w->list.scroll),y=0;i<w->list.count && y<inner;i++,y+=max(1,w->list.line_height)) {
        bool selected=i==w->list.selected;
        if(selected)fill_opaque(&content,0,y,width,w->list.line_height,35,65,125);
        struct parts_widget text=*w;text.text=w->list.items[i];text.text_origin=4;
        if(selected)text.font.color=(SDL_Color){255,255,255,255};
        draw_text(&content,&text,0,y,width,w->list.line_height,false);
    }
    gfx_copy(dst,w->list.margin_x,w->list.margin_y,&content,0,0,width,inner);
    gfx_delete_texture(&content);
}
static void parts_listbox_save(struct iarray_writer *out,struct parts_widget *w)
{
    if(w->type!=PARTS_WIDGET_LISTBOX)return;
    int values[]={w->list.line_height,w->list.margin_x,w->list.margin_y,w->list.scroll,w->list.selected,w->list.count};
    for(unsigned i=0;i<sizeof(values)/sizeof(*values);i++)iarray_write(out,values[i]);
    for(int i=0;i<w->list.count;i++)iarray_write_string(out,w->list.items[i]);
}
static bool parts_listbox_load(struct iarray_reader *in,struct parts_widget *w)
{
    if(w->type!=PARTS_WIDGET_LISTBOX)return !in->error;
    int v[6];for(int i=0;i<6;i++)v[i]=iarray_read(in);
    if(in->error || v[0]<1 || v[0]>4096 || v[1]<0 || v[1]>4096 || v[2]<0 || v[2]>4096 ||
        v[3]<0 || v[3]>10000000 || v[5]<0 || v[5]>4096 || v[4]<-1 || v[4]>=v[5]) {in->error=1;return false;}
    w->list.line_height=v[0];w->list.margin_x=v[1];w->list.margin_y=v[2];w->list.scroll=v[3];w->list.selected=v[4];
    w->list.items=xcalloc(max(1,v[5]),sizeof(*w->list.items));size_t total=0;
    for(int i=0;i<v[5];i++) {
        struct string *item=iarray_read_string(in);w->list.items[w->list.count++]=item;
        if(in->error || !item || item->size>262144 || (total+=item->size)>4*1024*1024) {in->error=1;return false;}
    }
    return true;
}
