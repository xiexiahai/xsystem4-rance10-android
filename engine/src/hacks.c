/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/buffer.h"
#include "system4/instructions.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "gfx/gfx.h"
#include "input.h"
#include "message_layout.h"
#include "xsystem4.h"

#include "hll/hll.h"

bool game_daibanchou_en = false;
bool game_rance02_mg = false;
bool game_rance6_mg = false;
bool game_rance7_mg = false;
bool game_rance8 = false;
bool game_rance8_mg = false;
bool game_dungeons_and_dolls = false;

static void write_instruction0(struct buffer *out, enum opcode op)
{
	buffer_write_int16(out, op);
}

static void write_instruction1(struct buffer *out, enum opcode op, int32_t arg)
{
	buffer_write_int16(out, op);
	buffer_write_int32(out, arg);
}

static void write_instruction3(struct buffer *out, enum opcode op, int32_t arg0, int32_t arg1,
		int32_t arg2)
{
	buffer_write_int16(out, op);
	buffer_write_int32(out, arg0);
	buffer_write_int32(out, arg1);
	buffer_write_int32(out, arg2);
}

void set_msgskip_delay(struct ain *ain, unsigned ms)
{
	int orig_fno = ain_get_function(ain, "A");
	if (orig_fno <= 0) {
		WARNING("No 'A' function when applying msgskip delay");
		return;
	}

	int new_fno = ain_dup_function(ain, orig_fno);
	struct ain_function *override_f = &ain->functions[orig_fno];

	// override void A(void) {
	//     super();
	//     if (key_is_down(VK_CONTROL))
	//         System.sleep(ms);
	// }
	struct buffer out;
	buffer_init(&out, NULL, 0);
	write_instruction1(&out, FUNC, orig_fno);
	write_instruction1(&out, CALLFUNC, new_fno);
	write_instruction1(&out, PUSH, VK_CONTROL);
	write_instruction1(&out, CALLSYS, VM_XSYS_KEY_IS_DOWN);
	uint32_t ifz_addr = out.index;
	write_instruction1(&out, IFZ, 0);
	write_instruction1(&out, PUSH, ms);
	write_instruction1(&out, CALLSYS, SYS_SLEEP);
	buffer_write_int32_at(&out, ifz_addr + 2, ain->code_size + out.index);
	write_instruction0(&out, RETURN);
	write_instruction1(&out, ENDFUNC, orig_fno);

	override_f->address = ain->code_size + 6;
	ain->code = xrealloc(ain->code, ain->code_size + out.index);
	memcpy(ain->code + ain->code_size, out.buf, out.index);
	ain->code_size += out.index;
}

// Rance 02 (MangaGamer version)
// -----------------------------
// Uses a custom proportional font implementation which makes assumptions
// about the font. We replace the _CalculateWidth function with a font-generic
// implementation.
//
// '^' character is mapped to 'é' (Rosé)
// '}' character is mapped to double-width dash
static void apply_rance02_hacks(struct ain *ain)
{
	int fno = ain_get_function(ain, "_CalculateWidth");
	if (fno <= 0) {
		WARNING("No '_CalculateWidth' function when applying Rance 02 compatibility hack");
		return;
	}
	struct ain_function *f = &ain->functions[fno];

	// Reduce the text scale if it wasn't set by the user
	if (!config.manual_text_x_scale) {
		config.text_x_scale = 0.9375;
	}

	// rewrite function
	struct buffer out;
	buffer_init(&out, ain->code, ain->code_size);
	buffer_seek(&out, f->address);

	// _CalculateWidth(ref string str, int nFontSize, int nWeightSize) {
	//     if (str[0] > 255 || str[0] == '}')
	//         return nFontSize + nWeightSize;
	//     return nFontSize / 2 + nWeightSize;
	// }

	// if (str[0] > 255 || str[0] == '}')
	write_instruction1(&out, SH_LOCALREF, 0);
	write_instruction1(&out, PUSH, 0);
	write_instruction0(&out, C_REF);
	write_instruction0(&out, DUP);
	write_instruction1(&out, PUSH, 255);
	write_instruction0(&out, GT);
	write_instruction0(&out, SWAP);
	write_instruction1(&out, PUSH, '}');
	write_instruction0(&out, EQUALE);
	write_instruction0(&out, OR);
	int ifz_addr = out.index;
	write_instruction1(&out, IFZ, 0);

	// return nFontSize + nWeightSize
	write_instruction1(&out, SH_LOCALREF, 1);
	write_instruction1(&out, SH_LOCALREF, 2);
	write_instruction0(&out, ADD);
	int jump_addr = out.index;
	write_instruction1(&out, JUMP, 0);

	// return nFontSize / 2 + nWeightSize;
	buffer_write_int32_at(&out, ifz_addr + 2, out.index);
	write_instruction1(&out, SH_LOCALREF, 1);
	write_instruction1(&out, PUSH, 2);
	write_instruction0(&out, DIV);
	write_instruction1(&out, SH_LOCALREF, 2);
	write_instruction0(&out, ADD);

	// multiply by config.text_x_scale (if not 1.0)
	buffer_write_int32_at(&out, jump_addr + 2, out.index);
	if (fabsf(1.0f - config.text_x_scale) > 0.01) {
		union { int32_t i; float f; } cast = { .f = config.text_x_scale };
		write_instruction0(&out, ITOF);
		write_instruction1(&out, F_PUSH, cast.i);
		write_instruction0(&out, F_MUL);
		write_instruction0(&out, FTOI);
	}

	write_instruction0(&out, RETURN);
	write_instruction1(&out, ENDFUNC, fno);

	game_rance02_mg = true;
}

// Rance VI (MangaGamer version)
// -----------------------------
// 'ﾉ' character is mapped to 'é' (Rosé)
static void apply_rance6_hacks(struct ain *ain)
{
	if (ain_get_function(ain, "GetTextWidth") < 0)
		return;
	game_rance6_mg = true;
}

// Rance IX
// --------
// T_PartsNo class is implemented in an extremely inefficient way, which
// causes noticable performance issues when using the UI (e.g. when loading
// the items screen).
// We rewrite this class to improve performance.
static void apply_rance9_hacks(struct ain *ain)
{
	int add_fno = ain_get_function(ain, "T_PartsNo@add");
	int erase_fno = ain_get_function(ain, "T_PartsNo@erase");
	if (add_fno < 0 || erase_fno < 0) {
		WARNING("Couldn't location T_PartsNo@add or T_PartsNo@erase");
		return;
	}

	struct buffer out;
	buffer_init(&out, ain->code, ain->code_size);

	// bool T_PartsNo::add(int nNo)
	// {
	//     if (nNo < 10000 || nNo > 999999)
	//         return false;
	//     int idx = nNo - 10000;
	//     if (this.m_abUsed[idx])
	//         return false;
	//     return (this.m_abUsed[idx] = true);
	// }
	int32_t return_false_addr[3];
	struct ain_function *f = &ain->functions[add_fno];
	buffer_seek(&out, f->address);
	// if (nNo < 10000 || nNo > 999999) return false;
	write_instruction3(&out, SH_IF_LOC_LT_IMM, 0, 10000, 0);
	return_false_addr[0] = out.index - 4;
	write_instruction3(&out, SH_IF_LOC_GT_IMM, 0, 999999, 0);
	return_false_addr[1] = out.index - 4;
	// int idx = nNo - 10000;
	write_instruction0(&out, PUSHLOCALPAGE);
	write_instruction1(&out, PUSH, 1);
	write_instruction1(&out, SH_LOCALREF, 0);
	write_instruction1(&out, PUSH, 10000);
	write_instruction0(&out, SUB);
	write_instruction0(&out, ASSIGN);
	write_instruction0(&out, POP);
	// if (this.m_abUsed[idx]) return false;
	write_instruction1(&out, SH_STRUCTREF, 1);
	write_instruction1(&out, SH_LOCALREF, 1);
	write_instruction0(&out, REF);
	write_instruction1(&out, IFNZ, 0);
	return_false_addr[2] = out.index - 4;
	// return (this.m_abUsed[idx] = true);
	write_instruction1(&out, SH_STRUCTREF, 1);
	write_instruction1(&out, SH_LOCALREF, 1);
	write_instruction1(&out, PUSH, 1);
	write_instruction0(&out, ASSIGN);
	write_instruction0(&out, RETURN);
	// [return false]
	buffer_write_int32_at(&out, return_false_addr[0], out.index);
	buffer_write_int32_at(&out, return_false_addr[1], out.index);
	buffer_write_int32_at(&out, return_false_addr[2], out.index);
	write_instruction1(&out, PUSH, 0);
	write_instruction0(&out, RETURN);

	// bool T_PartsNo::erase(int nNo)
	// {
	//     if (nNo < 10000 || nNo > 999999)
	//         return false;
	//     this.m_abUsed[nNo - 10000] = false;
	//     return true;
	// }
	f = &ain->functions[erase_fno];
	buffer_seek(&out, f->address);
	// if (nNo < 10000 || nNo > 999999) return false;
	write_instruction3(&out, SH_IF_LOC_LT_IMM, 0, 10000, 0);
	return_false_addr[0] = out.index - 4;
	write_instruction3(&out, SH_IF_LOC_GT_IMM, 0, 999999, 0);
	return_false_addr[1] = out.index - 4;
	// this.m_abUsed[nNo - 10000] = false;
	write_instruction1(&out, SH_STRUCTREF, 1);
	write_instruction1(&out, SH_LOCALREF, 0);
	write_instruction1(&out, PUSH, 10000);
	write_instruction0(&out, SUB);
	write_instruction1(&out, PUSH, 0);
	write_instruction0(&out, ASSIGN);
	write_instruction0(&out, POP);
	// return true;
	write_instruction1(&out, PUSH, 1);
	write_instruction0(&out, RETURN);
	// [return false]
	buffer_write_int32_at(&out, return_false_addr[0], out.index);
	buffer_write_int32_at(&out, return_false_addr[1], out.index);
	write_instruction1(&out, PUSH, 0);
	write_instruction0(&out, RETURN);
}

// Daibanchou (English fan translation)
// ------------------------------------
// Gpx2Plus.CopyStretchReduceAMap behaves differently in order to work around
// text rendering issues caused by how the game calculates text width in
// StructRegionalSelect::DrawMenuItem.
static void apply_daibanchou_hacks(struct ain *ain)
{
	if (ain->nr_strings > 2 && !strcmp(ain->strings[2]->text, "Seijou Academy")) {
		game_daibanchou_en = true;
	}
}

static void apply_rance03_message_layout(struct ain *ain)
{
	// Reflow raw glyph positions before the script applies centre/right alignment.
	int fno=ain_get_function(ain,"CMessageTextView@MoveTextPosForLayout");
	if (fno<0 || ain->functions[fno].nr_args!=1 || !message_layout_init_rance03(ain)) return;
	int original=ain_dup_function(ain,fno);
	struct buffer out;
	buffer_init(&out,NULL,0);
	write_instruction1(&out,FUNC,fno);
	write_instruction0(&out,PUSHSTRUCTPAGE);
	write_instruction1(&out,CALLSYS,VM_XSYS_FIT_RANCE03_MESSAGE);
	write_instruction0(&out,PUSHSTRUCTPAGE);
	write_instruction1(&out,SH_LOCALREF,0);
	write_instruction1(&out,CALLMETHOD,original);
	write_instruction0(&out,RETURN);
	write_instruction1(&out,ENDFUNC,fno);
	ain->functions[fno].address=ain->code_size+6;
	ain->code=xrealloc(ain->code,ain->code_size+out.index);
	memcpy(ain->code+ain->code_size,out.buf,out.index);
	ain->code_size+=out.index;
	free(out.buf);
}

static void apply_rance03_card_caption_layout(struct ain *ain)
{
	// Leave a little more padding below Chinese place-card captions. Change
	// only this method's Y argument; instruction and saved return addresses stay put.
	int fno=ain_get_function(ain,"AdvSelectionButton@setPartsPos");
	int set_pos=ain_get_function(ain,"Parts@setPos");
	if (fno<0 || set_pos<0) return;
	int end=ain->code_size,found=-1;
	for (int i=0;i<ain->nr_functions;i++) {
		int address=ain->functions[i].address;
		if (address>ain->functions[fno].address && address<end) end=address;
	}
	union { float f; uint32_t i; } old={.f=125.f},replacement={.f=121.f};
	for (int address=ain->functions[fno].address;address+2<=end;) {
		enum opcode op=LittleEndian_getW(ain->code,address);
		if (op>=NR_OPCODES) return;
		int size=instruction_width(op);
		if (address+size>end) return;
		if (op==F_PUSH && LittleEndian_getDW(ain->code,address+2)==old.i
				&& address+size+6<=end && LittleEndian_getW(ain->code,address+size)==CALLMETHOD
				&& LittleEndian_getDW(ain->code,address+size+2)==set_pos) {
			if (found>=0) return; // Unexpected script version: leave it untouched.
			found=address+2;
		}
		address+=size;
	}
	if (found>=0) LittleEndian_putDW(ain->code,found,replacement.i);
}

static void fix_rance03_effect_sound_prefix(struct ain *ain, const char *method,
		const char *folder, int old_skip, int nr_args)
{
	// The fan translation renamed FLAT folders, but kept the Japanese script's
	// fixed character offsets. For example 戦闘效果\\ needs five characters
	// removed, whereas バトルエフェクト\\ needed eight. Keep the shared sound
	// logic and every saved code address; change only its two substring operands.
	int fno=ain_get_function(ain,method);
	if (fno<0 || ain->functions[fno].nr_args!=nr_args) return;
	char *prefix=utf2sjis(folder,0);
	size_t prefix_bytes=strlen(prefix);
	bool found_prefix=false;
	for (int i=0;i<ain->nr_strings;i++) {
		const char *s=ain->strings[i]->text;
		if (!strncmp(s,prefix,prefix_bytes) && (s[prefix_bytes]=='/' || s[prefix_bytes]=='\\')) {
			found_prefix=true;break;
		}
	}
	int replacement=sjis_count_char(prefix)+1;
	free(prefix);
	if (!found_prefix || replacement==old_skip) return;
	int begin=ain->functions[fno].address,end=ain->code_size;
	for (int i=0;i<ain->nr_functions;i++) {
		int address=ain->functions[i].address;
		if (address>begin && address<end) end=address;
	}
	int operands[2],count=0,length=-1,substring=-1;
	for (int p=begin;p+2<=end;) {
		enum opcode op=LittleEndian_getW(ain->code,p);
		if (op>=NR_OPCODES) return;
		int size=instruction_width(op);
		if (size<=0 || p+size>end) return;
		if (op==PUSH && LittleEndian_getDW(ain->code,p+2)==(unsigned)old_skip) {
			if (count==2) return;
			operands[count++]=p;
		}
		if (op==S_LENGTH) { if(length>=0)return; length=p; }
		if (op==S_GETPART) { if(substring>=0)return; substring=p; }
		p+=size;
	}
	if (count!=2 || !(operands[0]<length && length<operands[1])
			|| operands[1]+8!=substring || LittleEndian_getW(ain->code,operands[1]+6)!=SUB) return;
	for (int i=0;i<2;i++) LittleEndian_putDW(ain->code,operands[i]+2,replacement);
}

static void apply_rance03_effect_sound_paths(struct ain *ain)
{
	fix_rance03_effect_sound_prefix(ain,"DamageEffect@playSound","戦闘效果",8,0);
	fix_rance03_effect_sound_prefix(ain,"SceneDungeon@openDungeonEffectFlat","迷宫",6,1);
}

static void apply_rance03_skill_cooldown_layout(struct ain *ain)
{
	// This translated battle FLAT has its circle centred at (45,45).
	// Anchor it to the cooldown numeral at (59,61). Restrict the adjustment
	// to part 8 in the battle branch; party CGs and blue action badges differ.
	int fno=ain_get_function(ain,"SkillSelector@setPartsPos");
	int set_pos=ain_get_function(ain,"Parts@setPos");
	if (fno<0 || set_pos<0 || ain->functions[fno].nr_args) return;
	int begin=ain->functions[fno].address,end=ain->code_size,found=-1;
	for (int i=0;i<ain->nr_functions;i++) {
		int address=ain->functions[i].address;
		if (address>begin && address<end) end=address;
	}
	union {float f;uint32_t i;} old_x={.f=-30.f},old_y={.f=18.f};
	for (int p=begin;p+2<=end;) {
		enum opcode op=LittleEndian_getW(ain->code,p);
		if (op>=NR_OPCODES) return;
		int size=instruction_width(op);
		if (size<=0 || p+size>end) return;
		if (op==F_PUSH && p>=begin+8 && p+18<=end
				&& LittleEndian_getW(ain->code,p-8)==PUSH
				&& LittleEndian_getDW(ain->code,p-6)==8
				&& LittleEndian_getW(ain->code,p-2)==REF
				&& LittleEndian_getDW(ain->code,p+2)==old_x.i
				&& LittleEndian_getW(ain->code,p+6)==F_PUSH
				&& LittleEndian_getDW(ain->code,p+8)==old_y.i
				&& LittleEndian_getW(ain->code,p+12)==CALLMETHOD
				&& LittleEndian_getDW(ain->code,p+14)==set_pos) {
			if (found>=0) return;
			found=p;
		}
		p+=size;
	}
	if (found>=0) {
		// Change operands only: existing save/return addresses remain valid.
		union {float f;uint32_t i;} x={.f=14.f},y={.f=16.f};
		LittleEndian_putDW(ain->code,found+2,x.i);
		LittleEndian_putDW(ain->code,found+8,y.i);
	}
}

void apply_game_specific_hacks(struct ain *ain)
{
	if (ain->version == 12 && config.game_name &&
		!strncmp(config.game_name, "\xc0\xbc\xcb\xb9" "10", 6))
		sys_text_is_gbk = true;
	// Detect the Chinese Rance 03 title before decoding it or loading fonts.
	if (config.game_name && !strncmp(config.game_name, "\xc0\xbc\xcb\xb9" "03", 6)) {
		sys_text_is_gbk = true;
		apply_rance03_message_layout(ain);
		apply_rance03_card_caption_layout(ain);
		apply_rance03_effect_sound_paths(ain);
		apply_rance03_skill_cooldown_layout(ain);
	}
	char *game_name = sjis2utf(config.game_name, 0);
	if (!strcmp(game_name, "大番長")) {
		apply_daibanchou_hacks(ain);
	} else if (!strcmp(game_name, "Rance 02")) {
		apply_rance02_hacks(ain);
	} else if (!strcmp(game_name, "Rance6")) {
		apply_rance6_hacks(ain);
	} else if (!strcmp(game_name, "ランス・クエスト")) {
		game_rance8 = true;
	} else if (!strcmp(game_name, "Rance Quest")) {
		game_rance8 = true;
		game_rance8_mg = true;
	} else if (!strcmp(game_name, "ＤＵＮＧＥＯＮＳ＆ＤＯＬＬＳ")) {
		game_dungeons_and_dolls = true;
	} else if (!strcmp(game_name, "ランス９")
			|| !strcmp(game_name, "Rance 9 - The Helmanian Revolution")) {
		apply_rance9_hacks(ain);
	}
	free(game_name);

	// XXX: we don't rely on game_name because mods change it
	if (ain_get_library(ain, "SengokuRanceFont") >= 0)
		game_rance7_mg = true;
}
