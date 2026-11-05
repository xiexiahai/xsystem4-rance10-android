/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Include after parts_internal.h. Read-only R10 AIN12 capability bridge.
 * Unknown metadata preserves the existing modern manual-drag policy. Once the
 * schema is recognized, missing/stale native-to-script routes have no drag
 * capability. Query the live table: SetDelegateIndex precedes registration.
 * No heap references, delegate GC, cached pages, or script calls are made. */
#include <string.h>
#include "system4/ain.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

static bool parts_drag_name_is(const char *actual, const char *expected)
{
    return actual && !strcmp(actual, expected);
}

static bool parts_drag_member_is(const struct ain_variable *member,
        const char *name, enum ain_data_type type, int structure)
{
    return parts_drag_name_is(member->name, name) && member->type.data == type &&
        member->type.struc == structure && member->type.rank == 0 && !member->type.array_type;
}

static bool parts_drag_crc_is(char *name, int32_t crc)
{
    int function = ain_get_function(ain, name);
    return function > 0 && function < ain->nr_functions &&
        parts_drag_name_is(ain->functions[function].name, name) &&
        ain->functions[function].crc == crc;
}

static bool parts_drag_delegate_type_is(const struct ain_variable *member,
        const char *name, const char *delegate_name, int arguments)
{
    const struct ain_type *type = &member->type;
    if (!parts_drag_name_is(member->name, name) || type->data != AIN_DELEGATE ||
            type->rank || type->array_type || type->struc < 0 || type->struc >= ain->nr_delegates)
        return false;
    const struct ain_function_type *delegate = &ain->delegates[type->struc];
    if (!parts_drag_name_is(delegate->name, delegate_name) || delegate->return_type.data != AIN_VOID ||
            delegate->return_type.struc != -1 || delegate->return_type.rank || delegate->return_type.array_type ||
            delegate->nr_arguments != arguments || delegate->nr_variables < arguments || !delegate->variables)
        return false;
    for (int i = 0; i < arguments; i++) {
        const struct ain_type *arg = &delegate->variables[i].type;
        enum ain_data_type expected = arguments == 2 && i == 1 ? AIN_BOOL : AIN_INT;
        if (arg->data != expected || arg->struc != -1 || arg->rank || arg->array_type) return false;
    }
    return true;
}

/* Resolve by names, then revalidate the metadata on every query. In particular,
 * replacing/mutating an AIN at the same address cannot reuse cached indices. */
static bool parts_drag_r10_schema(int *global_out, int *manager_out, int *functions_out)
{
    if (!ain || ain->version != 12 || !ain->globals || !ain->structures ||
            !ain->functions || !ain->delegates || !ain->_func_ht || !ain->_struct_ht)
        return false;
    int global = ain_get_global(ain, "parts::detail::g_PartsMessageManager");
    int manager = ain_get_struct(ain, "parts::detail::CPartsMessageManager");
    int functions = ain_get_struct(ain, "parts::detail::CPartsFunctionSet");
    if (global < 0 || global >= ain->nr_globals || manager < 0 || manager >= ain->nr_structures ||
            functions < 0 || functions >= ain->nr_structures)
        return false;
    const struct ain_struct *m = &ain->structures[manager], *f = &ain->structures[functions];
    if (!parts_drag_name_is(m->name, "parts::detail::CPartsMessageManager") ||
            !parts_drag_name_is(f->name, "parts::detail::CPartsFunctionSet") ||
            m->nr_members != 4 || f->nr_members != 42 || !m->members || !f->members ||
            !parts_drag_member_is(&ain->globals[global], "parts::detail::g_PartsMessageManager", AIN_STRUCT, manager) ||
            !parts_drag_member_is(&m->members[0], "m_WholeFunctionSet", AIN_STRUCT, functions) ||
            !parts_drag_member_is(&m->members[3], "m_ErrorFunctionSet", AIN_STRUCT, functions) ||
            !parts_drag_member_is(&f->members[0], "m_Using", AIN_BOOL, -1) ||
            !parts_drag_member_is(&f->members[1], "m_PartsNumber", AIN_INT, -1) ||
            !parts_drag_member_is(&f->members[2], "m_Used", AIN_BOOL, -1))
        return false;
    const struct ain_type *list = &m->members[1].type, *unused = &m->members[2].type;
    if (!parts_drag_name_is(m->members[1].name, "m_FunctionSetList") ||
            list->data != AIN_ARRAY || list->struc != functions || list->rank != 1 || !list->array_type ||
            list->array_type->data != AIN_REF_STRUCT || list->array_type->struc != functions ||
            list->array_type->rank || list->array_type->array_type ||
            !parts_drag_name_is(m->members[2].name, "m_UnusedIndexList") ||
            unused->data != AIN_ARRAY || unused->struc != -1 || unused->rank != 1 || !unused->array_type ||
            unused->array_type->data != AIN_INT || unused->array_type->struc != -1 ||
            unused->array_type->rank || unused->array_type->array_type ||
            !parts_drag_delegate_type_is(&f->members[16], "<DragBeginEvent>", "DG_DragBeginHandler", 1) ||
            !parts_drag_delegate_type_is(&f->members[17], "<DragingEvent>", "DG_DragingHandler", 5) ||
            !parts_drag_delegate_type_is(&f->members[18], "<DragEndEvent>", "DG_DragEndHandler", 2) ||
            !parts_drag_crc_is("parts::detail::CPartsMessageManager@GetFunctionSetWithoutCreate", -887087771) ||
            !parts_drag_crc_is("parts::detail::CPartsFunctionSet@CallFunctionDragBegin", 1465532907) ||
            !parts_drag_crc_is("parts::detail::CPartsFunctionSet@CallFunctionDraging", -632576745) ||
            !parts_drag_crc_is("parts::detail::CPartsFunctionSet@CallFunctionDragEnd", -1543838069))
        return false;
    *global_out = global; *manager_out = manager; *functions_out = functions;
    return true;
}

static const struct page *parts_drag_page(int slot, enum page_type type)
{
    if (!heap || !page_index_valid(slot)) return NULL;
    const struct page *page = heap[slot].page;
    return page && page->type == type ? page : NULL;
}

static bool parts_drag_has_live_delegate(int slot)
{
    const struct page *delegate = parts_drag_page(slot, DELEGATE_PAGE);
    if (!delegate || delegate->nr_vars <= 0 || delegate->nr_vars % 3) return false;
    for (int i = 0; i < delegate->nr_vars; i += 3) {
        int object = delegate->values[i].i, function = delegate->values[i + 1].i;
        uint32_t sequence = (uint32_t)delegate->values[i + 2].i;
        if (function <= 0 || function >= ain->nr_functions) continue;
        /* Existing delegates allow a static callback as object=-1, seq=0.
         * Other invalid object numbers with seq=0 are not static callbacks. */
        if (object == -1) { if (!sequence) return true; continue; }
        const struct page *instance = parts_drag_page(object, STRUCT_PAGE);
        if (instance && instance->index >= 0 && instance->index < ain->nr_structures &&
                instance->nr_vars == ain->structures[instance->index].nr_members &&
                heap_get_seq(object) == sequence)
            return true;
    }
    return false;
}

/* Used only with schema indices freshly checked in the current native input
 * call. The caller retains no heap pages and must not reuse across VM work. */
static bool parts_drag_enabled_in_schema(struct parts *p,
        int global, int manager_type, int function_type)
{
    if (!p) return false;
    if (p->no <= 0 || p->delegate_index < 0) return false;
    const struct page *globals = parts_drag_page(0, GLOBAL_PAGE);
    if (!globals || globals->nr_vars != ain->nr_globals || global >= globals->nr_vars) return false;
    const struct page *manager = parts_drag_page(globals->values[global].i, STRUCT_PAGE);
    if (!manager || manager->index != manager_type || manager->nr_vars != 4) return false;
    const struct page *list = parts_drag_page(manager->values[1].i, ARRAY_PAGE);
    if (!list || list->a_type != AIN_ARRAY || list->array.rank != 1 ||
            list->array.struct_type != function_type || !list->element_type ||
            list->element_type->data != AIN_REF_STRUCT || list->element_type->struc != function_type ||
            list->element_type->rank || list->element_type->array_type ||
            p->delegate_index >= list->nr_vars)
        return false;
    const struct page *functions = parts_drag_page(list->values[p->delegate_index].i, STRUCT_PAGE);
    if (!functions || functions->index != function_type || functions->nr_vars != 42 ||
            !functions->values[0].i || functions->values[1].i != p->no)
        return false;
    return parts_drag_has_live_delegate(functions->values[16].i) ||
        parts_drag_has_live_delegate(functions->values[17].i) ||
        parts_drag_has_live_delegate(functions->values[18].i);
}

static bool parts_ain12_manual_drag_enabled(struct parts *p)
{
    if (!p) return false;
    int global, manager_type, function_type;
    if (!parts_drag_r10_schema(&global, &manager_type, &function_type)) return true;
    return parts_drag_enabled_in_schema(p, global, manager_type, function_type);
}
