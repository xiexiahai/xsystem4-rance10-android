/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "input.h"
static struct parts_widget *lb_widget(int no) {return parts_widget_get(no,PARTS_WIDGET_LISTBOX);}
static void lb_dirty(int no) {struct parts *p=parts_get(no);p->widget->dirty=true;parts_component_dirty(p);parts_dirty(p);}
static void LB_SetListBoxSize(int no,int w,int h)
{struct parts_widget *p=lb_widget(no);p->width=max(1,min(4096,w));p->height=max(1,min(4096,h));lb_dirty(no);}
static void LB_SetListBoxLineHeight(int no,int value)
{lb_widget(no)->list.line_height=max(1,min(4096,value));lb_dirty(no);}
static int LB_GetListBoxLineHeight(int no)
{return lb_widget(no)->list.line_height;}
static void LB_SetListBoxMargin(int no,int x,int y)
{struct parts_widget *p=lb_widget(no);p->list.margin_x=max(0,min(4096,x));p->list.margin_y=max(0,min(4096,y));lb_dirty(no);}
static int LB_GetListBoxWidthMargin(int no)
{return lb_widget(no)->list.margin_x;}
static int LB_GetListBoxHeightMargin(int no)
{return lb_widget(no)->list.margin_y;}
static int LB_GetListBoxScrollPos(int no)
{return lb_widget(no)->list.scroll;}
static int LB_GetListBoxItemCount(int no)
{return lb_widget(no)->list.count;}
static int LB_GetListBoxSelectIndex(int no)
{int v=lb_widget(no)->list.selected;NOTICE("[lb] get-select no=%d -> %d count=%d",no,v,lb_widget(no)->list.count);return v;}
static void LB_SetListBoxCGName(int no,struct string *value)
{lb_widget(no);if(value->size)VM_ERROR("ListBox CG skin is not implemented");lb_dirty(no);}
static struct string * LB_GetListBoxCGName(int no)
{return string_dup(lb_widget(no)->cg_name);}
static void LB_SetListBoxScrollPos(int no,int value)
{lb_widget(no)->list.scroll=max(0,min(10000000,value));lb_dirty(no);}
static void LB_AddListBoxItem(int no,struct string *value)
{struct parts_widget *w=lb_widget(no);parts_listbox_insert(parts_get(no),w->list.count,value);}
static void LB_InsertListBoxItem(int no,int index,struct string *value)
{lb_widget(no);parts_listbox_insert(parts_get(no),index,value);}
static void LB_SetListBoxItem(int no,int index,struct string *value)
{lb_widget(no);parts_listbox_set(parts_get(no),index,value);}
static struct string * LB_GetListBoxItem(int no,int index)
{struct parts_widget *w=lb_widget(no);return index>=0&&index<w->list.count?string_dup(w->list.items[index]):string_ref(&EMPTY_STRING);}
static void LB_EraseListBoxItem(int no,int index)
{lb_widget(no);parts_listbox_erase(parts_get(no),index);}
static void LB_ClearListBoxItem(int no)
{lb_widget(no);parts_listbox_clear(parts_get(no));}
static int LB_GetListBoxOnCursorItemIndex(int no)
{lb_widget(no);struct parts *p=parts_get(no);Point pos;mouse_get_pos(&pos.x,&pos.y);int v=p->is_hovered&&parts_widget_visible(p)?parts_listbox_cursor_index(p,pos):-1;NOTICE("[lb] on-cursor no=%d hovered=%d pos=%d,%d -> %d selected=%d",no,p->is_hovered,pos.x,pos.y,v,lb_widget(no)->list.selected);return v;}
static struct string * LB_GetListBoxOnCursorItem(int no)
{return LB_GetListBoxItem(no,LB_GetListBoxOnCursorItemIndex(no));}
static void LB_SetListBoxSelectIndex(int no,int value)
{lb_widget(no);parts_listbox_select(parts_get(no),value);NOTICE("[lb] set-select no=%d value=%d -> %d count=%d",no,value,lb_widget(no)->list.selected,lb_widget(no)->list.count);}
static void LB_SetListBoxCharSpace(int no,int value)
{lb_widget(no)->char_space=max(-512,min(512,value));lb_dirty(no);}
static int LB_GetListBoxCharSpace(int no)
{return lb_widget(no)->char_space;}
static void LB_SetListBoxFontProperty(int no,int type,int size,int r,int g,int b,float bold,int er,int eg,int eb,float edge)
{parts_widget_font_set(&lb_widget(no)->font,type,size,r,g,b,bold,er,eg,eb,edge);lb_dirty(no);}
static void LB_GetListBoxFontProperty(int no,int *type,int *size,int *r,int *g,int *b,float *bold,int *er,int *eg,int *eb,float *edge)
{parts_widget_font_get(&lb_widget(no)->font,type,size,r,g,b,bold,er,eg,eb,edge);}
#define LISTBOX_HLL_EXPORTS \
    HLL_EXPORT(SetListBoxSize, LB_SetListBoxSize), \
    HLL_EXPORT(SetListBoxLineHeight, LB_SetListBoxLineHeight), \
    HLL_EXPORT(GetListBoxLineHeight, LB_GetListBoxLineHeight), \
    HLL_EXPORT(SetListBoxMargin, LB_SetListBoxMargin), \
    HLL_EXPORT(GetListBoxWidthMargin, LB_GetListBoxWidthMargin), \
    HLL_EXPORT(GetListBoxHeightMargin, LB_GetListBoxHeightMargin), \
    HLL_EXPORT(GetListBoxScrollPos, LB_GetListBoxScrollPos), \
    HLL_EXPORT(GetListBoxItemCount, LB_GetListBoxItemCount), \
    HLL_EXPORT(GetListBoxSelectIndex, LB_GetListBoxSelectIndex), \
    HLL_EXPORT(SetListBoxCGName, LB_SetListBoxCGName), \
    HLL_EXPORT(GetListBoxCGName, LB_GetListBoxCGName), \
    HLL_EXPORT(SetListBoxScrollPos, LB_SetListBoxScrollPos), \
    HLL_EXPORT(AddListBoxItem, LB_AddListBoxItem), \
    HLL_EXPORT(InsertListBoxItem, LB_InsertListBoxItem), \
    HLL_EXPORT(SetListBoxItem, LB_SetListBoxItem), \
    HLL_EXPORT(GetListBoxItem, LB_GetListBoxItem), \
    HLL_EXPORT(EraseListBoxItem, LB_EraseListBoxItem), \
    HLL_EXPORT(ClearListBoxItem, LB_ClearListBoxItem), \
    HLL_EXPORT(GetListBoxOnCursorItemIndex, LB_GetListBoxOnCursorItemIndex), \
    HLL_EXPORT(GetListBoxOnCursorItem, LB_GetListBoxOnCursorItem), \
    HLL_EXPORT(SetListBoxSelectIndex, LB_SetListBoxSelectIndex), \
    HLL_EXPORT(SetListBoxCharSpace, LB_SetListBoxCharSpace), \
    HLL_EXPORT(GetListBoxCharSpace, LB_GetListBoxCharSpace), \
    HLL_EXPORT(SetListBoxFontProperty, LB_SetListBoxFontProperty), \
    HLL_EXPORT(GetListBoxFontProperty, LB_GetListBoxFontProperty),
