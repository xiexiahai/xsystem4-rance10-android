/* Copyright (C) 2023 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include <assert.h>
#include <string.h>

#include "system4/ain.h"
#include "system4/file.h"
#include "system4/string.h"

#include "asset_manager.h"
#include "xsystem4.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "parts.h"
#include "parts_widget.h"
#include "parts_activity.h"
#include "parts_message_window.h"
#include "../parts/parts_internal.h"
#include "hll.h"
#include "parts_geometry_query.inc"
#include "parts_spin_form.inc"
#include "parts_listbox.inc"

static void PartsEngine_ModuleInit(void)
{
	PE_Init();
}

static void PartsEngine_SetHGaugeReverse(int number, bool reverse, int state)
{
	if (!PE_SetHGaugeReverse(number,reverse,state))
		WARNING("Cannot set HGauge reverse on part %d, state %d",number,state);
}

static void PartsEngine_SetComment(int number, struct string *comment)
{
	struct parts *p = parts_get(number);
	if (p->comment) free_string(p->comment);
	p->comment = string_dup(comment);
}

static struct string *PartsEngine_GetComment(int number)
{
	struct parts *p = parts_get(number);
	return p->comment ? string_dup(p->comment) : string_ref(&EMPTY_STRING);
}

static int PartsEngine_GetComponentAddColorR(int number)
{
	int r, g, b;
	PE_GetAddColor(number, &r, &g, &b);
	return r;
}

static int PartsEngine_GetComponentAddColorG(int number)
{
	int r, g, b;
	PE_GetAddColor(number, &r, &g, &b);
	return g;
}

static int PartsEngine_GetComponentAddColorB(int number)
{
	int r, g, b;
	PE_GetAddColor(number, &r, &g, &b);
	return b;
}

static int PartsEngine_GetComponentMulColorR(int number)
{
	int r, g, b;
	PE_GetMultiplyColor(number, &r, &g, &b);
	return r;
}

static int PartsEngine_GetComponentMulColorG(int number)
{
	int r, g, b;
	PE_GetMultiplyColor(number, &r, &g, &b);
	return g;
}

static int PartsEngine_GetComponentMulColorB(int number)
{
	int r, g, b;
	PE_GetMultiplyColor(number, &r, &g, &b);
	return b;
}

static void PartsEngine_SetComponentType12(int number, int type, int state)
{
	if(type==15) {
		PE_SetUserComponentName(number,&EMPTY_STRING);
		PE_SetLayoutBoxLayoutType(number,0);
		return;
	}
    switch(type) {
    case 4:type=PARTS_WIDGET_TEXTBOX;break;
    case 5:type=PARTS_WIDGET_LISTBOX;break;
    case 7:type=PARTS_WIDGET_MULTITEXTBOX;break;
    case 10:type=19;break;
    case 11:type=PARTS_WIDGET_SPINBOX;break;
    case 13:type=PARTS_WIDGET_FORM;break;
    case 12:type=PARTS_WIDGET_PANEL;break;
    case 17:case 18:case 19:case 20:case 21:case 22:case 23:case 24:type-=6;break;
    case 26:case 27:case 28:type-=6;break;
    case 0:case 1:case 2:case 3:case 8:case 9:break;
    default:VM_ERROR("Unsupported AIN12 component type %d",type);
    }

	PE_SetComponentType(number, type, state);
}

static int PartsEngine_GetComponentType12(int number, int state)
{
	struct parts *p=parts_try_get(number);
	if(p && p->user_component_name) return 15;
	int type = PE_GetComponentType(number, state);
	if(type==PARTS_WIDGET_TEXTBOX)return 4;
    if(type==PARTS_WIDGET_LISTBOX)return 5;
    if(type==PARTS_WIDGET_MULTITEXTBOX)return 7;
    if(type==PARTS_WIDGET_PANEL)return 12;
    if(type==PARTS_WIDGET_SPINBOX)return 11;
    if(type==PARTS_WIDGET_FORM)return 13;
    if(type==19)return 10;
    return type >= 11 ? type+6 : type;
}
static int PartsEngine_GetParentPartsNumber12(int number)
{
	return PE_GetParentPartsNumber12(number);
}

static void PartsEngine_AddChild(int number,int child)
{
	if (ain && ain->version >= 11)
		PE_SetParentPartsNumber12(child,number);
	else
		PE_SetParentPartsNumber(child,number);
}
static int PartsEngine_NumofChild(int number)
{
	struct parts *p=parts_try_get(number),*child;int count=0;
	if(p) TAILQ_FOREACH(child,&p->children,child_list_entry) count++;
	return count;
}
static int PartsEngine_GetChild(int number,int index)
{
	struct parts *p=parts_try_get(number),*child;int i=0;
	if(p) TAILQ_FOREACH(child,&p->children,child_list_entry) if(i++==index) return child->no;
	return 0;
}
static int PartsEngine_GetChildIndex(int number,int child_number)
{
	struct parts *p=parts_try_get(number),*child;int i=0;
	if(p) TAILQ_FOREACH(child,&p->children,child_list_entry) {if(child->no==child_number) return i;i++;}
	return -1;
}
static bool PartsEngine_IsExistChild(int number,int child) {return PartsEngine_GetChildIndex(number,child)>=0;}
static void PartsEngine_Parts_SetWheelable(int number,bool enabled) {parts_get(number)->wheelable=enabled;}
static void PartsEngine_SetEnableInputProcess(int number,bool enabled) {parts_get(number)->input_disabled=!enabled;}
static bool PartsEngine_IsEnableInputProcess(int number) {return !parts_get(number)->input_disabled;}
static struct string *PartsEngine_GetActivityPartsName12(struct string *name,int number)
{
	struct string *s=string_ref(&EMPTY_STRING);PE_GetActivityPartsName(&s,name,number);return s;
}
static struct string *PartsEngine_GetActivityIntentDataDestination12(int index,struct string *name,struct string *part)
{
	struct string *s=string_ref(&EMPTY_STRING);PE_GetActivityIntentDataDestination(&s,index,name,part);return s;
}
static struct string *PartsEngine_GetMessageVariableString12(int index)
{
	struct string *s=string_ref(&EMPTY_STRING);PE_GetMessageVariableString(index,&s);return s;
}
static struct string *PartsEngine_GetPartsCGName12(int number,int state)
{
	struct string *s=string_ref(&EMPTY_STRING);PE_GetPartsCGName(number,&s,state);return s;
}
static struct string *PartsEngine_GetPartsSoundName12(int number,int state)
{
	struct string *s=string_ref(&EMPTY_STRING);PE_GetPartsSoundName(number,&s,state);return s;
}

static int PartsEngine_GetTextCharSpace(int number,int state)
{
	if (!parts_state_valid(--state)) return 0;
	struct parts *p=parts_try_get(number);
	return p && p->states[state].type==PARTS_TEXT ? p->states[state].text.ts.font_spacing : 0;
}

static int PartsEngine_GetTextLineSpace(int number,int state)
{
	if (!parts_state_valid(--state)) return 0;
	struct parts *p=parts_try_get(number);
	return p && p->states[state].type==PARTS_TEXT ? p->states[state].text.line_space : 0;
}

static int *user_parts_numbers,nr_user_parts_numbers;
static int PartsEngine_CreateUserPartsNumber(void)
{
	// Script-owned IDs occupy the public range; PACT/system IDs use the
	// separate GetFreeNumber range above one billion.
	static int next=1;
	while(next<1000000000 && (PE_IsExist(next) || PE_get_controller_index(next)>=0)) next++;
	if(next>=1000000000) VM_ERROR("User part number range exhausted");
	int number=next++;
	user_parts_numbers=xrealloc(user_parts_numbers,sizeof(int)*(nr_user_parts_numbers+1));
	user_parts_numbers[nr_user_parts_numbers++]=number;
	return number;
}
static void PartsEngine_ReleaseUserPartsNumber(int number)
{
	for(int i=0;i<nr_user_parts_numbers;i++) if(user_parts_numbers[i]==number) {
		memmove(user_parts_numbers+i,user_parts_numbers+i+1,sizeof(int)*(--nr_user_parts_numbers-i));return;
	}
	VM_ERROR("User part number %d is not reserved",number);
}

static void PartsEngine_ModuleFini(void)
{
	PE_Reset();
	free(user_parts_numbers);user_parts_numbers=NULL;nr_user_parts_numbers=0;
}

static void PartsEngine_Update(int passed_time, bool is_skip, bool message_window_show)
{
	PE_Update(passed_time, message_window_show);
}
static void PartsEngine_UpdateComponent12(int base_time, int scaled_time,
		bool message_window_show, float mul, float alpha)
{
	PE_SetMessageWindowRates(mul, alpha);
	PE_Update12(base_time, scaled_time, message_window_show);
}

static void PartsEngine_Update_Pascha3PC(struct string *xxx1, struct string *xxx2, int passed_time, bool is_skip, bool message_window_show)
{
	PE_Update(passed_time, message_window_show);
}

// Oyako Rankan
static bool PartsEngine_AddDrawCutCGToPartsConstructionProcess_old(int parts_no,
		struct string *cg_name, int dx, int dy, int sx, int sy, int w, int h,
		int state)
{
	return PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name, dx, dy, w, h,
			sx, sy, w, h, 0, state);
}

// Oyako Rankan
static bool PartsEngine_AddCopyCutCGToPartsConstructionProcess_old(int parts_no,
		struct string *cg_name, int dx, int dy, int sx, int sy, int w, int h,
		int state)
{
	return PE_AddCopyCutCGToPartsConstructionProcess(parts_no, cg_name, dx, dy, w, h,
			sx, sy, w, h, 0, state);
}

// Rance 9: argument types changed from int to float
static void PartsEngine_SetComponentPos(int parts_no, float x, float y)
{
	PE_SetPos(parts_no, x, y);
}

// Rance 9: return type changed from int to float
static float PartsEngine_Parts_GetPartsUpperLeftPosX(int parts_no, int state)
{
	return PE_GetPartsUpperLeftPosX(parts_no, state);
}

static float PartsEngine_Parts_GetPartsUpperLeftPosY(int parts_no, int state)
{
	return PE_GetPartsUpperLeftPosY(parts_no, state);
}

// Rance 9: return type changed from int to float
static float PartsEngine_Parts_GetComponentPosX(int parts_no)
{
	return PE_GetPartsX(parts_no);
}

static float PartsEngine_GetComponentPosY(int parts_no)
{
	return PE_GetPartsY(parts_no);
}

// Rance 9: X/Y coordinate types changed from int to float
static void PartsEngine_AddComponentMotionPos(int parts_no, float begin_x, float begin_y,
		float end_x, float end_y, int begin_t, int end_t,
		struct string *curve_name)
{
	PE_AddMotionPos_curve(parts_no, begin_x, begin_y, end_x, end_y,
			begin_t, end_t, curve_name);
}

static void PartsEngine_add_construction_process(union vm_value *ints,
		union vm_value *floats, union vm_value *strings)
{
	int parts_no    = ints[0].i;
	int state       = ints[1].i;
	int command     = ints[2].i;
	int interp_type = ints[3].i;
	int sx          = ints[4].i;
	int sy          = ints[5].i;
	int sw          = ints[6].i;
	int sh          = ints[7].i;
	int dx          = ints[8].i;
	int dy          = ints[9].i;
	int dw          = ints[12].i;
	int dh          = ints[13].i;
	int r           = ints[14].i;
	int g           = ints[15].i;
	int b           = ints[16].i;
	int a           = ints[17].i;
	// int r2       = ints[18].i;
	// int g2       = ints[19].i;
	// int b2       = ints[20].i;
	int char_space  = ints[21].i;
	int line_space  = ints[22].i;
	int font_type   = ints[23].i;
	int font_size   = ints[24].i;
	int font_r      = ints[25].i;
	int font_g      = ints[26].i;
	int font_b      = ints[27].i;
	int edge_r      = ints[28].i;
	int edge_g      = ints[29].i;
	int edge_b      = ints[30].i;
	int full_size   = ints[31].i;
	float bold_weight = floats[0].f;
	float edge_weight = floats[1].f;
	struct string *text    = heap_get_string(strings[0].i);
	struct string *cg_name = heap_get_string(strings[1].i);

	switch (command) {
	case 0:  // CASConstructionProcess::SetCreate
		PE_AddCreateToPartsConstructionProcess(parts_no, dw, dh, state);
		break;
	case 1:  // CASConstructionProcess::SetCreatePixelOnly
		PE_AddCreatePixelOnlyToPartsConstructionProcess(parts_no, dw, dh, state);
		break;
	case 2:  // CASConstructionProcess::SetCreateCG
		PE_AddCreateCGToProcess(parts_no, cg_name, state);
		break;
	case 3:  // CASConstructionProcess::SetFill
		PE_AddFillToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, state);
		break;
	case 4:  // CASConstructionProcess::SetFillAlphaColor
		PE_AddFillAlphaColorToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, a, state);
		break;
	case 5:  // CASConstructionProcess::SetFillAMap
		PE_AddFillAMapToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, a, state);
		break;
	case 6:  // CASConstructionProcess::SetFillWithAlpha
		PE_AddFillWithAlphaToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, a, state);
		break;
	case 7:  // CASConstructionProcess::SetDrawText
		PE_AddDrawTextToPartsConstructionProcess(parts_no,
				dx, dy, text, font_type, font_size,
				font_r, font_g, font_b, bold_weight,
				edge_r, edge_g, edge_b, edge_weight,
				char_space, line_space, state);
		break;
	case 8:  // CASConstructionProcess::SetCopyText
		PE_AddCopyTextToPartsConstructionProcess(parts_no,
				dx, dy, text, font_type, font_size,
				font_r, font_g, font_b, bold_weight,
				edge_r, edge_g, edge_b, edge_weight,
				char_space, line_space, state);
		break;
	case 23:  // CASConstructionProcess::SetAlphaBlendText
		PE_AddAlphaBlendTextToPartsConstructionProcess(parts_no,
				dx, dy, text, font_type, font_size,
				font_r, font_g, font_b, bold_weight,
				edge_r, edge_g, edge_b, edge_weight,
				char_space, line_space, state);
		break;
	case 9:  // CASConstructionProcess::SetFillGradationHorizon
		WARNING("AddConstructProcess: FillGradationHorizon unimplemented");
		break;
	case 10:  // CASConstructionProcess::SetDrawRect
		PE_AddDrawRectToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, state);
		break;
	case 11:  // CASConstructionProcess::SetCutCGBlend (equal scale)
		PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, dw, dh,
				interp_type, state);
		break;
	case 12:  // CASConstructionProcess::SetCutCGCopy (equal scale)
		PE_AddCopyCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, dw, dh,
				interp_type, state);
		break;
	case 13:  // CASConstructionProcess::SetCutCGScaleBlend
		PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, sw, sh,
				interp_type, state);
		break;
	case 14:  // CASConstructionProcess::SetCutCGScaleCopy
		PE_AddCopyCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, sw, sh,
				interp_type, state);
		break;
	case 15:  // CASConstructionProcess::SetGrayFilter
		PE_AddGrayFilterToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, full_size, state);
		break;
	case 16:  // CASConstructionProcess::SetAddFilter
		WARNING("AddConstructProcess: AddFilter unimplemented");
		break;
	case 17:  // CASConstructionProcess::SetMulFilter
		WARNING("AddConstructProcess: MulFilter unimplemented");
		break;
	case 18:  // CASConstructionProcess::SetDrawLine
		WARNING("AddConstructProcess: DrawLine unimplemented");
		break;
	case 19:  // SetCutCGAlphaBlend (equal scale)
	case 20:  // SetCutCGScaleAlphaBlend
		PE_AddAlphaBlendCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, command == 19 ? dw : sw,
				command == 19 ? dh : sh, interp_type, state);
		break;
	case 21:  // SetCutCGOnlyAlpha (equal scale)
	case 22:  // SetCutCGScaleOnlyAlpha
		PE_AddOnlyAlphaCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, command == 21 ? dw : sw,
				command == 21 ? dh : sh, interp_type, state);
		break;
	case 29:  // SetCGBlend: whole image at destination origin, native size
		if (!PE_AddDrawCGToPartsConstructionProcess(parts_no, cg_name, dx, dy, state))
			WARNING("AddConstructProcess: whole CG blend rejected");
		break;
	default:
		WARNING("AddConstructProcess: unknown command %d", command);
		break;
	}
}

static void PartsEngine_AddPartsConstructionProcess(struct page **ints,
		struct page **floats, struct page **strings)
{
	if (!ints || !*ints || (*ints)->nr_vars < 32
			|| !floats || !*floats || (*floats)->nr_vars < 2
			|| !strings || !*strings || (*strings)->nr_vars < 2)
		VM_ERROR("Invalid arrays for AddPartsConstructionProcess (need 32 ints, 2 floats, 2 strings)");
	PartsEngine_add_construction_process((*ints)->values, (*floats)->values, (*strings)->values);
}

static void PartsEngine_AddPartsConstructionProcess12(int number, struct page **ints,
		struct page **floats, struct page **strings, int state)
{
	if (!ints || !*ints || (*ints)->nr_vars < 32 || !floats || !*floats ||
		(*floats)->nr_vars < 2 || !strings || !*strings || (*strings)->nr_vars < 2)
		VM_ERROR("Invalid AIN12 construction process arrays");
	union vm_value translated[34] = {0};
	translated[0].i=number; translated[1].i=state;
	// AIN12 adds the second color's alpha at index 19. The legacy dispatch
	// layout lacks that slot; shift the subsequent font/spacing/filter fields
	// back once, in addition to supplying the separate number/state arguments.
	memcpy(translated+2,(*ints)->values,19*sizeof(*translated));
	memcpy(translated+21,(*ints)->values+20,12*sizeof(*translated));
	PartsEngine_add_construction_process(translated,(*floats)->values,(*strings)->values);
}

// Rance9 predates the layer functions at func_id 3-5.  Use the numbering from
// later PartsEngine versions as canonical and adjust Rance9's ids to match.
static bool parts_func_uses_rance9_ids;

// Generic dispatch function for PartsEngine operations.
// func_id selects the operation; arguments and return values are passed
// through three typed arrays (int/bool, float, string).
//
// Calling convention (from game script):
//   1. Push input values into the appropriate arrays.
//   2. Push a placeholder (0 or "") for each output slot.
//   3. Call PartsFunc; outputs are written back into the placeholder slots.
static int PartsEngine_PartsFunc(int func_id, struct page **array_int,
		struct page **array_float, struct page **array_string)
{
	int nr_ints = (array_int && *array_int) ? (*array_int)->nr_vars : 0;
	union vm_value *ints = nr_ints ? (*array_int)->values : NULL;
	int nr_floats = (array_float && *array_float) ? (*array_float)->nr_vars : 0;
	union vm_value *floats = nr_floats ? (*array_float)->values : NULL;
	int nr_strings = (array_string && *array_string) ? (*array_string)->nr_vars : 0;
	union vm_value *strings = nr_strings ? (*array_string)->values : NULL;

#define REQUIRE_INTS(n) \
	if (nr_ints != (n)) VM_ERROR("Invalid arguments for PartsFunc %d: expected %d ints, got %d", func_id, (n), nr_ints)
#define REQUIRE_FLOATS(n) \
	if (nr_floats < (n)) VM_ERROR("Invalid arguments for PartsFunc %d: expected %d floats, got %d", func_id, (n), nr_floats)
#define REQUIRE_STRINGS(n) \
	if (nr_strings < (n)) VM_ERROR("Invalid arguments for PartsFunc %d: expected %d strings, got %d", func_id, (n), nr_strings)

	int canonical_func_id = func_id;
	if (parts_func_uses_rance9_ids && canonical_func_id > 2)
		canonical_func_id += 3;

	switch (canonical_func_id) {
	case 0:  // void SetActiveLayer(int layer)
		REQUIRE_INTS(1);
		PE_set_active_controller(ints[0].i);
		return 1;
	case 1:  // int GetActiveLayer()
		REQUIRE_INTS(1);
		ints[0].i = PE_get_active_controller();
		return 1;
	case 2:  // int GetSystemOverlayLayer()
		REQUIRE_INTS(1);
		ints[0].i = PE_get_system_controller();
		return 1;
	// Layers are identified by their index in the controller stack, so the
	// layer id and index conversions are identity functions.
	case 3:  // int GetLayerIndex(int layer_id)
	case 4:  // int GetLayerID(int layer_index)
		REQUIRE_INTS(2);
		ints[1].i = ints[0].i;
		return 1;
	case 5:  // int NumofLayer()
		REQUIRE_INTS(1);
		ints[0].i = PE_get_nr_controllers();
		return 1;
	case 6:  // void PauseMotion(bool pause)
		REQUIRE_INTS(1);
		PE_PauseMotion(!!ints[0].i);
		return 1;
	case 7:  // void SetWantSave(int parts_no, bool want_save)
		REQUIRE_INTS(2);
		PE_parts_set_want_save(ints[0].i, !!ints[1].i);
		return 1;
	case 9:  // bool SaveThumbnail(string filename, int reduction_factor)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(1);
		ints[1].i = PE_save_thumbnail(heap_get_string(strings[0].i), ints[0].i);
		return 1;
	case 43:  // float PARTS_GetAbsoluteX(int number)
		REQUIRE_INTS(1);
		REQUIRE_FLOATS(1);
		floats[0].f = PE_parts_get_absolute_x(ints[0].i);
		return 1;
	case 44:  // float PARTS_GetAbsoluteY(int number)
		REQUIRE_INTS(1);
		REQUIRE_FLOATS(1);
		floats[0].f = PE_parts_get_absolute_y(ints[0].i);
		return 1;
	case 45:  // int PARTS_GetAbsoluteZ(int number)
		REQUIRE_INTS(2);
		ints[1].i = PE_parts_get_absolute_z(ints[0].i);
		return 1;
	case 48:  // void PARTS_SetLockInputState(int number, bool lock)
		REQUIRE_INTS(2);
		PE_parts_set_lock_input_state(ints[0].i, !!ints[1].i);
		return 1;
	case 60:  // void AppendChild(int number, int child_number)
		REQUIRE_INTS(2);
		PE_SetParentPartsNumber(ints[1].i, ints[0].i);
		return 1;
	case 94:  // void SetLayoutBoxPadding(int parts_no, int top, int bottom, int left, int right)
		REQUIRE_INTS(5);
		PE_set_layoutbox_padding(ints[0].i, ints[1].i, ints[2].i, ints[3].i, ints[4].i);
		return 1;
	case 95:  // int GetLayoutBoxPaddingTop(int parts_no)
		REQUIRE_INTS(2);
		ints[1].i = PE_get_layoutbox_padding_top(ints[0].i);
		return 1;
	case 96:  // int GetLayoutBoxPaddingBottom(int parts_no)
		REQUIRE_INTS(2);
		ints[1].i = PE_get_layoutbox_padding_bottom(ints[0].i);
		return 1;
	case 97:  // int GetLayoutBoxPaddingLeft(int parts_no)
		REQUIRE_INTS(2);
		ints[1].i = PE_get_layoutbox_padding_left(ints[0].i);
		return 1;
	case 98:  // int GetLayoutBoxPaddingRight(int parts_no)
		REQUIRE_INTS(2);
		ints[1].i = PE_get_layoutbox_padding_right(ints[0].i);
		return 1;
	case 106:  // void GetPartsCGSurfaceArea(int parts_no, int *x, int *y, int *w, int *h, int state)
		REQUIRE_INTS(6);
		PE_GetPartsCGSurfaceArea(ints[0].i, &ints[1].i, &ints[2].i, &ints[3].i, &ints[4].i, ints[5].i);
		return 1;
	case 112: {  // string GetText(int parts_no, int state)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(1);
		struct string *text = PE_GetText(ints[0].i, ints[1].i);
		if (text) {
			heap_string_assign(strings[0].i, text);
			free_string(text);
		}
		return 1;
	}
	case 162:  // AddConstructProcess(ArrayInt[32], ArrayFloat[2], ArrayString[2])
		REQUIRE_INTS(32);
		REQUIRE_FLOATS(2);
		REQUIRE_STRINGS(2);
		PartsEngine_add_construction_process(ints, floats, strings);
		return 1;
	case 165:  // bool InitPartsMovie(int parts_no, int width, int height, int bg_r, int bg_g, int bg_b, int state)
		REQUIRE_INTS(8);
		ints[7].i = PE_init_parts_movie(ints[0].i, ints[1].i, ints[2].i, ints[3].i, ints[4].i, ints[5].i, ints[6].i);
		return 1;
	case 166:  // int GetMovieSprite(int parts_no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PE_get_movie_sprite(ints[0].i, ints[1].i);
		return 1;
	default:
		WARNING("Unknown func_id: %d", func_id);
		return 0;
	}
#undef REQUIRE_INTS
}

HLL_WARN_UNIMPLEMENTED(, void, PartsEngine, StopSoundWithoutSystemSound);

// ---------------------------------------------------------------------------
// Rance 03 port: controller accessors and back-scene functions.
//
// Ragged edges of the PartsEngine interface generation that Rance 03 uses.
// The controller accessors all have working PE_ implementations already; only
// the HLL wrappers were missing. GetActiveController in particular sits on the
// game's startup path (main -> SYS_Init -> PARTS_NotifyChangedActiveLayerEvent
// -> PARTS_GetActiveLayer -> GetActiveController), so without it the game's
// layer bookkeeping never sees a valid controller.
// ---------------------------------------------------------------------------

static void PartsEngine_SetActiveController(int id)
{
	PE_set_active_controller(id);
}

static int PartsEngine_GetActiveController(void)
{
	return PE_get_active_controller();
}

static int PartsEngine_GetControllerLength(void)
{
	return PE_get_nr_controllers();
}

static int PartsEngine_GetSystemOverlayController(void)
{
	return PE_get_system_controller();
}

static int PartsEngine_GetControllerIndex(int id)
{
	return PE_get_controller_index(id);
}

static int PartsEngine_GetControllerID(int index)
{
	return PE_get_controller_id(index);
}

static bool PartsEngine_IsExistActivityFile(struct string *name)
{
	if (!name || !name->size)
		return false;
	// The shipped activities live in Pact.afa as *.pactex. The archive
	// manager resolves both extensionless and explicit basenames.
	if (asset_exists_by_name(ASSET_PACT, name->text, NULL))
		return true;
	char *path = gamedir_path(name->text);
	bool exists = file_exists(path);
	free(path);
	if (exists || file_extension(name->text))
		return exists;
	struct string *filename = string_dup(name);
	string_append_cstr(&filename, ".pact", 5);
	path = gamedir_path(filename->text);
	exists = file_exists(path);
	free(path);
	free_string(filename);
	return exists;
}


static void PartsEngine_PreLink(void);

#include "parts_widget.inc"
#include "parts_textbox.inc"
#include "parts_scroll_override.inc"

#define WIDGET12_STRING_GETTERS(X) \
 X(GetButtonCGName) X(GetButtonFlatName) X(GetButtonText) \
 X(GetCheckBoxCGName) X(GetCheckBoxFlatName) X(GetCheckBoxText) \
 X(GetVScrollbarCGName) X(GetVScrollbarFlatName) X(GetHScrollbarCGName) X(GetHScrollbarFlatName)
#define WIDGET12_GET(name) static struct string *Widget12_##name(int number) { \
 struct string *s=string_ref(&EMPTY_STRING);Widget_##name(number,&s);return s; }
WIDGET12_STRING_GETTERS(WIDGET12_GET)
#undef WIDGET12_GET

static void PartsEngine_SetNumeralFont(int no, int type, int size, int r, int g, int b,
		float bold, int er, int eg, int eb, float edge, int state)
{
	if (!PE_SetNumeralFont(no,type,size,r,g,b,bold,er,eg,eb,edge,state))
		VM_ERROR("Invalid numeral text font");
}

HLL_LIBRARY(PartsEngine,
        HLL_EXPORT(GetComponentAbsolutePos, PartsEngine_GetComponentAbsolutePos),
        HLL_EXPORT(Parts_GetPartsUpperLeftPos, PartsEngine_GetPartsUpperLeftPos),
        HLL_EXPORT(Parts_GetPartsSize, PartsEngine_GetPartsSize),
        HLL_EXPORT(GetPartsCGSurfaceArea, PE_GetPartsCGSurfaceArea),
        HLL_EXPORT(GetPartsTextSurfaceArea, PartsEngine_GetPartsTextSurfaceArea),
        HLL_EXPORT(GetHGaugeSurfaceArea, PartsEngine_GetHGaugeSurfaceArea),
        HLL_EXPORT(GetVGaugeSurfaceArea, PartsEngine_GetVGaugeSurfaceArea),
        HLL_EXPORT(GetNumeralSurfaceArea, PartsEngine_GetNumeralSurfaceArea),
        HLL_EXPORT(GetConstructionSurfaceArea, PartsEngine_GetConstructionSurfaceArea),
    SCROLL_OVERRIDE_EXPORTS
    HLL_EXPORT(SetMessageWindowActive, PE_SetMessageWindowActive),
    HLL_EXPORT(SetMessageWindowInactiveMultipleColor, PE_SetMessageWindowInactiveMultipleColor),
    HLL_EXPORT(GetMessageWindowInactiveMultipleColorR, PE_GetMessageWindowInactiveMultipleColorR),
    HLL_EXPORT(GetMessageWindowInactiveMultipleColorG, PE_GetMessageWindowInactiveMultipleColorG),
    HLL_EXPORT(GetMessageWindowInactiveMultipleColorB, PE_GetMessageWindowInactiveMultipleColorB),
    HLL_EXPORT(SetMessageWindowCGName, PE_SetMessageWindowCGName),
    HLL_EXPORT(GetMessageWindowCGName, PE_GetMessageWindowCGName),
    HLL_EXPORT(SetMessageWindowFlatName, PE_SetMessageWindowFlatName),
    HLL_EXPORT(GetMessageWindowFlatName, PE_GetMessageWindowFlatName),
    HLL_EXPORT(SetMessageWindowFlatShowWaitFrameNumber, PE_SetMessageWindowFlatShowWaitFrameNumber),
    HLL_EXPORT(GetMessageWindowFlatShowWaitFrameNumber, PE_GetMessageWindowFlatShowWaitFrameNumber),
    HLL_EXPORT(IsOverMessageWindowFlatShowWaitFrame, PE_IsOverMessageWindowFlatShowWaitFrame),
    HLL_EXPORT(BackMessageWindowFlatBeginFrame, PE_BackMessageWindowFlatBeginFrame),
    HLL_EXPORT(StepMessageWindowFlatFinalFrame, PE_StepMessageWindowFlatFinalFrame),
    HLL_EXPORT(SetKeyWaitCGName, PE_SetKeyWaitCGName),
    HLL_EXPORT(GetKeyWaitCGName, PE_GetKeyWaitCGName),
    HLL_EXPORT(SetKeyWaitFlatName, PE_SetKeyWaitFlatName),
    HLL_EXPORT(GetKeyWaitFlatName, PE_GetKeyWaitFlatName),
    HLL_EXPORT(SetKeyWaitPos, PE_SetKeyWaitPos),
    HLL_EXPORT(GetKeyWaitPosX, PE_GetKeyWaitPosX),
    HLL_EXPORT(GetKeyWaitPosY, PE_GetKeyWaitPosY),
    HLL_EXPORT(GetKeyWaitPosZ, PE_GetKeyWaitPosZ),
    HLL_EXPORT(SetKeyWaitShow, PE_SetKeyWaitShow),
    HLL_EXPORT(IsKeyWaitShow, PE_IsKeyWaitShow),
    HLL_EXPORT(SetMessageWindowText, PE_SetMessageWindowText),
    HLL_EXPORT(GetMessageWindowText, PE_GetMessageWindowText),
    HLL_EXPORT(FixMessageWindowText, PE_FixMessageWindowText),
    HLL_EXPORT(IsFixedMessageWindowText, PE_IsFixedMessageWindowText),
    HLL_EXPORT(SetMessageWindowTextArea, PE_SetMessageWindowTextArea),
    HLL_EXPORT(GetMessageWindowTextArea, PE_GetMessageWindowTextArea),
    HLL_EXPORT(SetMessageWindowTextOriginPosMode, PE_SetMessageWindowTextOriginPosMode),
    HLL_EXPORT(GetMessageWindowTextOriginPosMode, PE_GetMessageWindowTextOriginPosMode),
    HLL_EXPORT(SetMessageWindowTextFont, PE_SetMessageWindowTextFont),
    HLL_EXPORT(GetMessageWindowTextFont, PE_GetMessageWindowTextFont),
    HLL_EXPORT(SetMessageWindowTextSpeed, PE_SetMessageWindowTextSpeed),
    HLL_EXPORT(GetMessageWindowTextSpeed, PE_GetMessageWindowTextSpeed),
    HLL_EXPORT(SetMessageWindowTextSpace, PE_SetMessageWindowTextSpace),
    HLL_EXPORT(GetMessageWindowTextSpace, PE_GetMessageWindowTextSpace),

	    HLL_EXPORT(SetEnableInputProcess, PartsEngine_SetEnableInputProcess),
	    HLL_EXPORT(IsEnableInputProcess, PartsEngine_IsEnableInputProcess),
	    HLL_EXPORT(CreateUserPartsNumber, PartsEngine_CreateUserPartsNumber),
	    HLL_EXPORT(ReleaseUserPartsNumber, PartsEngine_ReleaseUserPartsNumber),
	    HLL_EXPORT(Parts_SetWheelable, PartsEngine_Parts_SetWheelable),
	    HLL_EXPORT(SetClickMissSoundName, PE_SetClickMissSoundName),
	    HLL_EXPORT(GetClickMissSoundName, PE_GetClickMissSoundName),
	    HLL_EXPORT(NumofChild, PartsEngine_NumofChild),
	    HLL_EXPORT(GetChild, PartsEngine_GetChild),
	    HLL_EXPORT(GetChildIndex, PartsEngine_GetChildIndex),
	    HLL_EXPORT(IsExistChild, PartsEngine_IsExistChild),
	    HLL_EXPORT(SetUserComponentName, PE_SetUserComponentName),
	    HLL_EXPORT(GetUserComponentName, PE_GetUserComponentName),
	    HLL_EXPORT(SetUserComponentData, PE_SetUserComponentData),
	    HLL_EXPORT(GetUserComponentData, PE_GetUserComponentData),
	    HLL_EXPORT(Parts_SetComment, PartsEngine_SetComment),
	    HLL_EXPORT(Parts_GetComment, PartsEngine_GetComment),
        SPIN_FORM_HLL_EXPORTS
        LISTBOX_HLL_EXPORTS
        WIDGET_HLL_EXPORTS
        TEXTBOX_HLL_EXPORTS
	    HLL_EXPORT(IsExistActivity, PE_IsExistActivity),
	    HLL_EXPORT(CreateActivity, PE_CreateActivity),
	    HLL_EXPORT(ReadActivityFile, PE_ReadActivityFile),
	    HLL_EXPORT(AddActivityParts, PE_AddActivityParts),
	    HLL_EXPORT(RemoveActivityParts, PE_RemoveActivityParts),
	    HLL_EXPORT(RemoveAllActivityParts, PE_RemoveAllActivityParts),
	    HLL_EXPORT(NumofActivityParts, PE_NumofActivityParts),
	    HLL_EXPORT(GetActivityParts, PE_GetActivityParts),
	    HLL_EXPORT(IsExistActivityPartsByName, PE_IsExistActivityPartsByName),
	    HLL_EXPORT(IsExistActivityPartsByNumber, PE_IsExistActivityPartsByNumber),
	    HLL_EXPORT(GetActivityPartsNumber, PE_GetActivityPartsNumber),
	    HLL_EXPORT(GetActivityPartsName, PE_GetActivityPartsName),
	    HLL_EXPORT(AddActivityCloseParts, PE_AddActivityCloseParts),
	    HLL_EXPORT(RemoveActivityCloseParts, PE_RemoveActivityCloseParts),
	    HLL_EXPORT(RemoveAllActivityCloseParts, PE_RemoveAllActivityCloseParts),
	    HLL_EXPORT(IsExistActivityCloseParts, PE_IsExistActivityCloseParts),
	    HLL_EXPORT(SetActivityIntentData, PE_SetActivityIntentData),
	    HLL_EXPORT(AddActivityIntentDataDestination, PE_AddActivityIntentDataDestination),
	    HLL_EXPORT(IsExistActivityIntentData, PE_IsExistActivityIntentData),
	    HLL_EXPORT(NumofActivityIntentDataDestination, PE_NumofActivityIntentDataDestination),
	    HLL_EXPORT(GetActivityIntentDataDestination, PE_GetActivityIntentDataDestination),
	    HLL_EXPORT(GetActivityIntentDataType, PE_GetActivityIntentDataType),
	    HLL_EXPORT(SetActivityEndKey, PE_SetActivityEndKey),
	    HLL_EXPORT(EraseActivityEndKey, PE_EraseActivityEndKey),
	    HLL_EXPORT(IsExistActivityEndKey, PE_IsExistActivityEndKey),
	    HLL_EXPORT(NumofActivityEndKey, PE_NumofActivityEndKey),
	    HLL_EXPORT(GetActivityEndKey, PE_GetActivityEndKey),
	    HLL_EXPORT(IsExistActivityFile, PartsEngine_IsExistActivityFile),
	    HLL_EXPORT(SetFlatFPSRate, PE_SetFlatFPSRate),
	    HLL_EXPORT(GetFlatFPSRate, PE_GetFlatFPSRate),
	    HLL_EXPORT(GetFlatFPS, PE_GetFlatFPS),
	    HLL_EXPORT(CreatePartsMovie, PE_CreatePartsMovie),
	    HLL_EXPORT(PlayPartsMovie, PE_PlayPartsMovie),
	    HLL_EXPORT(GetPartsMovieEndTime, PE_GetPartsMovieEndTime),
	    HLL_EXPORT(GetPartsMovieCurrentTime, PE_GetPartsMovieCurrentTime),
	    HLL_EXPORT(IsEndPartsMovie, PE_IsEndPartsMovie),
	    HLL_EXPORT(ReleasePartsMovie, PE_ReleasePartsMovie),
	    HLL_EXPORT(_PreLink, PartsEngine_PreLink),
	    // for versions without PartsEngine.Init
	    HLL_EXPORT(_ModuleInit, PartsEngine_ModuleInit),
	    HLL_EXPORT(_ModuleFini, PartsEngine_ModuleFini),
	    // Oyako Rankan
	    HLL_EXPORT(Init, PE_Init),
	    HLL_EXPORT(Update, PartsEngine_Update),
	    HLL_TODO_EXPORT(UpdateGUIController, PartsEngine_UpdateGUIController),
	    HLL_EXPORT(GetFreeSystemPartsNumber, PE_GetFreeNumber),
	    // FIXME: what is the difference?
	    HLL_EXPORT(GetFreeSystemPartsNumberNotSaved, PE_GetFreeNumber),
	    HLL_EXPORT(IsExistParts, PE_IsExist),
	    HLL_EXPORT(SetPartsCG, PE_SetPartsCG),
	    HLL_EXPORT(GetPartsCGName, PE_GetPartsCGName),
	    HLL_EXPORT(SetPartsCGSurfaceArea, PE_SetPartsCGSurfaceArea),
	    HLL_EXPORT(SetLoopCG, PE_SetLoopCG),
	    HLL_EXPORT(SetLoopCGSurfaceArea, PE_SetLoopCGSurfaceArea),
	    HLL_EXPORT(SetText, PE_SetText),
	    HLL_EXPORT(AddPartsText, PE_AddPartsText),
	    HLL_TODO_EXPORT(DeletePartsTopTextLine, PartsEngine_DeletePartsTopTextLine),
	    HLL_EXPORT(SetPartsTextSurfaceArea, PE_SetPartsTextSurfaceArea),
	    HLL_TODO_EXPORT(SetPartsTextHighlight, PartsEngine_SetPartsTextHighlight),
	    HLL_TODO_EXPORT(AddPartsTextHighlight, PartsEngine_AddPartsTextHighlight),
	    HLL_TODO_EXPORT(ClearPartsTextHighlight, PartsEngine_ClearPartsTextHighlight),
	    HLL_TODO_EXPORT(SetPartsTextCountReturn, PartsEngine_SetPartsTextCountReturn),
	    HLL_TODO_EXPORT(GetPartsTextCountReturn, PartsEngine_GetPartsTextCountReturn),
	    HLL_EXPORT(SetFont, PE_SetFont),
	    HLL_EXPORT(SetPartsFontType, PE_SetPartsFontType),
	    HLL_EXPORT(SetPartsFontSize, PE_SetPartsFontSize),
	    HLL_EXPORT(SetPartsFontColor, PE_SetPartsFontColor),
	    HLL_EXPORT(SetPartsFontBoldWeight, PE_SetPartsFontBoldWeight),
	    HLL_EXPORT(SetPartsFontEdgeColor, PE_SetPartsFontEdgeColor),
	    HLL_EXPORT(SetPartsFontEdgeWeight, PE_SetPartsFontEdgeWeight),
	    HLL_EXPORT(SetTextCharSpace, PE_SetTextCharSpace),
	    HLL_EXPORT(SetTextLineSpace, PE_SetTextLineSpace),
	    HLL_EXPORT(SetHGaugeCG, PE_SetHGaugeCG),
	    HLL_EXPORT(SetHGaugeRate, PE_SetHGaugeRate_int),
	    HLL_EXPORT(SetVGaugeCG, PE_SetVGaugeCG),
	    HLL_EXPORT(SetVGaugeRate, PE_SetVGaugeRate_int),
	    HLL_EXPORT(SetHGaugeSurfaceArea, PE_SetHGaugeSurfaceArea),
	    HLL_EXPORT(SetVGaugeSurfaceArea, PE_SetVGaugeSurfaceArea),
	    HLL_EXPORT(SetNumeralCG, PE_SetNumeralCG),
	    HLL_EXPORT(SetNumeralLinkedCGNumberWidthWidthList, PE_SetNumeralLinkedCGNumberWidthWidthList),
	    HLL_EXPORT(SetNumeralFont, PartsEngine_SetNumeralFont),
	    HLL_EXPORT(SetNumeralNumber, PE_SetNumeralNumber),
	    HLL_EXPORT(SetNumeralShowComma, PE_SetNumeralShowComma),
	    HLL_EXPORT(SetNumeralSpace, PE_SetNumeralSpace),
	    HLL_EXPORT(SetNumeralLength, PE_SetNumeralLength),
	    HLL_EXPORT(SetNumeralSurfaceArea, PE_SetNumeralSurfaceArea),
	    HLL_EXPORT(SetPartsRectangleDetectionSize, PE_SetPartsRectangleDetectionSize),
	    HLL_TODO_EXPORT(SetPartsRectangleDetectionSurfaceArea, PartsEngine_SetPartsRectangleDetectionSurfaceArea),
	    HLL_EXPORT(SetPartsCGDetectionSize, PE_SetPartsCGDetectionSize),
	    HLL_TODO_EXPORT(SetPartsCGDetectionSurfaceArea, PartsEngine_SetPartsCGDetectionSurfaceArea),
	    HLL_EXPORT(SetPartsFlash, PE_SetPartsFlash),
	    HLL_EXPORT(IsPartsFlashEnd, PE_IsPartsFlashEnd),
	    HLL_EXPORT(GetPartsFlashCurrentFrameNumber, PE_GetPartsFlashCurrentFrameNumber),
	    HLL_EXPORT(BackPartsFlashBeginFrame, PE_BackPartsFlashBeginFrame),
	    HLL_EXPORT(StepPartsFlashFinalFrame, PE_StepPartsFlashFinalFrame),
	    HLL_TODO_EXPORT(SetPartsFlashSurfaceArea, PE_SetPartsFlashSurfaceArea),
	    HLL_EXPORT(SetPartsFlashAndStop, PE_SetPartsFlashAndStop),
	    HLL_EXPORT(StopPartsFlash, PE_StopPartsFlash),
	    HLL_EXPORT(StartPartsFlash, PE_StartPartsFlash),
	    HLL_EXPORT(GoFramePartsFlash, PE_GoFramePartsFlash),
	    HLL_EXPORT(GetPartsFlashEndFrame, PE_GetPartsFlashEndFrame),
	    HLL_EXPORT(ExistsFlashFile, PE_ExistsFlashFile),
	    HLL_EXPORT(ClearPartsConstructionProcess, PE_ClearPartsConstructionProcess),
	    HLL_EXPORT(AddCreateToPartsConstructionProcess, PE_AddCreateToPartsConstructionProcess),
	    HLL_EXPORT(AddCreatePixelOnlyToPartsConstructionProcess, PE_AddCreatePixelOnlyToPartsConstructionProcess),
	    HLL_EXPORT(AddCreateCGToProcess, PE_AddCreateCGToProcess),
	    HLL_EXPORT(AddFillToPartsConstructionProcess, PE_AddFillToPartsConstructionProcess),
	    HLL_EXPORT(AddFillAlphaColorToPartsConstructionProcess, PE_AddFillAlphaColorToPartsConstructionProcess),
	    HLL_EXPORT(AddFillAMapToPartsConstructionProcess, PE_AddFillAMapToPartsConstructionProcess),
	    HLL_EXPORT(AddFillWithAlphaToPartsConstructionProcess, PE_AddFillWithAlphaToPartsConstructionProcess),
	    HLL_TODO_EXPORT(AddFillGradationHorizonToPartsConstructionProcess, PartsEngine_AddFillGradationHorizonToPartsConstructionProcess),
	    HLL_EXPORT(AddDrawRectToPartsConstructionProcess, PE_AddDrawRectToPartsConstructionProcess),
	    HLL_EXPORT(AddDrawCutCGToPartsConstructionProcess, PartsEngine_AddDrawCutCGToPartsConstructionProcess_old),
	    HLL_EXPORT(AddCopyCutCGToPartsConstructionProcess, PartsEngine_AddCopyCutCGToPartsConstructionProcess_old),
	    HLL_EXPORT(AddGrayFilterToPartsConstructionProcess, PE_AddGrayFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(AddAddFilterToPartsConstructionProcess, PartsEngine_AddAddFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(AddMulFilterToPartsConstructionProcess, PartsEngine_AddMulFilterToPartsConstructionProcess),
	    HLL_EXPORT(BuildPartsConstructionProcess, PE_BuildPartsConstructionProcess),
	    HLL_EXPORT(AddDrawTextToPartsConstructionProcess, PE_AddDrawTextToPartsConstructionProcess),
	    HLL_EXPORT(AddCopyTextToPartsConstructionProcess, PE_AddCopyTextToPartsConstructionProcess),
	    HLL_EXPORT(SetPartsConstructionSurfaceArea, PE_SetPartsConstructionSurfaceArea),
	    HLL_EXPORT(ReleaseParts, PE_ReleaseParts),
	    HLL_EXPORT(ReleaseAllParts, PE_ReleaseAllParts),
	    HLL_EXPORT(ReleaseAllPartsWithoutSystem, PE_ReleaseAllPartsWithoutSystem),
	    HLL_EXPORT(SetPos, PE_SetPos),
	    HLL_EXPORT(SetZ, PE_SetZ),
	    HLL_EXPORT(SetShow, PE_SetShow),
	    HLL_EXPORT(SetAlpha, PE_SetAlpha),
	    HLL_EXPORT(SetPartsDrawFilter, PE_SetPartsDrawFilter),
	    HLL_EXPORT(SetAddColor, PE_SetAddColor),
	    HLL_EXPORT(SetMultiplyColor, PE_SetMultiplyColor),
	    HLL_EXPORT(SetPassCursor, PE_SetPassCursor),
	    HLL_EXPORT(SetClickable, PE_SetClickable),
	    HLL_EXPORT(SetSpeedupRateByMessageSkip, PE_SetSpeedupRateByMessageSkip),
	    HLL_TODO_EXPORT(SetResetTimerByChangeInputStatus, PartsEngine_SetResetTimerByChangeInputStatus),
	    HLL_EXPORT(GetPartsX, PE_GetPartsX),
	    HLL_EXPORT(GetPartsY, PE_GetPartsY),
	    HLL_EXPORT(GetPartsZ, PE_GetPartsZ),
	    HLL_EXPORT(GetPartsShow, PE_GetPartsShow),
	    HLL_EXPORT(GetPartsAlpha, PE_GetPartsAlpha),
	    HLL_TODO_EXPORT(GetAddColor, PartsEngine_GetAddColor),
	    HLL_TODO_EXPORT(GetMultiplyColor, PartsEngine_GetMultiplyColor),
	    HLL_EXPORT(GetPartsPassCursor, PE_GetPartsPassCursor),
	    HLL_EXPORT(GetPartsClickable, PE_GetPartsClickable),
	    HLL_TODO_EXPORT(GetPartsSpeedupRateByMessageSkip, PartsEngine_GetPartsSpeedupRateByMessageSkip),
	    HLL_TODO_EXPORT(GetResetTimerByChangeInputStatus, PartsEngine_GetResetTimerByChangeInputStatus),
	    HLL_EXPORT(GetPartsUpperLeftPosX, PE_GetPartsUpperLeftPosX),
	    HLL_EXPORT(GetPartsUpperLeftPosY, PE_GetPartsUpperLeftPosY),
	    HLL_EXPORT(GetPartsWidth, PE_GetPartsWidth),
	    HLL_EXPORT(GetPartsHeight, PE_GetPartsHeight),
	    HLL_EXPORT(SetInputState, PE_SetInputState),
	    HLL_EXPORT(GetInputState, PE_GetInputState),
	    HLL_EXPORT(SetPartsOriginPosMode, PE_SetPartsOriginPosMode),
	    HLL_EXPORT(GetPartsOriginPosMode, PE_GetPartsOriginPosMode),
	    HLL_EXPORT(SetParentPartsNumber, PE_SetParentPartsNumber),
	    HLL_EXPORT(SetPartsGroupNumber, PE_SetPartsGroupNumber),
	    HLL_EXPORT(SetPartsGroupDecideOnCursor, PE_SetPartsGroupDecideOnCursor),
	    HLL_EXPORT(SetPartsGroupDecideClick, PE_SetPartsGroupDecideClick),
	    HLL_EXPORT(SetOnCursorShowLinkPartsNumber, PE_SetOnCursorShowLinkPartsNumber),
	    HLL_EXPORT(SetPartsMessageWindowShowLink, PE_SetPartsMessageWindowShowLink),
	    HLL_EXPORT(GetPartsMessageWindowShowLink, PE_GetPartsMessageWindowShowLink),
	    HLL_EXPORT(AddMotionPos, PE_AddMotionPos_curve),
	    HLL_EXPORT(AddMotionAlpha, PE_AddMotionAlpha_curve),
	    HLL_TODO_EXPORT(AddMotionCG, PartsEngine_AddMotionCG),
	    HLL_EXPORT(AddMotionHGaugeRate, PE_AddMotionHGaugeRate_curve),
	    HLL_EXPORT(AddMotionVGaugeRate, PE_AddMotionVGaugeRate_curve),
	    HLL_EXPORT(AddMotionNumeralNumber, PE_AddMotionNumeralNumber_curve),
	    HLL_EXPORT(AddMotionMagX, PE_AddMotionMagX_curve),
	    HLL_EXPORT(AddMotionMagY, PE_AddMotionMagY_curve),
	    HLL_EXPORT(AddMotionRotateX, PE_AddMotionRotateX_curve),
	    HLL_EXPORT(AddMotionRotateY, PE_AddMotionRotateY_curve),
	    HLL_EXPORT(AddMotionRotateZ, PE_AddMotionRotateZ_curve),
	    HLL_EXPORT(AddMotionVibrationSize, PE_AddMotionVibrationSize),
	    HLL_EXPORT(AddWholeMotionVibrationSize, PE_AddWholeMotionVibrationSize),
	    HLL_EXPORT(AddMotionSound, PE_AddMotionSound),
	    HLL_TODO_EXPORT(SetSoundNumber, PartsEngine_SetSoundNumber),
	    HLL_TODO_EXPORT(GetSoundNumber, PartsEngine_GetSoundNumber),
	    HLL_EXPORT(SetClickMissSoundNumber, PE_SetClickMissSoundNumber),
	    HLL_TODO_EXPORT(GetClickMissSoundNumber, PartsEngine_GetClickMissSoundNumber),
	    HLL_EXPORT(BeginMotion, PE_BeginMotion),
	    HLL_EXPORT(PauseMotion, PE_PauseMotion),
	    HLL_EXPORT(EndMotion, PE_EndMotion),
	    HLL_EXPORT(IsMotion, PE_IsMotion),
	    HLL_EXPORT(SeekEndMotion, PE_SeekEndMotion),
	    HLL_EXPORT(UpdateMotionTime, PE_UpdateMotionTime),
	    HLL_EXPORT(BeginInput, PE_BeginInput),
	    HLL_EXPORT(EndInput, PE_EndInput),
	    HLL_EXPORT(GetClickPartsNumber, PE_GetClickPartsNumber),
	    HLL_EXPORT(GetFocusPartsNumber, PE_GetFocusPartsNumber),
	    HLL_EXPORT(SetFocusPartsNumber, PE_SetFocus),
	    HLL_TODO_EXPORT(PushGUIController, PartsEngine_PushGUIController),
	    HLL_TODO_EXPORT(PopGUIController, PartsEngine_PopGUIController),
	    HLL_EXPORT(SetPartsMagX, PE_SetPartsMagX),
	    HLL_EXPORT(SetPartsMagY, PE_SetPartsMagY),
	    HLL_EXPORT(SetPartsRotateX, PE_SetPartsRotateX),
	    HLL_EXPORT(SetPartsRotateY, PE_SetPartsRotateY),
	    HLL_EXPORT(SetPartsRotateZ, PE_SetPartsRotateZ),
	    HLL_EXPORT(SetPartsAlphaClipperPartsNumber, PE_SetPartsAlphaClipperPartsNumber),
	    HLL_EXPORT(SetPartsPixelDecide, PE_SetPartsPixelDecide),
	    HLL_EXPORT(IsCursorIn, PE_IsCursorIn),
	    HLL_EXPORT(SetThumbnailReductionSize, PE_SetThumbnailReductionSize),
	    HLL_EXPORT(SaveThumbnail, PE_save_thumbnail),
	    HLL_EXPORT(SetThumbnailMode, PE_SetThumbnailMode),
	    HLL_EXPORT(Save, PE_Save),
	    HLL_EXPORT(SaveWithoutHideParts, PE_SaveWithoutHideParts),
	    HLL_EXPORT(Load, PE_Load),
	    // Rance 9
	    HLL_EXPORT(PartsFunc, PartsEngine_PartsFunc),
	    HLL_EXPORT(AddPartsConstructionProcess, PartsEngine_AddPartsConstructionProcess),
	    HLL_EXPORT(Release, PE_ReleaseParts),
	    HLL_TODO_EXPORT(ReleaseAll, PartsEngine_ReleaseAll),
	    HLL_EXPORT(ReleaseAllWithoutSystem, PE_ReleaseAllWithoutSystem),
	    HLL_EXPORT(GetFreeNumber, PE_GetFreeNumber),
	    HLL_EXPORT(IsExist, PE_IsExist),
	    HLL_EXPORT(AddController, PE_AddController),
	    HLL_EXPORT(RemoveController, PE_RemoveController),
	    // Rance 03 port: controller accessors + back-scene functions
	    HLL_EXPORT(SetActiveController, PartsEngine_SetActiveController),
	    HLL_EXPORT(GetActiveController, PartsEngine_GetActiveController),
	    HLL_EXPORT(GetControllerLength, PartsEngine_GetControllerLength),
	    HLL_EXPORT(GetSystemOverlayController, PartsEngine_GetSystemOverlayController),
	    HLL_EXPORT(GetControllerIndex, PartsEngine_GetControllerIndex),
	    HLL_EXPORT(GetControllerID, PartsEngine_GetControllerID),
	    HLL_EXPORT(SetWantSaveBackScene, PE_SetWantSaveBackScene),
	    HLL_EXPORT(IsWantSaveBackScene, PE_IsWantSaveBackScene),
	    HLL_EXPORT(ClearBackScene, PE_ClearBackScene),
	    HLL_EXPORT(SetFontColorForBackScene, PE_SetFontColorForBackScene),
	    HLL_EXPORT(SetMulColorForBackScene, PE_SetMulColorForBackScene),
	    HLL_EXPORT(SetAlphaForBackScene, PE_SetAlphaForBackScene),
	    HLL_EXPORT(SetShowForBackScene, PE_SetShowForBackScene),
	    HLL_EXPORT(SaveBackScene, PE_SaveBackScene),
	    HLL_EXPORT(LoadBackScene, PE_LoadBackScene),
	    HLL_EXPORT(UpdateComponent, PartsEngine_Update),
	    HLL_EXPORT(SetEnableSpeedUp, PE_SetEnableSpeedUp),
	    HLL_EXPORT(IsEnableSpeedUp, PE_IsEnableSpeedUp),
	    HLL_EXPORT(Parts_SetThumbnailReductionSize, PE_SetThumbnailReductionSize),
	    HLL_EXPORT(Parts_SetThumbnailMode, PE_SetThumbnailMode),
	    HLL_EXPORT(GetClickNumber, PE_GetClickPartsNumber),
	    HLL_EXPORT(StopSoundWithoutSystemSound, PartsEngine_StopSoundWithoutSystemSound),
	    HLL_EXPORT(ReleaseActivity, PE_ReleaseActivity),
	    HLL_TODO_EXPORT(CrateActivityBinary, PartsEngine_CrateActivityBinary),
	    HLL_TODO_EXPORT(ReadActivityBinary, PartsEngine_ReadActivityBinary),
	    HLL_EXPORT(ReleaseMessage, PE_ReleaseMessage),
	    HLL_EXPORT(PopMessage, PE_PopMessage),
	    HLL_EXPORT(GetMessagePartsNumber, PE_GetMessagePartsNumber),
	    HLL_EXPORT(GetMessageDelegateIndex, PE_GetMessageDelegateIndex),
	    HLL_EXPORT(GetDelegateIndex, PE_GetDelegateIndex),
	    HLL_EXPORT(GetMessageType, PE_GetMessageType),
	    HLL_EXPORT(GetMessageVariableCount, PE_GetMessageVariableCount),
	    HLL_EXPORT(GetMessageVariableType, PE_GetMessageVariableType),
	    HLL_EXPORT(GetMessageVariableInt, PE_GetMessageVariableInt),
	    HLL_EXPORT(GetMessageVariableFloat, PE_GetMessageVariableFloat),
	    HLL_EXPORT(GetMessageVariableBool, PE_GetMessageVariableBool),
	    HLL_EXPORT(GetMessageVariableString, PE_GetMessageVariableString),
	    HLL_EXPORT(SetDelegateIndex, PE_SetDelegateIndex),

	    HLL_EXPORT(SetComponentType, PE_SetComponentType),
	    HLL_EXPORT(GetComponentType, PE_GetComponentType),
	    HLL_EXPORT(SetComponentPos, PartsEngine_SetComponentPos),
	    HLL_EXPORT(SetComponentPosZ, PE_SetZ),
	    HLL_EXPORT(GetComponentPosX, PartsEngine_Parts_GetComponentPosX),
	    HLL_EXPORT(GetComponentPosY, PartsEngine_GetComponentPosY),
	    HLL_EXPORT(GetComponentPosZ, PE_GetPartsZ),
	    HLL_EXPORT(GetComponentAbsolutePosX, PE_parts_get_absolute_x),
	    HLL_EXPORT(GetComponentAbsolutePosY, PE_parts_get_absolute_y),
	    HLL_EXPORT(GetComponentAbsolutePosZ, PE_parts_get_absolute_z),
	    HLL_EXPORT(GetComponentAbsoluteMaxPosZ, PE_parts_get_absolute_max_z),
	    HLL_EXPORT(GetActiveParts, PE_GetFocusPartsNumber),
	    HLL_EXPORT(Parts_SetSoundName, PE_SetPartsSoundName),
	    HLL_EXPORT(Parts_GetSoundName, PE_GetPartsSoundName),
	    HLL_EXPORT(Parts_PlaySound, PE_PlayPartsSound),
	    HLL_EXPORT(Parts_GetParentPartsNumber, PE_GetParentPartsNumber),
	    HLL_EXPORT(AddChild, PartsEngine_AddChild),
	    HLL_EXPORT(Parts_GetTextCharSpace, PartsEngine_GetTextCharSpace),
	    HLL_EXPORT(Parts_GetTextLineSpace, PartsEngine_GetTextLineSpace),
	    HLL_EXPORT(Parts_GetPartsUpperLeftPosX, PartsEngine_Parts_GetPartsUpperLeftPosX),
	    HLL_EXPORT(Parts_GetPartsUpperLeftPosY, PartsEngine_Parts_GetPartsUpperLeftPosY),
	    HLL_EXPORT(SetComponentOriginPosMode, PE_SetPartsOriginPosMode),
	    HLL_EXPORT(GetComponentOriginPosMode, PE_GetPartsOriginPosMode),
	    HLL_TODO_EXPORT(GetComponentWidth, PartsEngine_GetComponentWidth),
	    HLL_TODO_EXPORT(GetComponentHeight, PartsEngine_GetComponentHeight),
	    HLL_EXPORT(Parts_GetPartsWidth, PE_GetPartsWidth),
	    HLL_EXPORT(Parts_GetPartsHeight, PE_GetPartsHeight),
	    HLL_EXPORT(SetComponentShow, PE_SetComponentShow),
	    HLL_EXPORT(IsComponentShow, PE_IsComponentShow),
	    HLL_EXPORT(SetComponentMessageWindowShowLink, PE_SetPartsMessageWindowShowLink),
	    HLL_EXPORT(IsComponentMessageWindowShowLink, PE_GetPartsMessageWindowShowLink),
	    HLL_EXPORT(SetComponentAlpha, PE_SetAlpha),
	    HLL_EXPORT(GetComponentAlpha, PE_GetPartsAlpha),
	    HLL_EXPORT(SetComponentAddColor, PE_SetAddColor),
	    HLL_EXPORT(GetComponentAddColorR, PartsEngine_GetComponentAddColorR),
	    HLL_EXPORT(GetComponentAddColorG, PartsEngine_GetComponentAddColorG),
	    HLL_EXPORT(GetComponentAddColorB, PartsEngine_GetComponentAddColorB),
	    HLL_EXPORT(SetComponentMulColor, PE_SetMultiplyColor),
	    HLL_EXPORT(GetComponentMulColorR, PartsEngine_GetComponentMulColorR),
	    HLL_EXPORT(GetComponentMulColorG, PartsEngine_GetComponentMulColorG),
	    HLL_EXPORT(GetComponentMulColorB, PartsEngine_GetComponentMulColorB),
	    HLL_EXPORT(SetComponentDrawFilter, PE_SetPartsDrawFilter),
	    HLL_EXPORT(GetComponentDrawFilter, PE_GetPartsDrawFilter),
	    HLL_EXPORT(SetComponentMagX, PE_SetPartsMagX),
	    HLL_EXPORT(SetComponentMagY, PE_SetPartsMagY),
	    HLL_EXPORT(GetComponentMagX, PE_GetPartsMagX),
	    HLL_EXPORT(GetComponentMagY, PE_GetPartsMagY),
	    HLL_EXPORT(SetComponentRotateX, PE_SetPartsRotateX),
	    HLL_EXPORT(SetComponentRotateY, PE_SetPartsRotateY),
	    HLL_EXPORT(SetComponentRotateZ, PE_SetPartsRotateZ),
	    HLL_TODO_EXPORT(GetComponentRotateX, PartsEngine_GetComponentRotateX),
	    HLL_TODO_EXPORT(GetComponentRotateY, PartsEngine_GetComponentRotateY),
	    HLL_EXPORT(GetComponentRotateZ, PE_GetPartsRotateZ),
	    HLL_EXPORT(SetComponentMargin, PE_SetComponentMargin),
	    HLL_EXPORT(GetComponentMarginTop, PE_GetComponentMarginTop),
	    HLL_EXPORT(GetComponentMarginBottom, PE_GetComponentMarginBottom),
	    HLL_EXPORT(GetComponentMarginLeft, PE_GetComponentMarginLeft),
	    HLL_EXPORT(GetComponentMarginRight, PE_GetComponentMarginRight),
	    HLL_EXPORT(SetComponentAlphaClipper, PE_SetPartsAlphaClipperPartsNumber),
	    HLL_TODO_EXPORT(GetComponentAlphaClipper, PartsEngine_GetComponentAlphaClipper),
	    HLL_TODO_EXPORT(SetComponentTextureFilterType, PartsEngine_SetComponentTextureFilterType),
	    HLL_TODO_EXPORT(GetComponentTextureFilterType, PartsEngine_GetComponentTextureFilterType),
	    HLL_TODO_EXPORT(SetComponentMipmap, PartsEngine_SetComponentMipmap),
	    HLL_TODO_EXPORT(IsComponentMipmap, PartsEngine_IsComponentMipmap),
	    HLL_EXPORT(SetComponentSpeedupRateByMessageSkip, PE_SetSpeedupRateByMessageSkip),
	    HLL_EXPORT(GetComponentSpeedupRateByMessageSkip, PE_GetSpeedupRateByMessageSkip),
	    HLL_EXPORT(AddComponentMotionPos, PartsEngine_AddComponentMotionPos),
	    HLL_EXPORT(AddComponentMotionAlpha, PE_AddMotionAlpha_curve),
	    HLL_TODO_EXPORT(AddComponentMotionCG, PartsEngine_AddComponentMotionCG),
	    HLL_TODO_EXPORT(AddComponentMotionCGTermination, PartsEngine_AddComponentMotionCGTermination),
	    HLL_EXPORT(AddComponentMotionHGaugeRate, PE_AddMotionHGaugeRate_curve),
	    HLL_EXPORT(AddComponentMotionVGaugeRate, PE_AddMotionVGaugeRate_curve),
	    HLL_EXPORT(AddComponentMotionNumeralNumber, PE_AddMotionNumeralNumber_curve),
	    HLL_EXPORT(AddComponentMotionMagX, PE_AddMotionMagX_curve),
	    HLL_EXPORT(AddComponentMotionMagY, PE_AddMotionMagY_curve),
	    HLL_EXPORT(AddComponentMotionRotateX, PE_AddMotionRotateX_curve),
	    HLL_EXPORT(AddComponentMotionRotateY, PE_AddMotionRotateY_curve),
	    HLL_EXPORT(AddComponentMotionRotateZ, PE_AddMotionRotateZ_curve),
	    HLL_EXPORT(AddComponentMotionVibrationSize, PE_AddMotionVibrationSize),
	    HLL_TODO_EXPORT(SuspendBuildView, PartsEngine_SuspendBuildView),
	    HLL_TODO_EXPORT(SuspendBuildViewAt, PartsEngine_SuspendBuildViewAt),
	    HLL_TODO_EXPORT(ResumeBuildView, PartsEngine_ResumeBuildView),

	    HLL_TODO_EXPORT(SetButtonDrag, PartsEngine_SetButtonDrag),
	    HLL_TODO_EXPORT(IsButtonDrag, PartsEngine_IsButtonDrag),

	    HLL_TODO_EXPORT(SetButtonPixelDecide, PartsEngine_SetButtonPixelDecide),
	    HLL_TODO_EXPORT(IsButtonPixelDecide, PartsEngine_IsButtonPixelDecide),

	    HLL_TODO_EXPORT(SetButtonOnCursorSoundNumber, PartsEngine_SetButtonOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetButtonClickSoundNumber, PartsEngine_SetButtonClickSoundNumber),
	    HLL_TODO_EXPORT(GetButtonOnCursorSoundNumber, PartsEngine_GetButtonOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetButtonClickSoundNumber, PartsEngine_GetButtonClickSoundNumber),

	    HLL_TODO_EXPORT(SetCheckBoxDrag, PartsEngine_SetCheckBoxDrag),
	    HLL_TODO_EXPORT(IsCheckBoxDrag, PartsEngine_IsCheckBoxDrag),


	    HLL_TODO_EXPORT(SetCheckBoxOnCursorSoundNumber, PartsEngine_SetCheckBoxOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetCheckBoxClickSoundNumber, PartsEngine_SetCheckBoxClickSoundNumber),
	    HLL_TODO_EXPORT(GetCheckBoxOnCursorSoundNumber, PartsEngine_GetCheckBoxOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetCheckBoxClickSoundNumber, PartsEngine_GetCheckBoxClickSoundNumber),

	    HLL_TODO_EXPORT(SetVScrollbarOnCursorSoundNumber, PartsEngine_SetVScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetVScrollbarClickSoundNumber, PartsEngine_SetVScrollbarClickSoundNumber),
	    HLL_TODO_EXPORT(GetVScrollbarOnCursorSoundNumber, PartsEngine_GetVScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetVScrollbarClickSoundNumber, PartsEngine_GetVScrollbarClickSoundNumber),

	    HLL_TODO_EXPORT(SetHScrollbarOnCursorSoundNumber, PartsEngine_SetHScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetHScrollbarClickSoundNumber, PartsEngine_SetHScrollbarClickSoundNumber),
	    HLL_TODO_EXPORT(GetHScrollbarOnCursorSoundNumber, PartsEngine_GetHScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetHScrollbarClickSoundNumber, PartsEngine_GetHScrollbarClickSoundNumber),
	    HLL_TODO_EXPORT(SetComboBoxSize, PartsEngine_SetComboBoxSize),
	    HLL_TODO_EXPORT(SetComboBoxTextMargin, PartsEngine_SetComboBoxTextMargin),
	    HLL_TODO_EXPORT(GetComboBoxTextWidthMargin, PartsEngine_GetComboBoxTextWidthMargin),
	    HLL_TODO_EXPORT(GetComboBoxTextHeightMargin, PartsEngine_GetComboBoxTextHeightMargin),
	    HLL_TODO_EXPORT(SetComboBoxCGName, PartsEngine_SetComboBoxCGName),
	    HLL_TODO_EXPORT(GetComboBoxCGName, PartsEngine_GetComboBoxCGName),
	    HLL_TODO_EXPORT(SetComboBoxText, PartsEngine_SetComboBoxText),
	    HLL_TODO_EXPORT(GetComboBoxText, PartsEngine_GetComboBoxText),
	    HLL_TODO_EXPORT(SetComboBoxFontProperty, PartsEngine_SetComboBoxFontProperty),
	    HLL_TODO_EXPORT(GetComboBoxFontProperty, PartsEngine_GetComboBoxFontProperty),


	    HLL_EXPORT(SetLayoutBoxLayoutType, PE_SetLayoutBoxLayoutType),
	    HLL_EXPORT(GetLayoutBoxLayoutType, PE_GetLayoutBoxLayoutType),
	    HLL_EXPORT(SetLayoutBoxReturn, PE_SetLayoutBoxReturn),
	    HLL_EXPORT(IsLayoutBoxReturn, PE_IsLayoutBoxReturn),
	    HLL_EXPORT(GetLayoutBoxReturnSize, PE_GetLayoutBoxReturnSize),
	    HLL_EXPORT(SetLayoutBoxAlign, PE_SetLayoutBoxAlign),
	    HLL_EXPORT(GetLayoutBoxAlign, PE_GetLayoutBoxAlign),
	    HLL_EXPORT(Parts_SetPartsCG, PE_SetPartsCG),
	    HLL_EXPORT(Parts_SetPartsCGThread, PE_SetPartsCGThread),
	    HLL_EXPORT(SuspendTexture, PE_SuspendTexture),
	    HLL_EXPORT(ResumeTexture, PE_ResumeTexture),
	    HLL_EXPORT(Parts_GetPartsCGName, PE_GetPartsCGName),
	    HLL_EXPORT(Parts_SetPartsCGSurfaceArea, PE_SetPartsCGSurfaceArea),
	    HLL_EXPORT(Parts_SetLoopCG, PE_SetLoopCG),
	    HLL_EXPORT(Parts_SetLoopCGSurfaceArea, PE_SetLoopCGSurfaceArea),
	    HLL_EXPORT(Parts_SetText, PE_SetText),
	    HLL_EXPORT(Parts_AddPartsText, PE_AddPartsText),
	    HLL_TODO_EXPORT(Parts_DeletePartsTopTextLine, PartsEngine_Parts_DeletePartsTopTextLine),
	    HLL_EXPORT(Parts_SetPartsTextSurfaceArea, PE_SetPartsTextSurfaceArea),
	    HLL_TODO_EXPORT(Parts_SetPartsTextHighlight, PartsEngine_Parts_SetPartsTextHighlight),
	    HLL_TODO_EXPORT(Parts_AddPartsTextHighlight, PartsEngine_Parts_AddPartsTextHighlight),
	    HLL_TODO_EXPORT(Parts_ClearPartsTextHighlight, PartsEngine_Parts_ClearPartsTextHighlight),
	    HLL_TODO_EXPORT(Parts_SetPartsTextCountReturn, PartsEngine_Parts_SetPartsTextCountReturn),
	    HLL_TODO_EXPORT(Parts_GetPartsTextCountReturn, PartsEngine_Parts_GetPartsTextCountReturn),
	    HLL_EXPORT(Parts_SetFont, PE_SetFont),
	    HLL_EXPORT(Parts_SetPartsFontType, PE_SetPartsFontType),
	    HLL_EXPORT(Parts_SetPartsFontSize, PE_SetPartsFontSize),
	    HLL_EXPORT(Parts_SetPartsFontColor, PE_SetPartsFontColor),
	    HLL_EXPORT(Parts_SetPartsFontBoldWeight, PE_SetPartsFontBoldWeight),
	    HLL_EXPORT(Parts_SetPartsFontEdgeColor, PE_SetPartsFontEdgeColor),
	    HLL_EXPORT(Parts_SetPartsFontEdgeWeight, PE_SetPartsFontEdgeWeight),
	    HLL_EXPORT(Parts_SetTextCharSpace, PE_SetTextCharSpace),
	    HLL_EXPORT(Parts_SetTextLineSpace, PE_SetTextLineSpace),
	    HLL_EXPORT(Parts_SetHGaugeCG, PE_SetHGaugeCG),
	    HLL_EXPORT(Parts_SetHGaugeRate, PE_SetHGaugeRate),
	    HLL_EXPORT(Parts_GetHGaugeNumerator, PE_GetHGaugeNumerator),
	    HLL_EXPORT(Parts_GetHGaugeDenominator, PE_GetHGaugeDenominator),
	    HLL_EXPORT(Parts_SetHGaugeReverse, PartsEngine_SetHGaugeReverse),
	    HLL_EXPORT(Parts_IsHGaugeReverse, PE_IsHGaugeReverse),
	    HLL_EXPORT(Parts_SetVGaugeCG, PE_SetVGaugeCG),
	    HLL_EXPORT(Parts_SetVGaugeRate, PE_SetVGaugeRate),
	    HLL_EXPORT(Parts_SetHGaugeSurfaceArea, PE_SetHGaugeSurfaceArea),
	    HLL_EXPORT(Parts_SetVGaugeSurfaceArea, PE_SetVGaugeSurfaceArea),
	    HLL_EXPORT(Parts_SetNumeralCG, PE_SetNumeralCG),
	    HLL_EXPORT(Parts_SetNumeralLinkedCGNumberWidthWidthList, PE_SetNumeralLinkedCGNumberWidthWidthList),
	    HLL_EXPORT(Parts_SetNumeralFont, PartsEngine_SetNumeralFont),
	    HLL_EXPORT(Parts_SetNumeralNumber, PE_SetNumeralNumber),
	    HLL_EXPORT(Parts_GetNumeralNumber, PE_GetNumeralNumber),
	    HLL_EXPORT(Parts_SetNumeralShowComma, PE_SetNumeralShowComma),
	    HLL_EXPORT(Parts_SetNumeralSpace, PE_SetNumeralSpace),
	    HLL_EXPORT(Parts_SetNumeralLength, PE_SetNumeralLength),
	    HLL_EXPORT(Parts_SetNumeralSurfaceArea, PE_SetNumeralSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsRectangleDetectionSize, PE_SetPartsRectangleDetectionSize),
	    HLL_TODO_EXPORT(Parts_SetPartsRectangleDetectionSurfaceArea, PartsEngine_Parts_SetPartsRectangleDetectionSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsCGDetectionSize, PE_SetPartsCGDetectionSize),
	    HLL_TODO_EXPORT(Parts_SetPartsCGDetectionSurfaceArea, PartsEngine_Parts_SetPartsCGDetectionSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsFlat, PE_SetPartsFlat),
	    HLL_EXPORT(Parts_SetPartsFlatThread, PE_SetPartsFlatThread),
	    HLL_EXPORT(Parts_GetPartsFlatName, PE_GetPartsFlatName),
	    HLL_EXPORT(Parts_IsPartsFlatStop, PE_IsPartsFlatStop),
	    HLL_EXPORT(Parts_IsThreadLoading, PE_IsThreadLoading),
	    HLL_EXPORT(IsExistWorkerThread, PE_IsExistWorkerThread),
	    HLL_EXPORT(Parts_IsPartsFlatEnd, PE_IsPartsFlatEnd),
	    HLL_EXPORT(Parts_GetPartsFlatCurrentFrameNumber, PE_GetPartsFlatCurrentFrameNumber),
	    HLL_EXPORT(Parts_BackPartsFlatBeginFrame, PE_BackPartsFlatBeginFrame),
	    HLL_EXPORT(Parts_StepPartsFlatFinalFrame, PE_StepPartsFlatFinalFrame),
	    HLL_EXPORT(Parts_SetPartsFlatSurfaceArea, PE_SetPartsFlatSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsFlatAndStop, PE_SetPartsFlatAndStop),
	    HLL_EXPORT(Parts_StopPartsFlat, PE_StopPartsFlat),
	    HLL_EXPORT(Parts_StartPartsFlat, PE_StartPartsFlat),
	    HLL_EXPORT(Parts_GoFramePartsFlat, PE_GoFramePartsFlat),
	    HLL_EXPORT(Parts_GetPartsFlatEndFrame, PE_GetPartsFlatEndFrame),
	    HLL_EXPORT(Parts_GetFlatFrame, PE_GetFlatFrame),
	    HLL_EXPORT(Parts_JumpFlatLabel, PE_JumpFlatLabel),
	    HLL_EXPORT(Parts_ExistsFlatFile, PE_ExistsFlatFile),
	    HLL_TODO_EXPORT(Parts_GetPartsFlatDataInfo, PartsEngine_Parts_GetPartsFlatDataInfo),
	    HLL_EXPORT(Parts_ChangeFlatCG, PE_ChangeFlatCG),
	    HLL_EXPORT(ReturnChangeFlatCG, PE_ReturnChangeFlatCG),
	    HLL_EXPORT(ClearChangeFlatCG, PE_ClearChangeFlatCG),
	    HLL_TODO_EXPORT(Parts_ChangeFlatSound, PartsEngine_Parts_ChangeFlatSound),
	    HLL_EXPORT(Parts_ClearPartsConstructionProcess, PE_ClearPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreateToPartsConstructionProcess, PE_AddCreateToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreatePixelOnlyToPartsConstructionProcess, PE_AddCreatePixelOnlyToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreateCGToProcess, PE_AddCreateCGToProcess),
	    HLL_EXPORT(Parts_AddFillToPartsConstructionProcess, PE_AddFillToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillAlphaColorToPartsConstructionProcess, PE_AddFillAlphaColorToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillAMapToPartsConstructionProcess, PE_AddFillAMapToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillWithAlphaToPartsConstructionProcess, PE_AddFillWithAlphaToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddFillGradationHorizonToPartsConstructionProcess, PartsEngine_Parts_AddFillGradationHorizonToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawRectToPartsConstructionProcess, PE_AddDrawRectToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawCutCGToPartsConstructionProcess, PE_AddDrawCutCGToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCopyCutCGToPartsConstructionProcess, PE_AddCopyCutCGToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddGrayFilterToPartsConstructionProcess, PE_AddGrayFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddAddFilterToPartsConstructionProcess, PartsEngine_Parts_AddAddFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddMulFilterToPartsConstructionProcess, PartsEngine_Parts_AddMulFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddDrawLineToPartsConstructionProcess, PartsEngine_Parts_AddDrawLineToPartsConstructionProcess),
	    HLL_EXPORT(Parts_BuildPartsConstructionProcess, PE_BuildPartsConstructionProcess),
	    HLL_EXPORT(Parts_BuildPartsConstructionProcessThread, PE_BuildPartsConstructionProcessThread),
	    HLL_EXPORT(Parts_AddDrawTextToPartsConstructionProcess, PE_AddDrawTextToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCopyTextToPartsConstructionProcess, PE_AddCopyTextToPartsConstructionProcess),
	    HLL_EXPORT(Parts_SetPartsConstructionSurfaceArea, PE_SetPartsConstructionSurfaceArea),
	    HLL_EXPORT(Parts_CreateParts3DLayerPluginID, PE_CreateParts3DLayerPluginID),
	    HLL_EXPORT(Parts_GetParts3DLayerPluginID, PE_GetParts3DLayerPluginID),
	    HLL_EXPORT(Parts_ReleaseParts3DLayerPluginID, PE_ReleaseParts3DLayerPluginID),
	    HLL_EXPORT(Parts_SetPassCursor, PE_SetPassCursor),
	    HLL_EXPORT(Parts_SetClickable, PE_SetClickable),
	    HLL_TODO_EXPORT(Parts_SetResetTimerByChangeInputStatus, PartsEngine_Parts_SetResetTimerByChangeInputStatus),
	    HLL_EXPORT(Parts_SetDrag, PE_SetDrag),
	    HLL_EXPORT(Parts_SetParentPartsNumber, PE_SetParentPartsNumber),
	    HLL_EXPORT(Parts_SetInputState, PE_SetInputState),
	    HLL_EXPORT(Parts_SetOnCursorShowLinkPartsNumber, PE_SetOnCursorShowLinkPartsNumber),
	    HLL_TODO_EXPORT(Parts_SetSoundNumber, PartsEngine_Parts_SetSoundNumber),
	    HLL_EXPORT(Parts_SetPartsPixelDecide, PE_SetPartsPixelDecide),
	    HLL_EXPORT(Parts_GetPartsPassCursor, PE_GetPartsPassCursor),
	    HLL_EXPORT(Parts_GetPartsClickable, PE_GetPartsClickable),
	    HLL_TODO_EXPORT(Parts_GetResetTimerByChangeInputStatus, PartsEngine_Parts_GetResetTimerByChangeInputStatus),
	    HLL_EXPORT(Parts_GetPartsDrag, PE_GetPartsDrag),
	    HLL_EXPORT(Parts_SetSwipeType, PE_SetSwipeType),
	    HLL_EXPORT(Parts_GetSwipeType, PE_GetSwipeType),
	    HLL_EXPORT(Parts_StopSwipe, PE_StopSwipe),
	    HLL_TODO_EXPORT(Parts_GetParentPartsNumber, PartsEngine_Parts_GetParentPartsNumber),
	    HLL_EXPORT(Parts_GetInputState, PE_GetInputState),
	    HLL_TODO_EXPORT(Parts_GetOnCursorShowLinkPartsNumber, PartsEngine_Parts_GetOnCursorShowLinkPartsNumber),
	    HLL_TODO_EXPORT(Parts_GetSoundNumber, PartsEngine_Parts_GetSoundNumber),
	    HLL_TODO_EXPORT(Parts_IsPartsPixelDecide, PartsEngine_Parts_IsPartsPixelDecide),
	    HLL_EXPORT(Parts_IsCursorIn, PE_IsCursorIn),
	    HLL_TODO_EXPORT(SaveParts, PartsEngine_SaveParts),
	    HLL_TODO_EXPORT(LoadParts, PartsEngine_LoadParts)
	    );

static struct ain_hll_function *get_fun(int libno, const char *name)
{
	int fno = ain_get_library_function(ain, libno, name);
	return fno >= 0 ? &ain->libraries[libno].functions[fno] : NULL;
}

static void PartsEngine_PreLink(void)
{
	if (ain->version >= 11) {
		static_library_replace(&lib_PartsEngine,"GetActivityPartsName",PartsEngine_GetActivityPartsName12);
		static_library_replace(&lib_PartsEngine,"GetActivityIntentDataDestination",PartsEngine_GetActivityIntentDataDestination12);
		static_library_replace(&lib_PartsEngine,"GetMessageVariableString",PartsEngine_GetMessageVariableString12);
		static_library_replace(&lib_PartsEngine,"Parts_GetPartsCGName",PartsEngine_GetPartsCGName12);
		static_library_replace(&lib_PartsEngine,"Parts_GetSoundName",PartsEngine_GetPartsSoundName12);
#define WIDGET12_REPLACE(name) static_library_replace(&lib_PartsEngine,#name,Widget12_##name);
		WIDGET12_STRING_GETTERS(WIDGET12_REPLACE)
#undef WIDGET12_REPLACE
#define TEXTBOX12_REPLACE(name) static_library_replace(&lib_PartsEngine,#name,Textbox12_##name);
        TEXTBOX12_STRING_GETTERS(TEXTBOX12_REPLACE)
#undef TEXTBOX12_REPLACE
		static_library_replace(&lib_PartsEngine,"Parts_GetParentPartsNumber",PartsEngine_GetParentPartsNumber12);
		static_library_replace(&lib_PartsEngine,"Parts_SetParentPartsNumber",PE_SetParentPartsNumber12);
		static_library_replace(&lib_PartsEngine, "AddPartsConstructionProcess", PartsEngine_AddPartsConstructionProcess12);
		static_library_replace(&lib_PartsEngine, "SetComponentType", PartsEngine_SetComponentType12);
		static_library_replace(&lib_PartsEngine, "GetComponentType", PartsEngine_GetComponentType12);
	}
	struct ain_hll_function *fun;
	int libno = ain_get_library(ain, "PartsEngine");
	assert(libno >= 0);
	fun = get_fun(libno, "PlayPartsMovie");
	if (fun && fun->nr_arguments == 3 &&
			fun->arguments[0].type.data == AIN_INT &&
			fun->arguments[1].type.data == AIN_INT &&
			fun->arguments[2].type.data == AIN_INT)
		static_library_replace(&lib_PartsEngine,"PlayPartsMovie",PE_PlayPartsMovieAt);
	fun = get_fun(libno, "ReadActivityFile");
	if (fun && fun->nr_arguments == 3 &&
			fun->arguments[0].type.data == AIN_STRING &&
			fun->arguments[1].type.data == AIN_STRING &&
			fun->arguments[2].type.data == AIN_BOOL)
		static_library_replace(&lib_PartsEngine,"ReadActivityFile",PE_ReadActivityFileEx);

	fun = get_fun(libno, "AddDrawCutCGToPartsConstructionProcess");
	if (fun && fun->nr_arguments == 12) {
		static_library_replace(&lib_PartsEngine, "AddDrawCutCGToPartsConstructionProcess",
				PE_AddDrawCutCGToPartsConstructionProcess);
	}
	fun = get_fun(libno, "AddCopyCutCGToPartsConstructionProcess");
	if (fun && fun->nr_arguments == 12) {
		static_library_replace(&lib_PartsEngine, "AddCopyCutCGToPartsConstructionProcess",
				PE_AddCopyCutCGToPartsConstructionProcess);
	}
	fun = get_fun(libno, "Update");
	if (fun && fun->nr_arguments == 5) {
		static_library_replace(&lib_PartsEngine, "Update",
				PartsEngine_Update_Pascha3PC);
	}
	if (ain->version >= 11) {
		fun = get_fun(libno, "UpdateComponent");
		if (fun && fun->nr_arguments == 5 && fun->arguments &&
		    fun->arguments[0].type.data == AIN_INT &&
		    fun->arguments[1].type.data == AIN_INT &&
		    fun->arguments[2].type.data == AIN_BOOL &&
		    fun->arguments[3].type.data == AIN_FLOAT &&
		    fun->arguments[4].type.data == AIN_FLOAT)
			static_library_replace(&lib_PartsEngine, "UpdateComponent",
					PartsEngine_UpdateComponent12);
		else if (fun && !(fun->nr_arguments == 3 && fun->arguments &&
			fun->arguments[0].type.data == AIN_INT &&
			fun->arguments[1].type.data == AIN_BOOL &&
			fun->arguments[2].type.data == AIN_BOOL))
			static_library_replace(&lib_PartsEngine, "UpdateComponent", NULL);
	}
	if (get_fun(libno, "AddController")) {
		PE_enable_multi_controller();
	}
	parts_func_uses_rance9_ids = ain_get_function(ain, "PARTS_GetLayerIndex") < 0;
}
