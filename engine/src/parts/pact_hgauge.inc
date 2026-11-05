/* Restricted PACT horizontal gauge (type 14) fields.
 *
 * This is not a full native/save/motion contract.  It accepts the observed
 * bitmap HGauge fields, validates them with double precision, and applies them
 * through the existing public APIs.  The reverse flag is passed to the A6
 * PE_SetHGaugeReverse entry; the renderer's reverse=false/true pixel contract
 * is validated by Codex, not by this field parser. */
struct pact_hgauge_data {
	struct string *cg_name;
	double denominator;
	double numerator;
	int32_t reverse;
	int32_t area[4];
};

static bool pact_hgauge_reject(struct pact_reader *r)
{
	r->error = true;
	return false;
}

static bool pact_hgauge_number(struct pact_reader *r, struct ex_tree *tree,
		const char *key, double *out)
{
	struct ex_value *v = pact_value(tree, key);
	if (!v)
		return pact_hgauge_reject(r);
	if (v->type == EX_INT) {
		*out = v->i;
	} else if (v->type == EX_FLOAT) {
		*out = v->f;
	} else {
		return pact_hgauge_reject(r);
	}
	if (!isfinite(*out))
		return pact_hgauge_reject(r);
	return true;
}

static bool pact_hgauge_cg(struct pact_reader *r, struct ex_tree *tree,
		struct string **out)
{
	struct ex_value *v = pact_value(tree, "ＣＧ名");
	if (!v || v->type != EX_STRING || !v->s || !v->s->size)
		return pact_hgauge_reject(r);
	*out = v->s;
	return true;
}

static bool pact_hgauge_int(struct pact_reader *r, struct ex_tree *tree,
		const char *key, int32_t min, int32_t max, int32_t *out)
{
	struct ex_value *v = pact_value(tree, key);
	if (!v || v->type != EX_INT || v->i < min || v->i > max)
		return pact_hgauge_reject(r);
	*out = v->i;
	return true;
}

static bool pact_hgauge_area(struct pact_reader *r, struct ex_tree *tree,
		int32_t out[4])
{
	struct ex_value *v = pact_value(tree, "サーフェイスエリア");
	if (!v || v->type != EX_LIST || !v->list || !v->list->items ||
			v->list->nr_items != 4)
		return pact_hgauge_reject(r);
	for (int i = 0; i < 4; i++) {
		struct ex_value *item = &v->list->items[i].value;
		if (item->type != EX_INT || item->i < 0 || item->i > 4096)
			return pact_hgauge_reject(r);
		out[i] = item->i;
	}
	return true;
}

static bool pact_hgauge_read(struct pact_reader *r, struct ex_tree *tree,
		struct pact_hgauge_data *out)
{
	memset(out, 0, sizeof(*out));
	if (!pact_hgauge_cg(r, tree, &out->cg_name))
		return false;
	if (!pact_hgauge_number(r, tree, "分母", &out->denominator))
		return false;
	if (!(out->denominator > 0.0) || out->denominator > 10000000.0)
		return pact_hgauge_reject(r);
	if (!pact_hgauge_number(r, tree, "分子", &out->numerator))
		return false;
	if (!(out->numerator >= 0.0) || out->numerator > out->denominator ||
			out->numerator > 10000000.0)
		return pact_hgauge_reject(r);
	if (!pact_hgauge_int(r, tree, "反转", 0, 1, &out->reverse))
		return false;
	if (!pact_hgauge_area(r, tree, out->area))
		return false;
	return !r->error;
}

static bool pact_hgauge_validate(struct pact_reader *r, struct ex_tree *tree)
{
	struct pact_hgauge_data data;
	return pact_hgauge_read(r, tree, &data);
}

static bool pact_hgauge_apply(struct pact_reader *r, int number,
		struct ex_tree *tree, int state)
{
	struct pact_hgauge_data data;
	if (!pact_hgauge_read(r, tree, &data))
		return false;

	/* The field contract validates in double.  These casts and the positive
	 * float check prevent a subnormal double denominator from reaching the
	 * existing float division as zero. */
	float numerator = (float)data.numerator;
	float denominator = (float)data.denominator;
	if (!isfinite(numerator) || !isfinite(denominator) || !(denominator > 0.0f))
		return false;

	struct string *cg = pact_to_game(data.cg_name->text);
	bool ok = PE_SetHGaugeCG(number, cg, state);
	free_string(cg);
	if (!ok)
		return false;
	if (!PE_SetHGaugeReverse(number, data.reverse != 0, state))
		return false;
	if (!PE_SetHGaugeRate(number, numerator, denominator, state))
		return false;
	if (!PE_SetHGaugeSurfaceArea(number, data.area[0], data.area[1],
			data.area[2], data.area[3], state))
		return false;
	return true;
}
