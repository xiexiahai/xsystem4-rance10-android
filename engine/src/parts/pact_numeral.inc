/* PACT numeral (type 16): linked-CG mode 1 and text-font mode 2. Full-width
 * numerals and disabling zero padding remain outside this bounded subset. */
struct pact_numeral_data {
	struct string *cg_name;
	int32_t type;
	int32_t value;
	int32_t comma;
	int32_t space;
	int32_t digits;
	int32_t zero_padding;
	int32_t zenkaku;
	int32_t width[12];
	int32_t area[4];
	struct text_style style;
};

static bool pact_numeral_reject(struct pact_reader *r)
{
	r->error = true;
	return false;
}

static bool pact_numeral_int(struct pact_reader *r, struct ex_tree *tree,
		const char *key, int32_t min, int32_t max, int32_t *out)
{
	struct ex_value *v = pact_value(tree, key);
	if (!v || v->type != EX_INT || v->i < min || v->i > max)
		return pact_numeral_reject(r);
	*out = v->i;
	return true;
}

static bool pact_numeral_cg(struct pact_reader *r, struct ex_tree *tree,
		struct string **out, bool allow_empty)
{
	struct ex_value *v = pact_value(tree, "ＣＧ名");
	if (!v || v->type != EX_STRING || !v->s || (!allow_empty && !v->s->size))
		return pact_numeral_reject(r);
	*out = v->s;
	return true;
}

static bool pact_numeral_int_list(struct pact_reader *r, struct ex_tree *tree,
		const char *key, unsigned count, int32_t min, int32_t max,
		int32_t *out)
{
	struct ex_value *v = pact_value(tree, key);
	if (!v || v->type != EX_LIST || !v->list || v->list->nr_items != count)
		return pact_numeral_reject(r);
	for (unsigned i = 0; i < count; i++) {
		struct ex_value *item = &v->list->items[i].value;
		if (item->type != EX_INT || item->i < min || item->i > max)
			return pact_numeral_reject(r);
		out[i] = item->i;
	}
	return true;
}

static bool pact_numeral_float(struct pact_reader *r, struct ex_tree *tree,
		const char *key, float *out)
{
	struct ex_value *v = pact_value(tree, key);
	if (!v || (v->type != EX_INT && v->type != EX_FLOAT)) return pact_numeral_reject(r);
	float n = v->type == EX_INT ? v->i : v->f;
	if (!isfinite(n) || n < 0 || n > 16) return pact_numeral_reject(r);
	*out = n;
	return true;
}

static bool pact_numeral_style(struct pact_reader *r, struct ex_tree *tree,
		struct text_style *out)
{
	int32_t face, size, color[3], edge_color[3];
	float bold, edge;
	if (!pact_numeral_int(r,tree,"フォントタイプ",0,65535,&face)
		|| !pact_numeral_int(r,tree,"フォント尺寸",1,512,&size)
		|| !pact_numeral_int_list(r,tree,"フォント色",3,0,255,color)
		|| !pact_numeral_int_list(r,tree,"フォント縁取り色",3,0,255,edge_color)
		|| !pact_numeral_float(r,tree,"フォント太さ",&bold)
		|| !pact_numeral_float(r,tree,"フォント縁取り",&edge)) return false;
	*out = (struct text_style){.face=face,.size=size,.bold_width=bold,.weight=(unsigned)(bold*1000),
		.color={color[0],color[1],color[2],255},.edge_color={edge_color[0],edge_color[1],edge_color[2],255},
		.scale_x=1,.space_scale_x=1};
	text_style_set_edge_width(out,edge);
	return true;
}

static bool pact_numeral_read(struct pact_reader *r, struct ex_tree *tree,
		struct pact_numeral_data *out)
{
	memset(out, 0, sizeof(*out));
	if (!pact_numeral_int(r, tree, "表示タイプ", 1, 2, &out->type))
		return false;
	if (!pact_numeral_cg(r, tree, &out->cg_name, out->type == 2))
		return false;
	if (!pact_numeral_int_list(r, tree, "幅リスト", 12, 0, 4096, out->width))
		return false;

	int64_t width_sum = 0;
	for (int i = 0; i < 12; i++) {
		if (out->type == 1 && i < 10 && out->width[i] <= 0)
			return pact_numeral_reject(r);
		width_sum += out->width[i];
	}
	if (out->type == 1 && width_sum > 4096)
		return pact_numeral_reject(r);
	if (out->type == 2 && !pact_numeral_style(r,tree,&out->style)) return false;

	if (!pact_numeral_int(r, tree, "数値", INT32_MIN, INT32_MAX, &out->value))
		return false;
	if (!pact_numeral_int(r, tree, "コンマ表示", 0, 1, &out->comma))
		return false;
	if (!pact_numeral_int(r, tree, "字間隔", -4096, 4096, &out->space))
		return false;
	if (!pact_numeral_int(r, tree, "桁数", 0, 24, &out->digits))
		return false;
	if (!pact_numeral_int(r, tree, "ゼロパディング", 1, 1, &out->zero_padding))
		return false;
	if (!pact_numeral_int(r, tree, "全角", 0, 0, &out->zenkaku))
		return false;
	if (!pact_numeral_int_list(r, tree, "サーフェイスエリア", 4, 0, 4096, out->area))
		return false;
	return !r->error;
}

static bool pact_numeral_validate(struct pact_reader *r, struct ex_tree *tree)
{
	struct pact_numeral_data data;
	return pact_numeral_read(r, tree, &data);
}

static bool pact_numeral_apply(struct pact_reader *r, int number,
		struct ex_tree *tree, int state)
{
	struct pact_numeral_data data;
	if (!pact_numeral_read(r, tree, &data))
		return false;
	if (data.type == 2) {
		struct parts_numeral config = {.have_num=true,.num=data.value,.show_comma=data.comma,
			.space=data.space,.length=max(1,data.digits),
			.common.surface_area={data.area[0],data.area[1],data.area[2],data.area[3]}};
		return parts_numeral_set_text_font(number,&data.style,&config,state);
	}

	struct string *cg = pact_to_game(data.cg_name->text);
	bool exists=asset_exists_by_name(ASSET_CG,cg->text,NULL);
	free_string(cg);
	if (!exists)
		return false;
	// PACT creates fresh states. Configure the final tuple before choosing a
	// font, so an intermediate default zero/padding cannot reject valid data.
	if (!PE_SetNumeralNumber(number, data.value, state))
		return false;
	if (!PE_SetNumeralShowComma(number, data.comma != 0, state))
		return false;
	if (!PE_SetNumeralSpace(number, data.space, state))
		return false;
	if (!PE_SetNumeralLength(number, max(1, data.digits), state))
		return false;
	cg=pact_to_game(data.cg_name->text);
	bool ok=PE_SetNumeralLinkedCGNumberWidthWidthList(number, cg,
		data.width[0], data.width[1], data.width[2], data.width[3],
		data.width[4], data.width[5], data.width[6], data.width[7],
		data.width[8], data.width[9], data.width[10], data.width[11], state);
	free_string(cg);
	if (!ok)
		return false;
	if (!PE_SetNumeralSurfaceArea(number, data.area[0], data.area[1],
		data.area[2], data.area[3], state))
		return false;
	return true;
}
