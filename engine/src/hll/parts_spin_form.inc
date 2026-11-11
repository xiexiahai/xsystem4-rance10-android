/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <math.h>
#include "../parts/textbox_internal.h"
static void SF_SetSpinBoxSize(int a0,int a1,int a2)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; w->width=max(1,min(4096,a1));w->height=max(1,min(4096,a2)); w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static void SF_SetSpinBoxFontProperty(int a0,int a1,int a2,int a3,int a4,int a5,float a6,int a7,int a8,int a9,float a10)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; parts_widget_font_set(&w->font,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10); w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static void SF_GetSpinBoxFontProperty(int a0,int * a1,int * a2,int * a3,int * a4,int * a5,float * a6,int * a7,int * a8,int * a9,float * a10)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; parts_widget_font_get(&w->font,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10); w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static void SF_SetSpinBoxReadOnly(int a0,bool a1)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; if(a1)parts_textbox_open(p,false);w->textbox->readonly=a1; w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static bool SF_IsSpinBoxReadOnly(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->textbox->readonly; }
static void SF_SetSpinBoxValue(int a0,float a1)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; if(!parts_widget_spin_value(p,a1))VM_ERROR("Nonfinite spinbox value"); w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static float SF_GetSpinBoxValue(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->spin.value; }
static void SF_SetSpinBoxDefaultValue(int a0,float a1)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; if(!isfinite(a1))VM_ERROR("Nonfinite spinbox parameter");w->spin.def=a1; w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static float SF_GetSpinBoxDefaultValue(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->spin.def; }
static void SF_SetSpinBoxMaxValue(int a0,float a1)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; if(!isfinite(a1))VM_ERROR("Nonfinite spinbox parameter");if(a1<w->spin.min)VM_ERROR("Inverted spinbox range");w->spin.max=a1;parts_widget_spin_value(p,w->spin.value); w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static float SF_GetSpinBoxMaxValue(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->spin.max; }
static void SF_SetSpinBoxMinValue(int a0,float a1)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; if(!isfinite(a1))VM_ERROR("Nonfinite spinbox parameter");if(a1>w->spin.max)VM_ERROR("Inverted spinbox range");w->spin.min=a1;parts_widget_spin_value(p,w->spin.value); w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static float SF_GetSpinBoxMinValue(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->spin.min; }
static void SF_SetSpinBoxIncrementalValue(int a0,float a1)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; if(!isfinite(a1))VM_ERROR("Nonfinite spinbox parameter");if(a1<0)VM_ERROR("Negative spinbox increment");w->spin.increment=a1; w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static float SF_GetSpinBoxIncrementalValue(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->spin.increment; }
static void SF_SetSpinBoxDecimalPlace(int a0,int a1)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; if(a1<0||a1>9)VM_ERROR("Spinbox decimal places outside 0..9");w->spin.decimals=a1;parts_widget_spin_refresh(p); w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static int SF_GetSpinBoxDecimalPlace(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->spin.decimals; }
static void SF_SetSpinBoxBGColor(int a0,int a1,int a2,int a3)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; w->textbox->bg=(SDL_Color){a1,a2,a3,255}; w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static int SF_GetSpinBoxBGR(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->textbox->bg.r; }
static int SF_GetSpinBoxBGG(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->textbox->bg.g; }
static int SF_GetSpinBoxBGB(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->textbox->bg.b; }
static void SF_SetSpinBoxReadOnlyBGColor(int a0,int a1,int a2,int a3)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; w->textbox->readonly_bg=(SDL_Color){a1,a2,a3,255}; w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static int SF_GetSpinBoxReadOnlyBGR(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->textbox->readonly_bg.r; }
static int SF_GetSpinBoxReadOnlyBGG(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->textbox->readonly_bg.g; }
static int SF_GetSpinBoxReadOnlyBGB(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->textbox->readonly_bg.b; }
static void SF_SetSpinBoxFrameColor(int a0,int a1,int a2,int a3)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; w->textbox->frame=(SDL_Color){a1,a2,a3,255}; w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static int SF_GetSpinBoxFrameR(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->textbox->frame.r; }
static int SF_GetSpinBoxFrameG(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->textbox->frame.g; }
static int SF_GetSpinBoxFrameB(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->textbox->frame.b; }
static void SF_SetSpinBoxSelectColor(int a0,int a1,int a2,int a3)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; w->textbox->selection=(SDL_Color){a1,a2,a3,255}; w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static int SF_GetSpinBoxSelectR(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->textbox->selection.r; }
static int SF_GetSpinBoxSelectG(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->textbox->selection.g; }
static int SF_GetSpinBoxSelectB(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->textbox->selection.b; }
static void SF_SetSpinBoxCGName(int a0,struct string * a1)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; if(a1->size)VM_ERROR("Spinbox CG skin contract is not implemented"); w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static struct string * SF_GetSpinBoxCGName(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return string_dup(w->cg_name); }
static void SF_SetSpinBoxCharSpace(int a0,int a1)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; w->char_space=max(-512,min(512,a1)); w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static int SF_GetSpinBoxCharSpace(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; return w->char_space; }
static void SF_SelectSpinBoxAll(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_SPINBOX);struct parts *p=parts_get(a0);(void)p;(void)w; parts_widget_spin_select_all(p); w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static void SF_SetFormSize(int a0,int a1,int a2)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; w->width=max(w->form.min_w,min(w->form.max_w,a1));w->height=max(w->form.min_h,min(w->form.max_h,a2)); w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static void SF_SetFormMinSize(int a0,int a1,int a2)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; if(a1<1||a2<1||a1>4096||a2>4096)VM_ERROR("Invalid form bound");if(a1>w->form.max_w||a2>w->form.max_h)VM_ERROR("Inverted form range");w->form.min_w=a1;w->form.min_h=a2; w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static int SF_GetFormMinWidth(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; return w->form.min_w; }
static int SF_GetFormMinHeight(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; return w->form.min_h; }
static void SF_SetFormMaxSize(int a0,int a1,int a2)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; if(a1<1||a2<1||a1>4096||a2>4096)VM_ERROR("Invalid form bound");if(a1<w->form.min_w||a2<w->form.min_h)VM_ERROR("Inverted form range");w->form.max_w=a1;w->form.max_h=a2; w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static int SF_GetFormMaxWidth(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; return w->form.max_w; }
static int SF_GetFormMaxHeight(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; return w->form.max_h; }
static void SF_SetFormResizable(int a0,bool a1)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; if(a1)VM_ERROR("Form Resizable behavior is not implemented"); w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static bool SF_IsFormResizable(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; return false; }
static void SF_SetFormCaptionPlusHeight(int a0,int a1)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; if(a1<0||a1>512)VM_ERROR("Invalid form caption height");w->form.caption_plus=a1; w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static int SF_GetFormCaptionPlusHeight(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; return w->form.caption_plus; }
static void SF_SetFormIcon(int a0,struct string * a1)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; if(a1->size)VM_ERROR("Form icon contract is not implemented"); w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static struct string * SF_GetFormIcon(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; return string_ref(&EMPTY_STRING); }
static void SF_SetFormTitle(int a0,struct string * a1)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; free_string(w->text);w->text=string_dup(a1); w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static struct string * SF_GetFormTitle(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; return string_dup(w->text); }
static void SF_SetFormCaptionColor(int a0,int a1,int a2,int a3)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; w->form.caption=(SDL_Color){a1,a2,a3,255}; w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static int SF_GetFormCaptionR(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; return w->form.caption.r; }
static int SF_GetFormCaptionG(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; return w->form.caption.g; }
static int SF_GetFormCaptionB(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; return w->form.caption.b; }
static void SF_SetFormClientColor(int a0,int a1,int a2,int a3)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; w->color=(SDL_Color){a1,a2,a3,255}; w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static int SF_GetFormClientR(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; return w->color.r; }
static int SF_GetFormClientG(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; return w->color.g; }
static int SF_GetFormClientB(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; return w->color.b; }
static void SF_SetFormFold(int a0,bool a1)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; if(a1)VM_ERROR("Form Fold behavior is not implemented"); w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static bool SF_IsFormFold(int a0)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);(void)p;(void)w; return false; }
static void SF_SetFormEnableCloseButton(int a0,bool a1)
{ struct parts_widget *w=parts_widget_get(a0,PARTS_WIDGET_FORM);struct parts *p=parts_get(a0);w->form.close_enabled=a1;w->dirty=true;parts_component_dirty(p);parts_dirty(p); }
static bool SF_IsFormEnableCloseButton(int a0)
{ return parts_widget_get(a0,PARTS_WIDGET_FORM)->form.close_enabled; }
static void SF_FormClose(int no)
{parts_widget_get(no,PARTS_WIDGET_FORM);parts_form_close(parts_get(no),false);}
static void SF_CancelFormClose(int no)
{parts_widget_get(no,PARTS_WIDGET_FORM);parts_form_close(parts_get(no),true);}
#define SPIN_FORM_HLL_EXPORTS \
    HLL_EXPORT(FormClose, SF_FormClose), \
    HLL_EXPORT(CancelFormClose, SF_CancelFormClose), \
    HLL_EXPORT(SetSpinBoxSize, SF_SetSpinBoxSize), \
    HLL_EXPORT(SetSpinBoxFontProperty, SF_SetSpinBoxFontProperty), \
    HLL_EXPORT(GetSpinBoxFontProperty, SF_GetSpinBoxFontProperty), \
    HLL_EXPORT(SetSpinBoxReadOnly, SF_SetSpinBoxReadOnly), \
    HLL_EXPORT(IsSpinBoxReadOnly, SF_IsSpinBoxReadOnly), \
    HLL_EXPORT(SetSpinBoxValue, SF_SetSpinBoxValue), \
    HLL_EXPORT(GetSpinBoxValue, SF_GetSpinBoxValue), \
    HLL_EXPORT(SetSpinBoxDefaultValue, SF_SetSpinBoxDefaultValue), \
    HLL_EXPORT(GetSpinBoxDefaultValue, SF_GetSpinBoxDefaultValue), \
    HLL_EXPORT(SetSpinBoxMaxValue, SF_SetSpinBoxMaxValue), \
    HLL_EXPORT(GetSpinBoxMaxValue, SF_GetSpinBoxMaxValue), \
    HLL_EXPORT(SetSpinBoxMinValue, SF_SetSpinBoxMinValue), \
    HLL_EXPORT(GetSpinBoxMinValue, SF_GetSpinBoxMinValue), \
    HLL_EXPORT(SetSpinBoxIncrementalValue, SF_SetSpinBoxIncrementalValue), \
    HLL_EXPORT(GetSpinBoxIncrementalValue, SF_GetSpinBoxIncrementalValue), \
    HLL_EXPORT(SetSpinBoxDecimalPlace, SF_SetSpinBoxDecimalPlace), \
    HLL_EXPORT(GetSpinBoxDecimalPlace, SF_GetSpinBoxDecimalPlace), \
    HLL_EXPORT(SetSpinBoxBGColor, SF_SetSpinBoxBGColor), \
    HLL_EXPORT(GetSpinBoxBGR, SF_GetSpinBoxBGR), \
    HLL_EXPORT(GetSpinBoxBGG, SF_GetSpinBoxBGG), \
    HLL_EXPORT(GetSpinBoxBGB, SF_GetSpinBoxBGB), \
    HLL_EXPORT(SetSpinBoxReadOnlyBGColor, SF_SetSpinBoxReadOnlyBGColor), \
    HLL_EXPORT(GetSpinBoxReadOnlyBGR, SF_GetSpinBoxReadOnlyBGR), \
    HLL_EXPORT(GetSpinBoxReadOnlyBGG, SF_GetSpinBoxReadOnlyBGG), \
    HLL_EXPORT(GetSpinBoxReadOnlyBGB, SF_GetSpinBoxReadOnlyBGB), \
    HLL_EXPORT(SetSpinBoxFrameColor, SF_SetSpinBoxFrameColor), \
    HLL_EXPORT(GetSpinBoxFrameR, SF_GetSpinBoxFrameR), \
    HLL_EXPORT(GetSpinBoxFrameG, SF_GetSpinBoxFrameG), \
    HLL_EXPORT(GetSpinBoxFrameB, SF_GetSpinBoxFrameB), \
    HLL_EXPORT(SetSpinBoxSelectColor, SF_SetSpinBoxSelectColor), \
    HLL_EXPORT(GetSpinBoxSelectR, SF_GetSpinBoxSelectR), \
    HLL_EXPORT(GetSpinBoxSelectG, SF_GetSpinBoxSelectG), \
    HLL_EXPORT(GetSpinBoxSelectB, SF_GetSpinBoxSelectB), \
    HLL_EXPORT(SetSpinBoxCGName, SF_SetSpinBoxCGName), \
    HLL_EXPORT(GetSpinBoxCGName, SF_GetSpinBoxCGName), \
    HLL_EXPORT(SetSpinBoxCharSpace, SF_SetSpinBoxCharSpace), \
    HLL_EXPORT(GetSpinBoxCharSpace, SF_GetSpinBoxCharSpace), \
    HLL_EXPORT(SelectSpinBoxAll, SF_SelectSpinBoxAll), \
    HLL_EXPORT(SetFormSize, SF_SetFormSize), \
    HLL_EXPORT(SetFormMinSize, SF_SetFormMinSize), \
    HLL_EXPORT(GetFormMinWidth, SF_GetFormMinWidth), \
    HLL_EXPORT(GetFormMinHeight, SF_GetFormMinHeight), \
    HLL_EXPORT(SetFormMaxSize, SF_SetFormMaxSize), \
    HLL_EXPORT(GetFormMaxWidth, SF_GetFormMaxWidth), \
    HLL_EXPORT(GetFormMaxHeight, SF_GetFormMaxHeight), \
    HLL_EXPORT(SetFormResizable, SF_SetFormResizable), \
    HLL_EXPORT(IsFormResizable, SF_IsFormResizable), \
    HLL_EXPORT(SetFormCaptionPlusHeight, SF_SetFormCaptionPlusHeight), \
    HLL_EXPORT(GetFormCaptionPlusHeight, SF_GetFormCaptionPlusHeight), \
    HLL_EXPORT(SetFormIcon, SF_SetFormIcon), \
    HLL_EXPORT(GetFormIcon, SF_GetFormIcon), \
    HLL_EXPORT(SetFormTitle, SF_SetFormTitle), \
    HLL_EXPORT(GetFormTitle, SF_GetFormTitle), \
    HLL_EXPORT(SetFormCaptionColor, SF_SetFormCaptionColor), \
    HLL_EXPORT(GetFormCaptionR, SF_GetFormCaptionR), \
    HLL_EXPORT(GetFormCaptionG, SF_GetFormCaptionG), \
    HLL_EXPORT(GetFormCaptionB, SF_GetFormCaptionB), \
    HLL_EXPORT(SetFormClientColor, SF_SetFormClientColor), \
    HLL_EXPORT(GetFormClientR, SF_GetFormClientR), \
    HLL_EXPORT(GetFormClientG, SF_GetFormClientG), \
    HLL_EXPORT(GetFormClientB, SF_GetFormClientB), \
    HLL_EXPORT(SetFormFold, SF_SetFormFold), \
    HLL_EXPORT(IsFormFold, SF_IsFormFold), \
    HLL_EXPORT(SetFormEnableCloseButton, SF_SetFormEnableCloseButton), \
    HLL_EXPORT(IsFormEnableCloseButton, SF_IsFormEnableCloseButton),
