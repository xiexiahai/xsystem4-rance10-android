/* Copyright (C) 2026 xsystem4 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/archive.h"
#include "system4/ex.h"
#include "system4/file.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "asset_manager.h"
#include "input.h"
#include "parts.h"
#include "parts_activity.h"
#include "parts_internal.h"
#include "parts_message_window.h"
#include "message_window_internal.h"
#include "textbox_internal.h"
#include "vm.h"
#include "xsystem4.h"
#include "perf_trace.h"

struct pact_reader {
	bool error;
	bool edit_mode;
	unsigned nodes;
	struct string *activity;
	const struct ex_tree *compat_hidden;
	const struct ex_tree *compat_unused_route[3];
};

// Localized AIN12 editor keys. Resolve aliases while preserving the EX tree.
static struct ex_tree *pact_child(struct ex_tree *tree,const char *key)
{
	if(!tree) return NULL;
	struct ex_tree *child=ex_tree_get_child(tree,key);
	if(child) return child;
	static const char *aliases[][2]={
		{"種類別情報","种类別情报"},{"パーツタイプ","パーツ类型"},
		{"通常状態","一般状态"},{"オンカーソル状態","光标选中状态"},{"キーダウン状態","键DOWN状态"},
		{"原点座標モード","原点座標模式"},{"加算色","加法色"},{"乗算色","乘法色"},{"回転","旋转"},
		{"メッセージウィンドウ表示連動","消息窗口表示連動"},{"クリック許可","点击許可"},
		{"オンカーソル効果音","光标选中效果音"},{"クリック効果音","点击效果音"},
		{"マウスカーソルピクセル判定","鼠标光标像素判定"},{"マージン","边距"},
		{"アルファクリッパー","α剪切器ー"},{"オンカーソル表示連動","光标选中表示連動"},
		{"チェックボックス表示連動","复选框表示連動"},{"スクロールＸ座標連動","滚动条Ｘ座標連動"},
		{"スクロールＹ座標連動","滚动条Ｙ座標連動"},{"スクロールアルファ連動","滚动条アルファ連動"},
		{"座標をレートとして認識する","座標を率として認識する"},{"変形","变形"},
		{"レイアウトタイプ","レイアウト类型"},{"折り返し許可","翻折許可"},{"折り返し尺寸","翻折尺寸"}
		,{"有効","有效"},{"ボタンの色","按钮の色"},{"フォントタイプ","字体类型"},{"フォント尺寸","字体尺寸"},
		{"スピードアップ有効","スピードアップ有效"},
		{"フォント色","字体色"},{"フォント太さ","字体粗度"},{"フォント縁取り色","字体镶边色"},{"フォント縁取り","字体镶边"},
		{"テキスト","文本"},{"テキスト位置","文本位置"},{"文字間隔","文字间距"},{"行間隔","行间距"}
		,{"テキスト装飾","文本装飾"},{"字間隔","字间距"},{"チェック状態","选择状态"},{"ボタンモード","按钮模式"},
        {"チェックボックスリスト","复选框リスト"},
        {"値","值"},{"既定値","默认值"},{"最大値","最大值"},{"最小値","最小值"},{"増分値","増分值"},
        {"最小サイズ","最小尺寸"},{"最大サイズ","最大尺寸"},{"リサイズ許可","リ尺寸許可"},
        {"アイコン","图标"},{"タイトル","标题"},{"キャプション色","字幕颜色"},{"クライアント色","客户端颜色"},
        {"折りたたみ時高さ","折りたたみ時高度"},{"閉じるボタン有効","关闭按钮有效"}
        ,{"行の高さ","行の高度"},{"選択インデックス","选择インデックス"}
		,{"数値","数值"},{"表示タイプ","表示类型"},
        {"読み取り専用","读取专用"},{"読み取り専用背景色","读取专用背景色"},
        {"選択色","选择色"},{"スクロールバー表示","滚动条栏表示"},
        {"全体スクロール量","全体滚动条量"},{"スクロール位置","滚动条位置"},
        {"ボタンクリック移動量","按钮点击移动量"},{"全体スクロール量サイズ連動","全体滚动条量尺寸連動"},{"表示量サイズ連動","表示量尺寸連動"}
	};
	for(unsigned i=0;i<sizeof(aliases)/sizeof(*aliases);i++)
		if(!strcmp(key,aliases[i][0])) return ex_tree_get_child(tree,aliases[i][1]);
	return NULL;
}

static struct ex_value *pact_value(struct ex_tree *tree, const char *key)
{
	struct ex_tree *child = pact_child(tree,key);
	return child && child->is_leaf ? &child->leaf.value : NULL;
}

static struct ex_tree *pact_branch(struct ex_tree *tree, const char *key)
{
	struct ex_tree *child = pact_child(tree,key);
	return child && !child->is_leaf ? child : NULL;
}

#include "pact_legacy_metadata.inc"

static float pact_number(struct pact_reader *r, struct ex_value *v, float dflt)
{
	if (!v)
		return dflt;
	float n;
	if (v->type == EX_INT)
		n = v->i;
	else if (v->type == EX_FLOAT)
		n = v->f;
	else {
		r->error = true;
		return dflt;
	}
	if (!isfinite(n) || fabsf(n) > 10000000.f) {
		r->error = true;
		return dflt;
	}
	return n;
}

static float pact_num(struct pact_reader *r, struct ex_tree *t, const char *key, float dflt)
{
	struct ex_value *v=pact_value(t,key);
	if(v && v->type==EX_STRING && !strcmp(key,"パーツタイプ")) {
		static const struct {const char *name;int type;} kinds[]={
			{"按钮",0},{"复选框",1},{"纵向滚动条栏",2},{"横向滚动条栏",3},
			{"文本框",5},{"テキストボックス",5},{"多行文本框",7},{"マルチテキストボックス",7},{"スピンボックス",6},{"フォーム",PARTS_WIDGET_FORM},{"リストボックス",PARTS_WIDGET_LISTBOX},{"面板",4},{"レイアウトボックス",8},{"ラジオ按钮ボックス",9},{"低レベルパーツ",10},
			{"ＣＧパーツ",11},{"文本パーツ",13},{"横向计量パーツ",14},{"数字パーツ",16},{"矩形パーツ",17},{"構築パーツ",18},{"消息窗口",19},{"メッセージウィンドウ",19},{"フラットパーツ",20},{"用户组件",1000}
		};
		for(unsigned i=0;i<sizeof(kinds)/sizeof(*kinds);i++) if(!strcmp(v->s->text,kinds[i].name)) return kinds[i].type;
		WARNING("Unsupported PACT component: %s",v->s->text);return -1;
	}
	return pact_number(r,v,dflt);
}

static float pact_at(struct pact_reader *r, struct ex_tree *t, const char *key, unsigned i, float dflt)
{
	struct ex_value *v = pact_value(t, key);
	if (!v)
		return dflt;
	if (v->type != EX_LIST || i >= v->list->nr_items) {
		r->error = true;
		return dflt;
	}
	return pact_number(r, &v->list->items[i].value, dflt);
}

static const char *pact_string(struct pact_reader *r, struct ex_tree *t, const char *key)
{
	struct ex_value *v = pact_value(t, key);
	if (!v)
		return "";
	if (v->type != EX_STRING) {
		r->error = true;
		return "";
	}
	return v->s->text;
}

static struct string *pact_to_utf8(const char *text, size_t size)
{
	// EX strings are length-prefixed, while the legacy converter scans to NUL.
	// Bound the input so following binary fields cannot become part of a key.
	struct string *bounded = make_string(text, size);
	char *utf = sjis2utf(bounded->text, bounded->size);
	struct string *s = cstr_to_string(utf);
	free_string(bounded);
	free(utf);
	return s;
}

static struct string *pact_to_game(const char *text)
{
	char *encoded = utf2sjis(text, 0);
	struct string *s = cstr_to_string(encoded);
	free(encoded);
	return s;
}

#include "pact_compatibility.inc"

#include "pact_numeral.inc"
#include "pact_hgauge.inc"
#include "pact_message_window.inc"
#include "pact_textbox.inc"
#include "pact_construction.inc"
#include "pact_spin_form.inc"
#include "pact_listbox.inc"

static bool pact_supported_state(struct pact_reader *r, struct ex_tree *t)
{
    int type=pact_num(r,t,"パーツタイプ",-1);
    if (type==11 || type==13 || type==17 || type==20) return true;
    if (type==14) return pact_hgauge_validate(r,t);
    if (type==16) return pact_numeral_validate(r,t);
    if (type==19) return pact_mw_validate(r,t);
    if (type!=18) return false;
    return pact_cp_validate(r,t);
}

static bool pact_supported_component(struct pact_reader *r, struct ex_tree *t)
{
	int type = pact_num(r, t, "パーツタイプ", -1);
	if(type==1000) {
		struct ex_value *data=pact_value(t,"数据");
		if(!data || data->type!=EX_LIST || data->list->nr_items%2 || !*pact_string(r,t,"用户组件名")) return false;
		for(unsigned i=0;i<data->list->nr_items;i++) if(data->list->items[i].value.type!=EX_STRING) return false;
		return !r->error;
	}
	if (type == 10) {
		const char *states[] = { "通常状態", "オンカーソル状態", "キーダウン状態" };
		for (unsigned i = 0; i < 3; i++) {
			struct ex_tree *state = pact_branch(t, states[i]);
			if (!pact_supported_state(r,state)) {
				WARNING("PACT unsupported low-level state %s (type=%d)",states[i],(int)pact_num(r,state,"パーツタイプ",-1));
				return false;
			}
		}
		return !r->error;
	}
    if(type==PARTS_WIDGET_SPINBOX)return pact_spin_validate(r,t);
    if(type==PARTS_WIDGET_FORM)return pact_form_validate(r,t);
    if(type==PARTS_WIDGET_LISTBOX)return pact_listbox_validate(r,t);
    if(type==5 || type==7)return pact_tb_validate(r,t);
	if(type==4) {
		for(int i=0;i<4;i++) if(pact_at(r,t,"アルファ色彩渐变",i,0)!=0) return false;
		return !r->error;
	}
    if(type==2 || type==3) {
        pact_string(r,t,"全体スクロール量サイズ連動");pact_string(r,t,"表示量サイズ連動");
        const char *keys[]={"長さ","幅","前尺寸","次尺寸","全体スクロール量","表示量","スクロール位置","ボタンクリック移動量"};
        const int defaults[]={100,24,0,0,100,1,0,1};
        for(int i=0;i<8;i++) {
            float v=pact_num(r,t,keys[i],defaults[i]);
            if(v<0 || v!=floorf(v) || (i<4 && v>4096) || (i<2 && v==0))return false;
        }
        if(*pact_string(r,t,"フラット名") && *pact_string(r,t,"ＣＧ名"))return false;
        const char *roles[]={"バー","前","次","背景"};
        for(int i=0;i<4;i++) {
            char a[96],b[96];snprintf(a,sizeof(a),"上書き%sフラット名",roles[i]);snprintf(b,sizeof(b),"上書き%sＣＧ名",roles[i]);
            if(*pact_string(r,t,a) && *pact_string(r,t,b))return false;
        }
        return !r->error;
    }

	if (type>=0 && type<=1)
		return !r->error && (!*pact_string(r,t,"フラット名") || !*pact_string(r,t,"ＣＧ名"));
	return !r->error && (type==8 || type==9 || pact_supported_state(r,t));
}

static bool pact_validate_part(struct pact_reader *r, struct ex_tree *t, unsigned depth)
{
	if (!t || t->is_leaf || depth > 64 || ++r->nodes > 4096)
		return false;
	struct ex_tree *component = pact_branch(t, "種類別情報");
	if (!component || !pact_supported_component(r, component)) {
		WARNING("Activity component %s type %d is not supported",t->name?t->name->text:"(unnamed)",(int)pact_num(r, component, "パーツタイプ", -1));
		return false;
	}
	// These references need a second binding pass. Do not report a successful
	// load while silently dropping a nonempty link. Resolve named references
	// after the complete activity exists.
	const char *links[] = { "スクロールＸ座標連動", "スクロールＹ座標連動", "スクロールアルファ連動" };
	for (unsigned i = 0; i < sizeof(links) / sizeof(*links); i++)
		pact_string(r,t,links[i]); /* Types checked now; references bind after creation. */
	if (!pact_mw_range(r,t,"座標をレートとして認識する",0,0,1,true)) return false;
	struct ex_value *destinations=pact_value(t,"遷移先");
	if (destinations) {
		if (destinations->type!=EX_LIST) return false;
		for (unsigned i=0;i<destinations->list->nr_items;i++)
			if (destinations->list->items[i].value.type!=EX_STRING) return false;
		pact_num(r,t,"遷移方法",0);
	}
	struct ex_tree *children = pact_branch(t, "子パーツ");
	if (children)
		for (unsigned i = 0; i < children->nr_children; i++)
			if (!pact_validate_part(r, &children->children[i], depth + 1))
				return false;
	return !r->error;
}

// Parts 27..33 share the observed state layout with restricted legacy flags.
// Only the listed header/parts pairs are admitted; nondefault older metadata
// needs separate state/timing/addressing semantics before acceptance.
static bool pact_validate_parts33_flags(struct ex_tree *t, unsigned depth, bool parts31)
{
	return pact_validate_legacy_metadata(t, depth, parts31, false);
}

static bool pact_supported_version(struct ex *ex)
{
	if (ex_get_int(ex,"バージョン",-1)==2 &&
	    ex_get_int(ex,"パーツバージョン",-1)==7)
		return true;
	int version = ex_get_int(ex,"版本",-1);
	int parts = ex_get_int(ex,"パーツ版本",-1);
	if (version==4 && parts==12) {
		struct ex_tree *root = pact_branch(ex_get_tree(ex,"アクティビティ"),"ルートパーツ");
		return pact_validate_legacy_metadata(root,0,true,true) &&
			pact_validate_parts12_marks(root,0);
	}
	if (version==5 && parts>=27 && parts<=30)
		return pact_validate_legacy_metadata(
			pact_branch(ex_get_tree(ex,"アクティビティ"),"ルートパーツ"),0,true,true);
	if (version!=6)
		return false;
	if (parts==30)
		return pact_validate_legacy_metadata(
			pact_branch(ex_get_tree(ex,"アクティビティ"),"ルートパーツ"),0,true,true);
	if (parts>=34 && parts<=40) return true;
	return (parts>=31 && parts<=33) &&
		pact_validate_parts33_flags(pact_branch(ex_get_tree(ex,"アクティビティ"),
			"ルートパーツ"),0,parts==31);
}

static bool pact_apply_state(struct pact_reader *r, int number, struct ex_tree *t, int state)
{
	int type = pact_num(r, t, "パーツタイプ", -1);
	if(type==19) return pact_mw_apply(r,number,t,state);
	if(type==17) {
		float x=pact_at(r,t,"左上",0,0),y=pact_at(r,t,"左上",1,0);
		float width=pact_at(r,t,"右下",0,0)-x,height=pact_at(r,t,"右下",1,0)-y;
		if(x!=0 || y!=0 || width<0 || height<0 || width>4096 || height>4096 ||
			pact_at(r,t,"右上",0,0)!=width || pact_at(r,t,"右上",1,0)!=0 ||
			pact_at(r,t,"左下",0,0)!=0 || pact_at(r,t,"左下",1,0)!=height) {
			WARNING("Nonrectangular PACT hit region is not implemented");return false;
		}
		if(!PE_SetPartsRectangleDetectionSize(number,width,height,state)) return false;
	} else if(type==20) {
		struct string *flat=pact_to_game(pact_string(r,t,"フラット名"));
		// Blank PACT states are placeholders, like their empty CG counterparts.
		// Nonempty missing names must still fail instead of disappearing.
		if (!flat->size) {free_string(flat);return !r->error;}
		bool ok=PE_SetPartsFlat(number,flat,state);free_string(flat);
		if(!ok) {WARNING("PACT failed to load FLAT state %d",state);return false;}
		PE_SetFlatFPSRate(number,pact_num(r,t,"FPS",1),state);
		int frame=pact_num(r,t,"开始時シーク量",0);
		if(frame) PE_GoFramePartsFlat(number,frame,state);
	} else if (type == 11) {
		struct string *cg = pact_to_game(pact_string(r, t, "ＣＧ名"));
		if (!cg->size) {
			// An absent hover/down image inherits the preceding input state.
			// Calling SetSurfaceArea here would create an empty CG state.
			free_string(cg);
			return !r->error;
		}
		// Editor projects contain named CG references absent from all shipped
		// archives. Keep their part/name as an empty image so script bindings
		// survive. This is an explicit project policy, not native DLL evidence.
		// Existing-but-undecodable resources must still fail transactionally.
		bool missing=!asset_exists_by_name(ASSET_CG,cg->text,NULL);
		bool ok;
		if (missing) {
			struct string *empty=string_ref(&EMPTY_STRING);
			ok=PE_SetPartsCG(number,empty,pact_num(r,t,"変形",0),state);
			free_string(empty);
			if (ok) {
				struct parts_cg *slot=parts_get_cg(parts_get(number),state-1);
				if (slot->name) free_string(slot->name);
				slot->name=string_dup(cg);
				WARNING("PACT missing CG reference retained as empty: %s (part=%d state=%d)",cg->text,number,state);
			}
		} else {
			ok=PE_SetPartsCG(number,cg,pact_num(r,t,"変形",0),state);
		}
		free_string(cg);
		if (!ok)
			return false;
		PE_SetPartsCGSurfaceArea(number, pact_at(r,t,"サーフェイスエリア",0,0),
			pact_at(r,t,"サーフェイスエリア",1,0), pact_at(r,t,"サーフェイスエリア",2,0),
			pact_at(r,t,"サーフェイスエリア",3,0), state);
	} else if (type == 13) {
		struct ex_tree *font = pact_branch(t, "テキスト装飾");
		if (!font)
			font = t;
		float size = pact_num(r, font, "フォント尺寸", 24);
		if (size < 1 || size > 512)
			return false;
		PE_SetFont(number, pact_num(r,font,"フォントタイプ",0), size,
			pact_at(r,font,"フォント色",0,255), pact_at(r,font,"フォント色",1,255),
			pact_at(r,font,"フォント色",2,255), pact_num(r,font,"フォント太さ",0),
			pact_at(r,font,"フォント縁取り色",0,0), pact_at(r,font,"フォント縁取り色",1,0),
			pact_at(r,font,"フォント縁取り色",2,0), pact_num(r,font,"フォント縁取り",0), state);
		PE_SetTextCharSpace(number, pact_num(r,font,"字間隔",0), state);
		PE_SetTextLineSpace(number, pact_num(r,font,"行間隔",0), state);
		struct string *text = pact_to_game(pact_string(r, t, "テキスト"));
		bool ok = PE_SetText(number, text, state);
		free_string(text);
		if (!ok)
			return false;
		PE_SetPartsTextSurfaceArea(number, pact_at(r,t,"サーフェイスエリア",0,0),
			pact_at(r,t,"サーフェイスエリア",1,0), pact_at(r,t,"サーフェイスエリア",2,0),
			pact_at(r,t,"サーフェイスエリア",3,0), state);
	} else if (type==18) {
        if (!pact_cp_apply(r,number,t,state)) return false;
	} else if (type==14) {
		if (!pact_hgauge_apply(r, number, t, state))
			return false;
	} else if (type==16) {
		if (!pact_numeral_apply(r, number, t, state))
			return false;
	} else {
		return false;
	}
	return !r->error;
}

static bool pact_apply_part(struct pact_reader *r, struct ex_tree *t, int parent)
{
	int number = PE_GetFreeNumber();
	PE_SetPos(number, pact_at(r,t,"座標",0,0), pact_at(r,t,"座標",1,0));
	if(pact_num(r,t,"座標をレートとして認識する",0) &&
	   !parts_set_ratio_position(parts_get(number),true,pact_at(r,t,"座標",0,0),pact_at(r,t,"座標",1,0))) return false;
	struct string *name = pact_to_game(t->name->text);
	bool added = PE_AddActivityParts(r->activity, name, number);
	if (pact_num(r,t,"閉じている",0))
		PE_AddActivityCloseParts(r->activity, name);
	struct ex_value *destinations=pact_value(t,"遷移先");
	if (added && destinations) {
		struct string *empty=string_ref(&EMPTY_STRING);
		PE_SetActivityIntentData(r->activity,name,empty,pact_num(r,t,"遷移方法",0));
		free_string(empty);
		for (unsigned i=0;i<destinations->list->nr_items;i++) {
			struct string *dest=pact_to_game(destinations->list->items[i].value.s->text);
			PE_AddActivityIntentDataDestination(r->activity,name,dest);
			free_string(dest);
		}
	}
	free_string(name);
	if (!added) {
		PE_ReleaseParts(number);
		return false;
	}
	if (parent)
		PE_SetParentPartsNumber(number, parent);
	PE_SetZ(number, pact_at(r,t,"座標",2,1));
	bool hidden=t==r->compat_hidden;
	for(int i=0;i<3;i++)hidden|=t==r->compat_unused_route[i];
	PE_SetShow(number, hidden ? false : pact_num(r,t,"表示",1));
	int speed_up = pact_num(r,t,"スピードアップ有効",1);
	if (speed_up < 0 || speed_up > 1) {
		r->error = true;
		return false;
	}
	PE_SetEnableSpeedUp(number, speed_up);
	PE_SetAlpha(number, pact_num(r,t,"アルファ",255));
	PE_SetPartsOriginPosMode(number, pact_num(r,t,"原点座標モード",1));
	PE_SetPartsMagX(number, pact_at(r,t,"拡大縮小",0,1));
	PE_SetPartsMagY(number, pact_at(r,t,"拡大縮小",1,1));
	PE_SetPartsRotateZ(number, pact_at(r,t,"回転",2,0));
	PE_SetAddColor(number, pact_at(r,t,"加算色",0,0), pact_at(r,t,"加算色",1,0), pact_at(r,t,"加算色",2,0));
	PE_SetMultiplyColor(number, pact_at(r,t,"乗算色",0,255), pact_at(r,t,"乗算色",1,255), pact_at(r,t,"乗算色",2,255));
	PE_SetPartsDrawFilter(number, pact_num(r,t,"描画フィルタ",0));
	PE_SetPartsMessageWindowShowLink(number, pact_num(r,t,"メッセージウィンドウ表示連動",0));
	PE_SetClickable(number, pact_num(r,t,"クリック許可",0));
	PE_SetPassCursor(number,pact_num(r,t,"光标选中透過",0));
	PE_SetDrag(number,pact_num(r,t,"ドラッグ",0));
	struct parts *native=parts_get(number);
	native->wheelable=pact_num(r,t,"ホイール有效",0);
	if(native->comment) free_string(native->comment);
	native->comment=pact_to_game(pact_string(r,t,"批注"));
	const char *sounds[]={"オンカーソル効果音","クリック効果音"};
	for (int i=0;i<2;i++) {
		struct string *sound=pact_to_game(pact_string(r,t,sounds[i]));
		int id=-1;
		bool ok=!sound->size || asset_exists_by_name(ASSET_SOUND,sound->text,&id);
		free_string(sound);
		if (!ok) {
			/* A missing optional cursor/click cue must not discard the page.
			 * Some shipped PACTs reference cues absent from the sound archive. */
			WARNING("PACT missing optional sound (disabled): %s / %s",t->name->text,sounds[i]);
			id=-1;
		}
		if (i==0) parts_get(number)->on_cursor_sound=id;
		else parts_get(number)->on_click_sound=id;
	}
	PE_SetPartsPixelDecide(number, pact_num(r,t,"マウスカーソルピクセル判定",0));
	PE_SetSpeedupRateByMessageSkip(number, pact_num(r,t,"跳过倍率",3));
	PE_SetComponentMargin(number, pact_at(r,t,"マージン",0,0), pact_at(r,t,"マージン",1,0),
		pact_at(r,t,"マージン",2,0), pact_at(r,t,"マージン",3,0));
	struct ex_tree *component = pact_branch(t, "種類別情報");
	int type = pact_num(r, component, "パーツタイプ", -1);
	if(type==1000) {
		struct string *user=pact_to_game(pact_string(r,component,"用户组件名"));
		PE_SetUserComponentName(number,user);free_string(user);
		PE_SetLayoutBoxLayoutType(number,0);
		struct ex_list *data=pact_value(component,"数据")->list;
		for(unsigned i=0;i<data->nr_items;i+=2) {
			struct string *key=pact_to_game(data->items[i].value.s->text), *value=pact_to_game(data->items[i+1].value.s->text);
			PE_SetUserComponentData(number,key,value);free_string(key);free_string(value);
		}
    } else if(type==PARTS_WIDGET_SPINBOX) {
        if(!pact_spin_apply(r,number,component))return false;
    } else if(type==PARTS_WIDGET_FORM) {
        if(!pact_form_apply(r,number,component))return false;
    } else if(type==PARTS_WIDGET_LISTBOX) {
        if(!pact_listbox_apply(r,number,component))return false;
    } else if(type==5 || type==7) {
        struct parts *part=parts_get(number);parts_widget_create(part,type);
        struct parts_widget *w=part->widget;struct parts_textbox *m=w->textbox;
        struct string *skin=pact_to_game(pact_string(r,component,"ＣＧ名"));
        bool skin_ok=parts_widget_set_cg(part,skin);free_string(skin);if(!skin_ok)return false;
        w->width=pact_at(r,component,"尺寸",0,w->width);w->height=pact_at(r,component,"尺寸",1,w->height);
        w->enabled=pact_num(r,component,"有效状态",pact_num(r,component,"有効",1));
        parts_widget_font_set(&w->font,pact_num(r,component,"フォントタイプ",0),pact_num(r,component,"フォント尺寸",22),
            pact_at(r,component,"フォント色",0,0),pact_at(r,component,"フォント色",1,0),pact_at(r,component,"フォント色",2,0),
            pact_num(r,component,"フォント太さ",0),pact_at(r,component,"フォント縁取り色",0,0),
            pact_at(r,component,"フォント縁取り色",1,0),pact_at(r,component,"フォント縁取り色",2,0),pact_num(r,component,"フォント縁取り",0));
        w->char_space=pact_num(r,component,"文字間隔",0);w->line_space=pact_num(r,component,"行間隔",0);
        m->readonly=pact_num(r,component,"読み取り専用",0);
        for(int i=0;i<4;i++)m->padding[i]=pact_at(r,component,"パディング",i,4);
        SDL_Color *colors[]={&m->bg,&m->readonly_bg,&m->frame,&m->selection};
        const char *keys[]={"背景色","読み取り専用背景色","枠色","選択色"};
        const int defaults[]={255,150,100,200};
        for(int i=0;i<4;i++)*colors[i]=(SDL_Color){pact_at(r,component,keys[i],0,defaults[i]),
            pact_at(r,component,keys[i],1,defaults[i]),pact_at(r,component,keys[i],2,defaults[i]),255};
        if(!parts_textbox_set_limit(part,pact_num(r,component,"最大文字数",2000)))return false;
        struct string *text=pact_to_game(pact_string(r,component,"テキスト"));
        bool ok=parts_textbox_set_text(part,text);free_string(text);if(!ok)return false;
        m->scroll_x=pact_at(r,component,"スクロール位置",0,0);m->scroll_y=pact_at(r,component,"スクロール位置",1,0);
        m->show_h=pact_at(r,component,"スクロールバー表示",0,0);m->show_v=pact_at(r,component,"スクロールバー表示",1,0);
        m->keep_caret_visible=false;
        if(!parts_widget_build(part))return false;
    } else if(type==4) {
		struct parts *part=parts_get(number);parts_widget_create(part,PARTS_WIDGET_PANEL);
		part->clickable=pact_num(r,t,"クリック許可",0);
		struct parts_widget *w=part->widget;
		// Runtime compatibility policy supported by the supplied PC comparison:
		// these non-editor panels mark layout, so suppress only their painted
		// background. Their children, hit region, identity and RGBA remain intact.
		w->panel_layout_only=!r->edit_mode && !pact_num(r,t,"エディタ上表示",1);
		w->width=pact_at(r,component,"尺寸",0,1);w->height=pact_at(r,component,"尺寸",1,1);
		w->color=(SDL_Color){pact_at(r,component,"色",0,0),pact_at(r,component,"色",1,0),pact_at(r,component,"色",2,0),pact_at(r,component,"色",3,255)};
		if(!parts_widget_build(part)) return false;
	} else if ((type>=0 && type<=3) || type==9) {
        struct parts *part=parts_get(number);
        parts_widget_create(part,type);
        struct parts_widget *w=part->widget;
        if (type<2) {
            w->width=pact_at(r,component,"尺寸",0,80); w->height=pact_at(r,component,"尺寸",1,24);
            w->enabled=pact_num(r,component,"有效状态",pact_num(r,component,"有効",1));
            w->color=(SDL_Color){pact_at(r,component,"ボタンの色",0,200),pact_at(r,component,"ボタンの色",1,200),pact_at(r,component,"ボタンの色",2,200),255};
            parts_widget_font_set(&w->font,pact_num(r,component,"フォントタイプ",0),pact_num(r,component,"フォント尺寸",22),
                pact_at(r,component,"フォント色",0,0),pact_at(r,component,"フォント色",1,0),pact_at(r,component,"フォント色",2,0),
                pact_num(r,component,"フォント太さ",0),pact_at(r,component,"フォント縁取り色",0,0),
                pact_at(r,component,"フォント縁取り色",1,0),pact_at(r,component,"フォント縁取り色",2,0),pact_num(r,component,"フォント縁取り",0));
            free_string(w->text); w->text=pact_to_game(pact_string(r,component,"テキスト"));
            w->text_origin=pact_num(r,component,"テキスト位置",5);
            w->char_space=pact_num(r,component,"文字間隔",0); w->line_space=pact_num(r,component,"行間隔",0);
            w->checked=pact_num(r,component,"チェック状態",0); w->button_mode=pact_num(r,component,"ボタンモード",0);
        } else if (type==2 || type==3) {
            int length=pact_num(r,component,"長さ",100),breadth=pact_num(r,component,"幅",24);
            w->width=type==2?breadth:length; w->height=type==2?length:breadth;
            w->previous_size=pact_num(r,component,"前尺寸",0); w->next_size=pact_num(r,component,"次尺寸",0);
            w->total=max(0,(int)pact_num(r,component,"全体スクロール量",100));
            w->view=max(0,(int)pact_num(r,component,"表示量",1));
            w->pos=max(0,min(max(0,w->total-w->view),(int)pact_num(r,component,"スクロール位置",0)));
            w->step=max(1,(int)pact_num(r,component,"ボタンクリック移動量",1));
        }
        if(type==2 || type==3) {
            const char *roles[]={"バー","前","次","背景"};
            struct string *fo[4],*co[4];
            for(int i=0;i<4;i++) {
                char key[96];snprintf(key,sizeof(key),"上書き%sフラット名",roles[i]);fo[i]=pact_to_game(pact_string(r,component,key));
                snprintf(key,sizeof(key),"上書き%sＣＧ名",roles[i]);co[i]=pact_to_game(pact_string(r,component,key));
            }
            struct string *flat=pact_to_game(pact_string(r,component,"フラット名"));
            struct string *cg=pact_to_game(pact_string(r,component,"ＣＧ名"));
            bool ok=parts_widget_set_scroll_skin(part,flat,cg,fo,co);
            free_string(flat);free_string(cg);for(int i=0;i<4;i++){free_string(fo[i]);free_string(co[i]);}
            if(!ok || !parts_widget_build(part))return false;
        } else if (type!=9) {
			struct string *flat=pact_to_game(pact_string(r,component,"フラット名"));
			if(flat->size) {
				bool ok=parts_widget_set_flat(part,flat);free_string(flat);if(!ok) {WARNING("PACT FLAT button failed: %s",t->name->text);return false;}
			} else {
			free_string(flat);
            struct string *cg=pact_to_game(pact_string(r,component,"ＣＧ名"));
            bool ok=parts_widget_set_cg(part,cg); free_string(cg);
            if (!ok || !parts_widget_build(part)) return false;
			}
        }
	} else if (type == 8) {
		PE_SetLayoutBoxLayoutType(number, pact_num(r,component,"レイアウトタイプ",0));
		PE_SetLayoutBoxReturn(number, pact_num(r,component,"折り返し許可",0), pact_num(r,component,"折り返し尺寸",0));
		PE_SetLayoutBoxAlign(number, pact_num(r,component,"配置",1));
		PE_set_layoutbox_padding(number, pact_at(r,component,"パディング",0,0), pact_at(r,component,"パディング",1,0),
			pact_at(r,component,"パディング",2,0), pact_at(r,component,"パディング",3,0));
	} else if (type == 10) {
		const char *states[] = { "通常状態", "オンカーソル状態", "キーダウン状態" };
		for (int i = 0; i < 3; i++)
			if (!pact_apply_state(r, number, pact_branch(component, states[i]), i + 1))
				return false;
	} else if (!pact_apply_state(r, number, component, 1)) {
		return false;
	}
	struct ex_tree *children = pact_branch(t, "子パーツ");
	if (children)
		for (unsigned i = 0; i < children->nr_children; i++)
			if (!pact_apply_part(r, &children->children[i], number))
				{WARNING("PACT child failed: %s / %s",t->name->text,children->children[i].name->text);return false;}
	return !r->error;
}

static int pact_find_part(struct pact_reader *r,const char *name)
{
    struct string *encoded=pact_to_game(name);
    int number=PE_GetActivityPartsNumber(r->activity,encoded);
    free_string(encoded); return number;
}
static bool pact_bind_links(struct pact_reader *r,struct ex_tree *t)
{
    int number=pact_find_part(r,t->name->text);
    if (!number) {WARNING("PACT link owner missing: %s",t->name->text);return false;}
    const char *keys[]={"アルファクリッパー","オンカーソル表示連動","チェックボックス表示連動"};
    for (int i=0;i<3;i++) {
        const char *ref=pact_string(r,t,keys[i]);
        if (!*ref) continue;
        int target=pact_find_part(r,ref);
        if (!target) {WARNING("PACT link target missing: %s / %s -> %s",t->name->text,keys[i],ref);return false;}
        if (i==0) PE_SetPartsAlphaClipperPartsNumber(number,target);
        if (i==1) PE_SetOnCursorShowLinkPartsNumber(number,target);
        if (i==2) {
            parts_get(number)->checkbox_show_link=target;
        }
    }
    const char *scroll_keys[]={"スクロールＸ座標連動","スクロールＹ座標連動","スクロールアルファ連動"};
    for(int i=0;i<3;i++) {
        const char *ref=pact_string(r,t,scroll_keys[i]);if(!*ref)continue;
        int target=pact_find_part(r,ref);if(!target){WARNING("PACT scroll link target missing: %s -> %s",t->name->text,ref);return false;}
        parts_set_scroll_link(parts_get(number),i,target);
    }
    struct ex_tree *component=pact_branch(t,"種類別情報");
    struct parts *owner=parts_get(number);
    if(owner->widget && (owner->widget->type==2 || owner->widget->type==3)) {
        const char *size_keys[]={"全体スクロール量サイズ連動","表示量サイズ連動"};
        for(int i=0;i<2;i++) {
            const char *ref=pact_string(r,component,size_keys[i]);if(!*ref)continue;
            int target=pact_find_part(r,ref);if(!target){WARNING("PACT scroll size link target missing: %s -> %s",t->name->text,ref);return false;}
            parts_widget_set_size_link(owner,i,target);
        }
    }
    if ((int)pact_num(r,component,"パーツタイプ",-1)==9) {
        struct ex_value *refs=pact_value(component,"チェックボックスリスト");
        /* An omitted list is an initially empty, runtime-populated group. */
        if (refs && refs->type!=EX_LIST) {WARNING("PACT radio checkbox list malformed: %s",t->name->text);return false;}
        for (unsigned i=0;refs && i<refs->list->nr_items;i++) {
            struct ex_value *ref=&refs->list->items[i].value;
            if (ref->type!=EX_STRING) return false;
            int target=pact_find_part(r,ref->s->text);
            if (!target || !parts_get(target)->widget || parts_get(target)->widget->type!=1) {WARNING("PACT radio target missing or wrong type: %s -> %s",t->name->text,ref->s->text);return false;}
            parts_widget_radio_add(parts_get(number),target);
        }
    }
    struct ex_tree *children=pact_branch(t,"子パーツ");
    if (children) for (unsigned i=0;i<children->nr_children;i++)
        if (!pact_bind_links(r,&children->children[i])) return false;
    return !r->error;
}

static struct ex *pact_read_file(struct string *filename)
{
	struct archive_data *asset = asset_get_by_name(ASSET_PACT, filename->text, NULL);
	if (asset) {
		struct ex *ex = NULL;
		if (archive_load_file(asset) && asset->size >= 4 && !memcmp(asset->data, "HEAD", 4))
			ex = ex_read_conv(asset->data, asset->size, pact_to_utf8);
		archive_free_data(asset);
		return ex;
	}
	const char *extensions[] = { "", ".pactex", ".pact" };
	for (unsigned i = 0; i < sizeof(extensions) / sizeof(*extensions); i++) {
		if (i && file_extension(filename->text))
			break;
		struct string *candidate = string_dup(filename);
		string_append_cstr(&candidate, extensions[i], strlen(extensions[i]));
		char *path = gamedir_path(candidate->text);
		free_string(candidate);
		size_t size;
		uint8_t *data = file_read(path, &size);
		free(path);
		if (!data)
			continue;
		struct ex *ex = size >= 4 && !memcmp(data, "HEAD", 4) ? ex_read_conv(data, size, pact_to_utf8) : NULL;
		free(data);
		return ex;
	}
	return NULL;
}

struct pact_perf_sample {
	Uint64 started, read_end, validated, built, updated, release_ticks;
};

static struct perf_trace_budget pact_perf_budget;

static void pact_perf_report(const struct pact_perf_sample *p, Uint64 ended,
		unsigned nodes_checked, bool ok)
{
	if (!perf_trace_claim(&pact_perf_budget, p->started, ended))
		return;
	double total = perf_trace_elapsed_ms(p->started, ended);
	double read = perf_trace_elapsed_ms(p->started, p->read_end);
	double validate = perf_trace_elapsed_ms(p->read_end, p->validated);
	double build = perf_trace_elapsed_ms(p->validated, p->built);
	double update = perf_trace_elapsed_ms(p->built, p->updated);
	double release = perf_trace_ms(p->release_ticks);
	double other = total - read - validate - build - update - release;
	sys_message("[perf][pact] ok=%d nodes_checked=%u total_ms=%.3f "
			"read_ms=%.3f validate_ms=%.3f build_ms=%.3f update_ms=%.3f "
			"release_ms=%.3f other_ms=%.3f\n", ok, nodes_checked, total,
			read, validate, build, update, release, other > 0.0 ? other : 0.0);
}

bool PE_ReadActivityFileEx(struct string *name, struct string *filename, bool edit)
{
	if (getenv("XSYSTEM4_TRACE_INPUT"))
		sys_message("[activity] read-attempt %s <- %s%s\n", (name && name->text) ? name->text : "(null)",
			(filename && filename->text) ? filename->text : "(null)",
			(name && PE_IsExistActivity(name)) ? " (rejected: exists)" : "");
	if (!name || !name->size || !filename || !filename->size || PE_IsExistActivity(name))
		return false;
	struct pact_perf_sample perf = { .started = perf_trace_begin(&pact_perf_budget) };
	struct ex *ex = pact_read_file(filename);
	perf.read_end = perf_trace_stamp(perf.started);
	if (!ex) {
		pact_perf_report(&perf, perf.read_end, 0, false);
		return false;
	}
	struct pact_reader r = { .activity = name, .edit_mode = edit };
	struct ex_tree *tree = ex_get_tree(ex, "アクティビティ");
	struct ex_tree *root = pact_branch(tree, "ルートパーツ");
	r.compat_hidden = edit ? NULL : pact_compat_new_backup(filename, root);
	if(!edit)pact_compat_unused_route(filename,root,r.compat_unused_route);
	struct ex_list *keys = NULL;
	bool version=pact_supported_version(ex);
	bool metadata=pact_metadata_endkeys(ex,&keys);
	bool ok = version && metadata
		&& pact_validate_part(&r, root, 0);
	perf.validated = perf_trace_stamp(perf.started);
	perf.built = perf.updated = perf.validated;
	if (ok) {
		ok = PE_CreateActivity(name);
		if(ok && !(ok=pact_apply_part(&r,root,0))) WARNING("PACT apply failed (parse_error=%d)",r.error);
		if(ok && !(ok=pact_bind_links(&r,root))) WARNING("PACT link binding failed (parse_error=%d)",r.error);
		perf.built = perf_trace_stamp(perf.started);
		if (ok)
			PE_UpdateComponent(0);
		perf.updated = perf_trace_stamp(perf.started);
		if (ok && keys)
			for (unsigned i = 0; i < keys->nr_items; i++)
				PE_SetActivityEndKey(name, keys->items[i].value.i);
		if (!ok) {
			struct page *delegates = NULL;
			Uint64 releasing = perf_trace_stamp(perf.started);
			PE_ReleaseActivity(name, &delegates);
			perf.release_ticks += perf_trace_stamp(perf.started) - releasing;
		}
	}
	if(ok && getenv("XSYSTEM4_TRACE_INPUT"))
		sys_message("[activity] load %s\n", filename->text);
	if(!ok) WARNING("PACT load failed: %s (version=%d parts=%d)",filename->text,ex_get_int(ex,"版本",-1),ex_get_int(ex,"パーツ版本",-1));
	// Explicit probe-only event: exercise the game's refusal callback inside
	// this process. No desktop input or network request is generated.
	if(ok && getenv("XSYSTEM4_TEST_REJECT_NETWORK") && strstr(filename->text,"NetworkConfirmation")) {
		struct string *cancel=cstr_to_string("CancelButton");
		int number=PE_GetActivityPartsNumber(name,cancel);free_string(cancel);
		if(number<=0) VM_ERROR("Probe could not find network refusal button");
		parts_probe_schedule_refusal(number);
		fprintf(stderr,"[probe] scheduled CancelButton refusal event: %d\n",number);
	}
	Uint64 releasing = perf_trace_stamp(perf.started);
	ex_free(ex);
	Uint64 ended = perf_trace_stamp(perf.started);
	perf.release_ticks += ended - releasing;
	pact_perf_report(&perf, ended, r.nodes, ok);
	return ok;
}

bool PE_ReadActivityFile(struct string *name, struct string *filename)
{
	return PE_ReadActivityFileEx(name, filename, false);
}
