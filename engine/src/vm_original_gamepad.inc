/* Rance10's original clickers are release-triggered. Never turn a touch/focus
 * handover into their pending release click. No scene or saved-game callbacks. */
static bool vm_original_gamepad_bindings_ready(void)
{
	if (!ain || ain->version != 12 || !heap || !heap_size || !heap[0].ref ||
			heap[0].type != VM_PAGE || !heap[0].page) return false;
	for (int i = 0; i < ain->nr_globals && i < heap[0].page->nr_vars; i++) {
		if (strcmp(ain->globals[i].name, "g_JoystickButtonAssignmentTable")) continue;
		int slot = heap[0].page->values[i].i;
		if (slot <= 0 || (size_t)slot >= heap_size || !heap[slot].ref ||
				heap[slot].type != VM_PAGE || !heap[slot].page) return false;
		struct page *p = heap[slot].page;
		if (p->type != ARRAY_PAGE || p->array.rank != 1 || p->nr_vars < 16 ||
				(p->a_type != AIN_ARRAY_INT && !(p->a_type == AIN_ARRAY &&
				p->element_type && p->element_type->data == AIN_INT)))
			return false;
		/* An uninitialized/empty profile retains the existing pointer fallback. */
		for (int j = 4; j <= 8; j++) if (p->values[j].i > 0 && p->values[j].i <= 12) return true;
		return false;
	}
	return false;
}

enum vm_gamepad_context vm_original_gamepad_context(void)
{
	if (!ain || ain->version != 12 || call_stack_ptr < 0 || call_stack_ptr > 4096)
		return VM_PAD_MOUSE;
	/* Innermost input owner wins: a menu opened by dialogue must not also
	 * advance that suspended dialogue. Names describe original library owners,
	 * never individual quests or button coordinates. No cross-frame VM pointers. */
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		int fno = call_stack[i].fno;
		if (fno < 0 || fno >= ain->nr_functions) continue;
		const char *n = ain->functions[fno].name;
		if (!n) continue;
		/* Settings, card/formation and other Parts panels can be opened from
		 * dialogue. Their own input wait masks the suspended message owner. */
		if (!strcmp(n, "parts::detail::WaitForClick")) return VM_PAD_MOUSE;
		if (!strncmp(n, "menu::detail::CMenuView@", sizeof("menu::detail::CMenuView@") - 1) ||
				!strncmp(n, "menu::detail::CTestMenuView@", sizeof("menu::detail::CTestMenuView@") - 1)) return VM_PAD_KEY_MENU;
		if (!strcmp(n, "W") || !strncmp(n, "message::", 9) || !strncmp(n, "movie::", 7))
			return vm_original_gamepad_bindings_ready() ? VM_PAD_ORIGINAL : VM_PAD_MOUSE;
	}
	return VM_PAD_MOUSE;
}

void vm_cancel_original_gamepad_clicks(void)
{
	if (!ain || ain->version != 12 || !heap) return;
	for (int s = 0; s < ain->nr_structures; s++) {
		struct ain_struct *st = &ain->structures[s];
		if (strcmp(st->name, "CASJoyClick")) continue;
		const char *names[] = {"m_bKeyDown", "m_bFirstClickOff", "m_Trigger",
			"m_bKeyDownLoop", "m_bKeyPress", "m_nKeyDownOnObject"};
		int fields[6];
		for (int k = 0; k < 6; k++) {
			fields[k] = -1;
			for (int j = 0; j < st->nr_members; j++) if (!strcmp(st->members[j].name, names[k])) {
				if (st->members[j].type.data != (k == 5 ? AIN_INT : AIN_BOOL)) return;
				fields[k] = j; break;
			}
			if (fields[k] < 0) return;
		}
		/* Only on a cancelled gamepad gesture, not on each touch move/frame.
		 * Scan live objects once; retain no heap references or stale slot IDs. */
		for (size_t i = 1; i < heap_size; i++) {
			if (!heap[i].ref || heap[i].type != VM_PAGE || !heap[i].page) continue;
			struct page *p = heap[i].page;
			if (p->type != STRUCT_PAGE || p->index != s || p->nr_vars < st->nr_members) continue;
			for (int k = 0; k < 5; k++) p->values[fields[k]].i = false;
			p->values[fields[5]].i = INT32_MIN;
		}
		return;
	}
}
