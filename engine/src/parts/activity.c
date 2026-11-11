/* Copyright (C) 2026 xsystem4 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdlib.h>
#include <SDL.h>
#include <string.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
#include "parts.h"
#include "rance10_diagnostics.h"
#include "parts_activity.h"
#include "vm/page.h"
#include "parts_internal.h"
#include "../hll/iarray.h"

struct activity_part {
	struct string *name;
	int number;
	bool closes, has_intent;
	int intent_type;
	struct string **destinations;
	int nr_destinations;
};

struct activity {
	struct activity *next;
	struct string *name;
	struct activity_part *parts;
	int nr_parts;
	int *end_keys, nr_end_keys;
	unsigned seq;
};

static struct activity *activities;
static unsigned activity_seq;

static bool named(struct string *name)
{
	return name && name->size;
}

static struct activity *find_activity(struct string *name)
{
	if (!named(name))
		return NULL;
	for (struct activity *a = activities; a; a = a->next)
		if (!strcmp(a->name->text, name->text))
			return a;
	return NULL;
}

static int part_index(struct activity *a, struct string *name)
{
	if (a && named(name))
		for (int i = 0; i < a->nr_parts; i++)
			if (!strcmp(a->parts[i].name->text, name->text))
				return i;
	return -1;
}

static struct activity_part *find_part(struct string *name, struct string *parts_name)
{
	struct activity *a = find_activity(name);
	int i = part_index(a, parts_name);
	return i < 0 ? NULL : &a->parts[i];
}

static void clear_destinations(struct activity_part *p)
{
	for (int i = 0; i < p->nr_destinations; i++)
		free_string(p->destinations[i]);
	free(p->destinations);
	p->destinations = NULL;
	p->nr_destinations = 0;
}

static void remove_part(struct activity *a, int index)
{
	struct activity_part *p = &a->parts[index];
	clear_destinations(p);
	free_string(p->name);
	a->nr_parts--;
	memmove(p, p + 1, (a->nr_parts - index) * sizeof(*p));
}

static void free_activity(struct activity *a)
{
	while (a->nr_parts)
		remove_part(a, a->nr_parts - 1);
	free(a->parts);
	free(a->end_keys);
	free_string(a->name);
	free(a);
}

void PE_ActivityReset(void)
{
	PE_ClearMenuHiddenParts();
	while (activities) {
		struct activity *a = activities;
		activities = a->next;
		free_activity(a);
	}
}

void PE_ActivityPartReleased(int number)
{
	// A controller can release a part without going through ReleaseActivity.
	// Remove weak references before the number is reused for another part.
	for (struct activity *a = activities; a; a = a->next)
		for (int i = a->nr_parts - 1; i >= 0; i--)
			if (a->parts[i].number == number) {
				remove_part(a, i);
			}
}

const char *PE_DebugPartName(int number)
{
    static char buf[256];
    for (struct activity *a = activities; a; a = a->next)
        for (int i = 0; i < a->nr_parts; i++)
            if (a->parts[i].number == number) {
                snprintf(buf, sizeof(buf), "%s/%s",
                    a->name && a->name->text ? a->name->text : "(null)",
                    a->parts[i].name && a->parts[i].name->text ? a->parts[i].name->text : "(null)");
                return buf;
            }
    return "";
}

bool parts_activity_has_part(int number)
{
	for (struct activity *a = activities; a; a = a->next)
		for (int i = 0; i < a->nr_parts; i++)
			if (a->parts[i].number == number) return true;
	return false;
}


int PE_CountActivities(void)
{
	int n = 0;
	for (struct activity *a = activities; a; a = a->next)
		n++;
	return n;
}

bool PE_IsActivityOpen(const char *prefix)
{
	size_t n = strlen(prefix);
	for (struct activity *a = activities; a; a = a->next)
		if (a->name && a->name->text && !strncmp(a->name->text, prefix, n))
			return true;
	return false;
}

bool PE_IsExistActivity(struct string *name)
{
	return find_activity(name) != NULL;
}

/* Rance10 rebuilds UI button activities as fresh instances ("...:N") and does
 * not release the previous instance's button. The stale part then sits on top
 * of the rebuilt screen and swallows clicks (dead delegate). Buttons are
 * single-instance UI, so drop older instances of the same base name. */
static bool release_activity_by_text(const char *text)
{
	struct activity **link = &activities;
	while (*link && (!(*link)->name || !(*link)->name->text || strcmp((*link)->name->text, text)))
		link = &(*link)->next;
	if (!*link)
		return false;
	struct activity *a = *link;
	*link = a->next;
	for (int i = 0; i < a->nr_parts; i++)
		PE_ReleaseParts(a->parts[i].number);
	free_activity(a);
	return true;
}

/* Rance10 rebuilds MessageWindow01_talk and re-creates its hit buttons, but
 * the Game/Component/TextButton instance created for the previous window is
 * never released. Its parts stay on top of the rebuilt window and swallow the
 * MENU click. Drop only prefixed button activities that are older than the
 * window and live in the same controller as the window's parts; the activity
 * name itself is raw SJIS, so match the ASCII prefix only. */
static bool is_component_text_button(const char *name)
{
	static const char prefix[] = "Game/Component/TextButton/";
	return name && !strncmp(name, prefix, sizeof(prefix) - 1);
}

static void release_stale_window_buttons(int controller, unsigned window_seq)
{
	if (controller < 0)
		return;
	for (struct activity *a = activities; a; a = a->next) {
		if (a->seq >= window_seq || !is_component_text_button(a->name->text))
			continue;
		bool same_controller = false;
		for (int i = 0; i < a->nr_parts; i++) {
			struct parts *p = parts_try_get(a->parts[i].number);
			if (p && p->controller_no == controller) { same_controller = true; break; }
		}
		if (!same_controller)
			continue;
		char *text = xstrdup(a->name->text);
		if (getenv("XSYSTEM4_TRACE_INPUT"))
			sys_message("[activity] release-stale-window-button %s\n", text);
		release_activity_by_text(text);
		free(text);
		release_stale_window_buttons(controller, window_seq);
		return;
	}
}

bool PE_CreateActivity(struct string *name)
{
	if (getenv("XSYSTEM4_TRACE_INPUT"))
		sys_message("[activity] create %s%s\n", named(name)?name->text:"(null)",
			find_activity(name) ? " (rejected: exists)" : "");
	if (!named(name) || find_activity(name))
		return false;
	struct activity *a = xcalloc(1, sizeof(*a));
	a->name = string_dup(name);
	a->next = activities;
	activities = a;
	a->seq = ++activity_seq;
	/* The window is visible again: bring back ADV hit layers that the
	 * script erased while it was hidden (hide-window / menu / rebuild). */
	if (!strncmp(a->name->text, "MessageWindow", 13))
		PE_RestoreMenuHiddenParts();
	if (!strncmp(a->name->text, "MessageWindow", 13) || strstr(a->name->text, "Quest"))
		PE_SchedulePartsDump();
	if (!strncmp(a->name->text, "Game/Battle", 11)) {
		PE_LogActivityCreated(a->name->text);
		PE_LogResourceUsage("battle-create");
	}
	return true;
}

bool PE_AddActivityParts(struct string *name, struct string *parts_name, int number)
{
	struct activity *a = find_activity(name);
	if (!a || !named(parts_name) || number <= 0 || !PE_IsExist(number) || part_index(a, parts_name) >= 0)
		return false;
	for (int i = 0; i < a->nr_parts; i++)
		if (a->parts[i].number == number)
			return false;
	a->parts = xrealloc(a->parts, (a->nr_parts + 1) * sizeof(*a->parts));
	a->parts[a->nr_parts++] = (struct activity_part) {
		.name = string_dup(parts_name), .number = number
	};
	/* Stale-button release is disabled: the live button is created before
	 * the window, so sequence order cannot distinguish it from the stale
	 * instance. Kept for reference until a reliable discriminator exists. */
	if (getenv("XSYSTEM4_TRACE_INPUT"))
		sys_message("[activity] add %s/%s number=%d\n", a->name->text, parts_name->text, number);
	return true;
}

bool PE_RemoveActivityParts(struct string *name, struct string *parts_name)
{
	struct activity *a = find_activity(name);
	int index = part_index(a, parts_name);
	if (index < 0)
		return false;
	remove_part(a, index);
	return true;
}

bool PE_RemoveAllActivityParts(struct string *name)
{
	struct activity *a = find_activity(name);
	if (!a)
		return false;
	while (a->nr_parts)
		remove_part(a, a->nr_parts - 1);
	return true;
}

int PE_NumofActivityParts(struct string *name)
{
	struct activity *a = find_activity(name);
	return a ? a->nr_parts : 0;
}

static void assign_string(struct string **out, struct string *value)
{
	struct string *copy = value ? string_dup(value) : cstr_to_string("");
	free_string(*out);
	*out = copy;
}

bool PE_GetActivityParts(int index, struct string *name, struct string **parts_name, int *number)
{
	struct activity *a = find_activity(name);
	if (!a || index < 0 || index >= a->nr_parts)
		return false;
	assign_string(parts_name, a->parts[index].name);
	*number = a->parts[index].number;
	return true;
}

bool PE_IsExistActivityPartsByName(struct string *name, struct string *parts_name)
{
	return find_part(name, parts_name) != NULL;
}

bool PE_IsExistActivityPartsByNumber(struct string *name, int number)
{
	struct activity *a = find_activity(name);
	if (a)
		for (int i = 0; i < a->nr_parts; i++)
			if (a->parts[i].number == number)
				return true;
	return false;
}

int PE_GetActivityPartsNumber(struct string *name, struct string *parts_name)
{
	struct activity_part *p = find_part(name, parts_name);
	return p ? p->number : 0;
}

void PE_GetActivityPartsName(struct string **out, struct string *name, int number)
{
	struct activity *a = find_activity(name);
	if (a)
		for (int i = 0; i < a->nr_parts; i++)
			if (a->parts[i].number == number) {
				assign_string(out, a->parts[i].name);
				return;
			}
	assign_string(out, NULL);
}

bool PE_ReleaseActivity(struct string *name, struct page **delegates)
{
	if (getenv("XSYSTEM4_TRACE_INPUT"))
		sys_message("[activity] release-attempt %s\n", named(name)?name->text:"(null)");
	struct activity **link = &activities;
	while (*link && (!named(name) || strcmp((*link)->name->text, name->text)))
		link = &(*link)->next;
	if (!*link)
		return false;
	struct activity *a = *link;
	if (getenv("XSYSTEM4_TRACE_INPUT"))
		sys_message("[activity] release %s parts=%d\n", named(name)?name->text:"(null)", a->nr_parts);
	*link = a->next; // detach before parts_release notifies other activities
	for (int i = 0; i < a->nr_parts; i++) {
		int number = a->parts[i].number;
		int delegate = PE_GetDelegateIndex(number);
		if (delegate >= 0) {
			bool duplicate = false;
			for (int j = 0; j < i; j++)
				if (PE_GetDelegateIndex(a->parts[j].number) == delegate)
					duplicate = true;
			if (!duplicate)
				*delegates = array_pushback(*delegates, (union vm_value){.i = delegate}, AIN_ARRAY_INT, -1);
		}
	}
	for (int i = 0; i < a->nr_parts; i++)
		PE_ReleaseParts(a->parts[i].number);
	free_activity(a);
	if (named(name) && !strncmp(name->text, "Game/CommonMenu/CommonMenu", 26))
		PE_RestoreMenuHiddenParts();
	return true;
}

void PE_AddActivityCloseParts(struct string *name, struct string *parts_name)
{
	struct activity_part *p = find_part(name, parts_name);
	if (p)
		p->closes = true;
}

void PE_RemoveActivityCloseParts(struct string *name, struct string *parts_name)
{
	struct activity_part *p = find_part(name, parts_name);
	if (p)
		p->closes = false;
}

void PE_RemoveAllActivityCloseParts(struct string *name)
{
	struct activity *a = find_activity(name);
	if (a)
		for (int i = 0; i < a->nr_parts; i++)
			a->parts[i].closes = false;
}

bool PE_IsExistActivityCloseParts(struct string *name, struct string *parts_name)
{
	struct activity_part *p = find_part(name, parts_name);
	return p && p->closes;
}

void PE_AddActivityIntentDataDestination(struct string *name, struct string *parts_name, struct string *destination)
{
	struct activity_part *p = find_part(name, parts_name);
	if (!p || !named(destination))
		return;
	p->destinations = xrealloc(p->destinations, (p->nr_destinations + 1) * sizeof(*p->destinations));
	p->destinations[p->nr_destinations++] = string_dup(destination);
	p->has_intent = true;
}

void PE_SetActivityIntentData(struct string *name, struct string *parts_name, struct string *destination, int type)
{
	struct activity_part *p = find_part(name, parts_name);
	if (!p)
		return;
	clear_destinations(p);
	p->has_intent = true;
	p->intent_type = type;
	PE_AddActivityIntentDataDestination(name, parts_name, destination);
}

bool PE_IsExistActivityIntentData(struct string *name, struct string *parts_name)
{
	struct activity_part *p = find_part(name, parts_name);
	return p && p->has_intent;
}

int PE_NumofActivityIntentDataDestination(struct string *name, struct string *parts_name)
{
	struct activity_part *p = find_part(name, parts_name);
	return p ? p->nr_destinations : 0;
}

void PE_GetActivityIntentDataDestination(struct string **out, int index, struct string *name, struct string *parts_name)
{
	struct activity_part *p = find_part(name, parts_name);
	assign_string(out, p && index >= 0 && index < p->nr_destinations ? p->destinations[index] : NULL);
}

int PE_GetActivityIntentDataType(struct string *name, struct string *parts_name)
{
	struct activity_part *p = find_part(name, parts_name);
	return p ? p->intent_type : 0;
}

bool PE_IsExistActivityEndKey(struct string *name, int key)
{
	struct activity *a = find_activity(name);
	if (a)
		for (int i = 0; i < a->nr_end_keys; i++)
			if (a->end_keys[i] == key)
				return true;
	return false;
}

void PE_SetActivityEndKey(struct string *name, int key)
{
	struct activity *a = find_activity(name);
	if (!a || key < 0 || PE_IsExistActivityEndKey(name, key))
		return;
	a->end_keys = xrealloc(a->end_keys, (a->nr_end_keys + 1) * sizeof(*a->end_keys));
	a->end_keys[a->nr_end_keys++] = key;
}

void PE_EraseActivityEndKey(struct string *name, int key)
{
	struct activity *a = find_activity(name);
	if (a)
		for (int i = 0; i < a->nr_end_keys; i++)
			if (a->end_keys[i] == key) {
				a->nr_end_keys--;
				memmove(a->end_keys + i, a->end_keys + i + 1, (a->nr_end_keys - i) * sizeof(*a->end_keys));
				return;
			}
}

int PE_NumofActivityEndKey(struct string *name)
{
	struct activity *a = find_activity(name);
	return a ? a->nr_end_keys : 0;
}

int PE_GetActivityEndKey(struct string *name, int index)
{
	struct activity *a = find_activity(name);
	return a && index >= 0 && index < a->nr_end_keys ? a->end_keys[index] : 0;
}

#include "activity_save.inc"
