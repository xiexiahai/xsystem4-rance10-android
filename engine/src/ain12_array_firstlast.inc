/* Array.First/Last predicate with a complete reference result.
 *
 * This file is included once from ain12_vm.inc. It only intercepts the
 * project-preset form:
 *   First/Last(ref array self, HLL_FUNC predicate) -> REF_HLL_PARAM
 *
 * The receiver and callback object are borrowed. The returned reference uses
 * the existing ain12_array_push_reference() shape:
 *   scalar/enum/interface -> two stack slots (owner/index or object/vtable)
 *   object/string/array   -> one stack slot (element reference)
 * Empty/no-match results use the same helper's null shapes.
 */
#define AIN12_FIRSTLAST_MAX_TYPE_DEPTH 16

static bool ain12_firstlast_type_supported(const struct ain_type *type, int depth)
{
	if (!type)
		VM_ERROR("Array.First/Last has a null element descriptor");
	if (depth > AIN12_FIRSTLAST_MAX_TYPE_DEPTH)
		VM_ERROR("Array.First/Last element descriptor nesting limit exceeded");
	switch (type->data) {
	case AIN_INT:
	case AIN_FLOAT:
	case AIN_BOOL:
	case AIN_ENUM:
	case AIN_ENUM2:
	case AIN_STRING:
	case AIN_STRUCT:
	case AIN_REF_STRUCT:
	case AIN_IFACE:
		return true;
	case AIN_ARRAY:
		if (type->rank < 1 || !type->array_type)
			VM_ERROR("Array.First/Last has an invalid nested array descriptor");
		return ain12_firstlast_type_supported(type->array_type, depth + 1);
	default:
		return false;
	}
}

static bool ain12_firstlast_type_equal(const struct ain_type *a,
		const struct ain_type *b, int depth)
{
	if (!a || !b)
		return a == b;
	if (depth > AIN12_FIRSTLAST_MAX_TYPE_DEPTH)
		VM_ERROR("Array.First/Last element descriptor nesting limit exceeded");
	if (a->data != b->data || a->struc != b->struc || a->rank != b->rank)
		return false;
	if (a->data == AIN_ARRAY) {
		if (!a->array_type || !b->array_type)
			return false;
		return ain12_firstlast_type_equal(a->array_type, b->array_type,
			depth + 1);
	}
	return true;
}

static bool ain12_array_firstlast(struct ain_hll_function *f)
{
	bool first = !strcmp(f->name, "First");
	bool last = !strcmp(f->name, "Last");
	if (!first && !last)
		return false;
	/* Unknown overloads are rejected before any operand is popped. */
	if (f->nr_arguments != 2 ||
			f->arguments[0].type.data != AIN_REF_ARRAY ||
			f->arguments[1].type.data != AIN_HLL_FUNC ||
			f->return_type.data != AIN_REF_HLL_PARAM)
		return false;

	int function = stack_pop().i;
	int object = stack_pop().i;
	int slot = stack_pop().i;
	if (function < 0 || function >= ain->nr_functions)
		VM_ERROR("Invalid Array.%s predicate", f->name);

	struct page *p = heap_get_page(slot);
	if (!p || p->type != ARRAY_PAGE || p->a_type != AIN_ARRAY ||
			p->array.rank != 1 || !p->element_type)
		VM_ERROR("Array.%s needs a rank-one generic array", f->name);
	int width = ain12_array_width(p);
	if (p->nr_vars < 0 || p->nr_vars % width)
		VM_ERROR("Array.%s has an incomplete element", f->name);
	int count = p->nr_vars / width;
	struct ain_type element = *p->element_type;
	if (!ain12_firstlast_type_supported(&element, 0))
		VM_ERROR("Array.%s has unsupported element type %d",
			f->name, element.data);

	int parent = local_page_slot();
	int found = -1;
	for (int step = 0; step < count; step++) {
		int i = last ? count - 1 - step : step;
		int result = ain12_array_predicate(slot, i, object, function, parent);

		/* A callback may call Array.Free/Realloc/etc. Re-fetch and verify
		 * the full shape before the next element access or result push. */
		if (!page_index_valid(slot))
			VM_ERROR("Array.%s callback released its receiver", f->name);
		struct page *after = heap_get_page(slot);
		if (!after || after->type != ARRAY_PAGE || after->a_type != AIN_ARRAY ||
				after->array.rank != 1 ||
				ain12_array_width(after) != width ||
				after->nr_vars != count * width ||
				!after->element_type ||
				!ain12_firstlast_type_equal(&element, after->element_type, 0))
			VM_ERROR("Array.%s callback changed the receiver shape", f->name);

		if (result) {
			found = i;
			break;
		}
	}

	ain12_array_push_reference(slot, found);
	return true;
}
