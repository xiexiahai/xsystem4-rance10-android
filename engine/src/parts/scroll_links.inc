/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Links retain IDs; lookups never construct or change their targets. */
static struct parts *scroll_link_target(struct parts *owner,int no)
{
    return !no ? NULL : owner->is_back_scene ? parts_back_scene_get(no) : parts_try_get(no);
}
static struct parts_widget *scroll_link_widget(struct parts *owner,int no)
{
    struct parts *source=scroll_link_target(owner,no);
    struct parts_widget *w=source?source->widget:NULL;
    return w && (w->type==PARTS_WIDGET_VSCROLL || w->type==PARTS_WIDGET_HSCROLL) ? w : NULL;
}
int parts_scroll_link_pos(struct parts *p,int axis)
{
    struct parts_widget *w=scroll_link_widget(p,p->scroll_pos_link[axis]);
    return w?w->pos:0;
}
float parts_scroll_link_alpha(struct parts *p)
{
    struct parts_widget *w=scroll_link_widget(p,p->scroll_alpha_link);
    return w?parts_widget_rate(w):1.f;
}
void parts_set_scroll_link(struct parts *p,int kind,int no)
{
    int *slot=kind==2?&p->scroll_alpha_link:&p->scroll_pos_link[kind];
    if(*slot==no)return;
    *slot=no;parts_component_dirty(p);parts_dirty(p);
}
int parts_widget_scroll_size(struct parts *p,bool view)
{
    struct parts_widget *w=p->widget;
    int fallback=view?w->view:w->total;
    struct parts *source=scroll_link_target(p,view?w->view_size_link:w->total_size_link);
    if(!source)return fallback;
    struct parts_common *c=&source->states[source->state].common;
    return max(0,w->type==PARTS_WIDGET_VSCROLL?c->h:c->w);
}
void parts_widget_set_size_link(struct parts *p,bool view,int no)
{
    struct parts_widget *w=p->widget;
    int *slot=view?&w->view_size_link:&w->total_size_link;
    if(*slot==no)return;
    *slot=no;w->dirty=true;parts_component_dirty(p);parts_dirty(p);
}
static void parts_widget_prepare_size_links(void)
{
    struct parts *p;
    /* Layout dimensions are measured before links read them; scrolling only
     * changes effective coordinates, so it cannot feed into the base measure. */
    bool any=false;
    PARTS_LIST_FOREACH(p) if(p->widget && (p->widget->type==2 || p->widget->type==3) &&
        (p->widget->total_size_link || p->widget->view_size_link)) {any=true;break;}
    if(!any)return;
    PARTS_LIST_FOREACH(p) if(!p->parent)parts_measure_layout(p);
    PARTS_LIST_FOREACH(p) {
        struct parts_widget *w=p->widget;
        if(!w || (w->type!=PARTS_WIDGET_VSCROLL && w->type!=PARTS_WIDGET_HSCROLL))continue;
        int total=parts_widget_scroll_size(p,false),view=parts_widget_scroll_size(p,true);
        if(total==w->total && view==w->view)continue;
        w->total=total;w->view=view;
        parts_widget_scroll(p,w->pos);w->dirty=true;parts_dirty(p);
    }
}
void parts_scroll_links_save(struct iarray_writer *out,struct parts *p)
{
    iarray_write(out,p->scroll_pos_link[0]);iarray_write(out,p->scroll_pos_link[1]);
    iarray_write(out,p->scroll_alpha_link);
    iarray_write(out,p->widget?p->widget->total_size_link:0);
    iarray_write(out,p->widget?p->widget->view_size_link:0);
}
void parts_scroll_links_load(struct iarray_reader *in,struct parts *p,int version)
{
    p->scroll_pos_link[0]=p->scroll_pos_link[1]=p->scroll_alpha_link=0;
    if(p->widget)p->widget->total_size_link=p->widget->view_size_link=0;
    if(version<27)return;
    p->scroll_pos_link[0]=iarray_read(in);p->scroll_pos_link[1]=iarray_read(in);
    p->scroll_alpha_link=iarray_read(in);
    int total=iarray_read(in),view=iarray_read(in);
    if((total || view) && (!p->widget || (p->widget->type!=2 && p->widget->type!=3))) {
        in->error=1;return;
    }
    if(p->widget){p->widget->total_size_link=total;p->widget->view_size_link=view;}
}
