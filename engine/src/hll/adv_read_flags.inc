#ifndef ADV_READ_FLAGS_INC
#define ADV_READ_FLAGS_INC

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "system4/string.h"

/*
 * Standalone MsgSkip read-key side table.
 *
 * This is a project-owned sidecar model, not the native MsgSkip bitmap or
 * save format.  One table owns all key bytes and is safe to zero-initialize.
 */

#ifndef ADV_READ_MAX_RECORDS
#define ADV_READ_MAX_RECORDS 65536u
#endif
#ifndef ADV_READ_MAX_KEY_BYTES
#define ADV_READ_MAX_KEY_BYTES 4096u
#endif
#ifndef ADV_READ_MAX_TOTAL_BYTES
#define ADV_READ_MAX_TOTAL_BYTES (8u * 1024u * 1024u)
#endif

struct adv_read_record {
	uint8_t *name;
	uint32_t name_len;
	int32_t version;
	int32_t step;
	bool read;
};

struct adv_read_table {
	struct adv_read_record *records;
	size_t count;
	size_t capacity;
	size_t total_key_bytes;
};

static uint32_t adv_read_get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void adv_read_put_le32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}

static size_t adv_read_find(const struct adv_read_table *table,
		const uint8_t *name, uint32_t name_len, int32_t version, int32_t step)
{
	if (!table || !name)
		return SIZE_MAX;
	for (size_t i = 0; i < table->count; i++) {
		const struct adv_read_record *record = &table->records[i];
		if (record->name_len == name_len &&
				record->version == version && record->step == step &&
				!memcmp(record->name, name, name_len))
			return i;
	}
	return SIZE_MAX;
}

static bool adv_read_append(struct adv_read_table *table, const uint8_t *name,
		uint32_t name_len, int32_t version, int32_t step)
{
	if (!table || !name || name_len == 0 ||
			name_len > (uint32_t)ADV_READ_MAX_KEY_BYTES)
		return false;
	if (table->count >= (size_t)ADV_READ_MAX_RECORDS)
		return false;
	if ((size_t)name_len > (size_t)ADV_READ_MAX_TOTAL_BYTES -
			table->total_key_bytes)
		return false;
	if (adv_read_find(table, name, name_len, version, step) != SIZE_MAX)
		return false;

	uint8_t *copy = malloc(name_len);
	if (!copy)
		return false;
	memcpy(copy, name, name_len);

	if (table->count == table->capacity) {
		size_t capacity = table->capacity ? table->capacity * 2 : 8;
		if (capacity > (size_t)ADV_READ_MAX_RECORDS)
			capacity = (size_t)ADV_READ_MAX_RECORDS;
		if (capacity <= table->capacity) {
			free(copy);
			return false;
		}
		struct adv_read_record *records = realloc(table->records,
				capacity * sizeof(*records));
		if (!records) {
			free(copy);
			return false;
		}
		table->records = records;
		table->capacity = capacity;
	}

	struct adv_read_record *record = &table->records[table->count++];
	record->name = copy;
	record->name_len = name_len;
	record->version = version;
	record->step = step;
	record->read = true;
	table->total_key_bytes += name_len;
	return true;
}

/* Returns true whether the key was newly inserted or already present. */
static bool adv_read_set(struct adv_read_table *table, const struct string *name,
		int32_t version, int32_t step)
{
	if (!table || !name || name->size <= 0 ||
			(uint32_t)name->size > (uint32_t)ADV_READ_MAX_KEY_BYTES)
		return false;
	size_t found = adv_read_find(table, (const uint8_t *)name->text,
			(uint32_t)name->size, version, step);
	if (found != SIZE_MAX) {
		table->records[found].read = true;
		return true;
	}
	return adv_read_append(table, (const uint8_t *)name->text,
			(uint32_t)name->size, version, step);
}

/* Unknown keys return false without allocating or inserting anything. */
static bool adv_read_get(const struct adv_read_table *table,
		const struct string *name, int32_t version, int32_t step)
{
	if (!table || !name || name->size <= 0 ||
			(uint32_t)name->size > (uint32_t)ADV_READ_MAX_KEY_BYTES)
		return false;
	size_t found = adv_read_find(table, (const uint8_t *)name->text,
			(uint32_t)name->size, version, step);
	return found != SIZE_MAX && table->records[found].read;
}

static void adv_read_clear(struct adv_read_table *table)
{
	if (!table)
		return;
	for (size_t i = 0; i < table->count; i++)
		free(table->records[i].name);
	free(table->records);
	memset(table, 0, sizeof(*table));
}

/*
 * Project-owned sidecar: "R10READ1" + LE32 count, then
 * LE32 name_len + raw name + LE32 version + LE32 step per record.
 * Insertion order is preserved; duplicate keys are rejected.
 */
static bool adv_read_pack(struct adv_read_table *table, uint8_t **out, size_t *len)
{
	if (out)
		*out = NULL;
	if (len)
		*len = 0;
	if (!table || !out || !len ||
			table->count > (size_t)ADV_READ_MAX_RECORDS)
		return false;
	if (table->count && !table->records)
		return false;

	size_t total = 12;
	for (size_t i = 0; i < table->count; i++) {
		const struct adv_read_record *record = &table->records[i];
		if (!record->name || record->name_len == 0 ||
				record->name_len > (uint32_t)ADV_READ_MAX_KEY_BYTES)
			return false;
		size_t add = 12 + (size_t)record->name_len;
		if (total > SIZE_MAX - add)
			return false;
		total += add;
		if (total > SIZE_MAX - 12)
			return false;
	}

	uint8_t *buffer = malloc(total);
	if (!buffer)
		return false;
	uint8_t *p = buffer;
	memcpy(p, "R10READ1", 8);
	p += 8;
	adv_read_put_le32(p, (uint32_t)table->count);
	p += 4;
	for (size_t i = 0; i < table->count; i++) {
		const struct adv_read_record *record = &table->records[i];
		adv_read_put_le32(p, record->name_len);
		p += 4;
		memcpy(p, record->name, record->name_len);
		p += record->name_len;
		adv_read_put_le32(p, (uint32_t)record->version);
		p += 4;
		adv_read_put_le32(p, (uint32_t)record->step);
		p += 4;
	}
	*out = buffer;
	*len = total;
	return true;
}

/*
 * Builds a temporary table and only replaces the destination after the whole
 * buffer validates.  Every rejected input leaves the destination untouched.
 */
static bool adv_read_unpack(struct adv_read_table *table,
		const uint8_t *data, size_t len)
{
	if (!table || !data || len < 12 || memcmp(data, "R10READ1", 8))
		return false;
	size_t pos = 8;
	uint32_t count = adv_read_get_le32(data + pos);
	pos += 4;
	if (count > (uint32_t)ADV_READ_MAX_RECORDS)
		return false;

	struct adv_read_table temporary;
	memset(&temporary, 0, sizeof(temporary));
	for (uint32_t i = 0; i < count; i++) {
		if (len - pos < 4)
			goto fail;
		uint32_t name_len = adv_read_get_le32(data + pos);
		pos += 4;
		if (name_len == 0 || name_len > (uint32_t)ADV_READ_MAX_KEY_BYTES)
			goto fail;
		if ((size_t)name_len > len - pos)
			goto fail;
		const uint8_t *name = data + pos;
		pos += name_len;
		if (len - pos < 8)
			goto fail;
		int32_t version = (int32_t)adv_read_get_le32(data + pos);
		pos += 4;
		int32_t step = (int32_t)adv_read_get_le32(data + pos);
		pos += 4;
		if (!adv_read_append(&temporary, name, name_len, version, step))
			goto fail;
	}
	if (pos != len)
		goto fail;

	adv_read_clear(table);
	*table = temporary;
	return true;

fail:
	adv_read_clear(&temporary);
	return false;
}

#endif /* ADV_READ_FLAGS_INC */
