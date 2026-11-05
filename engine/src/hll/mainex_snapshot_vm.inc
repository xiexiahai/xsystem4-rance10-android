/* Private EX memory images for VM save/resume. Never writes the original EX. */
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include "mainex_snapshot.h"

#define MEX_VM_TAG UINT32_C(0x58454d52)
#define MEX_VM_VERSION 1u
#define MEX_VM_MAX_IMAGE (65u * 1024u * 1024u)

static bool mex_vm_trace(void)
{
	const char *value = getenv("XSYSTEM4_TRACE_EX_SNAPSHOT");
	return value && !strcmp(value, "1");
}

static bool mex_vm_int_array(const struct page *page)
{
	if (!page || page->type != ARRAY_PAGE || page->array.rank != 1 || page->nr_vars < 0)
		return false;
	if (page->a_type != AIN_ARRAY_INT && page->a_type != AIN_ARRAY)
		return false;
	if (page->a_type == AIN_ARRAY && !page->element_type)
		return false;
	return !page->element_type || (page->element_type->data == AIN_INT &&
		page->element_type->rank == 0 && !page->element_type->array_type);
}

static bool MainEXFile_Save(struct page **buffer)
{
	if (!buffer || !ex || (*buffer && !mex_vm_int_array(*buffer)))
		return false;
	unsigned char *bytes = NULL;
	size_t len = 0;
	if (!mex_pack(ex, &bytes, &len))
		return false;
	if (len < 24 || len > MEX_VM_MAX_IMAGE) {
		free(bytes);
		return false;
	}
	size_t words = (len + 3) / 4;
	if (words > INT_MAX - 3u) {
		free(bytes);
		return false;
	}
	/* Use a fallible allocation here so a failed image never destroys the old
	 * ref array. free_page also accepts pages outside its small-page cache. */
	struct page *page = calloc(1, sizeof(*page) + (words + 3) * sizeof(*page->values));
	if (!page) {
		free(bytes);
		return false;
	}
	page->type = ARRAY_PAGE;
	page->a_type = AIN_ARRAY;
	page->nr_vars = (int)(words + 3);
	page->array.rank = 1;
	page->array.struct_type = -1;
	struct ain_type element = { .data = AIN_INT, .struc = -1, .rank = 0, .array_type = NULL };
	page_set_element_type(page, &element);
	page->values[0].i = (int32_t)MEX_VM_TAG;
	page->values[1].i = MEX_VM_VERSION;
	page->values[2].i = (int32_t)len;
	for (size_t i = 0; i < words; i++) {
		uint32_t word = 0;
		for (size_t b = 0; b < 4 && i * 4 + b < len; b++)
			word |= (uint32_t)bytes[i * 4 + b] << (b * 8);
		page->values[3 + i].i = (int32_t)word;
	}
	free(bytes);
	struct page *old = *buffer;
	*buffer = page;
	if (old) {
		delete_page_vars(old);
		free_page(old);
	}
	if (mex_vm_trace())
		NOTICE("[ex-snapshot] saved blocks=%u bytes=%zu words=%zu", ex->nr_blocks, len, words + 3);
	return true;
}

static bool MainEXFile_Load(struct page **buffer)
{
	if (!buffer || !mex_vm_int_array(*buffer))
		return false;
	struct page *page = *buffer;
	if (page->nr_vars < 3 || (uint32_t)page->values[0].i != MEX_VM_TAG ||
		(uint32_t)page->values[1].i != MEX_VM_VERSION)
		return false;
	int32_t stored_len = page->values[2].i;
	if (stored_len < 24 || (uint32_t)stored_len > MEX_VM_MAX_IMAGE)
		return false;
	size_t len = (size_t)stored_len;
	size_t words = (len + 3) / 4;
	if ((size_t)page->nr_vars != words + 3)
		return false;
	for (size_t i = len; i < words * 4; i++) {
		uint32_t word = (uint32_t)page->values[3 + i / 4].i;
		if ((word >> ((i % 4) * 8)) & 0xffu)
			return false;
	}
	unsigned char *bytes = malloc(len);
	if (!bytes)
		return false;
	for (size_t i = 0; i < len; i++) {
		uint32_t word = (uint32_t)page->values[3 + i / 4].i;
		bytes[i] = (unsigned char)(word >> ((i % 4) * 8));
	}
	struct ex *candidate = mex_unpack(bytes, len);
	free(bytes);
	if (!candidate)
		return false;

	/* The codec has bounded depth and value count and reconstructed tree aliases.
	 * Build the candidate mapping before publishing either root or handles. */
	int id = 1;
	for (unsigned i = 0; i < candidate->nr_blocks; i++)
		id = set_indices(&candidate->blocks[i].val, id);
	struct ex_value **map = calloc((size_t)id, sizeof(*map));
	if (!map) {
		ex_free(candidate);
		return false;
	}
	for (unsigned i = 0; i < candidate->nr_blocks; i++)
		map_handles_into(&candidate->blocks[i].val, map);
	struct ex *old = ex;
	struct ex_value **old_map = handles;
	ex = candidate;
	handles = map;
	nr_handles = (unsigned)id;
	free(old_map);
	if (old)
		ex_free(old);
	if (mex_vm_trace())
		NOTICE("[ex-snapshot] loaded blocks=%u handles=%u bytes=%zu", ex->nr_blocks, nr_handles, len);
	return true;
}
