/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../parts/textbox_internal.h"
static struct parts *Textbox_part(int no,int kind)
{parts_widget_get(no,kind);return parts_get(no);}
static struct parts *Textbox_existing(int no)
{struct parts *p=parts_try_get(no);return p && p->widget && p->widget->textbox?p:NULL;}
static void Textbox_SetTextBoxSize(int no,int width,int height)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;w->width=max(1,min(4096,width));w->height=max(1,min(4096,height));m->keep_caret_visible=true;parts_textbox_dirty(p);}

static void Textbox_SetTextBoxFontProperty(int no,int type,int size,int r,int g,int b,float bold,int er,int eg,int eb,float edge)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;parts_widget_font_set(&w->font,type,size,r,g,b,bold,er,eg,eb,edge);m->keep_caret_visible=true;parts_textbox_dirty(p);}

static void Textbox_GetTextBoxFontProperty(int no,int *type,int *size,int *r,int *g,int *b,float *bold,int *er,int *eg,int *eb,float *edge)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;parts_widget_font_get(&w->font,type,size,r,g,b,bold,er,eg,eb,edge);}

static void Textbox_SetTextBoxReadOnly(int no,bool value)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;m->readonly=value;if(value)parts_textbox_open(p,false);parts_textbox_dirty(p);}

static bool Textbox_IsTextBoxReadOnly(int no)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->readonly;}

static void Textbox_SetTextBoxText(int no,struct string *text)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;if(!parts_textbox_set_text(p,text))VM_ERROR("Invalid or over-limit textbox text");}

static void Textbox_GetTextBoxText(int no,struct string **out)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;free_string(*out);*out=string_ref(w->text);}

static void Textbox_GetTextBoxCGName(int no,struct string **out)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;free_string(*out);*out=string_ref(w->cg_name);}

static void Textbox_SetTextBoxMaxTextLength(int no,int limit)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;if(!parts_textbox_set_limit(p,max(0,min(65536,limit))))VM_ERROR("Textbox limit allocation failed");}

static int Textbox_GetTextBoxMaxTextLength(int no)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->buffer.limit;}

static void Textbox_SetTextBoxBGColor(int no,int r,int g,int b)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;m->bg=(SDL_Color){max(0,min(255,r)),max(0,min(255,g)),max(0,min(255,b)),255};parts_textbox_dirty(p);}

static int Textbox_GetTextBoxBGR(int no)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->bg.r;}

static int Textbox_GetTextBoxBGG(int no)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->bg.g;}

static int Textbox_GetTextBoxBGB(int no)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->bg.b;}

static void Textbox_SetTextBoxReadOnlyBGColor(int no,int r,int g,int b)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;m->readonly_bg=(SDL_Color){max(0,min(255,r)),max(0,min(255,g)),max(0,min(255,b)),255};parts_textbox_dirty(p);}

static int Textbox_GetTextBoxReadOnlyBGR(int no)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->readonly_bg.r;}

static int Textbox_GetTextBoxReadOnlyBGG(int no)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->readonly_bg.g;}

static int Textbox_GetTextBoxReadOnlyBGB(int no)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->readonly_bg.b;}

static void Textbox_SetTextBoxFrameColor(int no,int r,int g,int b)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;m->frame=(SDL_Color){max(0,min(255,r)),max(0,min(255,g)),max(0,min(255,b)),255};parts_textbox_dirty(p);}

static int Textbox_GetTextBoxFrameR(int no)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->frame.r;}

static int Textbox_GetTextBoxFrameG(int no)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->frame.g;}

static int Textbox_GetTextBoxFrameB(int no)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->frame.b;}

static void Textbox_SetTextBoxSelectColor(int no,int r,int g,int b)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;m->selection=(SDL_Color){max(0,min(255,r)),max(0,min(255,g)),max(0,min(255,b)),255};parts_textbox_dirty(p);}

static int Textbox_GetTextBoxSelectR(int no)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->selection.r;}

static int Textbox_GetTextBoxSelectG(int no)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->selection.g;}

static int Textbox_GetTextBoxSelectB(int no)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->selection.b;}

static void Textbox_SetTextBoxCGName(int no,struct string *name)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;if(!parts_widget_set_cg(p,name))VM_ERROR("Textbox named skin is not supported");parts_textbox_dirty(p);}

static void Textbox_SetTextBoxCharSpace(int no,int space)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;w->char_space=max(-512,min(512,space));m->keep_caret_visible=true;parts_textbox_dirty(p);}

static int Textbox_GetTextBoxCharSpace(int no)
{struct parts *p=Textbox_part(no,5);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return w->char_space;}

static void Textbox_SetMultiTextBoxSize(int no,int width,int height)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;w->width=max(1,min(4096,width));w->height=max(1,min(4096,height));m->keep_caret_visible=true;parts_textbox_dirty(p);}

static void Textbox_SetMultiTextBoxFontProperty(int no,int type,int size,int r,int g,int b,float bold,int er,int eg,int eb,float edge)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;parts_widget_font_set(&w->font,type,size,r,g,b,bold,er,eg,eb,edge);m->keep_caret_visible=true;parts_textbox_dirty(p);}

static void Textbox_GetMultiTextBoxFontProperty(int no,int *type,int *size,int *r,int *g,int *b,float *bold,int *er,int *eg,int *eb,float *edge)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;parts_widget_font_get(&w->font,type,size,r,g,b,bold,er,eg,eb,edge);}

static void Textbox_SetMultiTextBoxReadOnly(int no,bool value)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;m->readonly=value;if(value)parts_textbox_open(p,false);parts_textbox_dirty(p);}

static bool Textbox_IsMultiTextBoxReadOnly(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->readonly;}

static void Textbox_SetMultiTextBoxText(int no,struct string *text)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;if(!parts_textbox_set_text(p,text))VM_ERROR("Invalid or over-limit textbox text");}

static void Textbox_GetMultiTextBoxText(int no,struct string **out)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;free_string(*out);*out=string_ref(w->text);}

static void Textbox_GetMultiTextBoxCGName(int no,struct string **out)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;free_string(*out);*out=string_ref(w->cg_name);}

static void Textbox_SetMultiTextBoxMaxTextLength(int no,int limit)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;if(!parts_textbox_set_limit(p,max(0,min(65536,limit))))VM_ERROR("Textbox limit allocation failed");}

static int Textbox_GetMultiTextBoxMaxTextLength(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->buffer.limit;}

static void Textbox_SetMultiTextBoxBGColor(int no,int r,int g,int b)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;m->bg=(SDL_Color){max(0,min(255,r)),max(0,min(255,g)),max(0,min(255,b)),255};parts_textbox_dirty(p);}

static int Textbox_GetMultiTextBoxBGR(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->bg.r;}

static int Textbox_GetMultiTextBoxBGG(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->bg.g;}

static int Textbox_GetMultiTextBoxBGB(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->bg.b;}

static void Textbox_SetMultiTextBoxReadOnlyBGColor(int no,int r,int g,int b)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;m->readonly_bg=(SDL_Color){max(0,min(255,r)),max(0,min(255,g)),max(0,min(255,b)),255};parts_textbox_dirty(p);}

static int Textbox_GetMultiTextBoxReadOnlyBGR(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->readonly_bg.r;}

static int Textbox_GetMultiTextBoxReadOnlyBGG(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->readonly_bg.g;}

static int Textbox_GetMultiTextBoxReadOnlyBGB(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->readonly_bg.b;}

static void Textbox_SetMultiTextBoxFrameColor(int no,int r,int g,int b)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;m->frame=(SDL_Color){max(0,min(255,r)),max(0,min(255,g)),max(0,min(255,b)),255};parts_textbox_dirty(p);}

static int Textbox_GetMultiTextBoxFrameR(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->frame.r;}

static int Textbox_GetMultiTextBoxFrameG(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->frame.g;}

static int Textbox_GetMultiTextBoxFrameB(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->frame.b;}

static void Textbox_SetMultiTextBoxSelectColor(int no,int r,int g,int b)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;m->selection=(SDL_Color){max(0,min(255,r)),max(0,min(255,g)),max(0,min(255,b)),255};parts_textbox_dirty(p);}

static int Textbox_GetMultiTextBoxSelectR(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->selection.r;}

static int Textbox_GetMultiTextBoxSelectG(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->selection.g;}

static int Textbox_GetMultiTextBoxSelectB(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->selection.b;}

static void Textbox_SetMultiTextBoxCGName(int no,struct string *name)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;if(!parts_widget_set_cg(p,name))VM_ERROR("Textbox named skin is not supported");parts_textbox_dirty(p);}

static void Textbox_SetMultiTextBoxCharSpace(int no,int space)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;w->char_space=max(-512,min(512,space));m->keep_caret_visible=true;parts_textbox_dirty(p);}

static int Textbox_GetMultiTextBoxCharSpace(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return w->char_space;}

static void Textbox_SetMultiTextBoxLineSpace(int no,int space)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;w->line_space=max(-512,min(512,space));m->keep_caret_visible=true;parts_textbox_dirty(p);}

static int Textbox_GetMultiTextBoxLineSpace(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return w->line_space;}

static void Textbox_SetMultiTextBoxPadding(int no,int top,int bottom,int left,int right)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;int a[]={top,bottom,left,right};for(int i=0;i<4;i++)m->padding[i]=max(0,min(4096,a[i]));m->keep_caret_visible=true;parts_textbox_dirty(p);}

static int Textbox_GetMultiTextBoxPaddingTop(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->padding[0];}

static int Textbox_GetMultiTextBoxPaddingBottom(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->padding[1];}

static int Textbox_GetMultiTextBoxPaddingLeft(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->padding[2];}

static int Textbox_GetMultiTextBoxPaddingRight(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->padding[3];}

static void Textbox_SetMultiTextBoxVScrollShow(int no,bool show)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;m->show_v=show;m->keep_caret_visible=true;parts_textbox_dirty(p);}

static bool Textbox_IsMultiTextBoxVScrollShow(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->show_v;}

static void Textbox_SetMultiTextBoxHScrollShow(int no,bool show)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;m->show_h=show;m->keep_caret_visible=true;parts_textbox_dirty(p);}

static bool Textbox_IsMultiTextBoxHScrollShow(int no)
{struct parts *p=Textbox_part(no,7);struct parts_widget *w=p->widget;struct parts_textbox *m=w->textbox;(void)w;(void)m;return m->show_h;}

static void Textbox_SetOpenTextBoxIME(int no,bool open){parts_textbox_open(Textbox_existing(no),open);}

static void Textbox_OpenTextBoxIME(int no){parts_textbox_open(Textbox_existing(no),true);}

static void Textbox_CloseTextBoxIME(int no){parts_textbox_open(Textbox_existing(no),false);}

static void Textbox_SelectTextBoxAll(int no){struct parts *p=Textbox_existing(no);if(p){tb_select_all(&p->widget->textbox->buffer);parts_textbox_dirty(p);}}

static bool Textbox_IsOpenTextBoxIME(int no){return parts_textbox_is_open(Textbox_existing(no));}

static struct string *Textbox12_GetTextBoxText(int no){return string_ref(Textbox_part(no,5)->widget->text);}

static struct string *Textbox12_GetTextBoxCGName(int no){return string_ref(Textbox_part(no,5)->widget->cg_name);}

static struct string *Textbox12_GetMultiTextBoxText(int no){return string_ref(Textbox_part(no,7)->widget->text);}

static struct string *Textbox12_GetMultiTextBoxCGName(int no){return string_ref(Textbox_part(no,7)->widget->cg_name);}

#define TEXTBOX_HLL_EXPORTS \
    HLL_EXPORT(SetTextBoxSize,Textbox_SetTextBoxSize), \
    HLL_EXPORT(SetTextBoxFontProperty,Textbox_SetTextBoxFontProperty), \
    HLL_EXPORT(GetTextBoxFontProperty,Textbox_GetTextBoxFontProperty), \
    HLL_EXPORT(SetTextBoxReadOnly,Textbox_SetTextBoxReadOnly), \
    HLL_EXPORT(IsTextBoxReadOnly,Textbox_IsTextBoxReadOnly), \
    HLL_EXPORT(SetTextBoxText,Textbox_SetTextBoxText), \
    HLL_EXPORT(GetTextBoxText,Textbox_GetTextBoxText), \
    HLL_EXPORT(GetTextBoxCGName,Textbox_GetTextBoxCGName), \
    HLL_EXPORT(SetTextBoxMaxTextLength,Textbox_SetTextBoxMaxTextLength), \
    HLL_EXPORT(GetTextBoxMaxTextLength,Textbox_GetTextBoxMaxTextLength), \
    HLL_EXPORT(SetTextBoxBGColor,Textbox_SetTextBoxBGColor), \
    HLL_EXPORT(GetTextBoxBGR,Textbox_GetTextBoxBGR), \
    HLL_EXPORT(GetTextBoxBGG,Textbox_GetTextBoxBGG), \
    HLL_EXPORT(GetTextBoxBGB,Textbox_GetTextBoxBGB), \
    HLL_EXPORT(SetTextBoxReadOnlyBGColor,Textbox_SetTextBoxReadOnlyBGColor), \
    HLL_EXPORT(GetTextBoxReadOnlyBGR,Textbox_GetTextBoxReadOnlyBGR), \
    HLL_EXPORT(GetTextBoxReadOnlyBGG,Textbox_GetTextBoxReadOnlyBGG), \
    HLL_EXPORT(GetTextBoxReadOnlyBGB,Textbox_GetTextBoxReadOnlyBGB), \
    HLL_EXPORT(SetTextBoxFrameColor,Textbox_SetTextBoxFrameColor), \
    HLL_EXPORT(GetTextBoxFrameR,Textbox_GetTextBoxFrameR), \
    HLL_EXPORT(GetTextBoxFrameG,Textbox_GetTextBoxFrameG), \
    HLL_EXPORT(GetTextBoxFrameB,Textbox_GetTextBoxFrameB), \
    HLL_EXPORT(SetTextBoxSelectColor,Textbox_SetTextBoxSelectColor), \
    HLL_EXPORT(GetTextBoxSelectR,Textbox_GetTextBoxSelectR), \
    HLL_EXPORT(GetTextBoxSelectG,Textbox_GetTextBoxSelectG), \
    HLL_EXPORT(GetTextBoxSelectB,Textbox_GetTextBoxSelectB), \
    HLL_EXPORT(SetTextBoxCGName,Textbox_SetTextBoxCGName), \
    HLL_EXPORT(SetTextBoxCharSpace,Textbox_SetTextBoxCharSpace), \
    HLL_EXPORT(GetTextBoxCharSpace,Textbox_GetTextBoxCharSpace), \
    HLL_EXPORT(SetMultiTextBoxSize,Textbox_SetMultiTextBoxSize), \
    HLL_EXPORT(SetMultiTextBoxFontProperty,Textbox_SetMultiTextBoxFontProperty), \
    HLL_EXPORT(GetMultiTextBoxFontProperty,Textbox_GetMultiTextBoxFontProperty), \
    HLL_EXPORT(SetMultiTextBoxReadOnly,Textbox_SetMultiTextBoxReadOnly), \
    HLL_EXPORT(IsMultiTextBoxReadOnly,Textbox_IsMultiTextBoxReadOnly), \
    HLL_EXPORT(SetMultiTextBoxText,Textbox_SetMultiTextBoxText), \
    HLL_EXPORT(GetMultiTextBoxText,Textbox_GetMultiTextBoxText), \
    HLL_EXPORT(GetMultiTextBoxCGName,Textbox_GetMultiTextBoxCGName), \
    HLL_EXPORT(SetMultiTextBoxMaxTextLength,Textbox_SetMultiTextBoxMaxTextLength), \
    HLL_EXPORT(GetMultiTextBoxMaxTextLength,Textbox_GetMultiTextBoxMaxTextLength), \
    HLL_EXPORT(SetMultiTextBoxBGColor,Textbox_SetMultiTextBoxBGColor), \
    HLL_EXPORT(GetMultiTextBoxBGR,Textbox_GetMultiTextBoxBGR), \
    HLL_EXPORT(GetMultiTextBoxBGG,Textbox_GetMultiTextBoxBGG), \
    HLL_EXPORT(GetMultiTextBoxBGB,Textbox_GetMultiTextBoxBGB), \
    HLL_EXPORT(SetMultiTextBoxReadOnlyBGColor,Textbox_SetMultiTextBoxReadOnlyBGColor), \
    HLL_EXPORT(GetMultiTextBoxReadOnlyBGR,Textbox_GetMultiTextBoxReadOnlyBGR), \
    HLL_EXPORT(GetMultiTextBoxReadOnlyBGG,Textbox_GetMultiTextBoxReadOnlyBGG), \
    HLL_EXPORT(GetMultiTextBoxReadOnlyBGB,Textbox_GetMultiTextBoxReadOnlyBGB), \
    HLL_EXPORT(SetMultiTextBoxFrameColor,Textbox_SetMultiTextBoxFrameColor), \
    HLL_EXPORT(GetMultiTextBoxFrameR,Textbox_GetMultiTextBoxFrameR), \
    HLL_EXPORT(GetMultiTextBoxFrameG,Textbox_GetMultiTextBoxFrameG), \
    HLL_EXPORT(GetMultiTextBoxFrameB,Textbox_GetMultiTextBoxFrameB), \
    HLL_EXPORT(SetMultiTextBoxSelectColor,Textbox_SetMultiTextBoxSelectColor), \
    HLL_EXPORT(GetMultiTextBoxSelectR,Textbox_GetMultiTextBoxSelectR), \
    HLL_EXPORT(GetMultiTextBoxSelectG,Textbox_GetMultiTextBoxSelectG), \
    HLL_EXPORT(GetMultiTextBoxSelectB,Textbox_GetMultiTextBoxSelectB), \
    HLL_EXPORT(SetMultiTextBoxCGName,Textbox_SetMultiTextBoxCGName), \
    HLL_EXPORT(SetMultiTextBoxCharSpace,Textbox_SetMultiTextBoxCharSpace), \
    HLL_EXPORT(GetMultiTextBoxCharSpace,Textbox_GetMultiTextBoxCharSpace), \
    HLL_EXPORT(SetMultiTextBoxLineSpace,Textbox_SetMultiTextBoxLineSpace), \
    HLL_EXPORT(GetMultiTextBoxLineSpace,Textbox_GetMultiTextBoxLineSpace), \
    HLL_EXPORT(SetMultiTextBoxPadding,Textbox_SetMultiTextBoxPadding), \
    HLL_EXPORT(GetMultiTextBoxPaddingTop,Textbox_GetMultiTextBoxPaddingTop), \
    HLL_EXPORT(GetMultiTextBoxPaddingBottom,Textbox_GetMultiTextBoxPaddingBottom), \
    HLL_EXPORT(GetMultiTextBoxPaddingLeft,Textbox_GetMultiTextBoxPaddingLeft), \
    HLL_EXPORT(GetMultiTextBoxPaddingRight,Textbox_GetMultiTextBoxPaddingRight), \
    HLL_EXPORT(SetMultiTextBoxVScrollShow,Textbox_SetMultiTextBoxVScrollShow), \
    HLL_EXPORT(IsMultiTextBoxVScrollShow,Textbox_IsMultiTextBoxVScrollShow), \
    HLL_EXPORT(SetMultiTextBoxHScrollShow,Textbox_SetMultiTextBoxHScrollShow), \
    HLL_EXPORT(IsMultiTextBoxHScrollShow,Textbox_IsMultiTextBoxHScrollShow), \
    HLL_EXPORT(SetOpenTextBoxIME,Textbox_SetOpenTextBoxIME), \
    HLL_EXPORT(OpenTextBoxIME,Textbox_OpenTextBoxIME), \
    HLL_EXPORT(CloseTextBoxIME,Textbox_CloseTextBoxIME), \
    HLL_EXPORT(SelectTextBoxAll,Textbox_SelectTextBoxAll), \
    HLL_EXPORT(IsOpenTextBoxIME,Textbox_IsOpenTextBoxIME),

#define TEXTBOX12_STRING_GETTERS(X) X(GetTextBoxText) X(GetTextBoxCGName) X(GetMultiTextBoxText) X(GetMultiTextBoxCGName)
