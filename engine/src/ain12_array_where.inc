#ifndef AIN12_ARRAY_WHERE_INC
#define AIN12_ARRAY_WHERE_INC

/* Array.Where(ref array self, HLL_FUNC predicate) -> owned ARRAY.
 *
 * This helper owns only the result array. The three operand slots are borrowed
 * and are not popped unless the exact overload is recognized. The returned
 * array has its own descriptor/order and copied values; receiver mutation by a
 * callback is checked after every callback.
 */
#define AIN12_WHERE_MAX_TYPE_DEPTH 16

static bool ain12_where_type_supported(const struct ain_type *type, int depth)
{
	if (!type)
		VM_ERROR("Array.Where has a null element descriptor");
	if (depth > AIN12_WHERE_MAX_TYPE_DEPTH)
		VM_ERROR("Array.Where element descriptor nesting limit exceeded");
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
			VM_ERROR("Array.Where has an invalid nested array descriptor");
		return ain12_where_type_supported(type->array_type, depth + 1);
	default:
		return false;
	}
}

static struct ain_type *ain12_where_type_copy(const struct ain_type *type, int depth)
{
	if (!type)
		VM_ERROR("Array.Where has a null element descriptor");
	if (depth > AIN12_WHERE_MAX_TYPE_DEPTH)
		VM_ERROR("Array.Where element descriptor nesting limit exceeded");
	struct ain_type *copy = xmalloc(sizeof(*copy));
	*copy = *type;
	copy->array_type = NULL;
	if (type->data == AIN_ARRAY) {
		if (type->rank < 1 || !type->array_type)
			VM_ERROR("Array.Where has an invalid nested array descriptor");
		copy->array_type = ain12_where_type_copy(type->array_type, depth + 1);
	}
	return copy;
}

static void ain12_where_type_free(struct ain_type *type)
{
	while (type) {
		struct ain_type *next = type->array_type;
		free(type);
		type = next;
	}
}

static bool ain12_where_type_equal(const struct ain_type *a,
		const struct ain_type *b, int depth)
{
	if (!a || !b)
		return a == b;
	if (depth > AIN12_WHERE_MAX_TYPE_DEPTH)
		VM_ERROR("Array.Where element descriptor nesting limit exceeded");
	if (a->data != b->data || a->struc != b->struc || a->rank != b->rank)
		return false;
	if (a->data == AIN_ARRAY) {
		if (!a->array_type || !b->array_type)
			return false;
		return ain12_where_type_equal(a->array_type, b->array_type, depth + 1);
	}
	return true;
}

static bool ain12_array_where(struct ain_hll_function *f)
{
	if (!f || strcmp(f->name, "Where"))
		return false;
	if (f->nr_arguments != 2 ||
			f->arguments[0].type.data != AIN_REF_ARRAY ||
			f->arguments[1].type.data != AIN_HLL_FUNC ||
			f->return_type.data != AIN_ARRAY)
		return false;

	int function = stack_pop().i;
	int object = stack_pop().i;
	int slot = stack_pop().i;
	if (function < 0 || function >= ain->nr_functions ||
			ain->functions[function].return_type.data != AIN_BOOL)
		VM_ERROR("Array.Where needs a bool predicate");

	if (!page_index_valid(slot))
		VM_ERROR("Array.Where callback released its receiver");
	struct page *receiver = heap_get_page(slot);
	if (!receiver || receiver->type != ARRAY_PAGE || receiver->a_type != AIN_ARRAY ||
			receiver->array.rank != 1 || !receiver->element_type)
		VM_ERROR("Array.Where needs a rank-one generic array");
	int width = ain12_array_width(receiver);
	if ((width != 1 && width != 2) || receiver->nr_vars < 0 ||
			receiver->nr_vars % width)
		VM_ERROR("Array.Where has an incomplete element");
	int count = receiver->nr_vars / width;
	if (count>10000000) VM_ERROR("Array.Where element limit exceeded");
	if (!ain12_where_type_supported(receiver->element_type, 0))
		VM_ERROR("Array.Where has unsupported element type %d",
			receiver->element_type->data);
	int struct_type = receiver->array.struct_type;
	struct ain_type *snapshot = ain12_where_type_copy(receiver->element_type, 0);

	struct page *template = alloc_page(ARRAY_PAGE, AIN_ARRAY, 0);
	page_set_element_type(template, receiver->element_type);
	template->array = receiver->array;
	int result_slot = heap_alloc_page(template);
	int selected = 0;
	int parent = local_page_slot();

#define AIN12_WHERE_FAIL(...) do { \
	if (result_slot >= 0) heap_unref(result_slot); \
	ain12_where_type_free(snapshot); \
	VM_ERROR(__VA_ARGS__); \
} while (0)

	for (int i = 0; i < count; i++) {
		int selected_now = ain12_array_predicate(slot, i, object, function, parent);

		if (!page_index_valid(slot))
			AIN12_WHERE_FAIL("Array.Where callback released its receiver");
		struct page *after = heap_get_page(slot);
		if (!after || after->type != ARRAY_PAGE || after->a_type != AIN_ARRAY ||
				after->array.rank != 1 || after->array.struct_type != struct_type ||
				!after->element_type || ain12_array_width(after) != width ||
				after->nr_vars != count * width ||
				!ain12_where_type_equal(snapshot, after->element_type, 0))
			AIN12_WHERE_FAIL("Array.Where callback changed the receiver shape");

		if (!selected_now)
			continue;

		/* Append an uninitialized logical element, transferring existing
		 * owned slots without creating and discarding default nested arrays. */
		struct page *old=heap_get_page(result_slot);
		struct page *dst=alloc_page(ARRAY_PAGE,AIN_ARRAY,(selected+1)*width);
		page_set_element_type(dst,snapshot);dst->array=old->array;
		memcpy(dst->values,old->values,(size_t)selected*width*sizeof(*dst->values));
		heap_set_page(result_slot,dst);free_page(old);

		if (width == 2) {
			int object_value = after->values[i * 2].i;
			int vtable = after->values[i * 2 + 1].i;
			if (object_value != -1)
				heap_ref(object_value);
			dst->values[selected * 2].i = object_value;
			dst->values[selected * 2 + 1].i = vtable;
			selected++;
			continue;
		}

		enum ain_data_type type = after->element_type->data;
		union vm_value value;
		if (type == AIN_REF_STRUCT) {
			/* Identity, but this array owns its own reference. */
			value = after->values[i];
			if (value.i != -1)
				heap_ref(value.i);
			dst->values[selected].i = value.i;
		} else if ((type == AIN_STRING || type == AIN_STRUCT || type == AIN_ARRAY) &&
				after->values[i].i == -1) {
			value = after->values[i];
			dst->values[selected]=value;
		} else {
			value = vm_copy(after->values[i], type);
			dst->values[selected]=value;
		}
		selected++;
	}

	ain12_where_type_free(snapshot);
	stack_push(result_slot);
	return true;
#undef AIN12_WHERE_FAIL
}

#endif /* AIN12_ARRAY_WHERE_INC */
