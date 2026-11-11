/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include <stdbool.h>
#include "parts.h"
/* Test harness hook: expose the VM input scope to synthetic clicks. */
extern bool parts_began_click;
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <SDL.h>
#ifdef __ANDROID__
#include <SDL_system.h>
#include <jni.h>
#endif
#include "system4.h"
#include "queue.h"
#include "gfx/gfx.h"
#include "gfx/private.h"
#include "input.h"
#include "scene.h"
#include "vm.h"
#include "xsystem4.h"
#include "debugger.h"
#include "perf_window.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

bool key_state[VK_NR_KEYCODES];
static bool hook_close_app;
static unsigned pending_close_requests;

void input_reset_close_hook(void) {hook_close_app=false;pending_close_requests=0;}
void input_set_close_hook(bool enabled) {hook_close_app=enabled;}
bool input_get_close_hook(void) {return hook_close_app;}
bool input_has_system_message(void) {return pending_close_requests!=0;}
bool input_pop_system_message(int *message)
{
    if(!message || !pending_close_requests)return false;
    pending_close_requests--;
    *message=7; /* AppClosing consumed by the modern SystemService dispatcher. */
    return true;
}
static bool input_hook_close_request(void)
{
    if(!hook_close_app)return false;
    if(pending_close_requests<UINT_MAX)pending_close_requests++;
    return true;
}
static bool probe_mouse_position;
static SDL_Point probe_mouse_point;
static enum sact_keycode sdl_keytable[] = {
	[SDL_SCANCODE_0] = VK_0,
	[SDL_SCANCODE_1] = VK_1,
	[SDL_SCANCODE_2] = VK_2,
	[SDL_SCANCODE_3] = VK_3,
	[SDL_SCANCODE_4] = VK_4,
	[SDL_SCANCODE_5] = VK_5,
	[SDL_SCANCODE_6] = VK_6,
	[SDL_SCANCODE_7] = VK_7,
	[SDL_SCANCODE_8] = VK_8,
	[SDL_SCANCODE_9] = VK_9,
	[SDL_SCANCODE_A] = VK_A,
	[SDL_SCANCODE_B] = VK_B,
	[SDL_SCANCODE_C] = VK_C,
	[SDL_SCANCODE_D] = VK_D,
	[SDL_SCANCODE_E] = VK_E,
	[SDL_SCANCODE_F] = VK_F,
	[SDL_SCANCODE_G] = VK_G,
	[SDL_SCANCODE_H] = VK_H,
	[SDL_SCANCODE_I] = VK_I,
	[SDL_SCANCODE_J] = VK_J,
	[SDL_SCANCODE_K] = VK_K,
	[SDL_SCANCODE_L] = VK_L,
	[SDL_SCANCODE_M] = VK_M,
	[SDL_SCANCODE_N] = VK_N,
	[SDL_SCANCODE_O] = VK_O,
	[SDL_SCANCODE_P] = VK_P,
	[SDL_SCANCODE_Q] = VK_Q,
	[SDL_SCANCODE_R] = VK_R,
	[SDL_SCANCODE_S] = VK_S,
	[SDL_SCANCODE_T] = VK_T,
	[SDL_SCANCODE_U] = VK_U,
	[SDL_SCANCODE_V] = VK_V,
	[SDL_SCANCODE_W] = VK_W,
	[SDL_SCANCODE_X] = VK_X,
	[SDL_SCANCODE_Y] = VK_Y,
	[SDL_SCANCODE_Z] = VK_Z,
	[SDL_SCANCODE_BACKSPACE] = VK_BACK,
	[SDL_SCANCODE_TAB] = VK_TAB,
	[SDL_SCANCODE_CLEAR] = VK_CLEAR,
	[SDL_SCANCODE_RETURN] = VK_RETURN,
	[SDL_SCANCODE_LSHIFT] = VK_SHIFT,
	[SDL_SCANCODE_RSHIFT] = VK_SHIFT,
	[SDL_SCANCODE_LCTRL] = VK_CONTROL,
	[SDL_SCANCODE_RCTRL] = VK_CONTROL,
	[SDL_SCANCODE_LALT] = VK_MENU,
	[SDL_SCANCODE_RALT] = VK_MENU,
	[SDL_SCANCODE_PAUSE] = VK_PAUSE,
	[SDL_SCANCODE_CAPSLOCK] = VK_CAPITAL,
	[SDL_SCANCODE_ESCAPE] = VK_ESCAPE,
	[SDL_SCANCODE_SPACE] = VK_SPACE,
	[SDL_SCANCODE_PAGEUP] = VK_PRIOR,
	[SDL_SCANCODE_PAGEDOWN] = VK_NEXT,
	[SDL_SCANCODE_END] = VK_END,
	[SDL_SCANCODE_HOME] = VK_HOME,
	[SDL_SCANCODE_LEFT] = VK_LEFT,
	[SDL_SCANCODE_UP] = VK_UP,
	[SDL_SCANCODE_RIGHT] = VK_RIGHT,
	[SDL_SCANCODE_DOWN] = VK_DOWN,
	[SDL_SCANCODE_SELECT] = VK_SELECT,
	[SDL_SCANCODE_EXECUTE] = VK_EXECUTE,
	[SDL_SCANCODE_PRINTSCREEN] = VK_SNAPSHOT,
	[SDL_SCANCODE_INSERT] = VK_INSERT,
	[SDL_SCANCODE_DELETE] = VK_DELETE,
	[SDL_SCANCODE_HELP] = VK_HELP,
	[SDL_SCANCODE_KP_0] = VK_NUMPAD0,
	[SDL_SCANCODE_KP_1] = VK_NUMPAD1,
	[SDL_SCANCODE_KP_2] = VK_NUMPAD2,
	[SDL_SCANCODE_KP_3] = VK_NUMPAD3,
	[SDL_SCANCODE_KP_4] = VK_NUMPAD4,
	[SDL_SCANCODE_KP_5] = VK_NUMPAD5,
	[SDL_SCANCODE_KP_6] = VK_NUMPAD6,
	[SDL_SCANCODE_KP_7] = VK_NUMPAD7,
	[SDL_SCANCODE_KP_8] = VK_NUMPAD8,
	[SDL_SCANCODE_KP_9] = VK_NUMPAD9,
	[SDL_SCANCODE_KP_MULTIPLY] = VK_MULTIPLY,
	[SDL_SCANCODE_KP_PLUS] = VK_ADD,
	[SDL_SCANCODE_KP_VERTICALBAR] = VK_SEPARATOR, // ???
	[SDL_SCANCODE_KP_MINUS] = VK_SUBTRACT,
	[SDL_SCANCODE_KP_PERIOD] = VK_DECIMAL,
	[SDL_SCANCODE_KP_DIVIDE] = VK_DIVIDE,
	[SDL_SCANCODE_KP_ENTER] = VK_RETURN,
	[SDL_SCANCODE_F1] = VK_F1,
	[SDL_SCANCODE_F2] = VK_F2,
	[SDL_SCANCODE_F3] = VK_F3,
	[SDL_SCANCODE_F4] = VK_F4,
	[SDL_SCANCODE_F5] = VK_F5,
	[SDL_SCANCODE_F6] = VK_F6,
	[SDL_SCANCODE_F7] = VK_F7,
	[SDL_SCANCODE_F8] = VK_F8,
	[SDL_SCANCODE_F9] = VK_F9,
	[SDL_SCANCODE_F10] = VK_F10,
	[SDL_SCANCODE_F11] = VK_F11,
	[SDL_SCANCODE_F12] = VK_F12,
	[SDL_SCANCODE_F13] = VK_F13,
	[SDL_SCANCODE_F14] = VK_F14,
	[SDL_SCANCODE_F15] = VK_F15,
	[SDL_SCANCODE_F16] = VK_F16,
	[SDL_SCANCODE_F17] = VK_F17,
	[SDL_SCANCODE_F18] = VK_F18,
	[SDL_SCANCODE_F19] = VK_F19,
	[SDL_SCANCODE_F20] = VK_F20,
	[SDL_SCANCODE_F21] = VK_F21,
	[SDL_SCANCODE_F22] = VK_F22,
	[SDL_SCANCODE_F23] = VK_F23,
	[SDL_SCANCODE_F24] = VK_F24,
	[SDL_SCANCODE_NUMLOCKCLEAR] = VK_NUMLOCK,
	[SDL_SCANCODE_SCROLLLOCK] = VK_SCROLL,
};

static enum sact_keycode sdl_to_sact_key(SDL_Scancode scancode, SDL_Keymod modifiers)
{
	if (!(modifiers & KMOD_NUM)) {
		switch (scancode) {
		case SDL_SCANCODE_KP_0:      return VK_INSERT;
		case SDL_SCANCODE_KP_1:      return VK_END;
		case SDL_SCANCODE_KP_2:      return VK_DOWN;
		case SDL_SCANCODE_KP_3:      return VK_NEXT;
		case SDL_SCANCODE_KP_4:      return VK_LEFT;
		case SDL_SCANCODE_KP_5:      return VK_CLEAR;
		case SDL_SCANCODE_KP_6:      return VK_RIGHT;
		case SDL_SCANCODE_KP_7:      return VK_HOME;
		case SDL_SCANCODE_KP_8:      return VK_UP;
		case SDL_SCANCODE_KP_9:      return VK_PRIOR;
		case SDL_SCANCODE_KP_PERIOD: return VK_DELETE;
		default: break;
		}
	}
	if (scancode >= sizeof(sdl_keytable) / sizeof(*sdl_keytable))
		return 0;
	return sdl_keytable[scancode];
}

bool mouse_focus = true;
bool keyboard_focus = true;

#define MAX_CONTROLLERS 4
static SDL_GameController *controllers[MAX_CONTROLLERS];

#ifdef __ANDROID__
struct deferred_keyevent {
	STAILQ_ENTRY(deferred_keyevent) entry;
	SDL_Event e;
};
static STAILQ_HEAD(deferred_keyevent_queue, deferred_keyevent) deferred_keyevent_queue =
	STAILQ_HEAD_INITIALIZER(deferred_keyevent_queue);
static uint32_t last_keyevent_timestamp;
#define DEFERRED_KEY_DELAY 10
#endif

// Stores a mouse button event synthesized from a touch event (valid if
// .timestamp != 0). We defer such events to prevent games from handling
// button down events before reading the pointer position.
static SDL_MouseButtonEvent deferred_synthetic_mouse_event;
#define SYNTHETIC_MOUSE_EVENT_DELAY 50

// Parts input samples once per update. Keep every button edge until that
// update, even if several complete clicks arrive in one SDL event batch.
struct mouse_sample {
	STAILQ_ENTRY(mouse_sample) entry;
	SDL_MouseButtonEvent event;
};
static STAILQ_HEAD(mouse_sample_queue, mouse_sample) mouse_samples =
	STAILQ_HEAD_INITIALIZER(mouse_samples);
static bool mouse_sampling;
static bool mouse_sample_cancelled;
static bool mouse_sample_has_pos;
static SDL_Point mouse_sample_pos;

static uint32_t long_touch_start_timestamp;
static SDL_FRect long_touch_finger_rect;
#define LONG_TOUCH_DURATION 1000

static float scroll_gesture_y;
#define SCROLL_GESTURE_SENSITIVITY 0.02f  // 2% of screen height

static enum sact_keycode sdl_to_sact_button(int button)
{
	switch (button) {
	case SDL_BUTTON_LEFT:   return VK_LBUTTON;
	case SDL_BUTTON_RIGHT:  return VK_RBUTTON;
	case SDL_BUTTON_MIDDLE: return VK_MBUTTON;
	default:                return 0;
	}
}

bool key_is_down(enum sact_keycode code)
{
	if (code < 0 || code >= VK_NR_KEYCODES) {
		return false;
	}
	return key_state[code];
}

void key_clear_flag(bool no_ctrl)
{
	for (int i = 0; i < VK_NR_KEYCODES; i++) {
		if (no_ctrl && i == VK_CONTROL)
			continue;
		key_state[i] = false;
	}
}

void mouse_get_pos(int *x, int *y)
{
	if (probe_mouse_position) {
		*x=probe_mouse_point.x;*y=probe_mouse_point.y;
		return;
	}
	int wx, wy;
	SDL_PumpEvents();
	if (mouse_sample_has_pos) {
		wx = mouse_sample_pos.x;
		wy = mouse_sample_pos.y;
	} else {
		SDL_GetMouseState(&wx, &wy);
	}
	*x = (wx - sdl.viewport.x) * sdl.w / sdl.viewport.w;
	*y = (wy - sdl.viewport.y) * sdl.h / sdl.viewport.h;
}

void mouse_set_pos(int x, int y)
{
	int wx = x * sdl.viewport.w / sdl.w + sdl.viewport.x;
	int wy = y * sdl.viewport.h / sdl.h + sdl.viewport.y;
	SDL_WarpMouseInWindow(sdl.window, wx, wy);
}

static int wheel_dir = 0;

void mouse_get_wheel(int *forward, int *back)
{
	*forward = wheel_dir > 0;
	*back = wheel_dir < 0;
}

void mouse_clear_wheel(void)
{
	wheel_dir = 0;
}

bool mouse_show_cursor(bool show)
{
	return SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE) >= 0;
}

static void(*input_handler)(const char*);
static void(*editing_handler)(const char*, int, int);
static void(*key_handler)(int);

static void key_event(SDL_KeyboardEvent *e, bool pressed)
{
	enum sact_keycode code = sdl_to_sact_key(e->keysym.scancode, e->keysym.mod);
	if (code) {
		key_state[code] = pressed;
		if (pressed && key_handler)
			key_handler(code);
	}
}

static void mouse_event(SDL_MouseButtonEvent *e)
{
	if (e->state == SDL_PRESSED)
		perf_window_mouse_press();
	enum sact_keycode code = sdl_to_sact_button(e->button);
	if (code)
		key_state[code] = e->state == SDL_PRESSED;
#ifdef DEBUGGER_ENABLED
	if (e->button == SDL_BUTTON_MIDDLE && e->state == SDL_PRESSED
			&& (dbg_start_in_debugger || dbg_dap))
		dbg_repl(DBG_STOP_PAUSE, NULL);
#endif
}

#ifdef __ANDROID__
/* Show/hide the on-screen Ctrl button owned by SDLActivity. */
static void toggle_ctrl_overlay(void)
{
	JNIEnv *env = SDL_AndroidGetJNIEnv();
	jobject activity = SDL_AndroidGetActivity();
	if (!env || !activity)
		return;
	jclass cls = (*env)->GetObjectClass(env, activity);
	if (cls) {
		jmethodID mid = (*env)->GetMethodID(env, cls, "toggleCtrlOverlay", "()V");
		if (mid)
			(*env)->CallVoidMethod(env, activity, mid);
		(*env)->DeleteLocalRef(env, cls);
	}
	(*env)->DeleteLocalRef(env, activity);
}
#else
static void toggle_ctrl_overlay(void) {}
#endif

static void synthetic_mouse_event(SDL_MouseButtonEvent *e)
{
	if (e->state == SDL_PRESSED)
		perf_window_mouse_press();
	if (e->state == SDL_PRESSED) {
		// Touch outside the viewport toggles the on-screen Ctrl button.
		SDL_Point p = { .x = e->x, .y = e->y };
		if (SDL_PointInRect(&p, &sdl.viewport))
			key_state[VK_LBUTTON] = true;
		else
			toggle_ctrl_overlay();
	} else {
		key_state[VK_LBUTTON] = false;
	}
}

static void queue_mouse_sample(const SDL_MouseButtonEvent *event)
{
	struct mouse_sample *sample = xmalloc(sizeof(*sample));
	sample->event = *event;
	STAILQ_INSERT_TAIL(&mouse_samples, sample, entry);
}

static void clear_mouse_samples(void)
{
	while (!STAILQ_EMPTY(&mouse_samples)) {
		struct mouse_sample *sample = STAILQ_FIRST(&mouse_samples);
		STAILQ_REMOVE_HEAD(&mouse_samples, entry);
		free(sample);
	}
	mouse_sample_has_pos = false;
}

static void cancel_mouse_samples(void)
{
	clear_mouse_samples();
	deferred_synthetic_mouse_event.timestamp = 0;
	key_state[VK_LBUTTON] = key_state[VK_RBUTTON] = key_state[VK_MBUTTON] = false;
	mouse_sample_cancelled = true;
}

void input_begin_mouse_sampling(void)
{
	if (mouse_sampling)
		return;
	mouse_sampling = true;
	if (deferred_synthetic_mouse_event.timestamp) {
		queue_mouse_sample(&deferred_synthetic_mouse_event);
		deferred_synthetic_mouse_event.timestamp = 0;
	}
}

bool input_next_mouse_sample(void)
{
	bool cancelled = mouse_sample_cancelled;
	mouse_sample_cancelled = false;
	mouse_sample_has_pos = false;
	if (!mouse_sampling || STAILQ_EMPTY(&mouse_samples))
		return cancelled;
	struct mouse_sample *sample = STAILQ_FIRST(&mouse_samples);
	STAILQ_REMOVE_HEAD(&mouse_samples, entry);
	mouse_sample_pos = (SDL_Point){sample->event.x, sample->event.y};
	mouse_sample_has_pos = true;
	if (sample->event.which == SDL_TOUCH_MOUSEID)
		synthetic_mouse_event(&sample->event);
	else
		mouse_event(&sample->event);
	free(sample);
	return cancelled;
}

void input_end_mouse_sampling(void)
{
	// Hand back the latest physical state when leaving Parts input.
	while (!STAILQ_EMPTY(&mouse_samples))
		input_next_mouse_sample();
	mouse_sampling = false;
	mouse_sample_has_pos = false;
}

#define JOYAXIS_DEADZONE 13500

enum joyaxis_axis {
	JOYAXIS_X,
	JOYAXIS_Y
};

enum joyaxis_direction {
	JOYAXIS_DIR_LEFT_UP    = -1,
	JOYAXIS_DIR_NONE       =  0,
	JOYAXIS_DIR_RIGHT_DOWN =  1
};

enum joyaxis_state {
	// A: Initial state
	// - when joy_clear_flag is called, goes on long cooldown
	//   and then enters JOYAXIS_STATE_B
	JOYAXIS_STATE_A,
	// B: Held state
	// - when joy_clear_flag is called, goes on short cooldown
	//   state remains JOYAXIS_STATE_B until direction changes
	JOYAXIS_STATE_B,
};

struct joyaxis {
	enum joyaxis_state state;
	unsigned long cooldown_expire_time;
	enum joyaxis_direction real_direction;
	enum joyaxis_direction logical_direction;
};

struct joyaxis joyaxis_state[2] = { 0 };

bool joybutton_state[SDL_CONTROLLER_BUTTON_MAX];

static unsigned long joy_time(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((unsigned long)ts.tv_sec * 1000) + ((unsigned long)ts.tv_nsec / 1000000);
}

static bool joy_check_axis(struct joyaxis *axis, enum joyaxis_direction direction)
{
	if (axis->logical_direction == direction)
		return true;
	if (!axis->cooldown_expire_time)
		return false;
	// on cooldown: check if cooldown expired
	if (joy_time() >= axis->cooldown_expire_time) {
		axis->state = JOYAXIS_STATE_B;
		axis->cooldown_expire_time = 0;
		axis->logical_direction = axis->real_direction;
	}
	return axis->logical_direction == direction;
}

bool joy_key_is_down(uint8_t code)
{
	// Default SACT key mapping in comments below
	switch (code) {
	case 1: // Left Arrow
		return joy_check_axis(&joyaxis_state[JOYAXIS_X], JOYAXIS_DIR_LEFT_UP);
	case 2: // Right Arrow
		return joy_check_axis(&joyaxis_state[JOYAXIS_X], JOYAXIS_DIR_RIGHT_DOWN);
	case 3: // Up Arrow
		return joy_check_axis(&joyaxis_state[JOYAXIS_Y], JOYAXIS_DIR_LEFT_UP);
	case 4: // Down Arrow
		return joy_check_axis(&joyaxis_state[JOYAXIS_Y], JOYAXIS_DIR_RIGHT_DOWN);
	case 5: // Esc
		return joybutton_state[SDL_CONTROLLER_BUTTON_A];
	case 6: // Enter
		return joybutton_state[SDL_CONTROLLER_BUTTON_B];
	case 7: // Space
		return joybutton_state[SDL_CONTROLLER_BUTTON_X];
	case 8: // Ctrl
		return joybutton_state[SDL_CONTROLLER_BUTTON_Y];
	case 9: // a
		return joybutton_state[SDL_CONTROLLER_BUTTON_LEFTSHOULDER];
	case 10: // z
		return joybutton_state[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER];
	case 11: // Page Up
		return joybutton_state[SDL_CONTROLLER_BUTTON_BACK];
	case 12: // Page Down
		return joybutton_state[SDL_CONTROLLER_BUTTON_START];
	case 13:
		return joybutton_state[SDL_CONTROLLER_BUTTON_GUIDE];
	case 14:
		return joybutton_state[SDL_CONTROLLER_BUTTON_LEFTSTICK];
	case 15:
		return joybutton_state[SDL_CONTROLLER_BUTTON_RIGHTSTICK];
	case 16:
		return joybutton_state[SDL_CONTROLLER_BUTTON_DPAD_LEFT];
	case 17:
		return joybutton_state[SDL_CONTROLLER_BUTTON_DPAD_RIGHT];
	case 18:
		return joybutton_state[SDL_CONTROLLER_BUTTON_DPAD_UP];
	case 19:
		return joybutton_state[SDL_CONTROLLER_BUTTON_DPAD_DOWN];
	default:
		return false;
	}
}

static float joy_calc_deg(int x, int y)
{
	// calculate angle (degrees)
	float a = atan2f(y, x) * (180.0f/M_PI);
	if (a < 0)
		a += 360.0f;
	// rotate so that up is 0/360
	a += 90.0f;
	if (a > 360.0f)
		a -= 360.0f;
	return a;
}

static float joy_calc_pow(int x, int y)
{
	// XXX: in theory this should be scaled according to the angle so that
	//      (32767,32767) returns exactly 1, but alicesoft themselves seem
	//      to simply clamp pythagoras as below
	return min(1.0f, sqrtf(x*x + y*y) / 32767.0f);
}

static float joy_calc_trigger(int left, int right)
{
	// for whatever reason alicesoft treats triggers as an analog stick
	// (at least on an xbox controller)
	float v = 315.0f;
	v += ((float)left/32767.0f) * 90.0f;
	v -= ((float)right/32767.0f) * 90.0f;
	if (v < 0)
		v += 360.0f;
	if (v > 360.0f)
		v -= 360.0f;
	return v;
}

bool joy_get_stick_status(int controller, int type, float *deg, float *pow)
{
	SDL_GameController *ctrl = controllers[controller];
	if (!ctrl)
		goto invalid;
	if (type == 0) {
		int x = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTX);
		int y = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTY);
		if (abs(x) < JOYAXIS_DEADZONE && abs(y) < JOYAXIS_DEADZONE) {
			*pow = 0.0f;
			*deg = 0.0f;
		} else {
			*pow = joy_calc_pow(x, y);
			*deg = joy_calc_deg(x, y);
		}
		return true;
	}
	if (type == 1) {
		int left = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
		int right = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
		*pow = 1.0f;
		*deg = joy_calc_trigger(left, right);
		return true;
	}
invalid:
	*deg = 0.0f;
	*pow = 0.0f;
	return false;
}

static void joyaxis_clear_flag(struct joyaxis *joy)
{
	// put axis on cooldown
	if (joy->logical_direction) {
		joy->logical_direction = 0;
		if (joy->state == JOYAXIS_STATE_A) {
			joy->cooldown_expire_time = joy_time() + 300;
		} else {
			joy->cooldown_expire_time = joy_time() + 50;
		}
	}
}

void joy_clear_flag(void)
{
	for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
		joybutton_state[i] = false;
	}
	joyaxis_clear_flag(&joyaxis_state[JOYAXIS_X]);
	joyaxis_clear_flag(&joyaxis_state[JOYAXIS_Y]);
}

static void joyaxis_update(enum joyaxis_axis axis, int value)
{
	// determine direction
	int direction = 0;
	if (value < -JOYAXIS_DEADZONE)
		direction = -1;
	else if (value > JOYAXIS_DEADZONE)
		direction = 1;

	// reset axis state if direction changed
	if (direction != joyaxis_state[axis].real_direction) {
		joyaxis_state[axis].state = JOYAXIS_STATE_A;
		joyaxis_state[axis].cooldown_expire_time = 0;
		joyaxis_state[axis].real_direction = direction;
		joyaxis_state[axis].logical_direction = direction;
	}
}

static void controller_axis_event(SDL_ControllerAxisEvent *e)
{
	switch (e->axis) {
	case SDL_CONTROLLER_AXIS_LEFTX:
		joyaxis_update(JOYAXIS_X, e->value);
		break;
	case SDL_CONTROLLER_AXIS_LEFTY:
		joyaxis_update(JOYAXIS_Y, e->value);
		break;
	default:
		break;
	}
}

static void controller_button_event(SDL_ControllerButtonEvent *e)
{
	joybutton_state[e->button] = e->state == SDL_PRESSED;
}

void register_input_handler(void(*handler)(const char*))
{
	input_handler = handler;
}

void clear_input_handler(void)
{
	input_handler = NULL;
}

void register_editing_handler(void(*handler)(const char*, int, int))
{
	editing_handler = handler;
}

void clear_editing_handler(void)
{
	editing_handler = NULL;
}

void register_key_handler(void(*handler)(int))
{
	key_handler = handler;
}

void clear_key_handler(void)
{
	key_handler = NULL;
}

bool input_owns_text_handlers(void(*text)(const char*),void(*editing)(const char*,int,int),void(*key)(int))
{return input_handler==text && editing_handler==editing && key_handler==key;}
bool input_claim_text_handlers(void(*text)(const char*),void(*editing)(const char*,int,int),void(*key)(int))
{
    if((input_handler && input_handler!=text) || (editing_handler && editing_handler!=editing) || (key_handler && key_handler!=key))return false;
    input_handler=text;editing_handler=editing;key_handler=key;return true;
}
bool input_release_text_handlers(void(*text)(const char*),void(*editing)(const char*,int,int),void(*key)(int))
{
    if(!input_owns_text_handlers(text,editing,key))return false;
    input_handler=NULL;editing_handler=NULL;key_handler=NULL;return true;
}

static void fire_deferred_events(void)
{
	uint32_t now = SDL_GetTicks();

	// Flush the deferred mouse button event if it's older than 50ms.
	if (deferred_synthetic_mouse_event.timestamp &&
		deferred_synthetic_mouse_event.timestamp + SYNTHETIC_MOUSE_EVENT_DELAY < now) {
		synthetic_mouse_event(&deferred_synthetic_mouse_event);
		deferred_synthetic_mouse_event.timestamp = 0;
	}

#ifdef __ANDROID__
	// Fire the deferred keyboard and text input events.
	while (!STAILQ_EMPTY(&deferred_keyevent_queue) && now >= last_keyevent_timestamp + DEFERRED_KEY_DELAY) {
		struct deferred_keyevent *ev = STAILQ_FIRST(&deferred_keyevent_queue);
		switch (ev->e.type) {
		case SDL_TEXTINPUT:
			if (input_handler)
				input_handler(ev->e.text.text);
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			last_keyevent_timestamp = now;
			key_event(&ev->e.key, ev->e.type == SDL_KEYDOWN);
			break;
		}
		STAILQ_REMOVE_HEAD(&deferred_keyevent_queue, entry);
		free(ev);
	}
#endif
}

static void handle_window_event(SDL_Event *e)
{
	switch (e->window.event) {
	case SDL_WINDOWEVENT_EXPOSED:
		gfx_swap();
		break;
	case SDL_WINDOWEVENT_ENTER:
		mouse_focus = true;
		break;
	case SDL_WINDOWEVENT_LEAVE:
		mouse_focus = false;
		break;
	case SDL_WINDOWEVENT_FOCUS_GAINED:
		keyboard_focus = true;
		break;
	case SDL_WINDOWEVENT_FOCUS_LOST:
		keyboard_focus = false;
		cancel_mouse_samples();
		long_touch_start_timestamp = 0;
		break;
	case SDL_WINDOWEVENT_SIZE_CHANGED:
		gfx_update_screen_scale();
		gfx_swap();
		break;
	}
}

void handle_window_events(void)
{
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_QUIT:
			if(input_hook_close_request())break;
			/* DSH diagnostic */
			fprintf(stderr, "[diag] SDL_QUIT in handle_window_events -> exit\n");
			fflush(stderr);
			vm_exit(0);
			break;
		case SDL_WINDOWEVENT:
			handle_window_event(&e);
			break;
		}
	}
}

/*
 * ===== DSH DIAGNOSTIC: scripted key input =====
 *
 * Drives the game without a human, so an automated run can be advanced past
 * the title screen and screenshotted at each stage.
 *
 * Inert unless XSYSTEM4_AUTOKEY is set, in the form
 *     XSYSTEM4_AUTOKEY="<start_seconds>:<interval_seconds>:<key1,key2,...>"
 * e.g. "20:3:RETURN,SPACE,SPACE" presses RETURN, then SPACE, then SPACE,
 * 3 seconds apart, starting 20 seconds in, then repeats the list.
 * Key names are SDL scancode names (RETURN, SPACE, DOWN, Z, ESCAPE, ...).
 *
 * TODO(DSH): remove once automated playthroughs are no longer needed.
 */
static void diag_autokey(void)
{
	static int state;		/* 0 = uninitialised, 1 = armed, 2 = disabled */
	static Uint32 interval_ms;
	static SDL_Scancode keys[32];
	static int nr_keys, cur;
	static Uint32 next_at;
	static bool down;

	if (state == 0) {
		state = 2;
		const char *cfg = getenv("XSYSTEM4_AUTOKEY");
		if (cfg && *cfg) {
			double start_s = 0, iv_s = 0;
			char list[512];
			if (sscanf(cfg, "%lf:%lf:%511s", &start_s, &iv_s, list) == 3) {
				for (char *tok = strtok(list, ","); tok && nr_keys < 32; tok = strtok(NULL, ",")) {
					SDL_Scancode sc = SDL_GetScancodeFromName(tok);
					if (sc == SDL_SCANCODE_UNKNOWN)
						fprintf(stderr, "[diag] autokey: unknown key name '%s'\n", tok);
					else
						keys[nr_keys++] = sc;
				}
				if (nr_keys > 0) {
					interval_ms = (Uint32)(iv_s * 1000);
					next_at = SDL_GetTicks() + (Uint32)(start_s * 1000);
					state = 1;
					fprintf(stderr, "[diag] autokey armed: %d keys, every %.1fs, first at %.1fs\n",
						nr_keys, iv_s, start_s);
				} else {
					fprintf(stderr, "[diag] autokey: no valid key names in '%s'\n", list);
				}
			} else {
				fprintf(stderr, "[diag] autokey: bad config '%s' "
					"(want \"<start>:<interval>:<k1,k2,...>\")\n", cfg);
			}
		}
	}
	if (state != 1)
		return;

	Uint32 now = SDL_GetTicks();
	if (now < next_at)
		return;

	SDL_KeyboardEvent e;
	SDL_zero(e);
	e.keysym.scancode = keys[cur];
	e.keysym.mod = KMOD_NONE;
	key_event(&e, !down);

	if (!down) {
		down = true;
		next_at = now + 60;	/* hold for 60ms */
	} else {
		down = false;
		cur = (cur + 1) % nr_keys;
		next_at = now + interval_ms;
	}
}

/*
 * ===== DSH DIAGNOSTIC: scripted mouse click =====
 *
 * The title menu is mouse-driven (PartsEngine buttons tested with
 * PE_IsCursorIn), so key injection alone cannot select anything. This warps
 * the real cursor to a game-space coordinate and clicks there.
 *
 * Inert unless XSYSTEM4_AUTOCLICK is set, in the form
 *     XSYSTEM4_AUTOCLICK="<start_seconds>:<x>:<y>[:<repeat_interval_seconds>]"
 * Coordinates are in game space (1024x768 for Rance 03). The click repeats.
 *
 * TODO(DSH): remove once automated playthroughs are no longer needed.
 */
static void diag_autoclick(void)
{
	static int state;		/* 0 = uninitialised, 1 = armed, 2 = disabled */
	static Uint32 interval_ms;
	static int gx, gy;
	static int phase;		/* 0 = press pending, 1 = release pending */
	static Uint32 next_at;

	if (state == 0) {
		state = 2;
		const char *cfg = getenv("XSYSTEM4_AUTOCLICK");
		if (cfg && *cfg) {
			double start_s = 0, iv_s = 6;
			int n = sscanf(cfg, "%lf:%d:%d:%lf", &start_s, &gx, &gy, &iv_s);
			if (n >= 3) {
				interval_ms = (Uint32)(iv_s * 1000);
				next_at = SDL_GetTicks() + (Uint32)(start_s * 1000);
				state = 1;
				fprintf(stderr, "[diag] autoclick armed: game(%d,%d) from %.1fs, every %.1fs\n",
					gx, gy, start_s, iv_s);
			} else {
				fprintf(stderr, "[diag] autoclick: bad config '%s' "
					"(want \"<start>:<x>:<y>[:<interval>]\")\n", cfg);
			}
		}
	}
	if (state != 1)
		return;

	Uint32 now = SDL_GetTicks();
	if (now < next_at)
		return;

	if (phase == 0) {
		/* game coordinates -> window coordinates */
		int wx = sdl.viewport.x + gx * sdl.viewport.w / sdl.w;
		int wy = sdl.viewport.y + gy * sdl.viewport.h / sdl.h;
		SDL_WarpMouseInWindow(sdl.window, wx, wy);
		SDL_PumpEvents();
		key_state[VK_LBUTTON] = true;
		phase = 1;
		next_at = now + 80;	/* hold the button for 80ms */
	} else {
		key_state[VK_LBUTTON] = false;
		phase = 0;
		next_at = now + interval_ms;
	}
}

// One-shot, process-local input probe. Never warps the system cursor.
static void diag_test_click(void)
{
	static bool initialized;
	static int phase;
	static struct {Uint32 at;SDL_Point point;} clicks[64];
	static int count,index;
	static Uint32 release;
	static Uint8 button = SDL_BUTTON_LEFT;
	if(!initialized) {
		initialized=true;
		if (getenv("XSYSTEM4_TEST_RIGHT"))
			button = SDL_BUTTON_RIGHT;
		const char *cfg=getenv("XSYSTEM4_TEST_CLICK");
		if(cfg) {
			sys_message("[test-click] cfg='%s'\n", cfg);
			char *copy=strdup(cfg);Uint32 start=SDL_GetTicks();
			for(char *item=strtok(copy,";,");item;item=strtok(NULL,";,")) {
				double seconds;int x,y;
				if(count>=64 || sscanf(item,"%lf:%d:%d",&seconds,&x,&y)!=3 || seconds<0) VM_ERROR("Invalid test click sequence");
				clicks[count].at=start+(Uint32)(seconds*1000);clicks[count++].point=(SDL_Point){x,y};
			}
			free(copy);
			if (count)
				sys_message("[test-click] armed %d click(s), first at %u (game %dx%d viewport %dx%d)\n",
					count, clicks[0].at, sdl.w, sdl.h, sdl.viewport.w, sdl.viewport.h);
		}
	}
	if(index>=count || SDL_GetTicks()<(phase?release:clicks[index].at)) return;
	probe_mouse_position=true;probe_mouse_point=clicks[index].point;
	// Give hover processing a frame before pressing. A zero-time jump and
	// press can otherwise activate the previous widget or only advance text.
	if(!phase){
		if (getenv("XSYSTEM4_TEST_FORCE_INPUT") && !parts_began_click)
			PE_BeginInput();
		phase=1;release=SDL_GetTicks()+120;return;
	}
	SDL_MouseButtonEvent event={.button=button,.state=phase==2?SDL_RELEASED:SDL_PRESSED};
	mouse_event(&event);
	sys_message("[probe] local mouse %s at (%d,%d)\n",phase==2?"up":"down",probe_mouse_point.x,probe_mouse_point.y);
	if(phase==2) {phase=0;index++;} else {phase=2;release=SDL_GetTicks()+100;}
}

/* State-driven click plan: waits for a part to exist before clicking, so a
 * scripted run does not depend on scene load timing.
 *   XSYSTEM4_TEST_AWAIT_CLICK="p<part>:x:y,c:x:y,d<ms>,..." */
static void diag_await_click(void)
{
	static bool initialized;
	enum { AWAIT_PART, CLICK_NOW, DELAY };
	static struct { int kind; int part; int x, y; Uint32 ms; } steps[48];
	static int count, index, phase;
	static Uint32 release, deadline;
	if (!initialized) {
		initialized = true;
		const char *cfg = getenv("XSYSTEM4_TEST_AWAIT_CLICK");
		if (cfg) {
			char *copy = strdup(cfg);
			for (char *it = strtok(copy, ";,"); it; it = strtok(NULL, ";,")) {
				int n = -1;
				if (count >= 48)
					break;
				if (it[0] == 'p' && sscanf(it + 1, "%d:%d:%d", &n, &steps[count].x, &steps[count].y) == 3)
					steps[count].kind = AWAIT_PART;
				else if (it[0] == 'c' && sscanf(it + 1, ":%d:%d", &steps[count].x, &steps[count].y) == 2) {
					steps[count].kind = CLICK_NOW;
					n = 0;
				} else if (it[0] == 'd' && sscanf(it + 1, "%u", &steps[count].ms) == 1) {
					steps[count].kind = DELAY;
					n = 0;
				} else
					continue;
				steps[count].part = n;
				count++;
			}
			free(copy);
			sys_message("[await] armed %d step(s)\n", count);
		}
	}
	if (index >= count)
		return;

	Uint32 now = SDL_GetTicks();
	int kind = steps[index].kind;
	if (kind == DELAY) {
		if (phase == 0) { deadline = now + steps[index].ms; phase = 1; return; }
		if (now < deadline) return;
		phase = 0; index++;
		return;
	}
	if (kind == AWAIT_PART) {
		if (phase == 0) {
			if (!PE_IsExist(steps[index].part))
				return;
			sys_message("[await] part=%d present -> click (%d,%d)\n",
				steps[index].part, steps[index].x, steps[index].y);
		}
	} else if (phase == 0) {
		sys_message("[await] click (%d,%d)\n", steps[index].x, steps[index].y);
	}

	if (phase == 0) {
		probe_mouse_position = true;
		probe_mouse_point.x = steps[index].x;
		probe_mouse_point.y = steps[index].y;
		phase = 1;
		release = now + 150;
		return;
	}
	if (phase == 1 && now >= release) {
		SDL_MouseButtonEvent down = {.button = SDL_BUTTON_LEFT, .state = SDL_PRESSED};
		mouse_event(&down);
		phase = 2;
		release = now + 100;
		return;
	}
	if (phase == 2 && now >= release) {
		SDL_MouseButtonEvent up = {.button = SDL_BUTTON_LEFT, .state = SDL_RELEASED};
		mouse_event(&up);
		phase = 0;
		index++;
	}
}

// Bounded hidden-window drag probe; never changes the host cursor.
static void diag_test_drag(void)
{
	static bool initialized;
	static int phase,x1,y1,x2,y2,duration;
	static Uint32 at,pressed;
	if(!initialized){
		initialized=true;const char *cfg=getenv("XSYSTEM4_TEST_DRAG");
		if(cfg){
			double seconds;
			if(sscanf(cfg,"%lf:%d:%d:%d:%d:%d",&seconds,&x1,&y1,&x2,&y2,&duration)!=6 || seconds<0 || duration<100 || duration>3000)
				VM_ERROR("Invalid test drag sequence");
			at=SDL_GetTicks()+(Uint32)(seconds*1000);phase=1;
		}
	}
	Uint32 now=SDL_GetTicks();if(!phase || now<at)return;
	probe_mouse_position=true;
	if(phase==1){probe_mouse_point=(SDL_Point){x1,y1};phase=2;at=now+120;return;}
	if(phase==2){
		SDL_MouseButtonEvent event={.button=SDL_BUTTON_LEFT,.state=SDL_PRESSED};mouse_event(&event);
		fprintf(stderr,"[probe] local drag down (%d,%d)\n",x1,y1);pressed=now;phase=3;return;
	}
	unsigned elapsed=min((unsigned)duration,now-pressed);
	probe_mouse_point=(SDL_Point){x1+(x2-x1)*(int)elapsed/duration,y1+(y2-y1)*(int)elapsed/duration};
	if(elapsed==(unsigned)duration){
		SDL_MouseButtonEvent event={.button=SDL_BUTTON_LEFT,.state=SDL_RELEASED};mouse_event(&event);
		fprintf(stderr,"[probe] local drag up (%d,%d)\n",x2,y2);phase=0;
	}
}

void handle_events(void)
{
	perf_window_poll();
	diag_autokey();
	diag_autoclick();
	diag_test_click();
	diag_await_click();
	diag_test_drag();
	fire_deferred_events();

	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_QUIT:
			if(input_hook_close_request())break;
			/* DSH diagnostic */
			fprintf(stderr, "[diag] SDL_QUIT in handle_events -> exit\n");
			fflush(stderr);
			vm_exit(0);
			break;
		case SDL_WINDOWEVENT:
			handle_window_event(&e);
			break;
		case SDL_APP_DIDENTERFOREGROUND:
			gfx_swap();
			break;
		case SDL_KEYDOWN:
			if (e.key.keysym.scancode == SDL_SCANCODE_F9) {
				vm_stack_trace();
			} else if (e.key.keysym.scancode == SDL_SCANCODE_F11) {
				gfx_toggle_fullscreen();
			} else if (e.key.keysym.scancode == SDL_SCANCODE_S) {
				if (e.key.keysym.mod & (KMOD_LALT | KMOD_RALT)) {
					screenshot_save();
				}
			}
			// fallthrough
		case SDL_KEYUP:
#ifdef __ANDROID__
			if (input_handler && e.key.timestamp < last_keyevent_timestamp + DEFERRED_KEY_DELAY) {
				// Input from virtual keyboard is sent as consecutive
				// SDL_KEYDOWN and SDL_KEYUP events. To give the game a chance
				// to see the previous key event, delay the event.
				struct deferred_keyevent *ev = xmalloc(sizeof(struct deferred_keyevent));
				ev->e = e;
				STAILQ_INSERT_TAIL(&deferred_keyevent_queue, ev, entry);
			} else {
				last_keyevent_timestamp = e.key.timestamp;
				key_event(&e.key, e.type == SDL_KEYDOWN);
			}
#else
			key_event(&e.key, e.type == SDL_KEYDOWN);
#endif
			break;
		case SDL_MOUSEBUTTONUP:
		case SDL_MOUSEBUTTONDOWN:
			if (mouse_sampling) {
				queue_mouse_sample(&e.button);
			} else if (e.button.which == SDL_TOUCH_MOUSEID) {
				if (deferred_synthetic_mouse_event.timestamp)
					synthetic_mouse_event(&deferred_synthetic_mouse_event);
				deferred_synthetic_mouse_event = e.button;
			} else {
				mouse_event(&e.button);
			}
			break;
		case SDL_MOUSEWHEEL:
			wheel_dir = e.wheel.y;
			break;
		case SDL_FINGERDOWN:
			if (SDL_GetNumTouchFingers(e.tfinger.touchId) >= 2) {
				// The user is about to start a multi-touch gesture.
				cancel_mouse_samples();
				long_touch_start_timestamp = 0;
				scroll_gesture_y = -1.0f;
				break;
			}
			long_touch_start_timestamp = e.tfinger.timestamp;
			// Movement within this rect (1% of the screen size from the touch
			// start position) will be ignored.
			long_touch_finger_rect = (SDL_FRect) {
				.x = e.tfinger.x - 0.01,
				.y = e.tfinger.y - 0.01,
				.w = 0.02,
				.h = 0.02
			};
			break;
		case SDL_FINGERMOTION:
			if (SDL_PointInFRect(&(SDL_FPoint){ e.tfinger.x, e.tfinger.y }, &long_touch_finger_rect))
				break;
			// Cancel the timer only if Ctrl emulation has not already started.
			if (!key_state[VK_CONTROL])
				long_touch_start_timestamp = 0;
			break;
		case SDL_FINGERUP:
			long_touch_start_timestamp = 0;
			break;
		case SDL_MULTIGESTURE:
			if (e.mgesture.numFingers == 2) {
				if (scroll_gesture_y < 0.0f)
					scroll_gesture_y = e.mgesture.y;
				float dy = scroll_gesture_y - e.mgesture.y;
				if (dy * dy > SCROLL_GESTURE_SENSITIVITY * SCROLL_GESTURE_SENSITIVITY) {
					wheel_dir = dy < 0 ? 1 : -1;  // Swipe up to scroll down.
					scroll_gesture_y = e.mgesture.y;
				}
			}
			break;
		case SDL_CONTROLLERDEVICEADDED:
			if (e.cdevice.which >= 0 && e.cdevice.which < MAX_CONTROLLERS)
				controllers[e.cdevice.which] = SDL_GameControllerOpen(e.cdevice.which);
			break;
		case SDL_CONTROLLERAXISMOTION:
			controller_axis_event(&e.caxis);
			break;
		case SDL_CONTROLLERBUTTONUP:
		case SDL_CONTROLLERBUTTONDOWN:
			controller_button_event(&e.cbutton);
			break;
		case SDL_TEXTINPUT:
			if (!input_handler) {
				break;
#ifdef __ANDROID__
			} else if (!STAILQ_EMPTY(&deferred_keyevent_queue)) {
				// The order of key events and text events must be preserved.
				struct deferred_keyevent *ev = xmalloc(sizeof(struct deferred_keyevent));
				ev->e = e;
				STAILQ_INSERT_TAIL(&deferred_keyevent_queue, ev, entry);
#endif
			} else {
				input_handler(e.text.text);
			}
			break;
		case SDL_TEXTEDITING:
			if (editing_handler)
				editing_handler(e.edit.text, e.edit.start, e.edit.length);
			break;
		default:
			break;
		}
	}
	if (dbg_dap)
		dbg_dap_handle_messages();
}
