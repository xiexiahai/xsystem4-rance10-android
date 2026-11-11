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

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <SDL.h>

#include "system4/ain.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "cJSON.h"
#include "input.h"
#include "gfx/gfx.h"
#include "mixer.h"
#include "savedata.h"
#include "vm.h"
#include "xsystem4.h"
#include "hll.h"
#include "system4/file.h"

// Engine-side game variables retain reset/handoff state for this process.
// They are separate from user saves and system preferences.
static struct {struct string *key,*value;} *game_variables;
static int nr_game_variables;
static int game_variable_index(const char *key)
{
	for(int i=0;i<nr_game_variables;i++) if(!strcmp(game_variables[i].key->text,key)) return i;
	return -1;
}
static bool SystemService_GameVariable_IsExist(struct string *key)
{
    bool found=game_variable_index(key->text)>=0;
    if(getenv("XSYSTEM4_TRACE_RESET_HANDOFF"))fprintf(stderr,"[reset-handoff] Exists key=%s result=%d count=%d\n",key->text,found,nr_game_variables);
    return found;
}
static void SystemService_GameVariable_Set(struct string *key,struct string *value)
{
	if(getenv("XSYSTEM4_TRACE_RESET_HANDOFF"))fprintf(stderr,"[reset-handoff] Set key=%s value=%s\n",key->text,value->text);
	int i=game_variable_index(key->text);
	if(i<0) {
		i=nr_game_variables++;
		game_variables=xrealloc(game_variables,sizeof(*game_variables)*nr_game_variables);
		game_variables[i].key=string_ref(key);game_variables[i].value=NULL;
	}
	if(game_variables[i].value) free_string(game_variables[i].value);
	game_variables[i].value=string_ref(value);
}
static struct string *SystemService_GameVariable_Get(struct string *key)
{
	int i=game_variable_index(key->text);
	if(getenv("XSYSTEM4_TRACE_RESET_HANDOFF"))fprintf(stderr,"[reset-handoff] Get key=%s found=%d count=%d\n",key->text,i>=0,nr_game_variables);
	return string_ref(i<0?&EMPTY_STRING:game_variables[i].value);
}
static int SystemService_GameVariable_NumofKey(void) {return nr_game_variables;}
static struct string *SystemService_GameVariable_GetKey(int i) {return string_ref(i<0 || i>=nr_game_variables?&EMPTY_STRING:game_variables[i].key);}
static void SystemService_GameVariable_Erase(struct string *key)
{
	if(getenv("XSYSTEM4_TRACE_RESET_HANDOFF"))fprintf(stderr,"[reset-handoff] Erase key=%s\n",key->text);
	int i=game_variable_index(key->text);if(i<0) return;
	free_string(game_variables[i].key);free_string(game_variables[i].value);
	memmove(game_variables+i,game_variables+i+1,sizeof(*game_variables)*(--nr_game_variables-i));
}

static int SystemService_GetMixerName(int n, struct string **name)
{
	const char *r = mixer_get_name(n);
	if (!r)
		return 0;
	*name = make_string(r, strlen(r));
	return 1;
}

static bool SystemService_GetMixerDefaultVolume(int n, int *volume)
{
	if (n < 0 || (unsigned)n >= config.mixer_nr_channels)
		return false;
	*volume = config.mixer_volumes[n];
	return true;
}

static bool SystemService_SetMixerName(int n, struct string *name)
{
	return mixer_set_name(n, name->text);
}

static int SystemService_GetGameVersion(void)
{
	return ain->game_version;
}

static struct string *SystemService_GetGameVersionByText(void)
{
	if (ain->version!=12)
		VM_ERROR("GetGameVersionByText identity is only defined for the AIN12 prototype");
	// Rance10 main's only call checks [2:5]+[10:15]+[22:27] against
	// "sdff29fosdhf1", plus equal numeric characters at 6/29 and 18/31.
	// This is xsystem4's compatibility identity, NOT a recovered native
	// version string. The AIN and its checks remain untouched.
	return cstr_to_string("xssdfe14-rf29fo0-c2mpasdhf1it1-2");
}


static void SystemService_GetGameName(struct string **game_name)
{
	if (*game_name)
		free_string(*game_name);
	*game_name = cstr_to_string(config.game_name);
}

// Rance 03's GetGameTitle returns an engine identification string, not
// AliceStart.ini's localized GameName. The original executable stores this
// 49-byte value at file offset 0x30224c (VA 0x703c4c); main validates three
// substrings and two numeric relations before entering game_main.
static void SystemService_GetGameTitle(struct string **name)
{
	if (*name)
		free_string(*name);
	*name = cstr_to_string("4aluoi@64sdfg354di87sd6f5g4s8e46zd5f4gs987gas.2f1");
}

HLL_WARN_UNIMPLEMENTED(false, bool, SystemService, AddURLMenu, struct string *title, struct string *url);

static bool SystemService_IsFullScreen(void)
{
	return gfx_is_fullscreen();
}

static bool SystemService_ChangeNormalScreen(void)
{
	return gfx_set_fullscreen(false);
}

static bool SystemService_ChangeFullScreen(void)
{
	return gfx_set_fullscreen(true);
}

HLL_WARN_UNIMPLEMENTED(false, bool, SystemService, InitMainWindowPosAndSize);

//static bool SystemService_UpdateView(void);
HLL_QUIET_UNIMPLEMENTED(false, bool, SystemService, UpdateView);

static int SystemService_GetViewWidth(void)
{
	return config.view_width;
}

static float SystemService_GetGameViewScaleRate(void)
{
	// The native renderer keeps game coordinates at their logical resolution.
	return 1.0f;
}

static void SystemService_ShowWaitMessage(bool show)
{
	// This runtime has no separate native startup dialog to dismiss.
	if (show)
		VM_ERROR("Showing a native wait dialog is not implemented");
}
static void SystemService_SetAndroidViewKeepScreen(bool keep)
{
	if(keep) SDL_DisableScreenSaver();else SDL_EnableScreenSaver();
}

static int SystemService_GetViewHeight(void)
{
	return config.view_height;
}

static bool SystemService_MoveMouseCursorPosImmediately(int x, int y)
{
	mouse_set_pos(x, y);
	return true;
}

static bool SystemService_SetHideMouseCursorByGame(bool hide)
{
	return mouse_show_cursor(!hide);
}

//bool SystemService_GetHideMouseCursorByGame(void);
HLL_WARN_UNIMPLEMENTED(false, bool, SystemService, SetUsePower2Texture, bool use);
//bool SystemService_GetUsePower2Texture(void);
HLL_WARN_UNIMPLEMENTED(true, bool, SystemService, SetAntiAliasingMode, int mode);
//int SystemService_GetAntiAliasingMode(void);

enum window_settings_asect_ratio {
	ASPECT_RATIO_NORMAL,
	ASPECT_RATIO_FIXED
};

enum window_settings_scaling_type {
	SCALING_NORMAL,
	SCALING_BICUBIC,
};

struct window_settings {
	enum window_settings_asect_ratio aspect_ratio;
	enum window_settings_scaling_type scaling_type;
	bool wait_vsync;
	bool record_pos_size;
	bool minimize_by_full_screen_inactive;
	bool back_to_title_confirm;
	bool close_game_confirm;
};

static struct window_settings window_settings = {
	.aspect_ratio = ASPECT_RATIO_NORMAL,
	.scaling_type = SCALING_NORMAL,
	.wait_vsync = false,
	.record_pos_size = false,
	.minimize_by_full_screen_inactive = true,
	.back_to_title_confirm = true,
	.close_game_confirm = true,
};

enum window_settings_id {
	WINDOW_SETTINGS_ASPECT_RATIO = 0,
	WINDOW_SETTINGS_SCALING_TYPE = 1,
	WINDOW_SETTINGS_WAIT_VSYNC = 2,
	WINDOW_SETTINGS_RECORD_POS_SIZE = 3,
	WINDOW_SETTINGS_MINIMIZE_BY_FULL_SCREEN_INACTIVE = 4,
	WINDOW_SETTINGS_BACK_TO_TITLE_CONFIRM = 5,
	WINDOW_SETTINGS_CLOSE_GAME_CONFIRM = 6,
};

static void save_window_settings(void)
{
	cJSON *root = cJSON_CreateObject();
	cJSON_AddBoolToObject(root, "wait_vsync", window_settings.wait_vsync);
	save_json("WindowSetting.json", root);
	cJSON_Delete(root);
}

static void load_window_settings(void)
{
	cJSON *root = load_json("WindowSetting.json");
	if (!root)
		return;
	cJSON *v;
	if ((v = cJSON_GetObjectItem(root, "wait_vsync"))) {
		window_settings.wait_vsync = cJSON_IsTrue(v);
		gfx_set_wait_vsync(window_settings.wait_vsync);
	}
	cJSON_Delete(root);
}

static bool SystemService_SetWindowSetting(int type, int value)
{
	switch (type) {
	case WINDOW_SETTINGS_ASPECT_RATIO:
		window_settings.aspect_ratio = value;
		break;
	case WINDOW_SETTINGS_SCALING_TYPE:
		window_settings.scaling_type = value;
		break;
	case WINDOW_SETTINGS_WAIT_VSYNC:
		window_settings.wait_vsync = value;
		gfx_set_wait_vsync(value);
		break;
	case WINDOW_SETTINGS_RECORD_POS_SIZE:
		window_settings.record_pos_size = value;
		break;
	case WINDOW_SETTINGS_MINIMIZE_BY_FULL_SCREEN_INACTIVE:
		window_settings.minimize_by_full_screen_inactive = value;
		break;
	case WINDOW_SETTINGS_BACK_TO_TITLE_CONFIRM:
		window_settings.back_to_title_confirm = value;
		break;
	case WINDOW_SETTINGS_CLOSE_GAME_CONFIRM:
		window_settings.close_game_confirm = value;
		break;
	default:
		WARNING("Invalid window setting type: %d", type);
		return false;
	}
	save_window_settings();
	return true;
}

static bool SystemService_GetWindowSetting(int type, int *value)
{
	switch (type) {
	case WINDOW_SETTINGS_ASPECT_RATIO:
		*value = window_settings.aspect_ratio;
		break;
	case WINDOW_SETTINGS_SCALING_TYPE:
		*value = window_settings.scaling_type;
		break;
	case WINDOW_SETTINGS_WAIT_VSYNC:
		*value = window_settings.wait_vsync;
		break;
	case WINDOW_SETTINGS_RECORD_POS_SIZE:
		*value = window_settings.record_pos_size;
		break;
	case WINDOW_SETTINGS_MINIMIZE_BY_FULL_SCREEN_INACTIVE:
		*value = window_settings.minimize_by_full_screen_inactive;
		break;
	case WINDOW_SETTINGS_BACK_TO_TITLE_CONFIRM:
		*value = window_settings.back_to_title_confirm;
		break;
	case WINDOW_SETTINGS_CLOSE_GAME_CONFIRM:
		*value = window_settings.close_game_confirm;
		break;
	default:
		WARNING("Invalid window setting type: %d", type);
		return false;
	}
	return true;
}

// XXX: Values for 'type' above 1 are invalid in Haru Urare, may be different in other games
#define NR_MOUSE_CURSOR_CONFIG 1
static int mouse_cursor_config[NR_MOUSE_CURSOR_CONFIG] = {0};

static bool SystemService_SetMouseCursorConfig(int type, int value)
{
	if (type < 0 || type >= NR_MOUSE_CURSOR_CONFIG) {
		WARNING("Invalid mouse cursor config type: %d", type);
		return false;
	}
	mouse_cursor_config[type] = value;
	return true;
}

static bool SystemService_GetMouseCursorConfig(int type, int *value)
{
	if (type < 0 || type >= NR_MOUSE_CURSOR_CONFIG) {
		WARNING("Invalid mouse cursor config type: %d", type);
		return false;
	}
	// XXX: the value returned here is always 1 or 0
	*value = !!mouse_cursor_config[type];
	return true;
}

//bool SystemService_RunProgram(struct string *program_file_name, struct string *parameter);
//bool SystemService_IsOpenedMutex(struct string *mutex_name);

void SystemService_GetGameFolderPath(struct string **folder_path)
{
	char *sjis = utf2sjis(config.game_dir, 0);
	*folder_path = make_string(sjis, strlen(sjis));
	free(sjis);
}

static void SystemService_GetTime(int *hour, int *min, int *sec)
{
	int ms;
	get_time(hour, min, sec, &ms);
}

static bool SystemService_IsResetOnce(void)
{
	return vm_reset_once;
}

static bool SystemService_IsResetOnce_Drapeko(struct string **text)
{
	*text = cstr_to_string("XXX TTT YYY"); // ???
	return vm_reset_once;
}

static char *get_manual_filename(void) {
	char *manual_path = path_join("Manual", "index.html");
	char *file_path = path_join(config.game_dir, manual_path);
	free(manual_path);
	return file_path;
}

static bool SystemService_IsExistPlayingManual(void) {
	char *filename = get_manual_filename();
	const bool exists = file_exists(filename);
	free(filename);
	return exists;
}

#ifndef _WIN32
static char *percent_encode(const char *str) {
	const char *hex = "0123456789ABCDEF";
	// Worst case all characters are percent-encoded
	char *encoded = xmalloc(strlen(str) * 3 + 1);

	char *p = encoded;
	while (*str) {
		const unsigned char c = *str++;
		if ((c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') ||
			c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
			*p++ = (char)c;
		} else {
			*p++ = '%';
			*p++ = hex[c >> 4];
			*p++ = hex[c & 15];
		}
	}

	*p = '\0';
	return encoded;
}
#endif

static void SystemService_OpenPlayingManual(void) {
	if (!SystemService_IsExistPlayingManual()) {
		return;
	}
#ifdef __ANDROID__
	const int COMMAND_OPEN_PLAYING_MANUAL = 0x8000;
	SDL_AndroidSendMessage(COMMAND_OPEN_PLAYING_MANUAL, 0);
#else
	char *filename = get_manual_filename();

	char *real_path = realpath_utf8(filename);
	free(filename);
	if (!real_path) {
		return;
	}

#ifdef _WIN32
	const char *prefix = "file:///";
	char *path_component = real_path;
#else
	const char *prefix = "file://";
	char *path_component = percent_encode(real_path);
	free(real_path);
#endif
	char *url = xmalloc(strlen(prefix) + strlen(path_component) + 1);
	strcpy(url, prefix);
	strcat(url, path_component);

	if (SDL_OpenURL(url) < 0) {
		WARNING("Failed to open manual at '%s': %s", url, SDL_GetError());
	}

	free(url);
	free(path_component);
#endif
}

static void SystemService_SetHookCloseApp(bool enabled) {input_set_close_hook(enabled);}
static bool SystemService_IsHookCloseApp(void) {return input_get_close_hook();}
static bool SystemService_IsExistSystemMessage(void) {return input_has_system_message();}
static bool SystemService_PopSystemMessage(int *message) {return input_pop_system_message(message);}
/* Close requests contain one integer event and no following string payload. */
static bool SystemService_PopSystemMessageString(struct string **message) {return false;}

static void SystemService_RestrainScreensaver(void) { }

//static int SystemService_Debug_GetUseVideoMemorySize(void);

// Rance 01
static void SystemService_Rance0123456789(struct string **text)
{
	*text = cstr_to_string("-RANCE010ECNAR-");
}

// Rance 01 trial edition
static void SystemService_XXXXX01XXXXXXXX(struct string **text)
{
	*text = cstr_to_string("RANCE01RANCEKAKKOII");
}

// Drapeko
static void SystemService_Test(struct string **text)
{
	*text = cstr_to_string("DELETE ALL 758490275489207548093");
}

// Drapeko trial edition
static void SystemService_DRPKT(struct string **text)
{
	*text = cstr_to_string("DRPKT QWERTY NUFUAUEO 75849027582754829");
}

// Rance 9
static void SystemService_Rance96161988(struct string **text) {
	*text = cstr_to_string("=Rance99/RANCE99=");
}

// Pascha3 Plus Contents
static void SystemService_XXX(struct string **text) {
	*text = cstr_to_string("FORMAT HDD ERASE 578205024758284076520478254092784789752384758204687293");
}

// Busou Shoujotai Blade Briders
static void SystemService_GetSystemBufferName(struct string **text) {
	*text = cstr_to_string("xxx4wxx:xxxx-xxxxxfx5x3");
}

static void SystemService_PreLink(void);

/* void GetMemoryInfo(ref int pMax, ref int pUse)
 *
 * 游戏启动时通过它询问系统内存规模。上游完全没有这个导出，调用会命中
 * "Unimplemented HLL function" 并以 ERROR 中止启动，导致游戏打不开。
 * pMax / pUse 的单位是 MB。 */
static bool SystemService_GetMemoryInfo(int *pMax, int *pUse)
{
	/* SDL 提供物理内存总量的跨平台查询（单位 MB）。 */
	int total_mb = SDL_GetSystemRAM();
	int avail_mb = 0;

#if !defined(_WIN32)
	/* Linux / Android：从 /proc/meminfo 读可用内存，用来估算已用量。 */
	FILE *f = fopen("/proc/meminfo", "r");
	if (f) {
		char line[256];
		long avail_kb = 0;
		while (fgets(line, sizeof(line), f)) {
			long v;
			if (sscanf(line, "MemAvailable: %ld kB", &v) == 1)
				avail_kb = v;
		}
		fclose(f);
		if (avail_kb > 0)
			avail_mb = (int)(avail_kb / 1024);
	}
#endif

	*pMax = total_mb;
	if (total_mb > 0 && avail_mb > 0) {
		*pUse = (total_mb > avail_mb) ? total_mb - avail_mb : 0;
	} else {
		*pUse = 0;
	}
	return true;
}

/* void GetCPUInfo(ref string pVenderName, ref int pSignature, ref int pCPUFlag, ref string pBrandName)
 *
 * 与 GetMemoryInfo 属于同一批启动期硬件探测。上游同样没有导出，缺失时
 * 会在 GetMemoryInfo 修好之后紧接着以 ERROR 中止启动。
 * 游戏只用它做展示与分支，这里返回稳定的占位值即可。 */
static bool SystemService_GetCPUInfo(struct string **vendor, int *signature,
                                     int *flags, struct string **brand)
{
	*vendor = cstr_to_string("Generic");
	*brand = cstr_to_string("xsystem4");
	*signature = 0;
	*flags = 0;
	return true;
}

/* void GetOSInfo(ref string pText) */
static bool SystemService_GetOSInfo(struct string **text)
{
#if defined(__ANDROID__)
	*text = cstr_to_string("Android");
#elif defined(_WIN32)
	*text = cstr_to_string("Windows");
#elif defined(__linux__)
	*text = cstr_to_string("Linux");
#else
	*text = cstr_to_string("Unknown");
#endif
	return true;
}

/* --- 补齐 SystemService 缺失的导出 ---
 *
 * 上游完全没有这些函数，而《ランス03》在读取 PC 版存档后会逐个探测
 * 内存 / CPU / 屏幕 / 视图等信息，缺失时命中
 *   *ERROR* Unimplemented HLL function
 * 并直接中止启动，表现为"游戏打不开"。
 *
 * 简单能算的直接给真值；复杂的用 HLL_WARN_UNIMPLEMENTED 注册成
 * "打警告 + 返回默认值"的桩，保证游戏能继续跑。 */

static int SystemService_GetDefaultViewWidth(void)  { return config.view_width; }
static int SystemService_GetDefaultViewHeight(void) { return config.view_height; }

static bool SystemService_GetScreenInfo(struct string **text)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "%dx%d", config.view_width, config.view_height);
	*text = cstr_to_string(buf);
	return true;
}

HLL_WARN_UNIMPLEMENTED(false, bool, SystemService, RenderToSurfaceImage, struct page **SurfaceImage);
HLL_WARN_UNIMPLEMENTED(false, bool, SystemService, RenderToSurfaceImageByScale, struct page **SurfaceImage, int ReductionSize);
HLL_WARN_UNIMPLEMENTED(false, bool, SystemService, SetViewResizableMode, bool ViewResizableMode);
HLL_WARN_UNIMPLEMENTED(false, bool, SystemService, IsViewResizableMode);
HLL_WARN_UNIMPLEMENTED(false, bool, SystemService, ScanFloat, struct string *Text, float *Value);
HLL_WARN_UNIMPLEMENTED(false, bool, SystemService, SetWaitVSyncMode, int Mode);
HLL_WARN_UNIMPLEMENTED(0, int, SystemService, GetWaitVSyncMode);
HLL_WARN_UNIMPLEMENTED(false, bool, SystemService, Debug_SetShowMipmapLevel, bool DebugMipmapLevel);
HLL_WARN_UNIMPLEMENTED(false, bool, SystemService, Debug_IsShowMipmapLevel);

static void SystemService_ModuleInit(void)
{
	input_reset_close_hook();
	load_window_settings();
}

HLL_LIBRARY(SystemService,
	    HLL_EXPORT(SetHookCloseApp, SystemService_SetHookCloseApp),
	    HLL_EXPORT(IsHookCloseApp, SystemService_IsHookCloseApp),
	    HLL_EXPORT(SetAndroidViewKeepScreen, SystemService_SetAndroidViewKeepScreen),
	    HLL_EXPORT(GameVariable_IsExist, SystemService_GameVariable_IsExist),
	    HLL_EXPORT(GameVariable_Set, SystemService_GameVariable_Set),
	    HLL_EXPORT(GameVariable_Get, SystemService_GameVariable_Get),
	    HLL_EXPORT(GameVariable_NumofKey, SystemService_GameVariable_NumofKey),
	    HLL_EXPORT(GameVariable_GetKey, SystemService_GameVariable_GetKey),
	    HLL_EXPORT(GameVariable_Erase, SystemService_GameVariable_Erase),
	    HLL_EXPORT(ShowWaitMessage, SystemService_ShowWaitMessage),
	    HLL_EXPORT(GetGameVersionByText, SystemService_GetGameVersionByText),
	    HLL_EXPORT(GetGameViewScaleRate, SystemService_GetGameViewScaleRate),
	    HLL_EXPORT(_PreLink, SystemService_PreLink),
	    HLL_EXPORT(_ModuleInit, SystemService_ModuleInit),
	    HLL_EXPORT(GetMixerNumof, mixer_get_numof),
	    HLL_EXPORT(GetMixerName, SystemService_GetMixerName),
	    HLL_EXPORT(GetMixerVolume, mixer_get_volume),
	    HLL_EXPORT(GetMixerDefaultVolume, SystemService_GetMixerDefaultVolume),
	    HLL_EXPORT(GetMixerMute, mixer_get_mute),
	    HLL_EXPORT(SetMixerName, SystemService_SetMixerName),
	    HLL_EXPORT(SetMixerVolume, mixer_set_volume),
	    HLL_EXPORT(SetMixerMute, mixer_set_mute),
	    HLL_EXPORT(GetGameVersion, SystemService_GetGameVersion),
	    HLL_EXPORT(GetGameName, SystemService_GetGameName),
	    HLL_EXPORT(GetGameTitle, SystemService_GetGameTitle),
	    HLL_EXPORT(GetMemoryInfo, SystemService_GetMemoryInfo),
	    HLL_EXPORT(GetCPUInfo, SystemService_GetCPUInfo),
	    HLL_EXPORT(GetOSInfo, SystemService_GetOSInfo),
	    HLL_EXPORT(GetScreenInfo, SystemService_GetScreenInfo),
	    HLL_EXPORT(GetDefaultViewWidth, SystemService_GetDefaultViewWidth),
	    HLL_EXPORT(GetDefaultViewHeight, SystemService_GetDefaultViewHeight),
	    HLL_EXPORT(RenderToSurfaceImage, SystemService_RenderToSurfaceImage),
	    HLL_EXPORT(RenderToSurfaceImageByScale, SystemService_RenderToSurfaceImageByScale),
	    HLL_EXPORT(SetViewResizableMode, SystemService_SetViewResizableMode),
	    HLL_EXPORT(IsViewResizableMode, SystemService_IsViewResizableMode),
	    HLL_EXPORT(ScanFloat, SystemService_ScanFloat),
	    HLL_EXPORT(SetWaitVSyncMode, SystemService_SetWaitVSyncMode),
	    HLL_EXPORT(GetWaitVSyncMode, SystemService_GetWaitVSyncMode),
	    HLL_EXPORT(Debug_SetShowMipmapLevel, SystemService_Debug_SetShowMipmapLevel),
	    HLL_EXPORT(Debug_IsShowMipmapLevel, SystemService_Debug_IsShowMipmapLevel),
	    HLL_EXPORT(AddURLMenu, SystemService_AddURLMenu),
	    HLL_EXPORT(IsFullScreen, SystemService_IsFullScreen),
	    HLL_EXPORT(ChangeNormalScreen, SystemService_ChangeNormalScreen),
	    HLL_EXPORT(ChangeFullScreen, SystemService_ChangeFullScreen),
	    HLL_EXPORT(InitMainWindowPosAndSize, SystemService_InitMainWindowPosAndSize),
	    HLL_EXPORT(UpdateView, SystemService_UpdateView),
	    HLL_EXPORT(GetViewWidth, SystemService_GetViewWidth),
	    HLL_EXPORT(GetViewHeight, SystemService_GetViewHeight),
	    HLL_EXPORT(MoveMouseCursorPosImmediately, SystemService_MoveMouseCursorPosImmediately),
	    HLL_EXPORT(SetHideMouseCursorByGame, SystemService_SetHideMouseCursorByGame),
	    HLL_TODO_EXPORT(GetHideMouseCursorByGame, SystemService_GetHideMouseCursorByGame),
	    HLL_EXPORT(SetUsePower2Texture, SystemService_SetUsePower2Texture),
	    HLL_TODO_EXPORT(GetUsePower2Texture, SystemService_GetUsePower2Texture),
	    HLL_EXPORT(SetAntiAliasingMode, SystemService_SetAntiAliasingMode),
	    HLL_TODO_EXPORT(GetAntiAliasingMode, SystemService_GetAntiAliasingMode),
	    HLL_EXPORT(SetWindowSetting, SystemService_SetWindowSetting),
	    HLL_EXPORT(GetWindowSetting, SystemService_GetWindowSetting),
	    HLL_EXPORT(SetMouseCursorConfig, SystemService_SetMouseCursorConfig),
	    HLL_EXPORT(GetMouseCursorConfig, SystemService_GetMouseCursorConfig),
	    HLL_TODO_EXPORT(RunProgram, SystemService_RunProgram),
	    HLL_TODO_EXPORT(IsOpenedMutex, SystemService_IsOpenedMutex),
	    HLL_EXPORT(GetGameFolderPath, SystemService_GetGameFolderPath),
	    HLL_EXPORT(GetDate, get_date),
	    HLL_EXPORT(GetTime, SystemService_GetTime),
	    HLL_EXPORT(IsResetOnce, SystemService_IsResetOnce),
	    HLL_EXPORT(OpenPlayingManual, SystemService_OpenPlayingManual),
	    HLL_EXPORT(IsExistPlayingManual, SystemService_IsExistPlayingManual),
	    HLL_EXPORT(IsExistSystemMessage, SystemService_IsExistSystemMessage),
	    HLL_EXPORT(PopSystemMessage, SystemService_PopSystemMessage),
	    HLL_EXPORT(PopSystemMessageString, SystemService_PopSystemMessageString),
	    HLL_EXPORT(RestrainScreensaver, SystemService_RestrainScreensaver),
	    HLL_TODO_EXPORT(Debug_GetUseVideoMemorySize, SystemService_Debug_GetUseVideoMemorySize),
	    HLL_EXPORT(Rance0123456789, SystemService_Rance0123456789),
	    HLL_EXPORT(XXXXX01XXXXXXXX, SystemService_XXXXX01XXXXXXXX),
	    HLL_EXPORT(GetSystemBufferName, SystemService_GetSystemBufferName),
	    HLL_EXPORT(XXX, SystemService_XXX),
	    HLL_EXPORT(Test, SystemService_Test),
	    HLL_EXPORT(DRPKT, SystemService_DRPKT),
	    HLL_EXPORT(Rance96161988, SystemService_Rance96161988)
	);

static struct ain_hll_function *get_fun(int libno, const char *name)
{
	int fno = ain_get_library_function(ain, libno, name);
	return fno >= 0 ? &ain->libraries[libno].functions[fno] : NULL;
}

static void SystemService_PreLink(void)
{
	struct ain_hll_function *fun;
	int libno = ain_get_library(ain, "SystemService");
	assert(libno >= 0);

	fun = get_fun(libno, "IsResetOnce");
	if (fun && fun->nr_arguments == 1) {
		static_library_replace(&lib_SystemService, "IsResetOnce",
				SystemService_IsResetOnce_Drapeko);
	}
}
