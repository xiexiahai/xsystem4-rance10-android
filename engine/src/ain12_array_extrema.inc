/* Array.Min/Max return an owned reference to an element, not its value.
 * Comparator overloads use the same strict-less convention as Array.Sort. */
static void ain12_array_push_reference(int slot, int index)
{
	struct page *p = heap_get_page(slot);
	enum ain_data_type type = p->element_type->data;
	if (type == AIN_INT || type == AIN_FLOAT || type == AIN_BOOL ||
			type == AIN_ENUM || type == AIN_ENUM2) {
		if (index >= 0) heap_ref(slot);
		stack_push(index >= 0 ? slot : -1);
		stack_push(index >= 0 ? index : 0);
	} else if (type == AIN_IFACE) {
		int object = index >= 0 ? p->values[index * 2].i : -1;
		heap_ref(object); stack_push(object);
		stack_push(index >= 0 ? p->values[index * 2 + 1].i : 0);
	} else {
		switch (type) {
		case AIN_STRING: case AIN_STRUCT: case AIN_ARRAY:
		case AIN_REF_STRING: case AIN_REF_STRUCT: case AIN_REF_ARRAY:
			break;
		default: VM_ERROR("Unsupported generic element reference type %d", type);
		}
		int object = index >= 0 ? p->values[index].i : -1;
		heap_ref(object); stack_push(object);
	}
}

static bool ain12_array_extrema(struct ain_hll_function *f)
{
	bool maximum = !strcmp(f->name, "Max");
	if ((!maximum && strcmp(f->name, "Min")) ||
			f->return_type.data != AIN_REF_HLL_PARAM ||
			f->nr_arguments < 1 || f->nr_arguments > 2 ||
			f->arguments[0].type.data != AIN_REF_ARRAY)
		return false;
	bool callback = f->nr_arguments == 2;
	if (callback && f->arguments[1].type.data != AIN_HLL_FUNC)
		return false;
	int function = callback ? stack_pop().i : -1;
	int object = callback ? stack_pop().i : -1;
	int slot = stack_pop().i;
	struct page *p = heap_get_page(slot);
	if (!p || p->type != ARRAY_PAGE || p->a_type != AIN_ARRAY ||
			p->array.rank != 1 || !p->element_type)
		VM_ERROR("Array.%s needs a rank-one generic array", f->name);
	int width = ain12_array_width(p), slots = p->nr_vars, count = slots / width;
	if (slots % width)
		VM_ERROR("Array.%s has an incomplete interface element", f->name);
	enum ain_data_type type = p->element_type->data;
	int parent = -1;
	if (callback) {
		if (function < 0 || function >= ain->nr_functions ||
				ain->functions[function].return_type.data != AIN_BOOL)
			VM_ERROR("Array.%s needs a bool comparator", f->name);
		parent = local_page_slot();
	} else if (type != AIN_INT && type != AIN_FLOAT && type != AIN_BOOL &&
			type != AIN_ENUM && type != AIN_ENUM2 && type != AIN_STRING) {
		VM_ERROR("Array.%s has no natural ordering for type %d", f->name, type);
	}
	int best = count ? 0 : -1;
	for (int i = 1; i < count; i++) {
		int lhs = maximum ? best : i, rhs = maximum ? i : best;
		bool less;
		if (callback) {
			int pair[] = {lhs, rhs};
			less = ain12_array_callback(slot, pair, 2, object, function, parent) != 0;
			if (!page_index_valid(slot))
				VM_ERROR("Array.%s receiver released by comparator", f->name);
			p = heap_get_page(slot);
			if (!p || p->type != ARRAY_PAGE || p->a_type != AIN_ARRAY ||
					p->array.rank != 1 || p->nr_vars != slots || !p->element_type ||
					p->element_type->data != type || ain12_array_width(p) != width)
				VM_ERROR("Array.%s shape changed during comparator", f->name);
		} else if (type == AIN_FLOAT) {
			less = p->values[lhs].f < p->values[rhs].f;
		} else if (type == AIN_STRING) {
			less = strcmp(heap_get_string(p->values[lhs].i)->text,
				heap_get_string(p->values[rhs].i)->text) < 0;
		} else {
			less = p->values[lhs].i < p->values[rhs].i;
		}
		if (less) best = i;
	}
	ain12_array_push_reference(slot, best);
	return true;
}
