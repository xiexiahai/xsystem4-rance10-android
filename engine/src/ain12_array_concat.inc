/* Array.Concat(ref array self, ref array source) -> void.
 *
 * The receivers are borrowed: this helper never takes or drops an additional
 * reference on either array slot. Source elements are snapshotted before the
 * receiver is resized, which is required when self == source.
 *
 * The supported element descriptors are the ones whose value/reference
 * semantics are supplied by the existing vm_copy implementation and by
 * generic_array_resize: scalars, strings, value structs, ref structs, nested
 * value arrays and interfaces. Anything else is an explicit VM_ERROR rather
 * than a silently wrong copy. */
#define AIN12_CONCAT_MAX_ELEMENTS 10000000
#define AIN12_CONCAT_MAX_TYPE_DEPTH 16

static bool ain12_concat_type_supported(const struct ain_type *type, int depth)
{
	if (!type)
		VM_ERROR("Array.Concat has a null element descriptor");
	if (depth > AIN12_CONCAT_MAX_TYPE_DEPTH)
		VM_ERROR("Array.Concat element descriptor nesting limit exceeded");
	switch (type->data) {
	case AIN_INT:
	case AIN_FLOAT:
	case AIN_BOOL:
	case AIN_LONG_INT:
	case AIN_ENUM:
	case AIN_ENUM2:
	case AIN_FUNC_TYPE:
	case AIN_STRING:
	case AIN_STRUCT:
	case AIN_REF_STRUCT:
	case AIN_IFACE:
		return true;
	case AIN_ARRAY:
		if (type->rank < 1 || !type->array_type)
			VM_ERROR("Array.Concat has an invalid nested array descriptor");
		return ain12_concat_type_supported(type->array_type, depth + 1);
	default:
		return false;
	}
}

static bool ain12_concat_type_equal(const struct ain_type *a,
		const struct ain_type *b, int depth)
{
	if (!a || !b)
		return a == b;
	if (depth > AIN12_CONCAT_MAX_TYPE_DEPTH)
		VM_ERROR("Array.Concat element descriptor nesting limit exceeded");
	if (a->data != b->data || a->struc != b->struc || a->rank != b->rank)
		return false;
	if (a->data == AIN_ARRAY) {
		if (!a->array_type || !b->array_type)
			VM_ERROR("Array.Concat has an invalid nested array descriptor");
		return ain12_concat_type_equal(a->array_type, b->array_type, depth + 1);
	}
	return true;
}

static int ain12_concat_count(struct page *p, const char *which)
{
	if (!p || p->type != ARRAY_PAGE || p->a_type != AIN_ARRAY)
		VM_ERROR("Array.Concat %s must be a generic array", which);
	if (p->array.rank != 1 || !p->element_type)
		VM_ERROR("Array.Concat %s must be a rank-one generic array", which);
	int width = ain12_array_width(p);
	if (p->nr_vars < 0 || p->nr_vars % width)
		VM_ERROR("Array.Concat %s has an incomplete interface element", which);
	int count = p->nr_vars / width;
	if (count > AIN12_CONCAT_MAX_ELEMENTS)
		VM_ERROR("Array.Concat %s exceeds the element limit", which);
	return count;
}

static bool ain12_array_concat(struct ain_hll_function *f)
{
	if (strcmp(f->name, "Concat") || f->nr_arguments != 2 ||
			f->return_type.data != AIN_VOID ||
			f->arguments[0].type.data != AIN_REF_ARRAY ||
			f->arguments[1].type.data != AIN_REF_ARRAY)
		return false;

	/* The stack is [self, source] from bottom to top. */
	int source_slot = stack_pop().i;
	int self_slot = stack_pop().i;
	struct page *self = heap_get_page(self_slot);
	struct page *source = heap_get_page(source_slot);
	int self_count = ain12_concat_count(self, "self");
	int source_count = ain12_concat_count(source, "source");

	if (!ain12_concat_type_supported(self->element_type, 0))
		VM_ERROR("Array.Concat self element type %d is not supported",
				self->element_type->data);
	if (!ain12_concat_type_supported(source->element_type, 0))
		VM_ERROR("Array.Concat source element type %d is not supported",
				source->element_type->data);
	if (!ain12_concat_type_equal(self->element_type, source->element_type, 0))
		VM_ERROR("Array.Concat self and source element types are incompatible");
	if (source_count > AIN12_CONCAT_MAX_ELEMENTS - self_count)
		VM_ERROR("Array.Concat result exceeds %d elements",
				AIN12_CONCAT_MAX_ELEMENTS);

	int width = ain12_array_width(self);
	int source_slots = source_count * width;
	if (source_slots > 0) {
		union vm_value *snapshot = xmalloc(sizeof(*snapshot) * (size_t)source_slots);
		/* Snapshot every physical slot with variable_type/vm_copy. For an
		 * interface this copies the object slot once (AIN_REF_STRUCT) and the
		 * vtable slot as AIN_INT, so ownership is transferred exactly once. */
		for (int i = 0; i < source_slots; i++) {
			enum ain_data_type type = variable_type(source, i, NULL, NULL);
			union vm_value value = source->values[i];
			if ((type == AIN_STRING || type == AIN_ARRAY) && value.i == -1) {
				snapshot[i] = value;
			} else {
				snapshot[i] = vm_copy(value, type);
			}
		}

		/* This may reallocate and free the old self page. Never read source
		 * after this point; self == source is deliberately supported. */
		ain12_resize(self_slot, self_count + source_count, false);
		struct page *dst = heap_get_page(self_slot);
		for (int i = 0; i < source_slots; i++) {
			int at = self_count * width + i;
			enum ain_data_type type = variable_type(dst, at, NULL, NULL);
			variable_set(dst, at, type, snapshot[i]);
		}
		free(snapshot);
	}
	return true;
}
