/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Legacy PACT metadata helpers. Caller includes after pact_branch/pact_value. */

static bool pact_metadata_list_valid(const struct ex_value *value)
{
	if (!value || value->type != EX_LIST || !value->list)
		return false;
	struct ex_list *list = value->list;
	if (list->nr_items && !list->items)
		return false;
	for (uint32_t i = 0; i < list->nr_items; i++)
		if (list->items[i].value.type != EX_INT || list->items[i].value.i < 0)
			return false;
	return true;
}

static bool pact_metadata_endkeys(struct ex *ex, struct ex_list **out)
{
	if (out)
		*out = NULL;
	if (!ex || !out)
		return false;
	struct ex_value *jp = ex_get(ex, "終了キー");
	struct ex_value *cn = ex_get(ex, "结束キー");
	if (!jp && !cn)
		return true;
	if (jp && !pact_metadata_list_valid(jp))
		return false;
	if (cn && !pact_metadata_list_valid(cn))
		return false;
	if (jp && cn) {
		struct ex_list *a = jp->list, *b = cn->list;
		if (a->nr_items != b->nr_items)
			return false;
		for (uint32_t i = 0; i < a->nr_items; i++)
			if (a->items[i].value.i != b->items[i].value.i)
				return false;
	}
	*out = jp ? jp->list : cn->list;
	return true;
}

static bool pact_validate_legacy_metadata(struct ex_tree *t, unsigned depth,
		bool check_rate, bool check_address)
{
	if (!t || t->is_leaf || depth > 64)
		return false;
	const char *flags[] = {
		"时间初始化許可／一般", "时间初始化許可／オン", "时间初始化許可／DOWN"
	};
	for (unsigned i = 0; i < 3; i++) {
		struct ex_value *v = pact_value(t, flags[i]);
		if (!v || v->type != EX_INT || v->i != 1)
			return false;
	}
	struct ex_value *suspend = pact_value(t, "テクスチャサスペンド");
	if (suspend && (suspend->type != EX_INT || suspend->i != 0))
		return false;
	if (check_rate) {
		struct ex_value *rate = pact_value(t, "快进倍率");
		if (rate && (rate->type != EX_INT || rate->i != 3))
			return false;
	}
	if (check_address) {
		struct ex_value *address = pact_value(t, "テクスチャアドレッシング");
		if (address && (address->type != EX_INT || address->i != 0))
			return false;
	}
	struct ex_tree *children = pact_branch(t, "子パーツ");
	for (unsigned i = 0; children && i < children->nr_children; i++)
		if (!pact_validate_legacy_metadata(&children->children[i], depth + 1,
				check_rate, check_address))
			return false;
	return true;
}


/* PACT header 4 / parts 12 uses the same explicit message/window fields as
 * the supported loader. AUTO/skip marks are not implemented by this engine;
 * admit their inert old-editor records only, never discard effective data. */
static bool pact_parts12_inert_mark(struct ex_tree *mark)
{
	if (!mark || mark->is_leaf || (mark->nr_children && !mark->children)) return false;
	const char *keys[] = { "ＣＧ名", "フラット名", "循环ＣＧ开始编号", "循环ＣＧ张数", "循环ＣＧ切り替え时间", "座標" };
	unsigned seen = 0;
	for (unsigned j = 0; j < mark->nr_children; j++) {
		struct ex_tree *field = &mark->children[j];
		if (!field->is_leaf || !field->name) return false;
		unsigned key;
		for (key = 0; key < 6 && strcmp(field->name->text, keys[key]); key++) {}
		if (key == 6 || (seen & (1u << key))) return false;
		seen |= 1u << key;
		struct ex_value *v = &field->leaf.value;
		if (key < 2) {
			if (v->type != EX_STRING || !v->s || v->s->size) return false;
		} else if (key < 5) {
			if (v->type != EX_INT || v->i) return false;
		} else {
			if (v->type != EX_LIST || !v->list || v->list->nr_items != 3 || !v->list->items) return false;
			for (unsigned i = 0; i < 3; i++) {
				struct ex_value *coordinate = &v->list->items[i].value;
				if (!((coordinate->type == EX_INT && coordinate->i == 0) ||
				      (coordinate->type == EX_FLOAT && coordinate->f == 0.f))) return false;
			}
		}
	}
	return true;
}

static bool pact_validate_parts12_marks(struct ex_tree *t, unsigned depth)
{
	if (!t || t->is_leaf || depth > 64) return false;
	struct ex_tree *component = pact_branch(t, "種類別情報");
	const char *keys[] = { "AUTO模式标记", "快进模式标记" };
	for (unsigned i = 0; component && i < 2; i++) {
		struct ex_tree *mark = pact_child(component, keys[i]);
		if (mark && !pact_parts12_inert_mark(mark)) return false;
	}
	struct ex_tree *children = pact_branch(t, "子パーツ");
	for (unsigned i = 0; children && i < children->nr_children; i++)
		if (!pact_validate_parts12_marks(&children->children[i], depth + 1)) return false;
	return true;
}
