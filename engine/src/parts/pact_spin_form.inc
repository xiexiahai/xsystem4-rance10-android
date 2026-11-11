/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <float.h>
static bool pact_sf_list_storage(struct ex_tree *t)
{
    if(!t || t->is_leaf)return false;
    for(unsigned i=0;i<t->nr_children;i++) {
        struct ex_tree *c=&t->children[i];
        if(c->is_leaf && c->leaf.value.type==EX_LIST && (!c->leaf.value.list ||
            (c->leaf.value.list->nr_items && !c->leaf.value.list->items)))return false;
    }
    return true;
}
/* Numeric data is not a coordinate: real defaults use +/-FLT_MAX. Keep the
 * common coordinate reader's 10,000,000 bound unchanged. */
static float pact_spin_num(struct pact_reader *r,struct ex_tree *t,const char *key,float def)
{
    struct ex_value *v=pact_value(t,key);
    if(!v)return def;
    float n;
    if(v->type==EX_FLOAT)n=v->f;
    else if(v->type==EX_INT)n=v->i;
    else {r->error=true;return def;}
    if(!isfinite(n)){r->error=true;return def;}
    return n;
}
static bool pact_spin_validate(struct pact_reader *r,struct ex_tree *t)
{
    if(!pact_sf_list_storage(t))return false;
    const char *foreign[]={"テキスト","最大文字数","パディング","スクロール位置","スクロールバー表示","行間隔"};
    for(unsigned i=0;i<sizeof(foreign)/sizeof(*foreign);i++)if(pact_value(t,foreign[i]))return false;
    if(!pact_tb_validate(r,t))return false;
    const char *keys[]={"値","既定値","最小値","最大値","増分値"};
    const float defaults[]={0,0,-FLT_MAX,FLT_MAX,1};float v[5];
    for(int i=0;i<5;i++)v[i]=pact_spin_num(r,t,keys[i],defaults[i]);
    return !r->error && v[2]<=v[3] && v[0]>=v[2] && v[0]<=v[3] && v[4]>=0 &&
        pact_mw_range(r,t,"小数点以下の表示桁数",0,0,9,true);
}
static bool pact_spin_apply(struct pact_reader *r,int no,struct ex_tree *t)
{
    if(!pact_spin_validate(r,t))return false;
    struct parts *p=parts_get(no);parts_widget_create(p,PARTS_WIDGET_SPINBOX);
    struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;
    w->width=pact_at(r,t,"尺寸",0,80);w->height=pact_at(r,t,"尺寸",1,24);
    parts_widget_font_set(&w->font,pact_num(r,t,"フォントタイプ",0),pact_num(r,t,"フォント尺寸",22),
        pact_at(r,t,"フォント色",0,0),pact_at(r,t,"フォント色",1,0),pact_at(r,t,"フォント色",2,0),
        pact_num(r,t,"フォント太さ",0),pact_at(r,t,"フォント縁取り色",0,0),pact_at(r,t,"フォント縁取り色",1,0),
        pact_at(r,t,"フォント縁取り色",2,0),pact_num(r,t,"フォント縁取り",0));
    w->char_space=pact_num(r,t,"文字間隔",0);m->readonly=pact_num(r,t,"読み取り専用",0);
    SDL_Color *colors[]={&m->bg,&m->readonly_bg,&m->frame,&m->selection};
    const char *keys[]={"背景色","読み取り専用背景色","枠色","選択色"};int d[]={255,150,100,200};
    for(int i=0;i<4;i++)*colors[i]=(SDL_Color){pact_at(r,t,keys[i],0,d[i]),pact_at(r,t,keys[i],1,d[i]),pact_at(r,t,keys[i],2,d[i]),255};
    w->spin.min=pact_spin_num(r,t,"最小値",-FLT_MAX);w->spin.max=pact_spin_num(r,t,"最大値",FLT_MAX);
    w->spin.def=pact_spin_num(r,t,"既定値",0);w->spin.increment=pact_spin_num(r,t,"増分値",1);
    w->spin.decimals=pact_num(r,t,"小数点以下の表示桁数",0);
    if(!parts_widget_spin_value(p,pact_spin_num(r,t,"値",0)))return false;
    return parts_widget_build(p) && !r->error;
}
static bool pact_form_validate(struct pact_reader *r,struct ex_tree *t)
{
    if(!pact_sf_list_storage(t))return false;
    /* Resizing, folding, icons and child scrolling need
     * separate native contracts. Never accept them as a painted dummy form. */
    const char *unsupported[]={"リサイズ許可","折りたたみ"};
    for(unsigned i=0;i<sizeof(unsupported)/sizeof(*unsupported);i++)
        if(!pact_mw_range(r,t,unsupported[i],0,0,0,true)) {
            WARNING("PACT form requires unimplemented behavior: %s",unsupported[i]);return false;
        }
    if(*pact_string(r,t,"アイコン"))return false;
    if(!pact_mw_range(r,t,"閉じるステップ",0,0,2,true) ||
        !pact_mw_range(r,t,"閉じるボタン有効",0,0,1,true))return false;
    if(strlen(pact_string(r,t,"タイトル"))>65536)return false;
    if(!pact_mw_list(r,t,"尺寸",2,1,4096)||!pact_mw_list(r,t,"最小サイズ",2,1,4096)||
        !pact_mw_list(r,t,"最大サイズ",2,1,4096)||!pact_mw_list(r,t,"キャプション色",3,0,255)||
        !pact_mw_list(r,t,"クライアント色",3,0,255)||
        !pact_mw_range(r,t,"タイトルバー増加量",0,0,512,true))return false;
    for(int i=0;i<2;i++)if(pact_at(r,t,"最小サイズ",i,i?24:50)>pact_at(r,t,"最大サイズ",i,2000))return false;
    return !r->error;
}
static bool pact_form_apply(struct pact_reader *r,int no,struct ex_tree *t)
{
    if(!pact_form_validate(r,t))return false;
    struct parts *p=parts_get(no);parts_widget_create(p,PARTS_WIDGET_FORM);struct parts_widget *w=p->widget;
    w->width=pact_at(r,t,"尺寸",0,320);w->height=pact_at(r,t,"尺寸",1,240);
    w->form.min_w=pact_at(r,t,"最小サイズ",0,50);w->form.min_h=pact_at(r,t,"最小サイズ",1,24);
    w->form.max_w=pact_at(r,t,"最大サイズ",0,2000);w->form.max_h=pact_at(r,t,"最大サイズ",1,2000);
    w->form.caption_plus=pact_num(r,t,"タイトルバー増加量",0);
    w->form.close_enabled=pact_num(r,t,"閉じるボタン有効",0);w->form.close_phase=pact_num(r,t,"閉じるステップ",0);
    w->form.caption=(SDL_Color){pact_at(r,t,"キャプション色",0,50),pact_at(r,t,"キャプション色",1,50),pact_at(r,t,"キャプション色",2,50),255};
    w->color=(SDL_Color){pact_at(r,t,"クライアント色",0,120),pact_at(r,t,"クライアント色",1,120),pact_at(r,t,"クライアント色",2,120),255};
    free_string(w->text);w->text=pact_to_game(pact_string(r,t,"タイトル"));
    return parts_widget_build(p) && !r->error;
}
