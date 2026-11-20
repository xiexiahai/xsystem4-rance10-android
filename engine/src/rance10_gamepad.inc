/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Included by input.c. Fixed-size input state, no worker, heap cache or OS warp. */
#include "system4/ain.h"
#define R10_PAD_MOUSE_ID (UINT32_MAX - 1u)

// Android stores SDL's standard A..D-pad button numbers, followed by L2/R2.
enum { R10_PAD_L2 = SDL_CONTROLLER_BUTTON_DPAD_RIGHT + 1, R10_PAD_R2, R10_PAD_BINDING_COUNT };
static Uint8 r10_pad_binding[R10_PAD_BINDING_COUNT];
static bool r10_pad_has_bindings;
static bool r10_pad_trigger_held[2];

static void r10_pad_configure_bindings(const char *mapping)
{
	r10_pad_has_bindings = false;
	if (!mapping || !*mapping) return;
	bool used[R10_PAD_BINDING_COUNT] = {0};
	Uint8 bindings[R10_PAD_BINDING_COUNT];
	for (int physical = 0; physical < R10_PAD_BINDING_COUNT; physical++) {
		char *end;
		long action = strtol(mapping, &end, 10);
		if (end == mapping || action < 0 || action >= R10_PAD_BINDING_COUNT || used[action] ||
				*end != (physical == R10_PAD_BINDING_COUNT - 1 ? '\0' : ',')) return;
		used[action] = true;
		bindings[physical] = (Uint8)action;
		mapping = end + 1;
	}
	// The system Guide/Home button is reserved and often intercepted by Android.
	if (bindings[SDL_CONTROLLER_BUTTON_GUIDE] != SDL_CONTROLLER_BUTTON_GUIDE) return;
	memcpy(r10_pad_binding, bindings, sizeof(bindings));
	r10_pad_has_bindings = true;
}

static struct {
	SDL_JoystickID instance;
	bool visible, initialized, keys[VK_NR_KEYCODES];
	bool buttons[SDL_CONTROLLER_BUTTON_MAX], mouse[VK_MBUTTON + 1];
	Sint16 axes[SDL_CONTROLLER_AXIS_MAX];
	int context, left_route, axis_route[SDL_CONTROLLER_AXIS_MAX];
	int button_route[SDL_CONTROLLER_BUTTON_MAX], left_key, left_direction;
	Uint32 native_until[SDL_CONTROLLER_BUTTON_MAX], left_repeat_at, left_release;
	bool native_captured, native_keys[20];
	Uint32 native_capture_until;
	bool real_left;
	unsigned real_fingers;
	Uint32 last_tick, repeat_at, min_release[VK_NR_KEYCODES];
	Uint32 key_repeat_at[SDL_CONTROLLER_BUTTON_MAX];
	int wheel_direction;
	float x, y;
} r10_pad = {.instance = -1};

// Translate once before either input pump chooses native or mouse handling.
// Return false for intermediate trigger samples that produce no button edge.
static bool r10_pad_remap_event(SDL_Event *event)
{
	if (!r10_pad_has_bindings || !keyboard_focus) return true;
	if (event->type == SDL_CONTROLLERBUTTONDOWN || event->type == SDL_CONTROLLERBUTTONUP) {
		SDL_ControllerButtonEvent button = event->cbutton;
		if (button.which != r10_pad.instance || button.button >= R10_PAD_L2) return true;
		int action = r10_pad_binding[button.button];
		if (action < R10_PAD_L2) {
			event->cbutton.button = (Uint8)action;
		} else {
			event->caxis = (SDL_ControllerAxisEvent){
				.type = SDL_CONTROLLERAXISMOTION, .timestamp = button.timestamp, .which = button.which,
				.axis = SDL_CONTROLLER_AXIS_TRIGGERLEFT + action - R10_PAD_L2,
				.value = button.state == SDL_PRESSED ? INT16_MAX : 0
			};
		}
	} else if (event->type == SDL_CONTROLLERAXISMOTION) {
		SDL_ControllerAxisEvent axis = event->caxis;
		if (axis.which != r10_pad.instance || (axis.axis != SDL_CONTROLLER_AXIS_TRIGGERLEFT &&
				axis.axis != SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) return true;
		int trigger = axis.axis - SDL_CONTROLLER_AXIS_TRIGGERLEFT;
		int action = r10_pad_binding[R10_PAD_L2 + trigger];
		if (action >= R10_PAD_L2) {
			event->caxis.axis = SDL_CONTROLLER_AXIS_TRIGGERLEFT + action - R10_PAD_L2;
		} else {
			bool held = r10_pad_trigger_held[trigger];
			bool pressed = axis.value >= (held ? 10000 : 16000);
			if (held == pressed) return false;
			r10_pad_trigger_held[trigger] = pressed;
			event->cbutton = (SDL_ControllerButtonEvent){
				.type = pressed ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP,
				.timestamp = axis.timestamp, .which = axis.which, .button = (Uint8)action,
				.state = pressed ? SDL_PRESSED : SDL_RELEASED
			};
		}
	}
	return true;
}

bool input_gamepad_mouse_enabled(void)
{
	const char *enabled = getenv("XSYSTEM4_RANCE10_GAMEPAD");
	return ain && ain->version == 12 && enabled && !strcmp(enabled, "1");
}

bool input_gamepad_native_enabled(void)
{
	const char *enabled = getenv("XSYSTEM4_RANCE10_NATIVE_PAD");
	return input_gamepad_mouse_enabled() && enabled && !strcmp(enabled, "1");
}

int input_gamepad_native_count(void)
{
	/* The same selected controller owns native and pointer input. Secondary
	 * connected devices remain neutral, as in the original pointer adapter. */
	return input_gamepad_native_enabled() && r10_pad.instance >= 0 ? 1 : 0;
}

void input_gamepad_native_clear_capture(void)
{
	r10_pad.native_captured = false;
}

static bool r10_pad_original_button(int b)
{
	return b == SDL_CONTROLLER_BUTTON_A || b == SDL_CONTROLLER_BUTTON_B ||
		b == SDL_CONTROLLER_BUTTON_X || b == SDL_CONTROLLER_BUTTON_Y ||
		b == SDL_CONTROLLER_BUTTON_BACK || b == SDL_CONTROLLER_BUTTON_RIGHTSTICK ||
		(b >= SDL_CONTROLLER_BUTTON_DPAD_UP && b <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
}

static bool r10_pad_original_down(int b, Uint32 now)
{
	return r10_pad.button_route[b] == VM_PAD_ORIGINAL &&
		(r10_pad.buttons[b] || (Sint32)(r10_pad.native_until[b] - now) > 0);
}

bool input_gamepad_native_key(int device, int code)
{
	if (device != 0 || code <= 0 || code >= 20 || !keyboard_focus ||
			!input_gamepad_native_count()) return false;
	Uint32 now = SDL_GetTicks();
	if (r10_pad.native_captured && r10_pad.native_capture_until &&
			SDL_TICKS_PASSED(now, r10_pad.native_capture_until)) r10_pad.native_captured = false;
	if (!r10_pad.native_captured) {
		memset(r10_pad.native_keys, 0, sizeof(r10_pad.native_keys));
		bool left = r10_pad.left_route == VM_PAD_ORIGINAL && r10_pad.context == VM_PAD_ORIGINAL;
		r10_pad.native_keys[1] = (left && r10_pad.axes[SDL_CONTROLLER_AXIS_LEFTX] < -16000) || r10_pad_original_down(SDL_CONTROLLER_BUTTON_DPAD_LEFT, now);
		r10_pad.native_keys[2] = (left && r10_pad.axes[SDL_CONTROLLER_AXIS_LEFTX] > 16000) || r10_pad_original_down(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, now);
		r10_pad.native_keys[3] = (left && r10_pad.axes[SDL_CONTROLLER_AXIS_LEFTY] < -16000) || r10_pad_original_down(SDL_CONTROLLER_BUTTON_DPAD_UP, now);
		r10_pad.native_keys[4] = (left && r10_pad.axes[SDL_CONTROLLER_AXIS_LEFTY] > 16000) || r10_pad_original_down(SDL_CONTROLLER_BUTTON_DPAD_DOWN, now);
		/* Original logical codes: 6=message step, 5=hide, 9=backlog,
		 * 10=backscene. SDL bindings reserve L1 for forced Ctrl skip and
		 * R1 for pointer precision; the AIN assignment table is not modified. */
		r10_pad.native_keys[5] = r10_pad_original_down(SDL_CONTROLLER_BUTTON_X, now);
		r10_pad.native_keys[6] = r10_pad_original_down(SDL_CONTROLLER_BUTTON_A, now);
		r10_pad.native_keys[7] = r10_pad_original_down(SDL_CONTROLLER_BUTTON_B, now);
		r10_pad.native_keys[8] = r10_pad_original_down(SDL_CONTROLLER_BUTTON_Y, now);
		r10_pad.native_keys[9] = r10_pad.axis_route[SDL_CONTROLLER_AXIS_TRIGGERLEFT] == VM_PAD_ORIGINAL && r10_pad.axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT] > 16000;
		r10_pad.native_keys[10] = r10_pad.axis_route[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] == VM_PAD_ORIGINAL && r10_pad.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] > 16000;
		r10_pad.native_keys[11] = r10_pad_original_down(SDL_CONTROLLER_BUTTON_BACK, now);
		r10_pad.native_keys[12] = r10_pad_original_down(SDL_CONTROLLER_BUTTON_RIGHTSTICK, now);
		r10_pad.native_capture_until = 0;
		for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
			Uint32 until = r10_pad.native_until[b];
			if (!r10_pad.buttons[b] && (Sint32)(until - now) > 0 &&
					(!r10_pad.native_capture_until || (Sint32)(until - r10_pad.native_capture_until) < 0))
				r10_pad.native_capture_until = until;
		}
		r10_pad.native_captured = true;
	}
	return r10_pad.native_keys[code];
}

static void r10_pad_cancel_native(void)
{
	bool owned = r10_pad.left_route == VM_PAD_ORIGINAL;
	for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) if (
			r10_pad.button_route[b] == VM_PAD_ORIGINAL && r10_pad_original_button(b)) {
		owned = true; r10_pad.button_route[b] = -1;
	}
	for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; a++) if (r10_pad.axis_route[a] == VM_PAD_ORIGINAL) {
		owned = true; r10_pad.axis_route[a] = -1;
	}
	if (r10_pad.left_route == VM_PAD_ORIGINAL) r10_pad.left_route = -1;
	memset(r10_pad.native_until, 0, sizeof(r10_pad.native_until));
	r10_pad.native_captured = false;
	if (owned) vm_cancel_original_gamepad_clicks();
}

static void r10_pad_flush_mouse(void);

static void r10_pad_sync_context(void)
{
	int context = input_gamepad_native_enabled() && r10_pad.instance >= 0 ? vm_original_gamepad_context() : VM_PAD_MOUSE;
	if (r10_pad.context == context) return;
	r10_pad_cancel_native();
	/* A held pointer click from the previous page must not enter the new
	 * dialogue/menu as either a synthetic click or a native confirmation. */
	for (int b = SDL_CONTROLLER_BUTTON_A; b <= SDL_CONTROLLER_BUTTON_B; b++)
		if (r10_pad.buttons[b]) r10_pad.button_route[b] = -1;
	r10_pad_flush_mouse();
	for (int b = SDL_CONTROLLER_BUTTON_DPAD_UP; b <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT; b++)
		if (r10_pad.buttons[b]) r10_pad.button_route[b] = -1;
	for (int k = VK_LEFT; k <= VK_DOWN; k++) r10_pad.keys[k] = false;
	r10_pad.left_key = r10_pad.left_direction = 0;
	r10_pad.context = context;
}

static bool r10_pad_key_down(int code)
{
	return input_gamepad_mouse_enabled() && keyboard_focus &&
		(r10_pad.keys[code] || r10_pad.left_key == code || (code >= VK_LBUTTON && code <= VK_MBUTTON &&
		 r10_pad.mouse[code]));
}

static void r10_pad_clear_keys(bool no_ctrl)
{
	for (int i = 0; i < VK_NR_KEYCODES; i++)
		if (!no_ctrl || i != VK_CONTROL) r10_pad.keys[i] = false;
	memset(r10_pad.mouse, 0, sizeof(r10_pad.mouse));
	r10_pad.left_key = 0;
}

static void r10_pad_cancel_mouse(void)
{
	memset(r10_pad.mouse, 0, sizeof(r10_pad.mouse));
	r10_pad.buttons[SDL_CONTROLLER_BUTTON_A] = false;
	r10_pad.buttons[SDL_CONTROLLER_BUTTON_B] = false;
}

static bool r10_pad_mouse_event(SDL_MouseButtonEvent *event)
{
	if (event->which != R10_PAD_MOUSE_ID) return false;
	int code = sdl_to_sact_button(event->button);
	if (code >= VK_LBUTTON && code <= VK_MBUTTON)
		r10_pad.mouse[code] = event->state == SDL_PRESSED;
	return true;
}

static void r10_pad_flush_mouse(void)
{
	struct mouse_sample *sample = STAILQ_FIRST(&mouse_samples);
	while (sample) {
		struct mouse_sample *next = STAILQ_NEXT(sample, entry);
		if (sample->event.which == R10_PAD_MOUSE_ID) {
			STAILQ_REMOVE(&mouse_samples, sample, mouse_sample, entry);
			free(sample);
		}
		sample = next;
	}
	if (r10_pad.mouse[VK_LBUTTON] || r10_pad.mouse[VK_RBUTTON]) mouse_sample_cancelled = true;
	r10_pad_cancel_mouse();
}

static void r10_pad_reset(void)
{
	r10_pad_cancel_native();
	r10_pad_flush_mouse();
	memset(r10_pad.keys, 0, sizeof(r10_pad.keys));
	memset(r10_pad.buttons, 0, sizeof(r10_pad.buttons));
	memset(r10_pad.axes, 0, sizeof(r10_pad.axes));
	memset(r10_pad_trigger_held, 0, sizeof(r10_pad_trigger_held));
	memset(r10_pad.min_release, 0, sizeof(r10_pad.min_release));
	memset(r10_pad.key_repeat_at, 0, sizeof(r10_pad.key_repeat_at));
	memset(r10_pad.button_route, 0, sizeof(r10_pad.button_route));
	memset(r10_pad.axis_route, 0, sizeof(r10_pad.axis_route));
	r10_pad.context = r10_pad.left_route = VM_PAD_MOUSE;
	r10_pad.left_key = r10_pad.left_direction = 0;
	r10_pad.real_left = false;
	r10_pad.real_fingers = 0;
	r10_pad.visible = false;
	r10_pad.wheel_direction = 0;
	r10_pad.last_tick = SDL_GetTicks();
}

static SDL_JoystickID controller_instance(SDL_GameController *controller)
{
	return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
}

static void input_open_controller(int device)
{
	if (device < 0 || !SDL_IsGameController(device)) return;
	SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(device);
	int slot = -1;
	for (int i = 0; i < MAX_CONTROLLERS; i++) {
		if (controllers[i] && controller_instance(controllers[i]) == id) return;
		if (!controllers[i] && slot < 0) slot = i;
	}
	if (slot < 0) return;
	controllers[slot] = SDL_GameControllerOpen(device);
	if (controllers[slot] && r10_pad.instance < 0) {
		r10_pad.instance = id;
		r10_pad.last_tick = SDL_GetTicks();
	}
}

void input_open_controllers(void)
{
	if (input_gamepad_mouse_enabled()) r10_pad_configure_bindings(getenv("XSYSTEM4_RANCE10_PAD_BINDINGS"));
	for (int i = 0; i < SDL_NumJoysticks(); i++) input_open_controller(i);
}

static void input_remove_controller(SDL_JoystickID id)
{
	for (int i = 0; i < MAX_CONTROLLERS; i++) {
		if (controllers[i] && controller_instance(controllers[i]) == id) {
			SDL_GameControllerClose(controllers[i]);
			controllers[i] = NULL;
		}
	}
	if (r10_pad.instance != id) return;
	r10_pad_reset();
	r10_pad.instance = -1;
	memset(joybutton_state, 0, sizeof(joybutton_state));
	memset(joyaxis_state, 0, sizeof(joyaxis_state));
	for (int i = 0; i < MAX_CONTROLLERS; i++) if (controllers[i]) {
		r10_pad.instance = controller_instance(controllers[i]);
		break;
	}
}

void input_close_controllers(void)
{
	r10_pad_reset();
	r10_pad.instance = -1;
	for (int i = 0; i < MAX_CONTROLLERS; i++) if (controllers[i]) {
		SDL_GameControllerClose(controllers[i]);
		controllers[i] = NULL;
	}
}

static void r10_pad_activate_pointer(void)
{
	if (!r10_pad.initialized) {
		r10_pad.x = sdl.w / 2.f;
		r10_pad.y = sdl.h / 2.f;
		r10_pad.initialized = true;
	}
	r10_pad.visible = true;
}

bool input_gamepad_cursor(int *x, int *y)
{
	if (!input_gamepad_mouse_enabled() || !keyboard_focus || !r10_pad.visible || sdl.w <= 0 || sdl.h <= 0)
		return false;
	*x = (int)roundf(r10_pad.x);
	*y = (int)roundf(r10_pad.y);
	return true;
}

static bool r10_pad_set_pos(int x, int y)
{
	if (!input_gamepad_mouse_enabled() || !r10_pad.visible) return false;
	r10_pad.x = max(0, min(sdl.w - 1, x));
	r10_pad.y = max(0, min(sdl.h - 1, y));
	return true;
}

static void r10_pad_real_pointer(int x, int y)
{
	if (!input_gamepad_mouse_enabled()) return;
	r10_pad_cancel_native();
	r10_pad_flush_mouse();
	r10_pad.visible = false;
	memset(r10_pad.axes, 0, sizeof(r10_pad.axes));
	r10_pad.left_route = VM_PAD_MOUSE;
	r10_pad.left_key = r10_pad.left_direction = 0;
	if (sdl.viewport.w > 0 && sdl.viewport.h > 0) {
		r10_pad.x = max(0, min(sdl.w - 1, (x - sdl.viewport.x) * sdl.w / sdl.viewport.w));
		r10_pad.y = max(0, min(sdl.h - 1, (y - sdl.viewport.y) * sdl.h / sdl.viewport.h));
		r10_pad.initialized = true;
	}
}

static void r10_pad_pointer_event(SDL_Event *event)
{
	/* Observe ownership also in animation-only polling, without injecting the
	 * discarded touch into game input. A later poll must not lose its release. */
	if (event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP) {
		r10_pad_real_pointer(event->button.x, event->button.y);
		if (event->button.button == SDL_BUTTON_LEFT)
			r10_pad.real_left = event->type == SDL_MOUSEBUTTONDOWN;
	} else if (event->type == SDL_MOUSEMOTION) {
		r10_pad_real_pointer(event->motion.x, event->motion.y);
	} else if (input_gamepad_native_enabled()) {
		if (event->type == SDL_FINGERDOWN) {
			r10_pad_real_pointer((int)(event->tfinger.x * sdl.viewport.w) + sdl.viewport.x,
				(int)(event->tfinger.y * sdl.viewport.h) + sdl.viewport.y);
			if (r10_pad.real_fingers < 32) r10_pad.real_fingers++;
		} else if (event->type == SDL_FINGERUP && r10_pad.real_fingers) r10_pad.real_fingers--;
	}
}

static int r10_pad_button_key(int button)
{
	if (r10_pad.button_route[button] < 0 || (input_gamepad_native_enabled() &&
			r10_pad.button_route[button] == VM_PAD_ORIGINAL && r10_pad_original_button(button))) return 0;
	switch (button) {
	case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return VK_CONTROL;
	case SDL_CONTROLLER_BUTTON_START: return VK_ESCAPE;
	case SDL_CONTROLLER_BUTTON_DPAD_UP: return VK_UP;
	case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return VK_DOWN;
	case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return VK_LEFT;
	case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return VK_RIGHT;
	default: return 0;
	}
}

static void r10_pad_update(Uint32 now)
{
	r10_pad_sync_context();
	Uint32 elapsed = min(now - r10_pad.last_tick, 50u);
	r10_pad.last_tick = now;
	if (!input_gamepad_mouse_enabled() || !keyboard_focus || r10_pad.instance < 0) return;
	bool original = input_gamepad_native_enabled();
	/* A physical pointer gesture owns its entire drag, including events not yet
	 * consumed by the sampled mouse queue. Do not add pad movement/confirmation. */
	if (original && (r10_pad.real_left || r10_pad.real_fingers)) return;
	float x = r10_pad.axes[original ? SDL_CONTROLLER_AXIS_RIGHTX : SDL_CONTROLLER_AXIS_LEFTX] / 32768.f;
	float y = r10_pad.axes[original ? SDL_CONTROLLER_AXIS_RIGHTY : SDL_CONTROLLER_AXIS_LEFTY] / 32768.f;
	float magnitude = sqrtf(x*x + y*y);
	if (magnitude > .2f && sdl.w > 0 && sdl.h > 0) {
		r10_pad_activate_pointer();
		float speed = (min(magnitude, 1.f) - .2f) / .8f;
		speed = speed * speed * sdl.w * .65f * elapsed / 1000.f;
		if (r10_pad.buttons[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER]) speed *= .25f;
		r10_pad.x = max(0.f, min(sdl.w - 1.f, r10_pad.x + x / magnitude * speed));
		r10_pad.y = max(0.f, min(sdl.h - 1.f, r10_pad.y + y / magnitude * speed));
	}
	for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
		int key = r10_pad_button_key(b);
		if (key && key != VK_CONTROL && !r10_pad.buttons[b] &&
				(Sint32)(now - r10_pad.min_release[key]) >= 0) r10_pad.keys[key] = false;
		if (key && b >= SDL_CONTROLLER_BUTTON_DPAD_UP && b <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT &&
				r10_pad.buttons[b] && (Sint32)(now - r10_pad.key_repeat_at[b]) >= 0) {
			r10_pad.keys[key] = true;
			if (key_handler) key_handler(key);
			r10_pad.key_repeat_at[b] = now + 100;
		}
	}
	if (original && r10_pad.left_route == VM_PAD_KEY_MENU && r10_pad.context == VM_PAD_KEY_MENU) {
		int ax = r10_pad.axes[SDL_CONTROLLER_AXIS_LEFTX], ay = r10_pad.axes[SDL_CONTROLLER_AXIS_LEFTY];
		int key = abs(ax) > abs(ay) ? (ax < -16000 ? VK_LEFT : ax > 16000 ? VK_RIGHT : 0) :
			(ay < -16000 ? VK_UP : ay > 16000 ? VK_DOWN : 0);
		if (key && (key != r10_pad.left_direction || SDL_TICKS_PASSED(now, r10_pad.left_repeat_at))) {
			r10_pad_activate_pointer(); r10_pad.left_key = key;
			r10_pad.left_release = now + 40;
			r10_pad.left_repeat_at = now + (key != r10_pad.left_direction ? 350 : 100);
			if (key_handler) key_handler(key);
		}
		r10_pad.left_direction = key;
	}
	if (!r10_pad.left_direction && SDL_TICKS_PASSED(now, r10_pad.left_release)) r10_pad.left_key = 0;
	bool wheel_allowed = !original || r10_pad.context != VM_PAD_ORIGINAL;
	int wheel = wheel_allowed && r10_pad.buttons[SDL_CONTROLLER_BUTTON_X] && r10_pad.button_route[SDL_CONTROLLER_BUTTON_X] >= 0 ? 1 :
		wheel_allowed && r10_pad.buttons[SDL_CONTROLLER_BUTTON_Y] && r10_pad.button_route[SDL_CONTROLLER_BUTTON_Y] >= 0 ? -1 : 0;
	bool left_scroll = original && r10_pad.context == VM_PAD_MOUSE && r10_pad.left_route == VM_PAD_MOUSE;
	if (!wheel && (!original || left_scroll) && !r10_pad.buttons[SDL_CONTROLLER_BUTTON_A]) {
		int axis = original ? SDL_CONTROLLER_AXIS_LEFTY : SDL_CONTROLLER_AXIS_RIGHTY;
		wheel = r10_pad.axes[axis] < -16000 ? 1 : r10_pad.axes[axis] > 16000 ? -1 : 0;
	}
	if (wheel != r10_pad.wheel_direction) {
		r10_pad.wheel_direction = wheel;
		r10_pad.repeat_at = now;
	}
	if (wheel && (Sint32)(now - r10_pad.repeat_at) >= 0) {
		wheel_dir = wheel;
		r10_pad.repeat_at = now + 160;
	}
}

static void r10_pad_axis_event(SDL_ControllerAxisEvent *event)
{
	if (!keyboard_focus || event->which != r10_pad.instance || event->axis >= SDL_CONTROLLER_AXIS_MAX) return;
	r10_pad_sync_context();
	bool neutral = abs(r10_pad.axes[SDL_CONTROLLER_AXIS_LEFTX]) < 10000 && abs(r10_pad.axes[SDL_CONTROLLER_AXIS_LEFTY]) < 10000;
	int previous = r10_pad.axes[event->axis];
	r10_pad.axes[event->axis] = event->value;
	r10_pad.native_captured = false;
	if (!input_gamepad_native_enabled()) return;
	bool pointer_held = r10_pad.real_left || r10_pad.real_fingers;
	if (event->axis == SDL_CONTROLLER_AXIS_LEFTX || event->axis == SDL_CONTROLLER_AXIS_LEFTY) {
		if (neutral) r10_pad.left_route = pointer_held ? -1 : r10_pad.context;
		if (abs(r10_pad.axes[SDL_CONTROLLER_AXIS_LEFTX]) < 10000 && abs(r10_pad.axes[SDL_CONTROLLER_AXIS_LEFTY]) < 10000) {
			r10_pad.left_direction = 0;
			if (SDL_TICKS_PASSED(SDL_GetTicks(), r10_pad.left_release)) r10_pad.left_key = 0;
		}
	} else if (event->axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || event->axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
		if (previous < 10000 && event->value >= 10000) r10_pad.axis_route[event->axis] = pointer_held ? -1 : r10_pad.context;
		if (event->value < 10000) r10_pad.axis_route[event->axis] = VM_PAD_MOUSE;
	}
}

static void r10_pad_button_event(SDL_ControllerButtonEvent *event)
{
	if (!keyboard_focus || event->which != r10_pad.instance || event->button >= SDL_CONTROLLER_BUTTON_MAX) return;
	bool pressed = event->state == SDL_PRESSED;
	if (r10_pad.buttons[event->button] == pressed) return;
	r10_pad_update(SDL_GetTicks());
	r10_pad.buttons[event->button] = pressed;
	r10_pad.native_captured = false;
	if (pressed) {
		r10_pad.button_route[event->button] = r10_pad.context;
		if (input_gamepad_native_enabled() && (r10_pad.real_left || r10_pad.real_fingers) &&
				r10_pad_original_button(event->button)) r10_pad.button_route[event->button] = -1;
		if (input_gamepad_native_enabled() && r10_pad.button_route[event->button] == VM_PAD_ORIGINAL && r10_pad_original_button(event->button))
			r10_pad.native_until[event->button] = SDL_GetTicks() + 40;
	}
	if (r10_pad.button_route[event->button] < 0 || (input_gamepad_native_enabled() &&
			r10_pad.button_route[event->button] == VM_PAD_ORIGINAL && r10_pad_original_button(event->button))) return;
	if (event->button == SDL_CONTROLLER_BUTTON_A || event->button == SDL_CONTROLLER_BUTTON_B) {
		if (sdl.w <= 0 || sdl.h <= 0 || sdl.viewport.w <= 0 || sdl.viewport.h <= 0) return;
		r10_pad_activate_pointer();
		SDL_MouseButtonEvent mouse = {
			.type = pressed ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP,
			.timestamp = SDL_GetTicks(), .which = R10_PAD_MOUSE_ID,
			.button = event->button == SDL_CONTROLLER_BUTTON_A ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT,
			.state = event->state,
			.x = sdl.viewport.x + (int)roundf(r10_pad.x) * sdl.viewport.w / sdl.w,
			.y = sdl.viewport.y + (int)roundf(r10_pad.y) * sdl.viewport.h / sdl.h
		};
		if (mouse_sampling) queue_mouse_sample(&mouse);
		else mouse_event(&mouse);
		return;
	}
	int key = r10_pad_button_key(event->button);
	if (key) {
		if (pressed) {
			r10_pad.keys[key] = true;
			r10_pad.min_release[key] = SDL_GetTicks() + 40;
			r10_pad.key_repeat_at[event->button] = SDL_GetTicks() + 350;
			if (key_handler) key_handler(key);
		} else if (key == VK_CONTROL || (Sint32)(SDL_GetTicks() - r10_pad.min_release[key]) >= 0) {
			r10_pad.keys[key] = false;
		}
	}
}

/* Animation-only polling must still release held input and close removed pads. */
static bool r10_pad_window_event(SDL_Event *event)
{
	if (event->type == SDL_CONTROLLERDEVICEREMOVED) {
		input_remove_controller(event->cdevice.which);
		return true;
	}
	if (!input_gamepad_mouse_enabled()) return false;
	if (event->type == SDL_APP_WILLENTERBACKGROUND || event->type == SDL_APP_DIDENTERBACKGROUND) {
		r10_pad_reset();
		key_clear_flag(false);
		keyboard_focus = false;
		return true;
	}
	if (event->type == SDL_APP_DIDENTERFOREGROUND) {
		keyboard_focus = true;
		r10_pad.last_tick = SDL_GetTicks();
	}
	return false;
}
