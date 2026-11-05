/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "system4/utfsjis.h"
/* Invalidate stale handles after XPE restoration or controller destruction.
 * The script manager recreates windows only when its cached name is empty.
 * Limit this bridge to the verified AIN12 schema and method CRC. */
static unsigned reconcile_rance10_message_windows(void)
{
    if (!ain || ain->version != 12 || !heap_size || !ain->nr_functions) return 0;
    int method = ain_get_function(ain, "message::detail::CMessageWindow@GetPartsNumber");
    if (method < 0 || ain->functions[method].crc != (uint32_t)-63763144)
        return 0;
    int type = ain_get_struct(ain, "message::detail::CMessageWindow");
    if (type < 0) return 0;
    struct ain_struct *schema = &ain->structures[type];
    if (schema->nr_members != 5 ||
        strcmp(schema->members[0].name, "m_ActivityName") || schema->members[0].type.data != AIN_STRING ||
        strcmp(schema->members[1].name, "m_MessageWindowName") || schema->members[1].type.data != AIN_STRING ||
        strcmp(schema->members[2].name, "m_dgLoadedObject") || schema->members[2].type.data != AIN_DELEGATE ||
        strcmp(schema->members[3].name, "m_dgReleasingObject") || schema->members[3].type.data != AIN_DELEGATE ||
        strcmp(schema->members[4].name, "m_OffsetZ") || schema->members[4].type.data != AIN_INT)
        return 0;
    char *encoded_root = utf2sjis("ルートパーツ", 0);
    struct string *root_name = cstr_to_string(encoded_root);
    free(encoded_root);
    unsigned changed = 0;
    for (size_t i = 0; i < heap_size; i++) {
        if (!page_index_valid(i)) continue;
        struct page *window = heap[i].page;
        if (!window || window->type != STRUCT_PAGE || window->index != type || window->nr_vars != 5)
            continue;
        int slot = window->values[0].i;
        if (!string_index_valid(slot)) continue;
        struct string *name = heap[slot].s;
        if (!name || !name->size) continue;
        int part_slot = window->values[1].i;
        struct string *part_name = string_index_valid(part_slot) ? heap[part_slot].s : NULL;
        int number = part_name && part_name->size ? PE_GetActivityPartsNumber(name, part_name) : 0;
        int root_number = PE_GetActivityPartsNumber(name, root_name);
        if (number > 0 && PE_IsExist(number) && root_number > 0 && PE_IsExist(root_number)) continue;
        /* Filtered XPE images can retain an activity name with no parts.
         * Remove only that empty shell so the script can load the same name;
         * an unrelated deliberately empty activity remains a valid activity. */
        if (PE_IsExistActivity(name) && PE_NumofActivityParts(name) == 0) {
            struct page *delegates = NULL;
            PE_ReleaseActivity(name, &delegates);
            if (delegates) { delete_page_vars(delegates); free_page(delegates); }
        } else {
            /* A filtered image may retain the body but lose its root. Keep
             * callback ownership with the old layer, but hide its residual
             * image while the original manager creates a complete window. */
            struct string *part = string_ref(&EMPTY_STRING);
            int count = PE_NumofActivityParts(name), part_number;
            for (int j = 0; j < count; j++)
                if (PE_GetActivityParts(j, name, &part, &part_number) && PE_IsExist(part_number))
                    PE_SetShow(part_number, false);
            free_string(part);
        }
        /* Replace this variable's slot, preserving any independently aliased string. */
        window->values[0].i = heap_alloc_string(string_ref(&EMPTY_STRING));
        heap_unref(slot);
        changed++;
    }
    free_string(root_name);
    if (changed) {
        struct parts *p;
        PARTS_LIST_FOREACH(p) {
            if (!parts_activity_has_part(p->no) &&
                (p->message_window || p->states[0].type == PARTS_MESSAGE_WINDOW))
                PE_SetShow(p->no, false);
        }
        if (getenv("XSYSTEM4_TRACE_EX_SNAPSHOT"))
            fprintf(stderr, "[message-restore] scheduled %u missing activity windows for script recreation\n", changed);
    }
    return changed;
}
