/* Private, bounded MainEXFile memory image. This is not the native .ex format. */
#ifndef XSYSTEM4_MAINEX_SNAPSHOT_H
#define XSYSTEM4_MAINEX_SNAPSHOT_H

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "system4/ex.h"
#include "system4/string.h"

#define MEX_MAX_RAW (64u * 1024u * 1024u)
#define MEX_MAX_IMAGE (MEX_MAX_RAW + 1024u * 1024u)
#define MEX_MAX_ALLOC (192u * 1024u * 1024u)
#define MEX_MAX_NODES 1000000u
#define MEX_MAX_DEPTH 96u
#define MEX_HEADER_SIZE 24u

struct mex_mem { size_t used; };
struct mex_ptr_entry { const void *key; uint32_t value; };
struct mex_ptr_map { struct mex_ptr_entry *entries; size_t capacity, count; };
struct mex_schema { struct ex_field *fields; uint32_t count; bool ready; };
struct mex_writer {
	struct mex_mem mem;
	unsigned char *data;
	size_t length, capacity;
	uint32_t nodes;
	struct mex_ptr_map owners, objects;
	struct mex_schema *schemas;
	size_t nr_schemas, schema_capacity;
};
struct mex_reader {
	struct mex_mem mem;
	const unsigned char *data;
	size_t length, offset;
	uint32_t nodes;
	void **allocations;
	size_t nr_allocations, allocation_capacity;
	struct mex_schema *schemas;
	size_t nr_schemas, schema_capacity;
};

static uint32_t mex_get32(const unsigned char *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void mex_put32(unsigned char *p, uint32_t x)
{
	p[0] = x; p[1] = x >> 8; p[2] = x >> 16; p[3] = x >> 24;
}

static bool mex_size_product(size_t count, size_t width, size_t *out)
{
	if (width && count > MEX_MAX_ALLOC / width)
		return false;
	*out = count * width;
	return true;
}

static void *mex_resize(struct mex_mem *mem, void *p, size_t old, size_t size)
{
	if (size < old || size - old > MEX_MAX_ALLOC - mem->used)
		return NULL;
	void *q = realloc(p, size);
	if (!q)
		return NULL;
	mem->used += size - old;
	return q;
}

static size_t mex_hash_ptr(const void *key)
{
	uintptr_t x = (uintptr_t)key;
	x ^= x >> 17;
	x *= (uintptr_t)0xed5ad4bbu;
	x ^= x >> 11;
	return (size_t)x;
}

static struct mex_ptr_entry *mex_map_find(struct mex_ptr_map *map, const void *key)
{
	if (!key || !map->capacity)
		return NULL;
	size_t i = mex_hash_ptr(key) & (map->capacity - 1);
	while (map->entries[i].key) {
		if (map->entries[i].key == key)
			return &map->entries[i];
		i = (i + 1) & (map->capacity - 1);
	}
	return NULL;
}

static bool mex_map_add(struct mex_mem *mem, struct mex_ptr_map *map, const void *key, uint32_t value)
{
	if (!key || mex_map_find(map, key))
		return false;
	if (!map->capacity || map->count + 1 > map->capacity / 2) {
		size_t cap = map->capacity ? map->capacity * 2 : 128;
		size_t bytes;
		if (!mex_size_product(cap, sizeof(struct mex_ptr_entry), &bytes) || bytes > MEX_MAX_ALLOC - mem->used)
			return false;
		struct mex_ptr_entry *entries = calloc(cap, sizeof(*entries));
		if (!entries)
			return false;
		mem->used += bytes;
		for (size_t i = 0; i < map->capacity; i++) {
			if (!map->entries[i].key)
				continue;
			size_t j = mex_hash_ptr(map->entries[i].key) & (cap - 1);
			while (entries[j].key)
				j = (j + 1) & (cap - 1);
			entries[j] = map->entries[i];
		}
		mem->used -= map->capacity * sizeof(*entries);
		free(map->entries);
		map->entries = entries;
		map->capacity = cap;
	}
	size_t i = mex_hash_ptr(key) & (map->capacity - 1);
	while (map->entries[i].key)
		i = (i + 1) & (map->capacity - 1);
	map->entries[i] = (struct mex_ptr_entry){ key, value };
	map->count++;
	return true;
}

static bool mex_claim(struct mex_writer *w, const void *p, size_t count)
{
	return p ? mex_map_add(&w->mem, &w->owners, p, 0) : !count;
}

static bool mex_write(struct mex_writer *w, const void *p, size_t size)
{
	if (size > MEX_MAX_RAW - w->length)
		return false;
	size_t end = w->length + size;
	if (end > w->capacity) {
		size_t cap = w->capacity ? w->capacity : 1024;
		while (cap < end)
			cap = cap > MEX_MAX_RAW / 2 ? MEX_MAX_RAW : cap * 2;
		unsigned char *q = mex_resize(&w->mem, w->data, w->capacity, cap);
		if (!q)
			return false;
		w->data = q;
		w->capacity = cap;
	}
	if (size)
		memcpy(w->data + w->length, p, size);
	w->length = end;
	return true;
}

static bool mex_write32(struct mex_writer *w, uint32_t x)
{
	unsigned char p[4]; mex_put32(p, x); return mex_write(w, p, 4);
}

static bool mex_write64(struct mex_writer *w, uint64_t x)
{
	return mex_write32(w, (uint32_t)x) && mex_write32(w, (uint32_t)(x >> 32));
}

static bool mex_write_string(struct mex_writer *w, const struct string *s)
{
	return s && s->ref && s->size >= 0 && (unsigned)s->size <= MEX_MAX_RAW &&
		mex_write32(w, (uint32_t)s->size) && mex_write(w, s->text, (size_t)s->size);
}

static bool mex_schema_slot(struct mex_mem *mem, struct mex_schema **schemas,
		size_t *nr, size_t *capacity, struct ex_field *fields, uint32_t count, uint32_t *id)
{
	if (*nr >= MEX_MAX_NODES)
		return false;
	if (*nr == *capacity) {
		size_t cap = *capacity ? *capacity * 2 : 32;
		struct mex_schema *q = mex_resize(mem, *schemas, *capacity * sizeof(**schemas), cap * sizeof(**schemas));
		if (!q)
			return false;
		*schemas = q;
		*capacity = cap;
	}
	(*schemas)[*nr] = (struct mex_schema){fields, count, false};
	*id = (uint32_t)++*nr;
	return true;
}

static bool mex_write_value(struct mex_writer *, const struct ex_value *, unsigned);
static bool mex_write_tree(struct mex_writer *, const struct ex_tree *, unsigned);

static bool mex_write_schema(struct mex_writer *w, const struct ex_field *fields, uint32_t count, unsigned depth)
{
	if (depth > MEX_MAX_DEPTH || count > MEX_MAX_NODES || !mex_write32(w, count))
		return false;
	if (!count)
		return mex_claim(w, fields, 0);
	uint32_t id;
	if (!fields || !mex_schema_slot(&w->mem, &w->schemas, &w->nr_schemas, &w->schema_capacity,
			(struct ex_field*)fields, count, &id) || !mex_map_add(&w->mem, &w->owners, fields, id) || !mex_write32(w, id))
		return false;
	for (uint32_t i = 0; i < count; i++) {
		const struct ex_field *f = &fields[i];
		if (++w->nodes > MEX_MAX_NODES || f->type < EX_INT || f->type > EX_TABLE ||
			f->nr_subfields > 255 || (f->type != EX_TABLE && f->nr_subfields) ||
			!mex_write32(w, f->type) || !mex_write_string(w, f->name) ||
			!mex_write32(w, (uint32_t)f->has_value) || !mex_write32(w, (uint32_t)f->is_index))
			return false;
		if (f->has_value && (f->value.type != f->type || !mex_write_value(w, &f->value, depth + 1)))
			return false;
		if (!mex_write_schema(w, f->subfields, f->nr_subfields, depth + 1))
			return false;
	}
	w->schemas[id - 1].ready = true;
	return true;
}

static bool mex_write_table(struct mex_writer *w, const struct ex_table *t, unsigned depth)
{
	if (depth > MEX_MAX_DEPTH || !t || !mex_map_add(&w->mem, &w->objects, t, 1) ||
		!mex_claim(w, t, 1) || t->nr_columns != t->nr_fields || t->nr_rows > MEX_MAX_NODES ||
		t->nr_fields > MEX_MAX_NODES || (uint64_t)t->nr_rows * t->nr_columns > MEX_MAX_NODES ||
		!mex_write32(w, t->borrowed_fields ? 1 : 0) || !mex_write32(w, t->nr_fields))
		return false;
	if (t->borrowed_fields) {
		struct mex_ptr_entry *entry = mex_map_find(&w->owners, t->fields);
		uint32_t id = entry ? entry->value : 0;
		if (t->nr_fields && (!id || id > w->nr_schemas || !w->schemas[id - 1].ready ||
			w->schemas[id - 1].count != t->nr_fields))
			return false;
		if (!mex_write32(w, t->nr_fields ? id : 0))
			return false;
	} else if (!mex_write_schema(w, t->fields, t->nr_fields, depth + 1)) {
		return false;
	}
	if (!mex_write32(w, t->nr_rows) || !mex_write32(w, t->nr_columns) || !mex_claim(w, t->rows, t->nr_rows))
		return false;
	for (uint32_t i = 0; i < t->nr_rows; i++) {
		if (!mex_claim(w, t->rows[i], t->nr_columns))
			return false;
		for (uint32_t j = 0; j < t->nr_columns; j++) {
			if (!mex_write_value(w, &t->rows[i][j], depth + 1))
				return false;
		}
	}
	return true;
}

static bool mex_write_tree(struct mex_writer *w, const struct ex_tree *t, unsigned depth)
{
	if (depth > MEX_MAX_DEPTH || !t || ++w->nodes > MEX_MAX_NODES ||
		!mex_map_add(&w->mem, &w->objects, t, 1) || !mex_write_string(w, t->name) ||
		!mex_write32(w, t->is_leaf ? 1 : 0))
		return false;
	if (t->is_leaf)
		return mex_write32(w, t->leaf.size) && mex_write_string(w, t->leaf.name) &&
			mex_write_value(w, &t->leaf.value, depth + 1);
	if (t->nr_children > MEX_MAX_NODES || !mex_write32(w, t->nr_children) ||
		!mex_claim(w, t->children, t->nr_children) || !mex_claim(w, t->_children, t->nr_children))
		return false;
	for (uint32_t i = 0; i < t->nr_children; i++) {
		if (t->_children[i].type != EX_TREE || t->_children[i].tree != &t->children[i] ||
			!mex_write_tree(w, &t->children[i], depth + 1))
			return false;
	}
	return true;
}

static bool mex_write_value(struct mex_writer *w, const struct ex_value *v, unsigned depth)
{
	if (depth > MEX_MAX_DEPTH || !v || ++w->nodes > MEX_MAX_NODES || !mex_write32(w, v->type))
		return false;
	switch (v->type) {
	case EX_INT: return mex_write32(w, (uint32_t)v->i);
	case EX_FLOAT: {
		uint32_t bits; memcpy(&bits, &v->f, sizeof(bits)); return mex_write32(w, bits);
	}
	case EX_STRING: return mex_write_string(w, v->s);
	case EX_TABLE: return mex_write_table(w, v->t, depth + 1);
	case EX_LIST:
		if (!v->list || !mex_map_add(&w->mem, &w->objects, v->list, 1) || !mex_claim(w, v->list, 1) ||
			v->list->nr_items > MEX_MAX_NODES || !mex_claim(w, v->list->items, v->list->nr_items) ||
			!mex_write32(w, v->list->nr_items))
			return false;
		for (uint32_t i = 0; i < v->list->nr_items; i++) {
			if (!mex_write32(w, v->list->items[i].size) ||
				!mex_write_value(w, &v->list->items[i].value, depth + 1))
				return false;
		}
		return true;
	case EX_TREE:
		return mex_claim(w, v->tree, 1) && mex_write_tree(w, v->tree, depth + 1);
	default: return false;
	}
}

static void mex_writer_clear(struct mex_writer *w)
{
	free(w->data); free(w->owners.entries); free(w->objects.entries); free(w->schemas);
}

static bool mex_pack(const struct ex *source, unsigned char **data, size_t *size)
{
	if (!source || !data || !size || source->nr_blocks > MEX_MAX_NODES)
		return false;
	struct mex_writer w = {0};
	bool ok = mex_claim(&w, source, 1) && mex_claim(&w, source->blocks, source->nr_blocks) &&
		mex_write32(&w, source->nr_blocks);
	for (uint32_t i = 0; ok && i < source->nr_blocks; i++) {
		const struct ex_block *block = &source->blocks[i];
		ok = ++w.nodes <= MEX_MAX_NODES && mex_write64(&w, (uint64_t)block->size) &&
			mex_write_string(&w, block->name) && mex_write_value(&w, &block->val, 0);
	}
	unsigned char *image = NULL;
	if (ok) {
		uLongf compressed = compressBound((uLong)w.length);
		size_t bytes = (size_t)compressed + MEX_HEADER_SIZE;
		ok = bytes <= MEX_MAX_IMAGE && bytes <= MEX_MAX_ALLOC - w.mem.used;
		if (ok) {
			image = malloc(bytes);
			ok = image && compress2(image + MEX_HEADER_SIZE, &compressed, w.data, (uLong)w.length, Z_BEST_SPEED) == Z_OK;
		}
		if (ok) {
			mex_put32(image, 0x3158454d);
			mex_put32(image + 4, 1);
			mex_put32(image + 8, (uint32_t)w.length);
			mex_put32(image + 12, (uint32_t)compressed);
			mex_put32(image + 16, (uint32_t)crc32(0, w.data, (uInt)w.length));
			mex_put32(image + 20, 0);
			*data = image;
			*size = (size_t)compressed + MEX_HEADER_SIZE;
		} else {
			free(image);
		}
	}
	mex_writer_clear(&w);
	return ok;
}

static bool mex_read(struct mex_reader *r, void *p, size_t count)
{
	if (count > r->length - r->offset)
		return false;
	if (count)
		memcpy(p, r->data + r->offset, count);
	r->offset += count;
	return true;
}

static bool mex_read32(struct mex_reader *r, uint32_t *x)
{
	unsigned char p[4];
	if (!mex_read(r, p, 4))
		return false;
	*x = mex_get32(p);
	return true;
}

static bool mex_read64(struct mex_reader *r, size_t *x)
{
	uint32_t lo, hi;
	if (!mex_read32(r, &lo) || !mex_read32(r, &hi))
		return false;
	uint64_t value = (uint64_t)lo | (uint64_t)hi << 32;
	if (value > SIZE_MAX)
		return false;
	*x = (size_t)value;
	return true;
}

static bool mex_read_count(struct mex_reader *r, uint32_t *count, size_t minimum)
{
	return mex_read32(r, count) && *count <= MEX_MAX_NODES &&
		(!minimum || *count <= (r->length - r->offset) / minimum);
}

static void *mex_allocate(struct mex_reader *r, size_t count, size_t width)
{
	size_t bytes;
	if (!count || !mex_size_product(count, width, &bytes))
		return NULL;
	if (r->nr_allocations == r->allocation_capacity) {
		size_t cap = r->allocation_capacity ? r->allocation_capacity * 2 : 128;
		void **q = mex_resize(&r->mem, r->allocations, r->allocation_capacity * sizeof(void*), cap * sizeof(void*));
		if (!q)
			return NULL;
		r->allocations = q;
		r->allocation_capacity = cap;
	}
	if (bytes > MEX_MAX_ALLOC - r->mem.used)
		return NULL;
	void *p = calloc(1, bytes);
	if (!p)
		return NULL;
	r->mem.used += bytes;
	r->allocations[r->nr_allocations++] = p;
	return p;
}

static bool mex_read_string(struct mex_reader *r, struct string **out)
{
	uint32_t len;
	if (!mex_read32(r, &len) || len > INT_MAX || len > r->length - r->offset)
		return false;
	struct string *s = mex_allocate(r, sizeof(*s) + (size_t)len + 1, 1);
	if (!s)
		return false;
	s->size = (int)len;
	s->ref = 1;
	if (!mex_read(r, s->text, len))
		return false;
	*out = s;
	return true;
}

static bool mex_read_value(struct mex_reader *, struct ex_value *, unsigned);
static bool mex_read_tree(struct mex_reader *, struct ex_tree *, unsigned);

static bool mex_read_schema(struct mex_reader *r, struct ex_field **fields, uint32_t *count,
		unsigned depth, uint32_t maximum)
{
	if (depth > MEX_MAX_DEPTH || !mex_read_count(r, count, 20) || *count > maximum)
		return false;
	if (!*count) {
		*fields = NULL;
		return true;
	}
	uint32_t id, expected;
	if (!mex_read32(r, &id) || id != r->nr_schemas + 1)
		return false;
	*fields = mex_allocate(r, *count, sizeof(**fields));
	if (!*fields || !mex_schema_slot(&r->mem, &r->schemas, &r->nr_schemas, &r->schema_capacity,
			*fields, *count, &expected) || expected != id)
		return false;
	for (uint32_t i = 0; i < *count; i++) {
		struct ex_field *f = &(*fields)[i];
		uint32_t type, has_value, is_index;
		if (++r->nodes > MEX_MAX_NODES || !mex_read32(r, &type) || type < EX_INT || type > EX_TABLE ||
			!mex_read_string(r, &f->name) || !mex_read32(r, &has_value) || !mex_read32(r, &is_index))
			return false;
		f->type = (enum ex_value_type)type;
		memcpy(&f->has_value, &has_value, sizeof(has_value));
		memcpy(&f->is_index, &is_index, sizeof(is_index));
		if (f->has_value && (!mex_read_value(r, &f->value, depth + 1) || f->value.type != f->type))
			return false;
		if (!mex_read_schema(r, &f->subfields, &f->nr_subfields, depth + 1, 255) ||
			f->nr_subfields > 255 || (f->type != EX_TABLE && f->nr_subfields))
			return false;
	}
	r->schemas[id - 1].ready = true;
	return true;
}

static bool mex_read_table(struct mex_reader *r, struct ex_table **out, unsigned depth)
{
	uint32_t borrowed, count;
	if (depth > MEX_MAX_DEPTH || !mex_read32(r, &borrowed) || borrowed > 1 ||
		!mex_read_count(r, &count, 0))
		return false;
	struct ex_table *t = mex_allocate(r, 1, sizeof(*t));
	if (!t)
		return false;
	t->nr_fields = count;
	t->borrowed_fields = borrowed;
	if (borrowed) {
		uint32_t id;
		if (!mex_read32(r, &id))
			return false;
		if (count) {
			if (!id || id > r->nr_schemas || !r->schemas[id - 1].ready || r->schemas[id - 1].count != count)
				return false;
			t->fields = r->schemas[id - 1].fields;
		} else if (id) {
			return false;
		}
	} else {
		uint32_t actual;
		if (!mex_read_schema(r, &t->fields, &actual, depth + 1, MEX_MAX_NODES) || actual != count)
			return false;
	}
	if (!mex_read_count(r, &t->nr_rows, 0) || !mex_read_count(r, &t->nr_columns, 0) ||
		t->nr_columns != count || (uint64_t)t->nr_rows * count > MEX_MAX_NODES ||
		(uint64_t)t->nr_rows * count > (r->length - r->offset) / 8)
		return false;
	if (t->nr_rows && !(t->rows = mex_allocate(r, t->nr_rows, sizeof(*t->rows))))
		return false;
	for (uint32_t i = 0; i < t->nr_rows; i++) {
		if (count && !(t->rows[i] = mex_allocate(r, count, sizeof(**t->rows))))
			return false;
		for (uint32_t j = 0; j < count; j++) {
			if (!mex_read_value(r, &t->rows[i][j], depth + 1))
				return false;
		}
	}
	*out = t;
	return true;
}

static bool mex_read_tree(struct mex_reader *r, struct ex_tree *t, unsigned depth)
{
	uint32_t leaf;
	if (depth > MEX_MAX_DEPTH || ++r->nodes > MEX_MAX_NODES || !mex_read_string(r, &t->name) ||
		!mex_read32(r, &leaf) || leaf > 1)
		return false;
	t->is_leaf = leaf;
	if (leaf)
		return mex_read32(r, &t->leaf.size) && mex_read_string(r, &t->leaf.name) &&
			mex_read_value(r, &t->leaf.value, depth + 1);
	if (!mex_read_count(r, &t->nr_children, 12))
		return false;
	if (t->nr_children && (!(t->children = mex_allocate(r, t->nr_children, sizeof(*t->children))) ||
		!(t->_children = mex_allocate(r, t->nr_children, sizeof(*t->_children)))))
		return false;
	for (uint32_t i = 0; i < t->nr_children; i++) {
		t->_children[i].type = EX_TREE;
		t->_children[i].tree = &t->children[i];
		if (!mex_read_tree(r, &t->children[i], depth + 1))
			return false;
	}
	return true;
}

static bool mex_read_value(struct mex_reader *r, struct ex_value *v, unsigned depth)
{
	uint32_t type;
	if (depth > MEX_MAX_DEPTH || ++r->nodes > MEX_MAX_NODES || !mex_read32(r, &type))
		return false;
	v->type = (enum ex_value_type)type;
	switch (type) {
	case EX_INT: {
		uint32_t x;
		if (!mex_read32(r, &x)) return false;
		memcpy(&v->i, &x, sizeof(x)); return true;
	}
	case EX_FLOAT: {
		uint32_t x;
		if (!mex_read32(r, &x)) return false;
		memcpy(&v->f, &x, sizeof(x)); return true;
	}
	case EX_STRING: return mex_read_string(r, &v->s);
	case EX_TABLE: return mex_read_table(r, &v->t, depth + 1);
	case EX_LIST:
		v->list = mex_allocate(r, 1, sizeof(*v->list));
		if (!v->list || !mex_read_count(r, &v->list->nr_items, 12))
			return false;
		if (v->list->nr_items && !(v->list->items = mex_allocate(r, v->list->nr_items, sizeof(*v->list->items))))
			return false;
		for (uint32_t i = 0; i < v->list->nr_items; i++) {
			if (!mex_read32(r, &v->list->items[i].size) ||
				!mex_read_value(r, &v->list->items[i].value, depth + 1))
				return false;
		}
		return true;
	case EX_TREE:
		v->tree = mex_allocate(r, 1, sizeof(*v->tree));
		return v->tree && mex_read_tree(r, v->tree, depth + 1);
	default: return false;
	}
}

static struct ex *mex_unpack(const unsigned char *data, size_t size)
{
	if (!data || size < MEX_HEADER_SIZE || size > MEX_MAX_IMAGE || mex_get32(data) != 0x3158454d ||
		mex_get32(data + 4) != 1 || mex_get32(data + 20))
		return NULL;
	uint32_t raw_size = mex_get32(data + 8), compressed_size = mex_get32(data + 12);
	if (raw_size < 4 || raw_size > MEX_MAX_RAW || !compressed_size ||
		(size_t)compressed_size != size - MEX_HEADER_SIZE)
		return NULL;
	unsigned char *raw = malloc(raw_size);
	if (!raw)
		return NULL;
	z_stream stream = {0};
	stream.next_in = (Bytef*)(data + MEX_HEADER_SIZE);
	stream.avail_in = compressed_size;
	stream.next_out = raw;
	stream.avail_out = raw_size;
	bool ok = inflateInit(&stream) == Z_OK;
	if (ok) {
		int status = inflate(&stream, Z_FINISH);
		ok = status == Z_STREAM_END && stream.total_out == raw_size && stream.total_in == compressed_size;
		inflateEnd(&stream);
	}
	if (!ok || crc32(0, raw, raw_size) != mex_get32(data + 16)) {
		free(raw);
		return NULL;
	}
	struct mex_reader r = { .mem = {raw_size}, .data = raw, .length = raw_size };
	struct ex *candidate = mex_allocate(&r, 1, sizeof(*candidate));
	ok = candidate && mex_read_count(&r, &candidate->nr_blocks, 20);
	if (ok && candidate->nr_blocks)
		ok = (candidate->blocks = mex_allocate(&r, candidate->nr_blocks, sizeof(*candidate->blocks))) != NULL;
	for (uint32_t i = 0; ok && i < candidate->nr_blocks; i++) {
		struct ex_block *block = &candidate->blocks[i];
		ok = ++r.nodes <= MEX_MAX_NODES && mex_read64(&r, &block->size) &&
			mex_read_string(&r, &block->name) && mex_read_value(&r, &block->val, 0);
	}
	ok = ok && r.offset == r.length;
	if (!ok) {
		for (size_t i = 0; i < r.nr_allocations; i++)
			free(r.allocations[i]);
		candidate = NULL;
	}
	free(r.allocations);
	free(r.schemas);
	free(raw);
	return candidate;
}

#endif
