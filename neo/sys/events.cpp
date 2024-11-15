/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.	If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include <SDL.h>
#include <SDL_gamecontroller.h>

#include "sys/platform.h"
#include "idlib/containers/List.h"
#include "idlib/Heap.h"
#include "framework/Common.h"
#include "framework/Console.h"
#include "framework/KeyInput.h"
#include "framework/Session_local.h"
#include "renderer/RenderSystem.h"
#include "renderer/tr_local.h"

#include "sys/sys_public.h"

#if !SDL_VERSION_ATLEAST(2, 0, 0)
#define SDL_Keycode SDLKey
#define SDLK_APPLICATION SDLK_COMPOSE
#define SDLK_SCROLLLOCK SDLK_SCROLLOCK
#define SDLK_LGUI SDLK_LSUPER
#define SDLK_RGUI SDLK_RSUPER
#define SDLK_KP_0 SDLK_KP0
#define SDLK_KP_1 SDLK_KP1
#define SDLK_KP_2 SDLK_KP2
#define SDLK_KP_3 SDLK_KP3
#define SDLK_KP_4 SDLK_KP4
#define SDLK_KP_5 SDLK_KP5
#define SDLK_KP_6 SDLK_KP6
#define SDLK_KP_7 SDLK_KP7
#define SDLK_KP_8 SDLK_KP8
#define SDLK_KP_9 SDLK_KP9
#define SDLK_NUMLOCKCLEAR SDLK_NUMLOCK
#define SDLK_PRINTSCREEN SDLK_PRINT
#endif

#ifdef IMGUI_TOUCHSCREEN
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include "framework/Session_local.h"
#include <functional>
#endif

// NOTE: g++-4.7 doesn't like when this is static (for idCmdSystem::ArgCompletion_String<kbdNames>)
const char *_in_kbdNames[] = {
#if SDL_VERSION_ATLEAST(2, 0, 0) // auto-detection is only available for SDL2
	"auto",
#endif
	"english", "french", "german", "italian", "spanish", "turkish", "norwegian", "brazilian", NULL
};

bool Sys_joypadEvent(SDL_Event *event, sysEvent_t &res);

static idCVar in_kbd("in_kbd", _in_kbdNames[0], CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_NOCHEAT, "keyboard layout", _in_kbdNames, idCmdSystem::ArgCompletion_String<_in_kbdNames> );
// TODO: I'd really like to make in_ignoreConsoleKey default to 1, but I guess there would be too much confusion :-/
static idCVar in_ignoreConsoleKey("in_ignoreConsoleKey", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_NOCHEAT | CVAR_BOOL,
		"Console only opens with Shift+Esc, not ` or ^ etc");

static idCVar in_nograb("in_nograb", "0", CVAR_SYSTEM | CVAR_NOCHEAT, "prevents input grabbing");
static idCVar in_grabKeyboard("in_grabKeyboard", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_NOCHEAT | CVAR_BOOL,
		"if enabled, grabs all keyboard input if mouse is grabbed (so keyboard shortcuts from the OS like Alt-Tab or Windows Key won't work)");

// set in handleMouseGrab(), used in Sys_GetEvent() to decide what kind of internal mouse event to generate
static bool in_relativeMouseMode = true;
// set in Sys_GetEvent() on window focus gained/lost events
static bool in_hasFocus = true;

enum ImGui_TouchItem {
	// InGame gui
	TouchItem_ESC,
	TouchItem_Fire,
	TouchItem_Reload,
	TouchItem_Flashlight,
	TouchItem_Jump,
	TouchItem_Crounch,
	TouchItem_WeaponNext,
	TouchItem_WeaponPrev,
	TouchItem_PDA,
	TouchItem_QuickSave,
	TouchItem_QuickLoad,
	TouchItem_Zoom,
	// =======
	TouchItem_Settings,
	// Left and Right side areas should be last in enum
	TouchItem_LeftSide,
	TouchItem_RightSide,
	TouchItem_Count
};

#ifdef IMGUI_TOUCHSCREEN
static struct imgui_touch_state_t {
	int fingerId;
	ImGuiWindow *window;
	ImVec2 press_pos;
	std::function<sysEvent_t(const ImVec2 &, const ImVec2 &, SDL_Event &)> handler;

	imgui_touch_state_t() {
		fingerId = -1;
		window = NULL;
	}

	imgui_touch_state_t(int tid, ImGuiWindow *w) {
		fingerId = tid;
		window = w;
	}
} touch_fingers[TouchItem_Count];

#ifdef USE_LIPSTICK_FBO
std::function<void(SDL_TouchFingerEvent &tfinger, ImVec2 &finger_pos, ImVec2 &finger_dpos)> finger_pos_current_handler;
std::function<void(SDL_TouchFingerEvent &tfinger, ImVec2 &finger_pos, ImVec2 &finger_dpos)> finger_pos_landscape_handler;
std::function<void(SDL_TouchFingerEvent &tfinger, ImVec2 &finger_pos, ImVec2 &finger_dpos)> finger_pos_inverted_landscape_handler;
#endif
#endif

static const sysEvent_t default_res_none = { SE_NONE, 0, 0, 0, NULL };

struct kbd_poll_t {
	int key;
	bool state;

	kbd_poll_t() {
	}

	kbd_poll_t(int k, bool s) {
		key = k;
		state = s;
	}
};

struct mouse_poll_t {
	int action;
	int value;

	mouse_poll_t() {
	}

	mouse_poll_t(int a, int v) {
		action = a;
		value = v;
	}
};

typedef mouse_poll_t joypad_poll_t;

static idList<kbd_poll_t> kbd_polls;
static idList<mouse_poll_t> mouse_polls;
static idList<joypad_poll_t> joystick_polls;
static idList<mouse_poll_t> imgui_polls;

static SDL_Joystick *joystick = NULL;
static SDL_GameController *controller = NULL;

static int joy_axis_state[] = {0,0,0,0,0};

#define JOYSTICK_DEAD_ZONE 8000
#define JOYAXIS_MAX 32768
// joy axis state index
#define JOY_LEFT_STICK_XAXIS    0
#define JOY_LEFT_STICK_YAXIS    1
#define JOY_RIGHT_STICK_XAXIS   2
#define JOY_RIGHT_STICK_YAXIS   3

#define JOY_LOOK_SENSE 30.0f
#define JOY_AXIS_RMOUSE(value) (int)(((float)value / (float)(JOYAXIS_MAX - JOYSTICK_DEAD_ZONE)) * JOY_LOOK_SENSE);

#if SDL_VERSION_ATLEAST(2, 0, 0)
// for utf8ToISO8859_1() - used for non-ascii text input and Sys_GetLocalizedScancodeName()
static SDL_iconv_t iconvDesc = (SDL_iconv_t)-1;
#endif

struct scancodename_t {
	int sdlScancode;
	const char* name;
};

// scancodenames[keynum - K_FIRST_SCANCODE] belongs to keynum
static scancodename_t scancodemappings[] = {
	// NOTE: must be kept in sync with the K_SC_* section of keyNum_t in framework/KeyInput.h !

#if SDL_VERSION_ATLEAST(2, 0, 0)
	#define D3_SC_MAPPING(X) { SDL_SCANCODE_ ## X , "SC_" #X }
#else // SDL1.2 doesn't have scancodes
	#define D3_SC_MAPPING(X) { 0 , "SC_" #X }
#endif

	D3_SC_MAPPING(A), // { SDL_SCANCODE_A, "SC_A" },
	D3_SC_MAPPING(B),
	D3_SC_MAPPING(C),
	D3_SC_MAPPING(D),
	D3_SC_MAPPING(E),
	D3_SC_MAPPING(F),
	D3_SC_MAPPING(G),
	D3_SC_MAPPING(H),
	D3_SC_MAPPING(I),
	D3_SC_MAPPING(J),
	D3_SC_MAPPING(K),
	D3_SC_MAPPING(L),
	D3_SC_MAPPING(M),
	D3_SC_MAPPING(N),
	D3_SC_MAPPING(O),
	D3_SC_MAPPING(P),
	D3_SC_MAPPING(Q),
	D3_SC_MAPPING(R),
	D3_SC_MAPPING(S),
	D3_SC_MAPPING(T),
	D3_SC_MAPPING(U),
	D3_SC_MAPPING(V),
	D3_SC_MAPPING(W),
	D3_SC_MAPPING(X),
	D3_SC_MAPPING(Y),
	D3_SC_MAPPING(Z),
	// leaving out SDL_SCANCODE_1 ... _0, we handle them separately already
	// also return, escape, backspace, tab, space, already handled as keycodes
	D3_SC_MAPPING(MINUS),
	D3_SC_MAPPING(EQUALS),
	D3_SC_MAPPING(LEFTBRACKET),
	D3_SC_MAPPING(RIGHTBRACKET),
	D3_SC_MAPPING(BACKSLASH),
	D3_SC_MAPPING(NONUSHASH),
	D3_SC_MAPPING(SEMICOLON),
	D3_SC_MAPPING(APOSTROPHE),
	D3_SC_MAPPING(GRAVE),
	D3_SC_MAPPING(COMMA),
	D3_SC_MAPPING(PERIOD),
	D3_SC_MAPPING(SLASH),
	// leaving out lots of key incl. from keypad, we already handle them as normal keys
	D3_SC_MAPPING(NONUSBACKSLASH),
	D3_SC_MAPPING(INTERNATIONAL1), /**< used on Asian keyboards, see footnotes in USB doc */
	D3_SC_MAPPING(INTERNATIONAL2),
	D3_SC_MAPPING(INTERNATIONAL3), /**< Yen */
	D3_SC_MAPPING(INTERNATIONAL4),
	D3_SC_MAPPING(INTERNATIONAL5),
	D3_SC_MAPPING(INTERNATIONAL6),
	D3_SC_MAPPING(INTERNATIONAL7),
	D3_SC_MAPPING(INTERNATIONAL8),
	D3_SC_MAPPING(INTERNATIONAL9),
	D3_SC_MAPPING(THOUSANDSSEPARATOR),
	D3_SC_MAPPING(DECIMALSEPARATOR),
	D3_SC_MAPPING(CURRENCYUNIT),
	D3_SC_MAPPING(CURRENCYSUBUNIT)

#undef D3_SC_MAPPING
};

// for keynums between K_FIRST_SCANCODE and K_LAST_SCANCODE
// returns e.g. "SC_A" for K_SC_A
const char* Sys_GetScancodeName( int key ) {
	if ( key >= K_FIRST_SCANCODE && key <= K_LAST_SCANCODE ) {
		int scIdx = key - K_FIRST_SCANCODE;
		return scancodemappings[scIdx].name;
	}
	return NULL;
}

#if SDL_VERSION_ATLEAST(2, 0, 0)
static bool isAscii( const char* str_ ) {
	const unsigned char* str = (const unsigned char*)str_;
	while(*str != '\0') {
		if(*str > 127) {
			return false;
		}
		++str;
	}
	return true;
}

// convert inbuf (which is expected to be in UTF-8) to outbuf (in ISO-8859-1)
static bool utf8ToISO8859_1(const char* inbuf, char* outbuf, size_t outsize) {
	if ( iconvDesc == (SDL_iconv_t)-1 ) {
		return false;
	}

	size_t outbytesleft = outsize;
	size_t inbytesleft = strlen( inbuf ) + 1; // + terminating \0
	size_t ret = SDL_iconv( iconvDesc, &inbuf, &inbytesleft, &outbuf, &outbytesleft );

	while(inbytesleft > 0) {
		switch ( ret ) {
			case SDL_ICONV_E2BIG:
				outbuf[outbytesleft-1] = '\0'; // whatever, just cut it off..
				common->DPrintf( "Cutting off UTF-8 to ISO-8859-1 conversion to '%s' because destination is too small for '%s'\n", outbuf, inbuf );
				SDL_iconv( iconvDesc, NULL, NULL, NULL, NULL ); // reset descriptor for next conversion
				return true;
			case SDL_ICONV_EILSEQ:
				// try skipping invalid input data
				++inbuf;
				--inbytesleft;
				break;
			case SDL_ICONV_EINVAL:
			case SDL_ICONV_ERROR:
				// we can't recover from this
				SDL_iconv( iconvDesc, NULL, NULL, NULL, NULL ); // reset descriptor for next conversion
				return false;
		}
	}
	SDL_iconv( iconvDesc, NULL, NULL, NULL, NULL ); // reset descriptor for next conversion
	return outbytesleft < outsize; // return false if no char was written
}
#endif // SDL2

// returns localized name of the key (between K_FIRST_SCANCODE and K_LAST_SCANCODE),
// regarding the current keyboard layout - if that name is in ASCII or corresponds
// to a "High-ASCII" char supported by Doom3.
// Otherwise return same name as Sys_GetScancodeName()
// !! Returned string is only valid until next call to this function !!
const char* Sys_GetLocalizedScancodeName( int key ) {
	if ( key >= K_FIRST_SCANCODE && key <= K_LAST_SCANCODE ) {
		int scIdx = key - K_FIRST_SCANCODE;
#if SDL_VERSION_ATLEAST(2, 0, 0)
		SDL_Scancode sc = ( SDL_Scancode ) scancodemappings[scIdx].sdlScancode;
		SDL_Keycode k = SDL_GetKeyFromScancode( sc );
		if ( k >= 0xA1 && k <= 0xFF ) {
			// luckily, the "High-ASCII" (ISO-8559-1) chars supported by Doom3
			// have the same values as the corresponding SDL_Keycodes.
			static char oneCharStr[2] = {0, 0};
			oneCharStr[0] = (unsigned char)k;
			return oneCharStr;
		} else if ( k != SDLK_UNKNOWN ) {
			const char *ret = SDL_GetKeyName( k );
			// the keyname from SDL2 is in UTF-8, which Doom3 can't print,
			// so only return the name if it's ASCII, otherwise fall back to "SC_bla"
			if ( ret && *ret != '\0' ) {
				if( isAscii( ret ) ) {
					return ret;
				}
				static char isoName[32];
				// try to convert name to ISO8859-1 (Doom3's supported "High ASCII")
				if ( utf8ToISO8859_1( ret, isoName, sizeof(isoName) ) && isoName[0] != '\0' ) {
					return isoName;
				}
			}
		}
#endif  // SDL1.2 doesn't support this, use unlocalized name (also as fallback if we couldn't get a keyname)
		return scancodemappings[scIdx].name;

	}
	return NULL;
}

// returns keyNum_t (K_SC_* constant) for given scancode name (like "SC_A")
// only makes sense to call it if name starts with "SC_" (or "sc_")
// returns -1 if not found
int Sys_GetKeynumForScancodeName( const char* name ) {
	for( int scIdx = 0; scIdx < K_NUM_SCANCODES; ++scIdx ) {
		if ( idStr::Icmp( name, scancodemappings[scIdx].name ) == 0 ) {
			return scIdx + K_FIRST_SCANCODE;
		}
	}
	return -1;
}

#if SDL_VERSION_ATLEAST(2, 0, 0)
static int getKeynumForSDLscancode( SDL_Scancode scancode ) {
	int sc = scancode;
	for ( int scIdx=0; scIdx < K_NUM_SCANCODES; ++scIdx ) {
		if ( scancodemappings[scIdx].sdlScancode == sc ) {
			return scIdx + K_FIRST_SCANCODE;
		}
	}
	return 0;
}
#endif

static byte mapkey(SDL_Keycode key) {
	switch (key) {
	case SDLK_BACKSPACE:
		return K_BACKSPACE;
	case SDLK_PAUSE:
		return K_PAUSE;
	}

	if (key <= SDLK_z)
		return key & 0xff;

	switch (key) {
	case SDLK_APPLICATION:
		return K_COMMAND;
	case SDLK_CAPSLOCK:
		return K_CAPSLOCK;
	case SDLK_SCROLLLOCK:
		return K_SCROLL;
	case SDLK_POWER:
		return K_POWER;

	case SDLK_UP:
		return K_UPARROW;
	case SDLK_DOWN:
		return K_DOWNARROW;
	case SDLK_LEFT:
		return K_LEFTARROW;
	case SDLK_RIGHT:
		return K_RIGHTARROW;

	case SDLK_LGUI:
		return K_LWIN;
	case SDLK_RGUI:
		return K_RWIN;
	case SDLK_MENU:
		return K_MENU;

	case SDLK_LALT:
		return K_ALT;
	case SDLK_RALT:
		return K_RIGHT_ALT;
	case SDLK_RCTRL:
		return K_RIGHT_CTRL;
	case SDLK_LCTRL:
		return K_CTRL;
	case SDLK_RSHIFT:
		return K_RIGHT_SHIFT;
	case SDLK_LSHIFT:
		return K_SHIFT;
	case SDLK_INSERT:
		return K_INS;
	case SDLK_DELETE:
		return K_DEL;
	case SDLK_PAGEDOWN:
		return K_PGDN;
	case SDLK_PAGEUP:
		return K_PGUP;
	case SDLK_HOME:
		return K_HOME;
	case SDLK_END:
		return K_END;

	case SDLK_F1:
		return K_F1;
	case SDLK_F2:
		return K_F2;
	case SDLK_F3:
		return K_F3;
	case SDLK_F4:
		return K_F4;
	case SDLK_F5:
		return K_F5;
	case SDLK_F6:
		return K_F6;
	case SDLK_F7:
		return K_F7;
	case SDLK_F8:
		return K_F8;
	case SDLK_F9:
		return K_F9;
	case SDLK_F10:
		return K_F10;
	case SDLK_F11:
		return K_F11;
	case SDLK_F12:
		return K_F12;
	// K_INVERTED_EXCLAMATION;
	case SDLK_F13:
		return K_F13;
	case SDLK_F14:
		return K_F14;
	case SDLK_F15:
		return K_F15;

	case SDLK_KP_7:
		return K_KP_HOME;
	case SDLK_KP_8:
		return K_KP_UPARROW;
	case SDLK_KP_9:
		return K_KP_PGUP;
	case SDLK_KP_4:
		return K_KP_LEFTARROW;
	case SDLK_KP_5:
		return K_KP_5;
	case SDLK_KP_6:
		return K_KP_RIGHTARROW;
	case SDLK_KP_1:
		return K_KP_END;
	case SDLK_KP_2:
		return K_KP_DOWNARROW;
	case SDLK_KP_3:
		return K_KP_PGDN;
	case SDLK_KP_ENTER:
		return K_KP_ENTER;
	case SDLK_KP_0:
		return K_KP_INS;
	case SDLK_KP_PERIOD:
		return K_KP_DEL;
	case SDLK_KP_DIVIDE:
		return K_KP_SLASH;
	// K_SUPERSCRIPT_TWO;
	case SDLK_KP_MINUS:
		return K_KP_MINUS;
	// K_ACUTE_ACCENT;
	case SDLK_KP_PLUS:
		return K_KP_PLUS;
	case SDLK_NUMLOCKCLEAR:
		return K_KP_NUMLOCK;
	case SDLK_KP_MULTIPLY:
		return K_KP_STAR;
	case SDLK_KP_EQUALS:
		return K_KP_EQUALS;

	// K_MASCULINE_ORDINATOR;
	// K_GRAVE_A;
	// K_AUX1;
	// K_CEDILLA_C;
	// K_GRAVE_E;
	// K_AUX2;
	// K_AUX3;
	// K_AUX4;
	// K_GRAVE_I;
	// K_AUX5;
	// K_AUX6;
	// K_AUX7;
	// K_AUX8;
	// K_TILDE_N;
	// K_GRAVE_O;
	// K_AUX9;
	// K_AUX10;
	// K_AUX11;
	// K_AUX12;
	// K_AUX13;
	// K_AUX14;
	// K_GRAVE_U;
	// K_AUX15;
	// K_AUX16;

	case SDLK_PRINTSCREEN:
		return K_PRINT_SCR;
	case SDLK_MODE:
		// FIXME: is this really right alt? (also mapping SDLK_RALT to K_RIGHT_ALT)
		return K_RIGHT_ALT;
	}

	return 0;
}

static void PushConsoleEvent(const char *s) {
	char *b;
	size_t len;

	len = strlen(s) + 1;
	b = (char *)Mem_Alloc(len);
	strcpy(b, s);

	SDL_Event event;

	event.type = SDL_USEREVENT;
	event.user.code = SE_CONSOLE;
	event.user.data1 = (void *)len;
	event.user.data2 = b;

	SDL_PushEvent(&event);
}

#ifdef IMGUI_TOUCHSCREEN
static void ResetTouchFingerState() {
	for (int i = 0; i < TouchItem_Count; i++) {
		touch_fingers[i].fingerId = -1;
	}
}
#endif

/*
=================
Sys_InitInput
=================
*/
void Sys_InitInput() {
	kbd_polls.SetGranularity(64);
	mouse_polls.SetGranularity(64);

	assert(sizeof(scancodemappings)/sizeof(scancodemappings[0]) == K_NUM_SCANCODES && "scancodemappings incomplete?");

#if !SDL_VERSION_ATLEAST(2, 0, 0)
	SDL_EnableUNICODE(1);
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

#else // SDL2 - for utf8ToISO8859_1() (non-ascii text input and key naming)
	assert(iconvDesc == (SDL_iconv_t)-1);
	iconvDesc = SDL_iconv_open( "ISO-8859-1", "UTF-8" );
	if( iconvDesc == (SDL_iconv_t)-1 ) {
		common->Warning( "Sys_SetInput(): iconv_open( \"ISO-8859-1\", \"UTF-8\" ) failed! Can't translate non-ascii input!\n" );
	}
#endif

	in_kbd.SetModified();
	Sys_GetConsoleKey(false); // initialize consoleKeymappingIdx from in_kbd
#if SDL_VERSION_ATLEAST(2, 0, 0)
	const char* grabKeyboardEnv = SDL_getenv(SDL_HINT_GRAB_KEYBOARD);
	if ( grabKeyboardEnv ) {
		common->Printf("The SDL_GRAB_KEYBOARD environment variable is set, setting the in_grabKeyboard CVar to the same value (%s)\n", grabKeyboardEnv );
		in_grabKeyboard.SetString( grabKeyboardEnv );
	} else {
		in_grabKeyboard.SetModified();
	}
#else // SDL1.2 doesn't support this
	in_grabKeyboard.ClearModified();
#endif

#ifdef IMGUI_TOUCHSCREEN
	// nind touch screen handlers
	touch_fingers[TouchItem_LeftSide].handler = std::function<sysEvent_t(const ImVec2 &, const ImVec2 &, SDL_Event &)>(
		[](const ImVec2 &current_pos, const ImVec2 &relative_pos, SDL_Event &ev)->sysEvent_t{
		if (ev.type == SDL_FINGERUP) {
			joystick_polls.Append(joypad_poll_t(AXIS_SIDE, 0));
			joystick_polls.Append(joypad_poll_t(AXIS_FORWARD, 0));
			return default_res_none;
		}
		int side_state = (float)(current_pos.x - touch_fingers[TouchItem_LeftSide].press_pos.x) * 3.5f;
		int forward_state = (float)(current_pos.y - touch_fingers[TouchItem_LeftSide].press_pos.y) * 3.5f;
		joystick_polls.Append(joypad_poll_t(AXIS_SIDE, side_state));
		joystick_polls.Append(joypad_poll_t(AXIS_FORWARD, -forward_state));
		return default_res_none;
	});
	touch_fingers[TouchItem_RightSide].handler = std::function<sysEvent_t(const ImVec2 &, const ImVec2 &, SDL_Event &)>(
		[](const ImVec2 &current_pos, const ImVec2 &relative_pos, SDL_Event &ev)->sysEvent_t{
		(void)ev;
		mouse_polls.Append(joypad_poll_t(M_DELTAX, relative_pos.x * 5));
		mouse_polls.Append(joypad_poll_t(M_DELTAY, relative_pos.y * 5));
		return default_res_none;
	});
	touch_fingers[TouchItem_ESC].handler = std::function<sysEvent_t(const ImVec2 &, const ImVec2 &, SDL_Event &)>(
		[](const ImVec2 &current_pos, const ImVec2 &relative_pos, SDL_Event &ev)->sysEvent_t{
		if (ev.type == SDL_FINGERMOTION)
			return default_res_none;
		sysEvent_t res = {};
		res.evType = SE_KEY;
		res.evValue = K_ESCAPE;
		res.evValue2 = ev.type == SDL_FINGERDOWN ? 1 : 0;
		kbd_polls.Append(kbd_poll_t(K_ESCAPE, ev.type == SDL_FINGERDOWN));
		ResetTouchFingerState();
		return res;
	});
	touch_fingers[TouchItem_Fire].handler = std::function<sysEvent_t(const ImVec2 &, const ImVec2 &, SDL_Event &)>(
		[](const ImVec2 &current_pos, const ImVec2 &relative_pos, SDL_Event &ev)->sysEvent_t{
		if (ev.type == SDL_FINGERMOTION) // use mouse motion on fire button
			return touch_fingers[TouchItem_RightSide].handler(current_pos, relative_pos, ev);
		imgui_polls.Append(mouse_poll_t(IMPULSE_40 + 1 /*BUTTON_ATTACK*/, ev.type == SDL_FINGERDOWN ? 1 : 0));
		return default_res_none;
	});
	// + 2 BUTTON_SPEED
	// + 3 BUTTON_ZOOM
	// + 4 UB_UP, // jump
	// + 5 UB_DOWN, // crouch
	touch_fingers[TouchItem_Jump].handler = std::function<sysEvent_t(const ImVec2 &, const ImVec2 &, SDL_Event &)>(
		[](const ImVec2 &current_pos, const ImVec2 &relative_pos, SDL_Event &ev)->sysEvent_t{
		if (ev.type == SDL_FINGERMOTION) // use mouse motion
			return touch_fingers[TouchItem_RightSide].handler(current_pos, relative_pos, ev);
		imgui_polls.Append(mouse_poll_t(IMPULSE_40 + 4 /*BUTTON_UP*/, ev.type == SDL_FINGERDOWN ? 1 : 0));
		return default_res_none;
	});

	touch_fingers[TouchItem_Crounch].handler = std::function<sysEvent_t(const ImVec2 &, const ImVec2 &, SDL_Event &)>(
		[](const ImVec2 &current_pos, const ImVec2 &relative_pos, SDL_Event &ev)->sysEvent_t{
		if (ev.type == SDL_FINGERMOTION) // use mouse motion
			return touch_fingers[TouchItem_RightSide].handler(current_pos, relative_pos, ev);
		imgui_polls.Append(mouse_poll_t(IMPULSE_40 + 5 /*BUTTON_DOWN*/, ev.type == SDL_FINGERDOWN ? 1 : 0));
		return default_res_none;
	});

	touch_fingers[TouchItem_Flashlight].handler = std::function<sysEvent_t(const ImVec2 &, const ImVec2 &, SDL_Event &)>(
		[](const ImVec2 &current_pos, const ImVec2 &relative_pos, SDL_Event &ev)->sysEvent_t{
		if (ev.type == SDL_FINGERMOTION) // use mouse motion
			return touch_fingers[TouchItem_RightSide].handler(current_pos, relative_pos, ev);
		imgui_polls.Append(mouse_poll_t(
#ifdef _HUMANHEAD
			IMPULSE_16,
#else
			IMPULSE_11,
#endif
			 ev.type == SDL_FINGERDOWN ? 1 : 0));
		return default_res_none;
	});

	touch_fingers[TouchItem_WeaponNext].handler = std::function<sysEvent_t(const ImVec2 &, const ImVec2 &, SDL_Event &)>(
		[](const ImVec2 &current_pos, const ImVec2 &relative_pos, SDL_Event &ev)->sysEvent_t{
		if (ev.type == SDL_FINGERMOTION) // use mouse motion
			return touch_fingers[TouchItem_RightSide].handler(current_pos, relative_pos, ev);
		imgui_polls.Append(mouse_poll_t(IMPULSE_14, ev.type == SDL_FINGERDOWN ? 1 : 0));
		return default_res_none;
	});

	touch_fingers[TouchItem_WeaponPrev].handler = std::function<sysEvent_t(const ImVec2 &, const ImVec2 &, SDL_Event &)>(
		[](const ImVec2 &current_pos, const ImVec2 &relative_pos, SDL_Event &ev)->sysEvent_t{
		if (ev.type == SDL_FINGERMOTION) // use mouse motion
			return touch_fingers[TouchItem_RightSide].handler(current_pos, relative_pos, ev);
		imgui_polls.Append(mouse_poll_t(IMPULSE_15, ev.type == SDL_FINGERDOWN ? 1 : 0));
		return default_res_none;
	});

	touch_fingers[TouchItem_Reload].handler = std::function<sysEvent_t(const ImVec2 &, const ImVec2 &, SDL_Event &)>(
		[](const ImVec2 &current_pos, const ImVec2 &relative_pos, SDL_Event &ev)->sysEvent_t{
		if (ev.type == SDL_FINGERMOTION) // use mouse motion
			return touch_fingers[TouchItem_RightSide].handler(current_pos, relative_pos, ev);
		imgui_polls.Append(mouse_poll_t(IMPULSE_13, ev.type == SDL_FINGERDOWN ? 1 : 0));
		return default_res_none;
	});
	// Spirit mode for Prey
	touch_fingers[TouchItem_PDA].handler = std::function<sysEvent_t(const ImVec2 &, const ImVec2 &, SDL_Event &)>(
		[](const ImVec2 &current_pos, const ImVec2 &relative_pos, SDL_Event &ev)->sysEvent_t{
		if (ev.type == SDL_FINGERMOTION) // use mouse motion
			return touch_fingers[TouchItem_RightSide].handler(current_pos, relative_pos, ev);
		
#ifdef _HUMANHEAD
		common->Printf("Enable Spirit mode!\n");
#endif 
		imgui_polls.Append(mouse_poll_t(
#ifdef _HUMANHEAD
			// IMPULSE_25, // -- granade
			IMPULSE_54, // -- spirit
#else
			IMPULSE_19,
#endif
			 ev.type == SDL_FINGERDOWN ? 1 : 0));
		return default_res_none;
	});

	touch_fingers[TouchItem_QuickSave].handler = std::function<sysEvent_t(const ImVec2 &, const ImVec2 &, SDL_Event &)>(
		[](const ImVec2 &current_pos, const ImVec2 &relative_pos, SDL_Event &ev)->sysEvent_t{
		sessLocal.QuickSave();
		return default_res_none;
	});

	touch_fingers[TouchItem_QuickLoad].handler = std::function<sysEvent_t(const ImVec2 &, const ImVec2 &, SDL_Event &)>(
		[](const ImVec2 &current_pos, const ImVec2 &relative_pos, SDL_Event &ev)->sysEvent_t{
		sessLocal.QuickLoad();
		ResetTouchFingerState();
		return default_res_none;
	});

	touch_fingers[TouchItem_Settings].handler = std::function<sysEvent_t(const ImVec2 &, const ImVec2 &, SDL_Event &)>(
		[](const ImVec2 &current_pos, const ImVec2 &relative_pos, SDL_Event &ev)->sysEvent_t{
		
		ResetTouchFingerState();
		return default_res_none;
	});

#ifdef USE_LIPSTICK_FBO
	if (glConfig.vidWidthReal > glConfig.vidHeightReal) {
		finger_pos_landscape_handler = std::function<void(SDL_TouchFingerEvent &tfinger, ImVec2 &finger_pos, ImVec2 &finger_dpos)>(
			[](SDL_TouchFingerEvent &tfinger, ImVec2 &finger_pos, ImVec2 &finger_dpos){
				finger_pos.x = tfinger.x * glConfig.vidWidth;
				finger_pos.y = tfinger.y * glConfig.vidHeight;
				finger_dpos.x = tfinger.dx * glConfig.vidWidth * 0.5;
				finger_dpos.y = tfinger.dy * glConfig.vidHeight * 0.5;
			});
		finger_pos_inverted_landscape_handler = std::function<void(SDL_TouchFingerEvent &tfinger, ImVec2 &finger_pos, ImVec2 &finger_dpos)>(
			[](SDL_TouchFingerEvent &tfinger, ImVec2 &finger_pos, ImVec2 &finger_dpos){
				finger_pos.x = (1.0 - tfinger.x) * glConfig.vidWidth;
				finger_pos.y = (1.0 - tfinger.y) * glConfig.vidHeight;
				finger_dpos.x = -tfinger.dx * glConfig.vidWidth * 0.5;
				finger_dpos.y = -tfinger.dy * glConfig.vidHeight * 0.5;
			});
	} else {
		finger_pos_landscape_handler = std::function<void(SDL_TouchFingerEvent &tfinger, ImVec2 &finger_pos, ImVec2 &finger_dpos)>(
			[](SDL_TouchFingerEvent &tfinger, ImVec2 &finger_pos, ImVec2 &finger_dpos){
				finger_pos.x = (1.0f - tfinger.y) * glConfig.vidWidth;
				finger_pos.y = tfinger.x * glConfig.vidHeight;
				finger_dpos.x = -tfinger.dy * glConfig.vidWidth * 0.5;
				finger_dpos.y = tfinger.dx * glConfig.vidHeight * 0.5;
			});
		finger_pos_inverted_landscape_handler = std::function<void(SDL_TouchFingerEvent &tfinger, ImVec2 &finger_pos, ImVec2 &finger_dpos)>(
			[](SDL_TouchFingerEvent &tfinger, ImVec2 &finger_pos, ImVec2 &finger_dpos){
				finger_pos.x = tfinger.y * glConfig.vidWidth;
				finger_pos.y = (1.0f - tfinger.x) * glConfig.vidHeight;
				finger_dpos.x = tfinger.dy * glConfig.vidWidth * 0.5;
				finger_dpos.y = -tfinger.dx * glConfig.vidHeight * 0.5;
			});
	}
	finger_pos_current_handler = finger_pos_landscape_handler;
#endif
#endif
}

/*
=================
Sys_ShutdownInput
=================
*/
void Sys_ShutdownInput() {
	kbd_polls.Clear();
	mouse_polls.Clear();
#if SDL_VERSION_ATLEAST(2, 0, 0)
	SDL_iconv_close( iconvDesc ); // used by utf8ToISO8859_1()
	iconvDesc = ( SDL_iconv_t ) -1; 
#endif
}

/*
===========
Sys_InitScanTable
===========
*/
// Windows has its own version due to the tools
#ifndef _WIN32
void Sys_InitScanTable() {
}
#endif


struct ConsoleKeyMapping {
	const char* langName;
	unsigned char key;
	unsigned char keyShifted;
};

static ConsoleKeyMapping consoleKeyMappings[] = {
#if SDL_VERSION_ATLEAST(2, 0, 0)
	{ "auto",   	 0 ,	0   }, // special case: set current keycode for SDL_SCANCODE_GRAVE (no shifted keycode, though)
#endif
	{ "english",	'`',	'~' },
	{ "french", 	'<',	'>' },
	{ "german", 	'^',	176 }, // °
	{ "italian",	'\\',	'|' },
	{ "spanish",	186,	170 }, // º ª
	{ "turkish",	'"',	233 }, // é
	{ "norwegian",	124,	167 }, // | §
	{ "brazilian",	'\'',	'"' },
};
static int consoleKeyMappingIdx = 0;

static void initConsoleKeyMapping() {
	const int numMappings = sizeof(consoleKeyMappings)/sizeof(consoleKeyMappings[0]);

	idStr lang = in_kbd.GetString();
	consoleKeyMappingIdx = 0;

#if SDL_VERSION_ATLEAST(2, 0, 0)
	consoleKeyMappings[0].key = 0;
	if ( lang.Length() == 0 || lang.Icmp( "auto") == 0 ) {
		// auto-detection (SDL2-only)
		int keycode = SDL_GetKeyFromScancode( SDL_SCANCODE_GRAVE );
		if ( keycode > 0 && keycode <= 0xFF ) {
			// the SDL keycode and dhewm3 keycode should be identical for the mappings,
			// as it's ISO-8859-1 ("High ASCII") chars
			for( int i=1; i<numMappings; ++i ) {
				if ( consoleKeyMappings[i].key == keycode ) {
					consoleKeyMappingIdx = i;
					common->Printf("Detected keyboard layout as \"%s\"\n", consoleKeyMappings[i].langName );
					break;
				}
			}
			if ( consoleKeyMappingIdx == 0 ) { // not found in known mappings
				consoleKeyMappings[0].key = keycode;
			}
		}
	} else
#endif
	{
		for( int i=1; i<numMappings; ++i ) {
			if( lang.Icmp( consoleKeyMappings[i].langName ) == 0 ) {
				consoleKeyMappingIdx = i;
#if SDL_VERSION_ATLEAST(2, 0, 0)
				int keycode = SDL_GetKeyFromScancode( SDL_SCANCODE_GRAVE );
				if ( keycode && keycode != consoleKeyMappings[i].key ) {
					common->Warning( "in_kbd is set to \"%s\", but the actual keycode of the 'console key' is %c (%d), not %c (%d), so this might not work that well..\n",
							lang.c_str(), (unsigned char)keycode, keycode, consoleKeyMappings[i].key, consoleKeyMappings[i].key );
				}
#endif
				break;
			}
		}
	}
}

/*
===============
Sys_GetConsoleKey
===============
*/
unsigned char Sys_GetConsoleKey( bool shifted ) {

	if ( in_ignoreConsoleKey.GetBool() ) {
		return 0;
	}

	if ( in_kbd.IsModified() ) {
		initConsoleKeyMapping();
		in_kbd.ClearModified();
	}

	return shifted ? consoleKeyMappings[consoleKeyMappingIdx].keyShifted : consoleKeyMappings[consoleKeyMappingIdx].key;
}

/*
===============
Sys_MapCharForKey
===============
*/
unsigned char Sys_MapCharForKey(int key) {
	return key & 0xff;
}

/*
===============
Sys_GrabMouseCursor
Note: Usually grabbing is handled in idCommonLocal::Frame() -> Sys_GenerateEvents() -> handleMouseGrab()
      This function should only be used to release the mouse before long operations where
      common->Frame() won't be called for a while
===============
*/
void Sys_GrabMouseCursor(bool grabIt) {
	int flags = grabIt ? (GRAB_GRABMOUSE | GRAB_HIDECURSOR | GRAB_RELATIVEMOUSE) : 0;

	GLimp_GrabInput(flags);
}

/*
================
Sys_GetEvent
================
*/
sysEvent_t Sys_GetEvent() {
	SDL_Event ev;
	sysEvent_t res = default_res_none;
	int key;

#if SDL_VERSION_ATLEAST(2, 0, 0)
	static char s[SDL_TEXTINPUTEVENT_TEXT_SIZE] = {0};
	static size_t s_pos = 0;

	if (s[0] != '\0') {
		res.evType = SE_CHAR;
		res.evValue = (unsigned char)s[s_pos];

		++s_pos;

		if (!s[s_pos] || s_pos == SDL_TEXTINPUTEVENT_TEXT_SIZE) {
			memset(s, 0, sizeof(s));
			s_pos = 0;
		}

		return res;
	}
#endif

	if (joystick == NULL && controller == NULL && SDL_NumJoysticks() > 0) {
		if (SDL_IsGameController(0)) {
			controller = SDL_GameControllerOpen(0);
		} else {
			joystick = SDL_JoystickOpen(0);
		}
		common->Printf("Handle %s 0;\n", controller != NULL ? "controller" : "joystick");
	}

	static byte c = 0;

	if (c) {
		res.evType = SE_CHAR;
		res.evValue = c;

		c = 0;

		return res;
	}

	// loop until there is an event we care about (will return then) or no more events
	while(SDL_PollEvent(&ev)) {
#ifdef IMGUI_TOUCHSCREEN
		ImGui_ImplSDL2_ProcessEvent(&ev);
#endif
		switch (ev.type) {
#if SDL_VERSION_ATLEAST(2, 0, 0)
		case SDL_WINDOWEVENT:
			switch (ev.window.event) {
				case SDL_WINDOWEVENT_FOCUS_GAINED: {
						// unset modifier, in case alt-tab was used to leave window and ALT is still set
						// as that can cause fullscreen-toggling when pressing enter...
						SDL_Keymod currentmod = SDL_GetModState();
					
						int newmod = KMOD_NONE;
						if (currentmod & KMOD_CAPS) // preserve capslock
							newmod |= KMOD_CAPS;

						SDL_SetModState((SDL_Keymod)newmod);
					} // new context because visual studio complains about newmod and currentmod not initialized because of the case SDL_WINDOWEVENT_FOCUS_LOST

					
					in_hasFocus = true;

					// start playing the game sound world again (when coming from editor)
					session->SetPlayingSoundWorld();

					break;
				case SDL_WINDOWEVENT_FOCUS_LOST:
					in_hasFocus = false;
					break;
			}

			continue; // handle next event
#ifdef USE_LIPSTICK_FBO
		case SDL_DISPLAYEVENT:
			if (ev.display.event != SDL_DISPLAYEVENT_ORIENTATION)
				continue;
			// common->Warning("SDL_DISPLAYEVENT_ORIENTATION: %i", ev.display.data1);
			// switch(ev.display.data1) {
			// 	case SDL_ORIENTATION_UNKNOWN: common->Warning("SDL_ORIENTATION_UNKNOWN"); break;
			// 	case SDL_ORIENTATION_LANDSCAPE: common->Warning("SDL_ORIENTATION_LANDSCAPE"); break;
			// 	case SDL_ORIENTATION_LANDSCAPE_FLIPPED: common->Warning("SDL_ORIENTATION_LANDSCAPE_FLIPPED"); break;
			// 	case SDL_ORIENTATION_PORTRAIT: common->Warning("SDL_ORIENTATION_PORTRAIT"); break;
			// 	case SDL_ORIENTATION_PORTRAIT_FLIPPED: common->Warning("SDL_ORIENTATION_PORTRAIT_FLIPPED"); break;
			// }
			if (ev.display.data1 == SDL_ORIENTATION_LANDSCAPE) {
				finger_pos_current_handler = finger_pos_landscape_handler;
				GLimp_SetWindowOrientation((SDL_DisplayOrientation)ev.display.data1);
			} else if (ev.display.data1 == SDL_ORIENTATION_LANDSCAPE_FLIPPED) {
				finger_pos_current_handler = finger_pos_inverted_landscape_handler;
				GLimp_SetWindowOrientation((SDL_DisplayOrientation)ev.display.data1);
			}
			break;
#endif
#else
		case SDL_ACTIVEEVENT:
			{
				if (ev.active.gain) {
					in_hasFocus = true;

					// unset modifier, in case alt-tab was used to leave window and ALT is still set
					// as that can cause fullscreen-toggling when pressing enter...
					SDLMod currentmod = SDL_GetModState();
					int newmod = KMOD_NONE;
					if (currentmod & KMOD_CAPS) // preserve capslock
						newmod |= KMOD_CAPS;

					SDL_SetModState((SDLMod)newmod);
				} else {
					in_hasFocus = false;
				}
			}

			continue; // handle next event

		case SDL_VIDEOEXPOSE:
			continue; // handle next event
#endif

		case SDL_KEYDOWN:
			if (ev.key.keysym.sym == SDLK_RETURN && (ev.key.keysym.mod & KMOD_ALT) > 0) {
				cvarSystem->SetCVarBool("r_fullscreen", !renderSystem->IsFullScreen());
				PushConsoleEvent("vid_restart");
				return default_res_none;
			}

			// fall through
		case SDL_KEYUP:
#if !SDL_VERSION_ATLEAST(2, 0, 0)
			key = mapkey(ev.key.keysym.sym);
			if (!key) {
				if ( !in_ignoreConsoleKey.GetBool() ) {
					// check if its an unmapped console key
					int c = Sys_GetConsoleKey( (ev.key.keysym.mod & KMOD_SHIFT) != 0 );
					if (ev.key.keysym.unicode == c) {
						key = c;
					}
				}
				if (!key) {
					if (ev.type == SDL_KEYDOWN)
						common->Warning( "unmapped SDL key %d (0x%x) - if possible use SDL2 for better keyboard support",
						                 ev.key.keysym.sym, ev.key.keysym.unicode );
					continue; // handle next event
				}
			}
#else
		{
			// workaround for AZERTY-keyboards, which don't have 1, 2, ..., 9, 0 in first row:
			// always map those physical keys (scancodes) to those keycodes anyway
			// see also https://bugzilla.libsdl.org/show_bug.cgi?id=3188
			SDL_Scancode sc = ev.key.keysym.scancode;
			if(sc == SDL_SCANCODE_0)
			{
				key = '0';
			}
			else if(sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
			{
				// note that the SDL_SCANCODEs are SDL_SCANCODE_1, _2, ..., _9, SDL_SCANCODE_0
				// while in ASCII it's '0', '1', ..., '9' => handle 0 and 1-9 separately
				// (doom3 uses the ASCII values for those keys)
				key = '1' + (sc - SDL_SCANCODE_1);
			}
			else
			{
				key = mapkey(ev.key.keysym.sym);
			}

			if ( !in_ignoreConsoleKey.GetBool() && ev.key.keysym.scancode == SDL_SCANCODE_GRAVE ) {
				// that key between Esc, Tab and 1 is the console key
				key = K_CONSOLE;
			}

			if ( !key ) {
				// if the key couldn't be mapped so far, try to map the scancode to K_SC_*
				key = getKeynumForSDLscancode(sc);
				if(!key) {
					if (ev.type == SDL_KEYDOWN) {
						common->Warning("unmapped SDL key %d (scancode %d)", ev.key.keysym.sym, (int)sc);
					}
					continue; // handle next event
				}
			}
		}
#endif

			res.evType = SE_KEY;
			res.evValue = key;
			res.evValue2 = ev.key.state == SDL_PRESSED ? 1 : 0;

			kbd_polls.Append(kbd_poll_t(key, ev.key.state == SDL_PRESSED));

#if SDL_VERSION_ATLEAST(2, 0, 0)
			if (key == K_BACKSPACE && ev.key.state == SDL_PRESSED)
				c = key;
#else
			if (ev.key.state == SDL_PRESSED && (ev.key.keysym.unicode & 0xff00) == 0)
				c = ev.key.keysym.unicode & 0xff;
#endif

			return res;

#if SDL_VERSION_ATLEAST(2, 0, 0)
		case SDL_TEXTINPUT:
			if (ev.text.text[0]) {
				res.evType = SE_CHAR;

				if ( isAscii(ev.text.text) ) {
					res.evValue = ev.text.text[0];
					if ( ev.text.text[1] != '\0' ) {
						memcpy( s, ev.text.text, SDL_TEXTINPUTEVENT_TEXT_SIZE );
						s_pos = 1; // pos 0 is returned
					}
					return res;
				} else if( utf8ToISO8859_1( ev.text.text, s, sizeof(s) ) && s[0] != '\0' ) {
					res.evValue = (unsigned char)s[0];
					if ( s[1] == '\0' ) {
						s_pos = 0;
						s[0] = '\0';
					} else {
						s_pos = 1; // pos 0 is returned
					}
					return res;
				}
			}

			continue; // handle next event

		case SDL_TEXTEDITING:
			// on windows we get this event whenever the window gains focus.. just ignore it.
			continue;
#endif

		case SDL_MOUSEMOTION: {
#if defined(IMGUI_TOUCHSCREEN) || defined(USE_LIPSTICK_FBO)
			float mouse_pos[2];
#endif
#ifdef USE_LIPSTICK_FBO
			if (GLimp_GetWindowOrientation() == SDL_ORIENTATION_LANDSCAPE) {
				mouse_pos[0] = (1.0 - (float)ev.motion.y / glConfig.vidHeightReal) * glConfig.vidWidth;
				mouse_pos[1] = (float)ev.motion.x / glConfig.vidWidthReal * glConfig.vidHeight;
			} else {
				mouse_pos[0] = (float)ev.motion.y / glConfig.vidHeightReal * glConfig.vidWidth;
				mouse_pos[1] = (1.0 - (float)ev.motion.x / glConfig.vidWidthReal) * glConfig.vidHeight;
			}
#endif
			if ( in_relativeMouseMode ) {
#if defined(IMGUI_TOUCHSCREEN) && defined(USE_LIPSTICK_FBO)
				continue;
#endif
				res.evType = SE_MOUSE;
#ifdef USE_LIPSTICK_FBO
				if (GLimp_GetWindowOrientation() == SDL_ORIENTATION_LANDSCAPE) {
					res.evValue = -ev.motion.yrel;
					res.evValue2 = ev.motion.xrel;
				} else {
					res.evValue = ev.motion.yrel;
					res.evValue2 = -ev.motion.xrel;
				}
#else
				res.evValue = ev.motion.xrel;
				res.evValue2 = ev.motion.yrel;
#endif
				mouse_polls.Append(mouse_poll_t(M_DELTAX, res.evValue));
				mouse_polls.Append(mouse_poll_t(M_DELTAY, res.evValue2));
			} else {
				res.evType = SE_MOUSE_ABS;
#ifdef USE_LIPSTICK_FBO
				res.evValue = mouse_pos[0];
				res.evValue2 = mouse_pos[1];
#else
				res.evValue = ev.motion.x;
				res.evValue2 = ev.motion.y;
#endif
			}

#if IMGUI_TOUCHSCREEN
			ImGuiIO& io = ImGui::GetIO();
			io.AddMousePosEvent(mouse_pos[0], mouse_pos[1]);
#endif
			return res;
		}
#if IMGUI_TOUCHSCREEN
		case SDL_FINGERMOTION:
		case SDL_FINGERDOWN:
		case SDL_FINGERUP:
			{
				res.evType = SE_NONE;
				if (!touch_fingers[TouchItem_LeftSide].window) {
					touch_fingers[TouchItem_LeftSide].window = ImGui::FindWindowByName(imgui_left_side);
					touch_fingers[TouchItem_RightSide].window = ImGui::FindWindowByName(imgui_right_side);
					touch_fingers[TouchItem_ESC].window = ImGui::FindWindowByName(imgui_key_esc);
					touch_fingers[TouchItem_Fire].window = ImGui::FindWindowByName(imgui_key_fire);
					touch_fingers[TouchItem_Jump].window = ImGui::FindWindowByName(imgui_key_jump);
					touch_fingers[TouchItem_Crounch].window = ImGui::FindWindowByName(imgui_key_crounch);
					touch_fingers[TouchItem_Flashlight].window = ImGui::FindWindowByName(imgui_key_flashlight);
					touch_fingers[TouchItem_Reload].window = ImGui::FindWindowByName(imgui_key_reload);
					touch_fingers[TouchItem_WeaponNext].window = ImGui::FindWindowByName(imgui_key_weapnext);
					touch_fingers[TouchItem_WeaponPrev].window = ImGui::FindWindowByName(imgui_key_weapprev);
					touch_fingers[TouchItem_PDA].window = ImGui::FindWindowByName(imgui_key_pda);
					touch_fingers[TouchItem_QuickSave].window = ImGui::FindWindowByName(imgui_key_quicksave);
					touch_fingers[TouchItem_QuickLoad].window = ImGui::FindWindowByName(imgui_key_quickload);
					touch_fingers[TouchItem_Settings].window = ImGui::FindWindowByName(imgui_key_settings);
				}

				ImVec2 touch_pos;
				ImVec2 rel_pos;
				bool any_contains = false;
#ifdef USE_LIPSTICK_FBO
				finger_pos_current_handler(ev.tfinger, touch_pos, rel_pos);
#else
				touch_pos.x = ev.tfinger.x;
				touch_pos.y = ev.tfinger.y;
				rel_pos.x = ev.tfinger.dx;
				rel_pos.y = ev.tfinger.dy;
#endif

				for (int i = 0; i < TouchItem_Count; i++) {
					if (!touch_fingers[i].window)
						continue;
					if (sessLocal.guiActive != sessLocal.guiInGame && i <= TouchItem_Zoom) 
						continue;
					bool contains = touch_fingers[i].window->Rect().Contains(touch_pos);
					if (ev.type == SDL_FINGERDOWN && touch_fingers[i].fingerId == -1 && contains) {
						touch_fingers[i].fingerId = ev.tfinger.fingerId;
						touch_fingers[i].press_pos = touch_pos;
						any_contains = contains;
					}
					if (touch_fingers[i].fingerId == ev.tfinger.fingerId) {
						if (ev.type == SDL_FINGERUP)
							touch_fingers[i].fingerId = -1;
						if (touch_fingers[i].handler) 
							res = touch_fingers[i].handler(touch_pos, rel_pos, ev);
						break;
					}
				}

				if (any_contains) {
					ImGuiIO& io = ImGui::GetIO();
					io.AddMousePosEvent(touch_pos.x, touch_pos.y);
				}
				if (res.evType != SE_NONE)
					return res;
			}
			break;
#endif
#if SDL_VERSION_ATLEAST(2, 0, 0)
		case SDL_MOUSEWHEEL:
			res.evType = SE_KEY;

			if (ev.wheel.y > 0) {
				res.evValue = K_MWHEELUP;
				mouse_polls.Append(mouse_poll_t(M_DELTAZ, 1));
			} else if (ev.wheel.y < 0) {
				res.evValue = K_MWHEELDOWN;
				mouse_polls.Append(mouse_poll_t(M_DELTAZ, -1));
			}

			res.evValue2 = 1;

			return res;
#endif

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
#if defined(IMGUI_TOUCHSCREEN) && defined(USE_LIPSTICK_FBO)
			if (in_relativeMouseMode) // Do not shoot always
				continue;
#endif
			res.evType = SE_KEY;

			switch (ev.button.button) {
			case SDL_BUTTON_LEFT:
				res.evValue = K_MOUSE1;
				mouse_polls.Append(mouse_poll_t(M_ACTION1, ev.button.state == SDL_PRESSED ? 1 : 0));
				break;
			case SDL_BUTTON_MIDDLE:
				res.evValue = K_MOUSE3;
				mouse_polls.Append(mouse_poll_t(M_ACTION3, ev.button.state == SDL_PRESSED ? 1 : 0));
				break;
			case SDL_BUTTON_RIGHT:
				res.evValue = K_MOUSE2;
				mouse_polls.Append(mouse_poll_t(M_ACTION2, ev.button.state == SDL_PRESSED ? 1 : 0));
				break;

#if !SDL_VERSION_ATLEAST(2, 0, 0)
			case SDL_BUTTON_WHEELUP:
				res.evValue = K_MWHEELUP;
				if (ev.button.state == SDL_PRESSED)
					mouse_polls.Append(mouse_poll_t(M_DELTAZ, 1));
				break;
			case SDL_BUTTON_WHEELDOWN:
				res.evValue = K_MWHEELDOWN;
				if (ev.button.state == SDL_PRESSED)
					mouse_polls.Append(mouse_poll_t(M_DELTAZ, -1));
				break;
#endif
			default:
#if SDL_VERSION_ATLEAST(2, 0, 0)
				// handle X1 button and above
				if( ev.button.button < SDL_BUTTON_LEFT + 8 ) // doesn't support more than 8 mouse buttons
				{
					int buttonIndex = ev.button.button - SDL_BUTTON_LEFT;
					res.evValue = K_MOUSE1 + buttonIndex;
					mouse_polls.Append( mouse_poll_t( M_ACTION1 + buttonIndex, ev.button.state == SDL_PRESSED ? 1 : 0 ) );
				}
				else
#endif
				continue; // handle next event
			}

			res.evValue2 = ev.button.state == SDL_PRESSED ? 1 : 0;

			return res;
		case SDL_QUIT:
			PushConsoleEvent("quit");
			return default_res_none;

		case SDL_USEREVENT:
			switch (ev.user.code) {
			case SE_CONSOLE:
				res.evType = SE_CONSOLE;
				res.evPtrLength = (intptr_t)ev.user.data1;
				res.evPtr = ev.user.data2;
				return res;
			default:
				common->Warning("unknown user event %u", ev.user.code);
				continue; // handle next event
			}
		default:
			if (Sys_joypadEvent(&ev, res) ) {
				return res;
			}
			// ok, I don't /really/ care about unknown SDL events. only uncomment this for debugging.
			// common->Warning("unknown SDL event 0x%x", ev.type);
			continue; // handle next event
		}
	}

	if (joystick || controller) {
		if (joy_axis_state[JOY_RIGHT_STICK_XAXIS] != 0) {
			mouse_polls.Append(mouse_poll_t(M_DELTAX, joy_axis_state[JOY_RIGHT_STICK_XAXIS]));
		}
		if (joy_axis_state[JOY_RIGHT_STICK_YAXIS] != 0) {
			mouse_polls.Append(mouse_poll_t(M_DELTAY, joy_axis_state[JOY_RIGHT_STICK_YAXIS]));
		}
	}

	return default_res_none;
}

/*
================
Sys_joypadEvent
================
*/
bool Sys_joypadEvent(SDL_Event *event, sysEvent_t &res) {
	bool result = false;
	static int LEFT_SHOULDER = 0;
	static int RIGHT_SHOULDER = 1;
	static int joy_shoulder_state[] = {0,0};
	// static const int JOYSTICK_025_PRESS = JOYAXIS_MAX * 0.25;
	
	switch (event->type)
	{
	case SDL_CONTROLLERAXISMOTION:    /**< Game controller axis motion */
		// common->Printf("ControllerEvent: SDL_CONTROLLERAXISMOTION axis: %i\n", event->caxis.axis);
		{
			int value = 0;
			int pressed = 0;
			float coef;
			switch (event->caxis.axis) {
				case SDL_CONTROLLER_AXIS_RIGHTY:
					value = abs(event->caxis.value) < JOYSTICK_DEAD_ZONE ? 0 : (event->caxis.value + (event->caxis.value < 0 ? JOYSTICK_DEAD_ZONE : -JOYSTICK_DEAD_ZONE));
					joy_axis_state[JOY_RIGHT_STICK_YAXIS] = JOY_AXIS_RMOUSE(value);
					break;
				case SDL_CONTROLLER_AXIS_RIGHTX:
					value = abs(event->caxis.value) < JOYSTICK_DEAD_ZONE ? 0 : (event->caxis.value + (event->caxis.value < 0 ? JOYSTICK_DEAD_ZONE : -JOYSTICK_DEAD_ZONE));
					joy_axis_state[JOY_RIGHT_STICK_XAXIS] = JOY_AXIS_RMOUSE(value);
					break;
				case SDL_CONTROLLER_AXIS_LEFTY:
					value = abs(event->caxis.value) < JOYSTICK_DEAD_ZONE ? 0 : (event->caxis.value + (event->caxis.value < 0 ? JOYSTICK_DEAD_ZONE : -JOYSTICK_DEAD_ZONE));
					coef = value / (float(JOYAXIS_MAX - JOYSTICK_DEAD_ZONE) * 0.8);
					joy_axis_state[JOY_LEFT_STICK_YAXIS] = -coef * 127;
					joystick_polls.Append(mouse_poll_t(AXIS_FORWARD, joy_axis_state[JOY_LEFT_STICK_YAXIS]));
					break;
				case SDL_CONTROLLER_AXIS_LEFTX:
					value = abs(event->caxis.value) < JOYSTICK_DEAD_ZONE ? 0 : (event->caxis.value + (event->caxis.value < 0 ? JOYSTICK_DEAD_ZONE : -JOYSTICK_DEAD_ZONE));
					coef = value / (float(JOYAXIS_MAX - JOYSTICK_DEAD_ZONE) * 0.8);
					joy_axis_state[JOY_LEFT_STICK_XAXIS] = coef * 127;
					joystick_polls.Append(mouse_poll_t(AXIS_SIDE, joy_axis_state[JOY_LEFT_STICK_XAXIS]));
					break;
				case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
					pressed = (event->caxis.value > (JOYSTICK_DEAD_ZONE)) ? 1 : 0;
					// common->Printf("JoyEvent: SDL_JOYAXISMOTION axis: %i value %i pressed %i", event->jaxis.axis, event->jaxis.value, pressed);
					if (joy_shoulder_state[LEFT_SHOULDER] != pressed) {
						joy_shoulder_state[LEFT_SHOULDER] = pressed;
						res.evType = SE_KEY;
						res.evValue = K_MOUSE2;
						res.evValue2 = pressed;
						mouse_polls.Append(mouse_poll_t(M_ACTION2, pressed));
						result = true;
					}
					break;
				case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
					pressed = (event->caxis.value > (JOYSTICK_DEAD_ZONE)) ? 1 : 0;
					// common->Printf("JoyEvent: SDL_CONTROLLER_AXIS_TRIGGERRIGHT axis: %i value %i pressed %i\n", event->caxis.axis, event->caxis.value, pressed);
					if (joy_shoulder_state[RIGHT_SHOULDER] != pressed) {
						joy_shoulder_state[RIGHT_SHOULDER] = pressed;
						res.evType = SE_KEY;
						res.evValue = K_MOUSE1;
						res.evValue2 = pressed;
						mouse_polls.Append(mouse_poll_t(M_ACTION1, pressed));
						result = true;
					}
					break;
				default: 
					common->Printf("JoyEvent: SDL_CONTROLLERAXISMOTION axis: %i value %i\n", event->caxis.axis, event->caxis.value);
			}
			break;
		}
	case SDL_CONTROLLERBUTTONDOWN:    /**< Game controller button pressed */
	case SDL_CONTROLLERBUTTONUP:      /**< Game controller button released */
		if (event->cbutton.button <= SDL_CONTROLLER_BUTTON_INVALID || event->cbutton.button > SDL_CONTROLLER_BUTTON_MAX)
			return false;
		
		if (event->cbutton.button == SDL_CONTROLLER_BUTTON_START)
			res.evValue = K_ESCAPE;
		else
			res.evValue = K_JOY1 + event->cbutton.button;
		res.evType = SE_KEY;
		res.evValue2 = event->type == SDL_CONTROLLERBUTTONDOWN ? 1 : 0;
		kbd_polls.Append(kbd_poll_t(res.evValue, event->type == SDL_CONTROLLERBUTTONDOWN));
		return true;
	case SDL_CONTROLLERDEVICEADDED:   /**< A new Game controller has been inserted into the system */
		common->Printf("ControllerEvent: SDL_CONTROLLERDEVICEADDED\n");
		break;
	case SDL_CONTROLLERDEVICEREMOVED: /**< An opened Game controller has been removed */
		common->Printf("ControllerEvent: SDL_CONTROLLERDEVICEREMOVED\n");
		break;
	case SDL_CONTROLLERDEVICEREMAPPED:/**< The controller mapping was updated */
		common->Printf("ControllerEvent: SDL_CONTROLLERDEVICEREMAPPED\n");
		break;
	case SDL_CONTROLLERTOUCHPADDOWN:  /**< Game controller touchpad was touched */
		// common->Printf("ControllerEvent: SDL_CONTROLLERTOUCHPADDOWN");
		break;
	case SDL_CONTROLLERTOUCHPADMOTION:/**< Game controller touchpad finger was moved */
		// common->Printf("ControllerEvent: SDL_CONTROLLERTOUCHPADMOTION");
		break;
	case SDL_CONTROLLERTOUCHPADUP:    /**< Game controller touchpad finger was lifted */
		// common->Printf("ControllerEvent: SDL_CONTROLLERTOUCHPADUP");
		break;
	case SDL_CONTROLLERSENSORUPDATE:  /**< Game controller sensor was updated */
		common->Printf("ControllerEvent: SDL_CONTROLLERSENSORUPDATE %i : %f\n", event->csensor.sensor, event->csensor.data);
		break;
	case SDL_JOYDEVICEADDED:         /**< A new joystick has been inserted into the system */
		if (!joystick && !controller) {
			common->Printf("JoyEvent: Try handle %i joystick.n", event->jdevice.which);
			if (SDL_IsGameController(event->jdevice.which)) {
				controller = SDL_GameControllerOpen(event->jdevice.which);
			} else {
				common->Printf("JoyEvent: SDL_JOYDEVICEADDED\n");
				joystick = SDL_JoystickOpen(event->jdevice.which);
			}
		}
		break;
	case SDL_JOYDEVICEREMOVED:       /**< An opened joystick has been removed */
		common->Printf("JoyEvent: SDL_JOYDEVICEREMOVED\n");
		break;
	case SDL_JOYBATTERYUPDATED:      /**< Joystick battery level change */
		common->Printf("JoyEvent: SDL_JOYBATTERYUPDATED: %i", event->jbattery.level);
		break;
	}
	return result;
}

/*
================
Sys_ClearEvents
================
*/
void Sys_ClearEvents() {
	SDL_Event ev;

	while (SDL_PollEvent(&ev))
		;

	kbd_polls.SetNum(0, false);
	mouse_polls.SetNum(0, false);
	joystick_polls.SetNum(0, false);
	imgui_polls.SetNum(0, false);
}

static void handleMouseGrab() {

	// these are the defaults for when the window does *not* have focus
	// (don't grab in any way)
	bool showCursor = true;
	bool grabMouse = false;
	bool relativeMouse = false;

	// if com_editorActive, release everything, just like when we have no focus
	if ( in_hasFocus && !com_editorActive ) {
		// Note: this generally handles fullscreen menus, but not the PDA, because the PDA
		//       is an ugly hack in gamecode that doesn't go through sessLocal.guiActive.
		//       It goes through weapon input code or sth? That's also the reason only
		//       leftclick (fire) works there (no mousewheel..)
		//       So the PDA will continue to use relative mouse events to set its cursor position.
		const bool menuActive = ( sessLocal.GetActiveMenu() != NULL );

		if ( menuActive ) {
			showCursor = false;
			relativeMouse = false;
			grabMouse = false; // TODO: or still grab to window? (maybe only if in exclusive fullscreen mode?)
		} else if ( console->Active() ) {
			showCursor = true;
			relativeMouse = grabMouse = false;
		} else { // in game
			showCursor = false;
			grabMouse = relativeMouse = true;
		}

		in_relativeMouseMode = relativeMouse;

		// if in_nograb is set, in_relativeMouseMode and relativeMouse can disagree
		// (=> don't enable relative mouse mode in SDL, but still use relative mouse events
		//  in the game, unless we'd use absolute mousemode anyway)
		if ( in_nograb.GetBool() ) {
			grabMouse = relativeMouse = false;
		}
	} else {
		in_relativeMouseMode = false;
	}

	int flags = 0;
	if ( !showCursor )
		flags |= GRAB_HIDECURSOR;
	if ( grabMouse )
		flags |= GRAB_GRABMOUSE;
	if ( relativeMouse )
		flags |= GRAB_RELATIVEMOUSE;

	GLimp_GrabInput( flags );
}

/*
================
Sys_GenerateEvents
================
*/
void Sys_GenerateEvents() {

	handleMouseGrab();

	char *s = Sys_ConsoleInput();

	if (s)
		PushConsoleEvent(s);

#ifndef ID_DEDICATED // doesn't make sense on dedicated server
	if ( in_grabKeyboard.IsModified() ) {
#if SDL_VERSION_ATLEAST(2, 0, 0)
		SDL_SetHint( SDL_HINT_GRAB_KEYBOARD, in_grabKeyboard.GetString() );
		if ( in_grabKeyboard.GetBool() ) {
			common->Printf("in_grabKeyboard: Will grab the keyboard if mouse is grabbed, so global keyboard-shortcuts (like Alt-Tab or the Windows key) will *not* work\n" );
		} else {
			common->Printf("in_grabKeyboard: Will *not* grab the keyboard if mouse is grabbed, so global keyboard-shortcuts (like Alt-Tab) will still work\n" );
		}
#else
		common->Printf("Note: SDL1.2 doesn't support in_grabKeyboard (it's always grabbed if mouse is grabbed)\n" );
#endif
		in_grabKeyboard.ClearModified();
	}
#endif

	SDL_PumpEvents();
}

/*
================
Sys_PollKeyboardInputEvents
================
*/
int Sys_PollKeyboardInputEvents() {
	return kbd_polls.Num();
}

/*
================
Sys_ReturnKeyboardInputEvent
================
*/
int Sys_ReturnKeyboardInputEvent(const int n, int &key, bool &state) {
	if (n >= kbd_polls.Num())
		return 0;

	key = kbd_polls[n].key;
	state = kbd_polls[n].state;
	return 1;
}

/*
================
Sys_EndKeyboardInputEvents
================
*/
void Sys_EndKeyboardInputEvents() {
	kbd_polls.SetNum(0, false);
}

/*
================
Sys_PollJoystickInputEvents
================
*/
int Sys_PollJoystickInputEvents() {
	return joystick_polls.Num();
}

/*
================
Sys_ReturnJoystickInputEvent
================
*/
int Sys_ReturnJoystickInputEvent(const int n, int &action, int &value) {
	if (n >= joystick_polls.Num())
		return 0;

	action = joystick_polls[n].action;
	value = joystick_polls[n].value;
	return 1;
}

/*
================
Sys_EndJoystickInputEvents
================
*/
void Sys_EndJoystickInputEvents() {
	joystick_polls.SetNum(0, false);
}

#ifdef IMGUI_TOUCHSCREEN
int Sys_PollImGuiEvents( void ) {
	return imgui_polls.Num();
}

int Sys_ReturnImGuiEvent( const int n, int &action, int &value ) {
	if (n >= imgui_polls.Num())
		return 0;

	action = imgui_polls[n].action;
	value = imgui_polls[n].value;
	return 1;
}

void Sys_EndImGuiEvents( void ) {
	imgui_polls.SetNum(0, false);
}
#endif

/*
================
Sys_PollMouseInputEvents
================
*/
int Sys_PollMouseInputEvents() {
	return mouse_polls.Num();
}

/*
================
Sys_ReturnMouseInputEvent
================
*/
int	Sys_ReturnMouseInputEvent(const int n, int &action, int &value) {
	if (n >= mouse_polls.Num())
		return 0;

	action = mouse_polls[n].action;
	value = mouse_polls[n].value;
	return 1;
}

/*
================
Sys_EndMouseInputEvents
================
*/
void Sys_EndMouseInputEvents() {
	mouse_polls.SetNum(0, false);
}
