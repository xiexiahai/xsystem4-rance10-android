#ifndef RESUME12_TYPES_INC
#define RESUME12_TYPES_INC

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <math.h>
#include "cJSON.h"
#include "system4/ain.h"

#define R12_TYPE_MAX_DEPTH 32

static bool r12_type_supported(int data)
{
	switch (data) {
	case AIN_VOID:
	case AIN_INT:
	case AIN_FLOAT:
	case AIN_STRING:
	case AIN_STRUCT:
	case AIN_FUNC_TYPE:
	case AIN_BOOL:
	case AIN_LONG_INT:
	case AIN_DELEGATE:
	case AIN_ARRAY:
	case AIN_REF_ARRAY:
	case AIN_IFACE:
	case AIN_ENUM2:
	case AIN_ENUM:
	case AIN_REF_INT:
	case AIN_REF_FLOAT:
	case AIN_REF_STRING:
	case AIN_REF_STRUCT:
	case AIN_REF_FUNC_TYPE:
	case AIN_REF_BOOL:
	case AIN_REF_LONG_INT:
	case AIN_REF_DELEGATE:
		return true;
	default:
		return false;
	}
}

static bool r12_type_is_array(int data)
{
	return data == AIN_ARRAY || data == AIN_REF_ARRAY;
}

static bool r12_type_needs_struc(int data)
{
	switch (data) {
	case AIN_STRUCT:
	case AIN_IFACE:
	case AIN_ENUM:
	case AIN_ENUM2:
	case AIN_FUNC_TYPE:
	case AIN_DELEGATE:
	case AIN_REF_STRUCT:
	case AIN_REF_FUNC_TYPE:
	case AIN_REF_DELEGATE:
		return true;
	default:
		return false;
	}
}

static bool r12_json_add_number(cJSON *array, int value)
{
	cJSON *number = cJSON_CreateNumber(value);
	if (!number)
		return false;
	cJSON_AddItemToArray(array, number);
	return true;
}

static cJSON *r12_encode_node(const struct ain_type *type, int depth,
		const struct ain_type **seen)
{
	if (!type || depth >= R12_TYPE_MAX_DEPTH)
		return NULL;
	for (int i = 0; i < depth; i++)
		if (seen[i] == type)
			return NULL;
	seen[depth] = type;
	if (!r12_type_supported(type->data))
		return NULL;

	bool array = r12_type_is_array(type->data);
	if (array) {
		if (type->rank < 1 || type->rank > 4 || !type->array_type)
			return NULL;
	} else if (type->rank != 0 || type->array_type) {
		return NULL;
	}

	cJSON *json = cJSON_CreateArray();
	if (!json)
		return NULL;
	if (!r12_json_add_number(json, type->data) ||
			!r12_json_add_number(json, type->struc) ||
			!r12_json_add_number(json, type->rank)) {
		cJSON_Delete(json);
		return NULL;
	}

	if (array) {
		cJSON *child = r12_encode_node(type->array_type, depth + 1, seen);
		if (!child) {
			cJSON_Delete(json);
			return NULL;
		}
		cJSON_AddItemToArray(json, child);
	} else {
		cJSON *null = cJSON_CreateNull();
		if (!null) {
			cJSON_Delete(json);
			return NULL;
		}
		cJSON_AddItemToArray(json, null);
	}
	return json;
}

static cJSON *r12_type_encode(const struct ain_type *type)
{
	const struct ain_type *seen[R12_TYPE_MAX_DEPTH] = { 0 };
	return r12_encode_node(type, 0, seen);
}

static bool r12_json_read_int32(const cJSON *item, int32_t *out)
{
	if (!item || !cJSON_IsNumber(item))
		return false;
	double value = item->valuedouble;
	if (!isfinite(value) || value < (double)INT32_MIN || value > (double)INT32_MAX)
		return false;
	int32_t number = (int32_t)value;
	if ((double)number != value)
		return false;
	*out = number;
	return true;
}

struct r12_parsed_type {
	struct ain_type nodes[R12_TYPE_MAX_DEPTH];
	size_t used;
};

static const struct ain_type *r12_parse_node(const cJSON *json, int depth,
		struct r12_parsed_type *parsed)
{
	if (!json || depth >= R12_TYPE_MAX_DEPTH ||
			parsed->used >= R12_TYPE_MAX_DEPTH)
		return NULL;
	if (!cJSON_IsArray(json) || cJSON_GetArraySize(json) != 4)
		return NULL;

	cJSON *data_json = cJSON_GetArrayItem(json, 0);
	cJSON *struc_json = cJSON_GetArrayItem(json, 1);
	cJSON *rank_json = cJSON_GetArrayItem(json, 2);
	cJSON *child = cJSON_GetArrayItem(json, 3);
	int32_t data, struc, rank;
	if (!r12_json_read_int32(data_json, &data) ||
			!r12_json_read_int32(struc_json, &struc) ||
			!r12_json_read_int32(rank_json, &rank) ||
			!r12_type_supported(data))
		return NULL;

	bool array = r12_type_is_array(data);
	if (array) {
		if (rank < 1 || rank > 4 || !cJSON_IsArray(child))
			return NULL;
	} else if (rank != 0 || !cJSON_IsNull(child)) {
		return NULL;
	}

	size_t index = parsed->used++;
	struct ain_type *node = &parsed->nodes[index];
	node->data = (enum ain_data_type)data;
	node->struc = struc;
	node->rank = rank;
	node->array_type = NULL;

	if (array) {
		const struct ain_type *child_type = r12_parse_node(child, depth + 1, parsed);
		if (!child_type)
			return NULL;
		node->array_type = (struct ain_type *)child_type;
	}
	return node;
}

static bool r12_type_matches(const struct ain_type *candidate,
		const struct ain_type *wanted, int depth)
{
	if (!candidate || !wanted || depth >= R12_TYPE_MAX_DEPTH)
		return false;
	if (candidate->data != wanted->data ||
			!r12_type_supported(candidate->data))
		return false;

	if (r12_type_is_array(candidate->data)) {
		if (candidate->rank != wanted->rank ||
				candidate->rank < 1 || candidate->rank > 4 ||
				!candidate->array_type || !wanted->array_type)
			return false;
		return r12_type_matches(candidate->array_type,
			wanted->array_type, depth + 1);
	}

	if (candidate->rank != 0 || wanted->rank != 0)
		return false;
	if (r12_type_needs_struc(candidate->data) &&
			candidate->struc != wanted->struc)
		return false;
	return true;
}

static const struct ain_type *r12_find_in_type(const struct ain_type *type,
		const struct ain_type *wanted, int depth)
{
	if (!type || depth > R12_TYPE_MAX_DEPTH)
		return NULL;
	if (r12_type_matches(type, wanted, 0))
		return type;
	if (r12_type_is_array(type->data) && type->array_type)
		return r12_find_in_type(type->array_type, wanted, depth + 1);
	return NULL;
}

static const struct ain_type *r12_find_program(const struct ain *program,
		const struct ain_type *wanted)
{
	if (!program || !wanted)
		return NULL;

	for (int i = 0; i < program->nr_globals; i++) {
		const struct ain_type *found =
			r12_find_in_type(&program->globals[i].type, wanted, 0);
		if (found)
			return found;
	}
	for (int i = 0; i < program->nr_functions; i++) {
		const struct ain_function *function = &program->functions[i];
		const struct ain_type *found =
			r12_find_in_type(&function->return_type, wanted, 0);
		if (found)
			return found;
		for (int j = 0; j < function->nr_vars; j++) {
			found = r12_find_in_type(&function->vars[j].type, wanted, 0);
			if (found)
				return found;
		}
	}
	for (int i = 0; i < program->nr_structures; i++) {
		const struct ain_struct *structure = &program->structures[i];
		for (int j = 0; j < structure->nr_members; j++) {
			const struct ain_type *found = r12_find_in_type(
				&structure->members[j].type, wanted, 0);
			if (found)
				return found;
		}
	}
	for (int i = 0; i < program->nr_function_types; i++) {
		const struct ain_function_type *function_type =
			&program->function_types[i];
		const struct ain_type *found =
			r12_find_in_type(&function_type->return_type, wanted, 0);
		if (found)
			return found;
		for (int j = 0; j < function_type->nr_variables; j++) {
			found = r12_find_in_type(&function_type->variables[j].type,
				wanted, 0);
			if (found)
				return found;
		}
	}
	for (int i = 0; i < program->nr_delegates; i++) {
		const struct ain_function_type *delegate = &program->delegates[i];
		const struct ain_type *found =
			r12_find_in_type(&delegate->return_type, wanted, 0);
		if (found)
			return found;
		for (int j = 0; j < delegate->nr_variables; j++) {
			found = r12_find_in_type(&delegate->variables[j].type,
				wanted, 0);
			if (found)
				return found;
		}
	}
	return NULL;
}

static const struct ain_type *r12_type_resolve(const cJSON *json,
		const struct ain *program)
{
	if (!json || !program)
		return NULL;
	struct r12_parsed_type parsed;
	parsed.used = 0;
	const struct ain_type *wanted = r12_parse_node(json, 0, &parsed);
	if (!wanted)
		return NULL;
	return r12_find_program(program, wanted);
}

#endif /* RESUME12_TYPES_INC */
