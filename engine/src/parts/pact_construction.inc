/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "system4/cg.h"

static bool pact_cp_int_value(struct pact_reader *r, struct ex_value *v,
        int lo, int hi, int *out)
{
    if (!v) return false;
    if (v->type == EX_INT) {
        if (v->i < lo || v->i > hi) { r->error = true; return false; }
        *out = v->i;
        return true;
    }
    if (v->type == EX_FLOAT && isfinite(v->f) && v->f == truncf(v->f) &&
            v->f >= (float)lo && v->f <= (float)hi) {
        *out = (int)v->f;
        return true;
    }
    r->error = true;
    return false;
}

static bool pact_cp_int(struct pact_reader *r, struct ex_tree *t, const char *key,
        int lo, int hi, int *out)
{
    struct ex_value *v = pact_value(t, key);
    if (!v) { r->error = true; return false; }
    return pact_cp_int_value(r, v, lo, hi, out);
}

static bool pact_cp_rect(struct pact_reader *r, struct ex_tree *t, const char *key,
        bool required, bool zero_wh, int out[6])
{
    struct ex_value *v = pact_value(t, key);
    if (!v) return !required;
    if (v->type != EX_LIST || !v->list || !v->list->items || v->list->nr_items != 6) {
        r->error = true; return false;
    }
    for (int i = 0; i < 6; i++) {
        int lo = i < 4 ? -4096 : 0;
        int hi = 4096;
        if (!pact_cp_int_value(r, &v->list->items[i].value, lo, hi, &out[i]))
            return false;
    }
    if (!zero_wh && (out[4] < 1 || out[5] < 1)) { r->error = true; return false; }
    return true;
}

static bool pact_cp_source(struct pact_reader *r, struct ex_tree *t, bool required,
        int out[4])
{
    struct ex_value *v = pact_value(t, "原矩形");
    if (!v) return !required;
    if (v->type != EX_LIST || !v->list || !v->list->items || v->list->nr_items != 4) {
        r->error = true; return false;
    }
    for (int i = 0; i < 4; i++)
        if (!pact_cp_int_value(r, &v->list->items[i].value, 0, 4096, &out[i]))
            return false;
    return true;
}

static bool pact_cp_color(struct pact_reader *r, struct ex_tree *t, bool require_alpha,
        int out[4])
{
    struct ex_value *v = pact_value(t, "色１");
    if (!v || v->type != EX_LIST || !v->list || !v->list->items ||
            (v->list->nr_items != 3 && v->list->nr_items != 4) ||
            (require_alpha && v->list->nr_items != 4)) {
        r->error = true; return false;
    }
    int count = v->list->nr_items;
    for (int i = 0; i < count; i++)
        if (!pact_cp_int_value(r, &v->list->items[i].value, 0, 255, &out[i]))
            return false;
    if (count == 3) out[3] = 255;
    return true;
}

static bool pact_cp_cg(struct pact_reader *r, struct ex_tree *t)
{
    struct ex_value *v = pact_value(t, "ＣＧ名");
    if (!v || v->type != EX_STRING || !v->s || v->s->size <= 0) {
        r->error = true; return false;
    }
    return true;
}

static bool pact_cp_interp(struct pact_reader *r, struct ex_tree *t, int *out)
{
    struct ex_value *v = pact_value(t, "补间类型");
    if (!v) { *out = 0; return true; }
    return pact_cp_int_value(r, v, 0, 1, out);
}

static bool pact_cp_gradient_alpha(struct pact_reader *r, struct ex_tree *t,
        const char *key, int *alpha)
{
    struct ex_value *v = pact_value(t, key);
    if (!v || v->type != EX_LIST || !v->list || !v->list->items || v->list->nr_items != 4) {
        r->error = true; return false;
    }
    int rgba[4];
    for (int i = 0; i < 4; i++)
        if (!pact_cp_int_value(r, &v->list->items[i].value, 0, 255, &rgba[i]))
            return false;
    *alpha = rgba[3];
    return true;
}

static bool pact_cp_optional_zero(struct pact_reader *r, struct ex_tree *t, const char *key)
{
    struct ex_value *v = pact_value(t, key);
    int zero;
    return !v || pact_cp_int_value(r, v, 0, 0, &zero);
}

static bool pact_cp_validate(struct pact_reader *r, struct ex_tree *t)
{
    struct ex_tree *steps = pact_branch(t, "手順リスト");
    if (!steps || !steps->children || steps->nr_children < 1 || steps->nr_children > 4096)
        return false;
    for (unsigned i = 0; i < steps->nr_children; i++) {
        struct ex_tree *step = &steps->children[i];
        if (step->is_leaf || !step->children) return false;
        int cmd, d[6], s[4], col[4], interp;
        if (!pact_cp_int(r, step, "コマンド", 0, 25, &cmd))
            return false;
        if (cmd != 0 && cmd != 1 && cmd != 2 && cmd != 3 && cmd != 4 &&
                cmd != 6 && cmd != 7 && cmd != 8 && cmd != 11 && cmd != 19 && cmd != 25) return false;
        if (cmd == 7 || cmd == 8) {
            struct ex_value *text=pact_value(step,"テキスト");
            if(!text || text->type!=EX_STRING || !text->s || text->s->size>262144 ||
                !pact_cp_rect(r,step,"先矩形",true,true,d) ||
                !pact_mw_range(r,step,"フォントタイプ",0,0,65535,true) ||
                !pact_mw_range(r,step,"フォント尺寸",11,1,512,true) ||
                !pact_mw_range(r,step,"フォント太さ",0,0,16,false) ||
                !pact_mw_range(r,step,"フォント縁取り",0,0,16,false) ||
                !pact_mw_range(r,step,"文字間隔",0,-512,512,true) ||
                !pact_mw_range(r,step,"行間隔",0,-512,512,true) ||
                !pact_mw_list(r,step,"フォント色",3,0,255) ||
                !pact_mw_list(r,step,"フォント縁取り色",3,0,255)) return false;
        } else if (cmd != 2) {
            if (!pact_cp_rect(r, step, "先矩形", true, false, d))
                return false;
        } else if (!pact_cp_rect(r, step, "先矩形", false, true, d)) {
            return false;
        }
        if (cmd == 2 || cmd == 11 || cmd == 19) {
            if (!pact_cp_cg(r, step)) return false;
        }
        if (cmd == 11 || cmd == 19) {
            if (!pact_cp_source(r, step, true, s)) return false;
        } else if (cmd == 2 && !pact_cp_source(r, step, false, s)) {
            return false;
        }
        if (cmd == 3 && !pact_cp_color(r, step, false, col)) return false;
        if ((cmd == 4 || cmd == 6) && !pact_cp_color(r, step, true, col))
            return false;
        if (cmd == 25) {
            int up, down;
            if (!pact_cp_gradient_alpha(r, step, "色１", &up) ||
                    !pact_cp_gradient_alpha(r, step, "色２", &down) ||
                    !pact_cp_optional_zero(r, step, "全体") ||
                    !pact_cp_optional_zero(r, step, "ブラー")) return false;
        }
        if (!pact_cp_interp(r, step, &interp)) return false;
        (void)d; (void)s; (void)col; (void)interp;
    }
    return !r->error;
}

static bool pact_cp_add_cg(struct pact_reader *r, int number, struct ex_tree *step,
        int state, int mode)
{
    struct string *name = pact_to_game(pact_string(r, step, "ＣＧ名"));
    bool ok = false;
    if (!name) return false;
    if (name->size <= 0) { free_string(name); return false; }
    if (mode == 2) {
        struct cg_metrics m;
        ok = asset_cg_get_metrics_by_name(name->text, &m) &&
            m.w >= 1 && m.w <= 4096 && m.h >= 1 && m.h <= 4096 &&
            PE_AddCreateCGToProcess(number, name, state);
    } else {
        int dx = pact_at(r, step, "先矩形", 0, 0);
        int dy = pact_at(r, step, "先矩形", 1, 0);
        int dw = pact_at(r, step, "先矩形", 4, 1);
        int dh = pact_at(r, step, "先矩形", 5, 1);
        int sx = pact_at(r, step, "原矩形", 0, 0);
        int sy = pact_at(r, step, "原矩形", 1, 0);
        int interp = pact_num(r, step, "补间类型", 0);
        if (mode == 11)
            ok = PE_AddDrawCutCGToPartsConstructionProcess(number, name, dx, dy, dw, dh,
                sx, sy, dw, dh, interp, state);
        else
            ok = PE_AddAlphaBlendCutCGToPartsConstructionProcess(number, name, dx, dy, dw, dh,
                sx, sy, dw, dh, interp, state);
    }
    free_string(name);
    return ok;
}

static bool pact_cp_apply(struct pact_reader *r, int number, struct ex_tree *t, int state)
{
    if (!pact_cp_validate(r, t)) return false;
    struct ex_tree *steps = pact_branch(t, "手順リスト");
    if (!PE_ClearPartsConstructionProcess(number, state)) return false;
    for (unsigned i = 0; i < steps->nr_children; i++) {
        struct ex_tree *step = &steps->children[i];
        int cmd = pact_num(r, step, "コマンド", -1);
        bool ok = false;
        if (cmd == 0 || cmd == 1) {
            int w = pact_at(r, step, "先矩形", 4, 0);
            int h = pact_at(r, step, "先矩形", 5, 0);
            ok = cmd == 0 ? PE_AddCreateToPartsConstructionProcess(number, w, h, state) :
                PE_AddCreatePixelOnlyToPartsConstructionProcess(number, w, h, state);
        } else if (cmd == 2 || cmd == 11 || cmd == 19) {
            ok = pact_cp_add_cg(r, number, step, state, cmd);
        } else if (cmd == 7 || cmd == 8) {
            struct string *text=pact_to_game(pact_string(r,step,"テキスト"));
            ok=(cmd==7?PE_AddDrawTextToPartsConstructionProcess:PE_AddCopyTextToPartsConstructionProcess)(number,
                pact_at(r,step,"先矩形",0,0),pact_at(r,step,"先矩形",1,0),text,
                pact_num(r,step,"フォントタイプ",0),pact_num(r,step,"フォント尺寸",11),
                pact_at(r,step,"フォント色",0,255),pact_at(r,step,"フォント色",1,255),pact_at(r,step,"フォント色",2,255),
                pact_num(r,step,"フォント太さ",0),
                pact_at(r,step,"フォント縁取り色",0,0),pact_at(r,step,"フォント縁取り色",1,0),pact_at(r,step,"フォント縁取り色",2,0),
                pact_num(r,step,"フォント縁取り",0),pact_num(r,step,"文字間隔",0),pact_num(r,step,"行間隔",0),state);
            free_string(text);
        } else if (cmd == 3) {
            ok = PE_AddFillToPartsConstructionProcess(number,
                pact_at(r, step, "先矩形", 0, 0), pact_at(r, step, "先矩形", 1, 0),
                pact_at(r, step, "先矩形", 4, 0), pact_at(r, step, "先矩形", 5, 0),
                pact_at(r, step, "色１", 0, 0), pact_at(r, step, "色１", 1, 0),
                pact_at(r, step, "色１", 2, 0), state);
        } else if (cmd == 4) {
            ok = PE_AddFillAlphaColorToPartsConstructionProcess(number,
                pact_at(r, step, "先矩形", 0, 0), pact_at(r, step, "先矩形", 1, 0),
                pact_at(r, step, "先矩形", 4, 0), pact_at(r, step, "先矩形", 5, 0),
                pact_at(r, step, "色１", 0, 0), pact_at(r, step, "色１", 1, 0),
                pact_at(r, step, "色１", 2, 0), pact_at(r, step, "色１", 3, 255), state);
        } else if (cmd == 6) {
            ok = PE_AddFillWithAlphaToPartsConstructionProcess(number,
                pact_at(r, step, "先矩形", 0, 0), pact_at(r, step, "先矩形", 1, 0),
                pact_at(r, step, "先矩形", 4, 0), pact_at(r, step, "先矩形", 5, 0),
                pact_at(r, step, "色１", 0, 0), pact_at(r, step, "色１", 1, 0),
                pact_at(r, step, "色１", 2, 0), pact_at(r, step, "色１", 3, 255), state);
        }
        if (cmd == 25) {
            ok = PE_AddMultiplyAMapGradationUDToPartsConstructionProcess(number,
                pact_at(r, step, "先矩形", 0, 0), pact_at(r, step, "先矩形", 1, 0),
                pact_at(r, step, "先矩形", 4, 0), pact_at(r, step, "先矩形", 5, 0),
                pact_at(r, step, "色１", 3, 0), pact_at(r, step, "色２", 3, 0), state);
        }
        if (!ok) return false;
    }
    return PE_BuildPartsConstructionProcess(number, state) && !r->error;
}
