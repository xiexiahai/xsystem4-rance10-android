/* SPDX-License-Identifier: GPL-2.0-or-later */
static bool pact_listbox_validate(struct pact_reader *r,struct ex_tree *t)
{
    if(!pact_sf_list_storage(t) || *pact_string(r,t,"ＣＧ名"))return false;
    if(!pact_mw_list(r,t,"尺寸",2,1,4096) || !pact_mw_list(r,t,"マージン",2,0,4096) ||
        !pact_mw_range(r,t,"行の高さ",26,1,4096,true) ||
        !pact_mw_range(r,t,"スクロール位置",0,0,10000000,true) ||
        !pact_mw_range(r,t,"文字間隔",0,-512,512,true) ||
        !pact_mw_range(r,t,"フォントタイプ",0,0,65535,true) ||
        !pact_mw_range(r,t,"フォント尺寸",22,1,512,true) ||
        !pact_mw_range(r,t,"フォント太さ",0,0,16,false) ||
        !pact_mw_range(r,t,"フォント縁取り",0,0,16,false) ||
        !pact_mw_list(r,t,"フォント色",3,0,255) || !pact_mw_list(r,t,"フォント縁取り色",3,0,255))return false;
    struct ex_value *v=pact_value(t,"リスト内容");
    if(!v || v->type!=EX_LIST || !v->list || v->list->nr_items>4096 ||
        (v->list->nr_items && !v->list->items))return false;
    size_t total=0;
    for(unsigned i=0;i<v->list->nr_items;i++) {
        struct ex_value *item=&v->list->items[i].value;
        if(item->type!=EX_STRING || !item->s || item->s->size>262144 || (total+=item->s->size)>4*1024*1024)return false;
    }
    return pact_mw_range(r,t,"選択インデックス",-1,-1,(int)v->list->nr_items-1,true) && !r->error;
}
static bool pact_listbox_apply(struct pact_reader *r,int no,struct ex_tree *t)
{
    if(!pact_listbox_validate(r,t))return false;
    struct parts *p=parts_get(no);parts_widget_create(p,PARTS_WIDGET_LISTBOX);struct parts_widget *w=p->widget;
    w->width=pact_at(r,t,"尺寸",0,120);w->height=pact_at(r,t,"尺寸",1,100);
    w->list.margin_x=pact_at(r,t,"マージン",0,0);w->list.margin_y=pact_at(r,t,"マージン",1,0);
    w->list.line_height=pact_num(r,t,"行の高さ",26);w->list.scroll=pact_num(r,t,"スクロール位置",0);
    parts_widget_font_set(&w->font,pact_num(r,t,"フォントタイプ",0),pact_num(r,t,"フォント尺寸",22),
        pact_at(r,t,"フォント色",0,0),pact_at(r,t,"フォント色",1,0),pact_at(r,t,"フォント色",2,0),
        pact_num(r,t,"フォント太さ",0),pact_at(r,t,"フォント縁取り色",0,0),pact_at(r,t,"フォント縁取り色",1,0),
        pact_at(r,t,"フォント縁取り色",2,0),pact_num(r,t,"フォント縁取り",0));
    w->char_space=pact_num(r,t,"文字間隔",0);
    struct ex_list *items=pact_value(t,"リスト内容")->list;
    for(unsigned i=0;i<items->nr_items;i++) {
        struct string *s=pact_to_game(items->items[i].value.s->text);parts_listbox_insert(p,w->list.count,s);free_string(s);
    }
    w->list.selected=pact_num(r,t,"選択インデックス",-1);
    return parts_widget_build(p) && !r->error;
}
