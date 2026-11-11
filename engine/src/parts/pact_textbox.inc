/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PACT_TEXTBOX_INC
#define PACT_TEXTBOX_INC
/* PACT textbox schema validation. Validation only; does not mutate the tree. */
static bool pact_tb_validate(struct pact_reader *r, struct ex_tree *t)
{
    if (!r || !t)
        return false;

    if (strlen(pact_string(r, t, "テキスト")) > 262144)
        return false;
    if (strlen(pact_string(r, t, "ＣＧ名")) > 4096)
        return false;

    if (!pact_mw_range(r, t, "フォントタイプ", 0, 0, 65535, true) ||
        !pact_mw_range(r, t, "フォント尺寸", 22, 1, 512, true) ||
        !pact_mw_range(r, t, "フォント太さ", 0, 0, 16, false) ||
        !pact_mw_range(r, t, "フォント縁取り", 0, 0, 16, false) ||
        !pact_mw_range(r, t, "読み取り専用", 0, 0, 1, true) ||
        !pact_mw_range(r, t, "最大文字数", 2000, 0, 65536, true) ||
        !pact_mw_range(r, t, "文字間隔", 0, -512, 512, true) ||
        !pact_mw_range(r, t, "行間隔", 0, -512, 512, true))
        return false;

    /* Size is intentionally checked with its own lower bound of 1, not the
     * generic list lower bound of 0. Missing lists retain schema defaults. */
    if (!pact_mw_list(r, t, "尺寸", 2, 1, 4096) ||
        !pact_mw_list(r, t, "パディング", 4, 0, 4096) ||
        !pact_mw_list(r, t, "背景色", 3, 0, 255) ||
        !pact_mw_list(r, t, "読み取り専用背景色", 3, 0, 255) ||
        !pact_mw_list(r, t, "枠色", 3, 0, 255) ||
        !pact_mw_list(r, t, "選択色", 3, 0, 255) ||
        !pact_mw_list(r, t, "フォント色", 3, 0, 255) ||
        !pact_mw_list(r, t, "フォント縁取り色", 3, 0, 255) ||
        !pact_mw_list(r, t, "スクロール位置", 2, 0, 10000000) ||
        !pact_mw_list(r, t, "スクロールバー表示", 2, 0, 1))
        return false;

    return !r->error;
}
#endif
