/* Array natural ordering and non-callback BinarySearch.
 *
 * This file is included once from ain12_vm.inc. It intentionally only owns the
 * non-callback forms:
 *   Sort/AscSort/DescSort/QuickSort(ref array self) -> ref array
 *   BinarySearch(ref array self, HLL_PARAM Search) -> int
 *
 * Callback sorts and the Min/Max implementation live in their existing files
 * and are not touched here. The interface contract is a project preset, not a
 * claim about the complete native specification.
 */
static int ain12_array_order_compare(enum ain_data_type type,
		union vm_value lhs, union vm_value rhs)
{
	if (type == AIN_STRING) {
		int cmp = strcmp(heap_get_string(lhs.i)->text,
			heap_get_string(rhs.i)->text);
		return (cmp > 0) - (cmp < 0);
	}
	return (lhs.i > rhs.i) - (lhs.i < rhs.i);
}

/* Stable bottom-up merge sort: O(n log n) comparisons and O(n) scratch slots.
 * Scratch values are borrowed relocations, never additional VM owners. */
static void ain12_array_order_sort(struct page *p, int count,
		enum ain_data_type type, bool descending)
{
	if (count < 2)
		return;
	union vm_value *scratch = xmalloc((size_t)count * sizeof(*scratch));
	for (int width = 1; width < count; width *= 2) {
		for (int start = 0; start < count; start += width * 2) {
			int mid = min(start + width, count);
			int end = min(start + width * 2, count);
			int left = start, right = mid;
			for (int dst = start; dst < end; dst++) {
				bool take_left = right == end;
				if (left < mid && right < end) {
					int cmp = ain12_array_order_compare(type,
						p->values[left], p->values[right]);
					take_left = descending ? cmp >= 0 : cmp <= 0;
				}
				scratch[dst] = take_left ? p->values[left++] : p->values[right++];
			}
		}
		memcpy(p->values, scratch, (size_t)count * sizeof(*scratch));
	}
	free(scratch);
}

static bool ain12_array_order(struct ain_hll_function *f, int type_param)
{
	bool sort = !strcmp(f->name, "Sort") || !strcmp(f->name, "AscSort") ||
		!strcmp(f->name, "DescSort") || !strcmp(f->name, "QuickSort");
	bool is_search = !strcmp(f->name, "BinarySearch");
	if (!sort && !is_search)
		return false;

	if (sort) {
		if (f->nr_arguments != 1 ||
				f->arguments[0].type.data != AIN_REF_ARRAY ||
				f->return_type.data != AIN_REF_ARRAY)
			return false;
		int slot = stack_pop().i;
		struct page *p = heap_get_page(slot);
		if (!p || p->type != ARRAY_PAGE || p->a_type != AIN_ARRAY ||
				p->array.rank != 1 || !p->element_type)
			VM_ERROR("Array.%s needs a rank-one generic array", f->name);
		enum ain_data_type type = p->element_type->data;
		if (type != AIN_INT && type != AIN_STRING)
			VM_ERROR("Array.%s has no natural ordering for type %d",
				f->name, type);
		int width = ain12_array_width(p);
		if (p->nr_vars < 0 || p->nr_vars > 10000000 || p->nr_vars % width)
			VM_ERROR("Array.%s has an incomplete element", f->name);
		ain12_array_order_sort(p, p->nr_vars / width, type,
			!strcmp(f->name, "DescSort"));
		/* The receiver is borrowed on input; the returned reference is the
		 * same array with exactly one additional reference. */
		heap_ref(slot);
		stack_push(slot);
		return true;
	}

	if (f->nr_arguments != 2 ||
			f->arguments[0].type.data != AIN_REF_ARRAY ||
			(f->arguments[1].type.data != AIN_HLL_PARAM &&
			 f->arguments[1].type.data != AIN_REF_HLL_PARAM) ||
			f->return_type.data != AIN_INT)
		return false;
	/* The concrete search type is carried by type_param, following the
	 * existing Find convention. Ref strings are borrowed; value strings are
	 * owned by the HLL call and released below. */
	if (type_param != AIN_INT && type_param != AIN_STRING &&
			type_param != AIN_REF_STRING)
		return false;

	union vm_value search = stack_pop();
	int slot = stack_pop().i;
	struct page *p = heap_get_page(slot);
	if (!p || p->type != ARRAY_PAGE || p->a_type != AIN_ARRAY ||
			p->array.rank != 1 || !p->element_type)
		VM_ERROR("Array.BinarySearch needs a rank-one generic array");
	enum ain_data_type type = p->element_type->data;
	if (type != AIN_INT && type != AIN_STRING)
		VM_ERROR("Array.BinarySearch has no natural ordering for type %d", type);
	if (type == AIN_INT) {
		if (type_param != AIN_INT)
			VM_ERROR("Array.BinarySearch search type %d does not match int array",
				type_param);
	} else if (type_param != AIN_STRING && type_param != AIN_REF_STRING) {
		VM_ERROR("Array.BinarySearch search type %d does not match string array",
			type_param);
	}
	int width = ain12_array_width(p);
	if (p->nr_vars < 0 || p->nr_vars > 10000000 || p->nr_vars % width)
		VM_ERROR("Array.BinarySearch has an incomplete element");

	int count = p->nr_vars / width;
	int lo = 0, hi = count;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		if (ain12_array_order_compare(type, p->values[mid], search) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	int result = -1;
	if (lo < count && ain12_array_order_compare(type, p->values[lo], search) == 0)
		result = lo;

	/* receiver is borrowed; only the owned value search is released. */
	if (type_param != AIN_REF_STRING)
		variable_fini(search, type_param, true);
	stack_push(result);
	return true;
}
