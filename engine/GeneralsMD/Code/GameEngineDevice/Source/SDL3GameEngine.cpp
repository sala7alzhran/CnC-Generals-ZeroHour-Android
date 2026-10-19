/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
** SDL3GameEngine.cpp
**
** Linux implementation of GameEngine using SDL3 for windowing/input.
**
** TheSuperHackers @feature CnC_Generals_Linux 07/02/2026
** Provides SDL3-based input and window management for Linux builds.
** Based on fighter19 reference implementation.
*/

#ifndef _WIN32

#include "SDL3GameEngine.h"
// GeneralsX @build Mr. Meeseeks 16/06/2026 Make audio headers mutually exclusive to avoid redefinition conflicts
#ifdef SAGE_USE_MINIAUDIO
#include "MiniAudioDevice/MiniAudioManager.h"
#elif defined(SAGE_USE_OPENAL)
#include "OpenALAudioManager.h"
#endif
#include "SDL3Device/GameClient/SDL3Mouse.h"
#include "SDL3Device/GameClient/SDL3Keyboard.h"
#include "GameClient/Mouse.h"
#include "GameClient/Keyboard.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Gadget.h"
#include "W3DDevice/GameLogic/W3DGameLogic.h"
#include "W3DDevice/GameClient/W3DGameClient.h"
#include "W3DDevice/Common/W3DModuleFactory.h"
#include "W3DDevice/Common/W3DThingFactory.h"
#include "W3DDevice/Common/W3DFunctionLexicon.h"
#include "W3DDevice/Common/W3DRadar.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "W3DDevice/GameClient/W3DWebBrowser.h"
#include "StdDevice/Common/StdLocalFileSystem.h"
#include "StdDevice/Common/StdBIGFileSystem.h"
#include "Common/GlobalData.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
// GeneralsX @feature Android touch → RTS controls: camera/view + game-mode gating
#if defined(__ANDROID__)
#include "GameClient/View.h"
#include "GameLogic/GameLogic.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Display.h"
#include "GameClient/VideoPlayer.h"
#include <jni.h>   // density + haptics come from ZerohourActivity via SDL_GetAndroidJNIEnv()
#include "GameClient/GadgetComboBox.h"   // smart touch: a dropdown's text part opens its list through the arrow button
#endif

// Extern globals for input devices (set by GameClient)
extern Mouse *TheMouse;
extern Keyboard *TheKeyboard;
extern GameWindowManager *TheWindowManager;

namespace {

Bool DecodeNextUtf8Codepoint(const char* text, size_t length, size_t& offset, UnsignedInt& outCodepoint)
{
	outCodepoint = 0;
	if (!text || offset >= length) {
		return false;
	}

	const unsigned char first = static_cast<unsigned char>(text[offset]);
	if (first == 0) {
		return false;
	}

	if (first < 0x80) {
		outCodepoint = first;
		offset += 1;
		return true;
	}

	if ((first & 0xE0) == 0xC0 && offset + 1 < length) {
		const unsigned char second = static_cast<unsigned char>(text[offset + 1]);
		if ((second & 0xC0) == 0x80) {
			outCodepoint = ((first & 0x1F) << 6) | (second & 0x3F);
			offset += 2;
			return true;
		}
	}

	if ((first & 0xF0) == 0xE0 && offset + 2 < length) {
		const unsigned char second = static_cast<unsigned char>(text[offset + 1]);
		const unsigned char third = static_cast<unsigned char>(text[offset + 2]);
		if ((second & 0xC0) == 0x80 && (third & 0xC0) == 0x80) {
			outCodepoint = ((first & 0x0F) << 12) | ((second & 0x3F) << 6) | (third & 0x3F);
			offset += 3;
			return true;
		}
	}

	if ((first & 0xF8) == 0xF0 && offset + 3 < length) {
		const unsigned char second = static_cast<unsigned char>(text[offset + 1]);
		const unsigned char third = static_cast<unsigned char>(text[offset + 2]);
		const unsigned char fourth = static_cast<unsigned char>(text[offset + 3]);
		if ((second & 0xC0) == 0x80 && (third & 0xC0) == 0x80 && (fourth & 0xC0) == 0x80) {
			outCodepoint = ((first & 0x07) << 18) | ((second & 0x3F) << 12) | ((third & 0x3F) << 6) | (fourth & 0x3F);
			offset += 4;
			return true;
		}
	}

	// Invalid UTF-8 sequence: skip one byte and keep processing.
	offset += 1;
	return false;
}

}

#if defined(__ANDROID__)
// ============================================================================
// GeneralsX @feature Android touch → RTS controls (docs/MOBILE-UX-SPEC.md, Phase A)
// SDL's single-finger→mouse synthesis is disabled (SDL_HINT_TOUCH_MOUSE_EVENTS
// "0" in init()). Gestures are recognised here; they either drive the camera
// directly or are synthesized as mouse input for the engine's own translators.
//
//   In a battle, with the finger on the terrain ("camera mode"):
//     1-finger drag      -> grab pan: the terrain point under the finger stays
//                           under it at every zoom; releasing flings with decay
//     tap                -> left click: select / context order (default mouse mode)
//     double tap         -> left double click: select all of that type on screen
//     long-press         -> haptic tick arms box-select; dragging then rubber-
//                           bands a selection box, with edge pan near the borders
//     2-finger pinch     -> zoom anchored under the pinch midpoint, soft limits
//     2-finger drag      -> the legacy scaled pan (kept so it can be compared)
//     2-finger tap       -> right click: deselect / cancel a placement
//   Everywhere else (menus, HUD windows, building placement, cutscenes) a finger
//   is a mouse: tap = left click, drag = left drag, 2-finger tap = right click.
//
// Thresholds are in millimetres from the real display density, never pixels.
// Nothing here allocates per event: all state lives in fixed-size statics, and
// the per-frame camera work runs once per rendered frame from update().
// ============================================================================
extern View     *TheTacticalView;
extern GameLogic *TheGameLogic;
// In-game ESC/pause menu toggle (QuitMenu.cpp) — same action the desktop ESC key runs.
extern void ToggleQuitMenu();

namespace {

// ------------------------------------------------------------------ tuning --
const Uint64 TAP_DEFER_MS        = 45;      // classic mode: defer left-down this long to catch a 2nd finger
const Uint64 CLICK_HOLD_MS       = 150;     // minimum synthesized press duration (the GUI needs it, see below)
const Uint64 TWO_TAP_MAX_MS      = 300;     // 2-finger press shorter than this w/o move = right-click
const Uint64 LONG_PRESS_NS       = 350ull * 1000000ull;  // stationary press that arms box-select
const Uint64 DOUBLE_TAP_NS       = 300ull * 1000000ull;  // second tap within this = double click
const Uint64 FLING_WINDOW_NS     = 80ull * 1000000ull;   // finger velocity is averaged over the last 80 ms
const float  TAP_SLOP_MM         = 2.5f;    // finger travel that turns a tap into a drag
const float  DOUBLE_TAP_MM       = 4.0f;    // max distance between the two taps of a double tap
const float  EDGE_PAN_BAND_MM    = 6.0f;    // a box-select finger this close to an edge pans the camera
const float  EDGE_PAN_SCREENS_S  = 0.45f;   // edge pan speed, screen heights per second (× camera speed)
const float  EDGE_GUARD_PX       = 4.0f;    // synthesized drags stay this far inside, off the engine's 3 px edge scroll
const float  FLING_TAU_S         = 0.30f;   // exponential decay time constant of a fling
const float  FLING_MIN_MM_S      = 12.0f;   // slower releases do not fling
const float  FLING_STOP_MM_S     = 0.4f;    // a glide ends below this finger-equivalent speed
const float  FLING_MAX_MM_S      = 450.0f;  // cap for velocities from touch glitches
const float  ZOOM_TAU_S          = 0.06f;   // the zoom follows the pinch through this low-pass (smooths 120 Hz jitter)
const float  ZOOM_SETTLE_TAU_S   = 0.12f;   // spring back from an over-zoom after release
const float  ZOOM_RUBBER_LOG     = 0.18f;   // max over-zoom past a limit, in ln(height) (≈ 20 %); the engine's clamp is not used
const float  PAN_K               = 1.0f;    // legacy 2-finger pan: scroll offset per pixel of centroid travel

// --------------------------------------------------------- finger tracking --
struct TouchFinger { SDL_FingerID id; float sx, sy, x, y; };

TouchFinger  g_tf[2];
int          g_tfCount = 0;

// Deferred single-finger left button ("classic" mode: menus, HUD windows, placement).
bool         g_leftPending = false;   // 1st finger down; left-down not yet committed
bool         g_leftHeld    = false;   // a left-button-down was synthesized and is held
SDL_FingerID g_leftId      = 0;
float        g_leftSX = 0, g_leftSY = 0;        // where the press lands (after smart touch)
float        g_leftRawX = 0, g_leftRawY = 0;    // where the finger really came down (tap-slop tests)
float        g_leftOffX = 0, g_leftOffY = 0;    // smart-touch shift applied to this press's drags and release
Uint64       g_leftDownMs  = 0;

// Primary (first) finger in a battle. In camera mode the finger is not a mouse until we
// know what it is doing: a tap clicks on release, a drag pans, a long-press arms box-select.
enum PrimaryMode { PM_NONE, PM_CLASSIC, PM_CAM_PENDING, PM_CAM_PAN, PM_CAM_ARMED, PM_CAM_BOX };
PrimaryMode  g_pm = PM_NONE;
SDL_FingerID g_primaryId = 0;
float        g_pSX = 0, g_pSY = 0;    // where the primary finger came down (window px)
Uint64       g_pDownNs = 0;
Coord3D      g_panAnchor = { 0, 0, 0 }; // terrain point kept under the finger while panning
bool         g_panAnchorValid = false;

// double-tap detection for camera-mode taps
Uint64       g_lastTapNs = 0;
float        g_lastTapX = 0, g_lastTapY = 0;

// two-finger gesture
bool         g_two = false;
bool         g_twoMoved = false;
float        g_lastCx = 0, g_lastCy = 0;
float        g_startCx = 0, g_startCy = 0, g_startDist = 0;
Uint64       g_twoStartMs = 0;

// after a 2-finger gesture begins, ignore leftover fingers for the left button
// until ALL fingers lift (stops a stray drag-box when one finger lifts first)
bool         g_suppressLeft = false;

// on-screen ESC/menu button (top-left). Tapping it toggles the in-game pause menu.
// KEEP THIS GEOMETRY IN SYNC WITH kMenuBtn* IN W3DInGameUI.cpp (the draw side).
const float  MENU_BTN_X = 16, MENU_BTN_Y = 14, MENU_BTN_W = 108, MENU_BTN_H = 56;
bool         g_escArmed = false;      // first finger came down on the menu button
SDL_FingerID g_escFingerId = 0;

// on-screen SKIP button (top-right), shown only during a video/cutscene. Tapping it
// synthesizes ESC → the movie loops skip. Right-anchored; KEEP IN SYNC WITH W3DDisplay.cpp.
const float  SKIP_BTN_W = 150, SKIP_BTN_H = 54, SKIP_BTN_MX = 20, SKIP_BTN_Y = 18;
bool         g_skipArmed = false;
SDL_FingerID g_skipFingerId = 0;

inline void winSize(float &w, float &h) {
	int iw = 1640, ih = 720;
	if (TheSDL3Window) SDL_GetWindowSize(TheSDL3Window, &iw, &ih);
	w = (float)iw; h = (float)ih;
}

inline float dist2D(float ax, float ay, float bx, float by) {
	float dx = ax - bx, dy = ay - by;
	return sqrtf(dx*dx + dy*dy);
}

inline bool inLiveGame() {
	return TheTacticalView && TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame();
}

// A video/cutscene is on screen (either the display's own movie or any open stream).
inline bool videoPlaying() {
	return (TheVideoPlayer && TheVideoPlayer->firstStream() != NULL)
	    || (TheDisplay && TheDisplay->isMoviePlaying());
}
// The menu button is live only while playing, while the pause menu is NOT already up, and
// NOT during a cutscene (then the Skip button takes over). Matches W3DInGameUI.cpp's draw gate.
inline bool menuButtonActive() {
	return inLiveGame() && (!TheInGameUI || !TheInGameUI->isQuitMenuVisible()) && !videoPlaying();
}
void toEngine(float px, float py, ICoord2D &out);   // window pixels -> display coordinates
// Both buttons are drawn in the display's coordinates (W3DInGameUI.cpp), which are not window
// pixels when the game renders below the screen resolution (Options > Resolution).
inline bool onMenuButton(float px, float py) {
	ICoord2D e; toEngine(px, py, e);
	return e.x >= MENU_BTN_X && e.x < MENU_BTN_X + MENU_BTN_W
	    && e.y >= MENU_BTN_Y && e.y < MENU_BTN_Y + MENU_BTN_H;
}
inline bool onSkipButton(float px, float py) {
	float w, h; winSize(w, h);
	if (TheDisplay && TheDisplay->getWidth()) w = (float)TheDisplay->getWidth();
	ICoord2D e; toEngine(px, py, e);
	float bx = w - SKIP_BTN_MX - SKIP_BTN_W;   // top-right, right-anchored (matches draw)
	return e.x >= bx && e.x < bx + SKIP_BTN_W && e.y >= SKIP_BTN_Y && e.y < SKIP_BTN_Y + SKIP_BTN_H;
}

// Camera sensitivity from the Options > Scroll Speed slider, which persists
// m_keyboardScrollFactor (default 0.5, slider max ≈ 1.0). Punchy mapping for the legacy
// 2-finger pan: default ≈ 3x, slider max ≈ 6x, floored at 0.5x so a drag is never dead.
inline float panSensitivity() {
	float f = TheGlobalData ? TheGlobalData->m_keyboardScrollFactor : 0.5f;
	float mult = f * 6.0f;
	if (mult < 0.5f) mult = 0.5f;
	if (mult > 8.0f) mult = 8.0f;
	return mult;
}
// The same slider as a plain multiplier (default 1.0) for fling strength and edge-pan speed.
// Grab-pan and pinch are direct manipulation and take no multiplier.
inline float cameraSpeedMultiplier() {
	float m = panSensitivity() / 3.0f;
	if (m < 0.4f) m = 0.4f;
	if (m > 2.0f) m = 2.0f;
	return m;
}

// ------------------------------------------------- Java bridge (density, haptics) --
// ZerohourActivity exposes two static methods for the touch layer; both are optional and
// every failure degrades to a sane default. The method IDs are resolved once.
struct JavaBridge { jclass cls; jmethodID pixelsPerMm; jmethodID hapticTick; bool resolved; };
JavaBridge g_java = { nullptr, nullptr, nullptr, false };

inline JNIEnv *jniEnv() { return static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv()); }

bool resolveJavaBridge() {
	if (g_java.resolved) return g_java.cls != nullptr;
	g_java.resolved = true;
	JNIEnv *env = jniEnv();
	if (!env) return false;
	jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
	if (!activity) return false;
	jclass local = env->GetObjectClass(activity);   // the running activity's own class (ZerohourActivity)
	if (local) {
		g_java.pixelsPerMm = env->GetStaticMethodID(local, "getPixelsPerMm", "()F");
		if (env->ExceptionCheck()) { env->ExceptionClear(); g_java.pixelsPerMm = nullptr; }
		g_java.hapticTick = env->GetStaticMethodID(local, "hapticTick", "(I)V");
		if (env->ExceptionCheck()) { env->ExceptionClear(); g_java.hapticTick = nullptr; }
		g_java.cls = static_cast<jclass>(env->NewGlobalRef(local));
		env->DeleteLocalRef(local);
	}
	env->DeleteLocalRef(activity);
	return g_java.cls != nullptr;
}

float javaPixelsPerMm() {
	if (!resolveJavaBridge() || !g_java.pixelsPerMm) return 0.0f;
	JNIEnv *env = jniEnv();
	if (!env) return 0.0f;
	float v = (float)env->CallStaticFloatMethod(g_java.cls, g_java.pixelsPerMm);
	if (env->ExceptionCheck()) { env->ExceptionClear(); return 0.0f; }
	return v;
}

// Mirrors ZerohourActivity.hapticTick(int kind).
enum HapticKind { HAPTIC_TICK = 0, HAPTIC_CLICK = 1, HAPTIC_ERROR = 2 };
void haptic(HapticKind kind) {
	if (!resolveJavaBridge() || !g_java.hapticTick) return;
	JNIEnv *env = jniEnv();
	if (!env) return;
	env->CallStaticVoidMethod(g_java.cls, g_java.hapticTick, (jint)kind);
	if (env->ExceptionCheck()) env->ExceptionClear();
}

// Physical pixels per millimetre of the window, so every threshold holds on any phone.
float g_pxPerMm = 0.0f;
float pxPerMm() {
	if (g_pxPerMm > 0.0f) return g_pxPerMm;
	float v = javaPixelsPerMm();                       // DisplayMetrics.xdpi (sanity-checked in Java)
	if (!(v > 2.0f && v < 60.0f)) {
		// SDL's display scale on Android is the density in 160 dpi units.
		float scale = TheSDL3Window ? SDL_GetWindowDisplayScale(TheSDL3Window) : 0.0f;
		v = (scale > 0.5f && scale < 8.0f) ? scale * (160.0f / 25.4f) : 0.0f;
	}
	if (!(v > 2.0f && v < 60.0f)) {
		float w, h; winSize(w, h);                     // last resort: a phone's short side is ≈ 68 mm
		v = (h < w ? h : w) / 68.0f;
	}
	g_pxPerMm = v;
	fprintf(stderr, "INFO: Android touch: %.2f px/mm\n", v);
	return v;
}
inline float mm(float millimetres) { return millimetres * pxPerMm(); }

// ------------------------------------------------- synthesized mouse input --
// The window the events claim to come from: SDL3Mouse::scaleMouseCoordinates maps window pixels
// to the display's coordinates only for a known window, which matters whenever the game renders
// below the screen resolution (Options > Resolution).
inline SDL_WindowID synthWindowId() { return TheSDL3Window ? SDL_GetWindowID(TheSDL3Window) : 0; }
void synthMotion(float x, float y) {
	if (!TheMouse) return;
	SDL3Mouse *m = dynamic_cast<SDL3Mouse*>(TheMouse); if (!m) return;
	SDL_Event e; memset(&e, 0, sizeof(e));
	e.type = SDL_EVENT_MOUSE_MOTION;
	e.motion.timestamp = SDL_GetTicksNS();
	e.motion.windowID = synthWindowId();
	e.motion.x = x; e.motion.y = y;
	m->addSDLEvent(&e);
}
void synthButton(Uint8 button, bool down, float x, float y, Uint8 clicks = 1) {
	if (!TheMouse) return;
	SDL3Mouse *m = dynamic_cast<SDL3Mouse*>(TheMouse); if (!m) return;
	SDL_Event e; memset(&e, 0, sizeof(e));
	e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
	e.button.timestamp = SDL_GetTicksNS();
	e.button.windowID = synthWindowId();
	e.button.button = button;
	e.button.down = down;
	e.button.clicks = clicks;   // 2 on a down = double click (SDL3Mouse::translateButtonEvent)
	e.button.x = x; e.button.y = y;
	m->addSDLEvent(&e);
}
// Feed a synthetic key press through the same buffer as a physical key. Used for the
// on-screen SKIP button: the movie loops poll TheKeyboard for an unused KEY_ESC down.
void synthKey(SDL_Scancode sc, bool down) {
	if (!TheKeyboard) return;
	SDL3Keyboard *kb = dynamic_cast<SDL3Keyboard*>(TheKeyboard); if (!kb) return;
	SDL_Event e; memset(&e, 0, sizeof(e));
	e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;   // union: also sets e.key.type
	e.key.timestamp = SDL_GetTicksNS();
	e.key.scancode = sc;
	e.key.down = down;
	e.key.repeat = false;
	kb->addSDLEvent(&e);
}
// A synthesized "click" must look like a real tap: the button must stay DOWN
// for a while before the UP, or a latching GUI widget arms on the down and its
// up lands in the same client update, so the callback never fires (the button
// sticks "selected"). Empirically the shell menu needs ~150ms of press. A real
// finger tap is only ~100ms and our 45ms commit-defer eats into that, so we
// GUARANTEE a minimum press by deferring the up through this queue whenever it
// would otherwise come too soon after the down.
struct PendingUp { Uint8 button; float x, y; Uint64 dueMs; bool active; };
PendingUp    g_ups[8] = {};
Uint64       g_leftDownSynthMs = 0;    // wall-clock of the last synthesized left-down

void queueUpAt(Uint8 button, float x, float y, Uint64 dueMs) {
	for (int i = 0; i < 8; ++i) if (!g_ups[i].active) { g_ups[i] = { button, x, y, dueMs, true }; return; }
	synthButton(button, false, x, y);   // queue full (unexpected): release immediately
}
void drainUps() {
	Uint64 now = SDL_GetTicks();
	for (int i = 0; i < 8; ++i) if (g_ups[i].active) {
		if (now >= g_ups[i].dueMs) {
			synthButton(g_ups[i].button, false, g_ups[i].x, g_ups[i].y);
			g_ups[i].active = false;
		} else {
			// Keep the synthesized press "alive" with a motion every frame, just
			// like a real held finger. Without this the WindowManager never sees
			// the press sustained and a pushbutton's up won't fire its callback.
			synthMotion(g_ups[i].x, g_ups[i].y);
		}
	}
}
// Release a queued up for this button now (a new press of the same button must not
// overlap it, e.g. the second tap of a fast double tap).
void flushPendingUp(Uint8 button) {
	for (int i = 0; i < 8; ++i) if (g_ups[i].active && g_ups[i].button == button) {
		synthButton(button, false, g_ups[i].x, g_ups[i].y);
		g_ups[i].active = false;
	}
}
void synthLeftDown(float x, float y, Uint8 clicks = 1) {
	flushPendingUp(SDL_BUTTON_LEFT);
	synthMotion(x, y);
	synthButton(SDL_BUTTON_LEFT, true, x, y, clicks);
	g_leftHeld = true;
	g_leftDownSynthMs = SDL_GetTicks();
}
// Release a held left button, guaranteeing >= CLICK_HOLD_MS of total press so
// the GUI registers the click. Long presses/drags release immediately.
void releaseLeftHeld(float x, float y) {
	Uint64 due = g_leftDownSynthMs + CLICK_HOLD_MS;
	if (SDL_GetTicks() >= due) synthButton(SDL_BUTTON_LEFT, false, x, y);
	else                       queueUpAt(SDL_BUTTON_LEFT, x, y, due);
	g_leftHeld = false;
}
// A complete left click (or double click) at a point: press now, release after the hold.
inline void synthClick(float x, float y, Uint8 clicks) {
	synthLeftDown(x, y, clicks);
	releaseLeftHeld(x, y);
}
inline void rightClick(float x, float y) {
	synthMotion(x, y);
	synthButton(SDL_BUTTON_RIGHT, true, x, y);
	queueUpAt(SDL_BUTTON_RIGHT, x, y, SDL_GetTicks() + CLICK_HOLD_MS);
}

int findFinger(SDL_FingerID id) { for (int i = 0; i < g_tfCount; ++i) if (g_tf[i].id == id) return i; return -1; }
void dropFinger(int idx) { if (idx < 0) return; for (int i = idx; i < g_tfCount - 1; ++i) g_tf[i] = g_tf[i+1]; if (g_tfCount) g_tfCount--; }

// ------------------------------------------------------- camera helpers --
// Window pixels -> the engine's display coordinates (SDL3Mouse::scaleMouseCoordinates does
// the same for synthesized mouse events, so picks line up with clicks).
void toEngine(float px, float py, ICoord2D &out) {
	float w, h; winSize(w, h);
	Int dw = (TheDisplay && TheDisplay->getWidth())  ? (Int)TheDisplay->getWidth()  : (Int)w;
	Int dh = (TheDisplay && TheDisplay->getHeight()) ? (Int)TheDisplay->getHeight() : (Int)h;
	Int pbX = 0, pbY = 0, pbW = (Int)w, pbH = (Int)h;
	if (TheDisplay && !TheDisplay->getViewportRect(pbX, pbY, pbW, pbH)) { pbX = 0; pbY = 0; pbW = (Int)w; pbH = (Int)h; }
	if (pbW <= 0) pbW = 1;
	if (pbH <= 0) pbH = 1;
	float cx = px - (float)pbX; if (cx < 0) cx = 0; if (cx > (float)pbW) cx = (float)pbW;
	float cy = py - (float)pbY; if (cy < 0) cy = 0; if (cy > (float)pbH) cy = (float)pbH;
	out.x = (Int)(cx * (float)dw / (float)pbW);
	out.y = (Int)(cy * (float)dh / (float)pbH);
}

// World point on the terrain under a window pixel.
bool pickTerrain(float px, float py, Coord3D &out) {
	if (!TheTacticalView) return false;
	ICoord2D s; toEngine(px, py, s);
	return TheTacticalView->screenToTerrain(&s, &out) ? true : false;
}
// World point under a window pixel on the horizontal plane at height z. Used for the
// anchoring math because it is exact under camera translation (no terrain "catching").
bool pickPlane(float px, float py, Real z, Coord3D &out) {
	if (!TheTacticalView) return false;
	ICoord2D s; toEngine(px, py, s);
	return TheTacticalView->screenToWorldAtZ(&s, &out, z) == PlaneClass::INSIDE_SEGMENT;
}

// Move the camera pivot by a world-space delta. Goes through the user-action path (refused
// while a script locks the camera, stops a scripted camera otherwise) and asks the view to
// rebuild its transform; the view clips the pivot into the map's camera constraints later.
bool moveCameraBy(Real dx, Real dy) {
	if (!TheTacticalView || (dx == 0.0f && dy == 0.0f)) return false;
	Coord3D pos = TheTacticalView->getPosition();
	pos.x += dx; pos.y += dy;
	if (!TheTacticalView->userSetPosition(pos)) return false;
	TheTacticalView->forceRedraw();
	return true;
}

// Grab pan: shift the camera so the anchored terrain point sits under the finger again.
// The pick uses the camera transform from the last rendered frame; a camera translation
// moves the point under a pixel by the same amount, so one correction per frame is exact.
void keepAnchorUnderFinger(const Coord3D &anchor, float fx, float fy) {
	Coord3D q;
	if (!pickPlane(fx, fy, anchor.z, q)) return;
	moveCameraBy(anchor.x - q.x, anchor.y - q.y);
}

// Convert a screen-space displacement at a screen point into the world displacement of the
// terrain plane at height z under it (how far the ground "under the finger" moves).
bool screenDeltaToWorld(float px, float py, float dxPx, float dyPx, Real z, Real &wx, Real &wy) {
	Coord3D a, b;
	if (!pickPlane(px, py, z, a) || !pickPlane(px + dxPx, py + dyPx, z, b)) return false;
	wx = b.x - a.x; wy = b.y - a.y;
	return true;
}

// Finger velocity over a short window, from a fixed ring of samples (fed on motion events
// and once per frame while the finger is down, so a finger that stops reads as zero).
struct MotionSample { Uint64 tNs; float x, y; };
struct VelocityTracker {
	enum { N = 32 };
	MotionSample s[N];
	int head, count;
	void reset() { head = 0; count = 0; }
	void push(Uint64 t, float x, float y) {
		s[head].tNs = t; s[head].x = x; s[head].y = y;
		head = (head + 1) % N;
		if (count < N) count++;
	}
	// Average velocity in px/s over the samples inside FLING_WINDOW_NS. False without enough history.
	bool velocity(Uint64 now, float &vx, float &vy) const {
		if (count < 2) return false;
		int newest = (head - 1 + N) % N;
		int oldest = newest;
		for (int i = 1; i < count; ++i) {
			int idx = (newest - i + N) % N;
			if (now - s[idx].tNs > FLING_WINDOW_NS) break;
			oldest = idx;
		}
		Uint64 dtNs = s[newest].tNs - s[oldest].tNs;
		if (dtNs < 8ull * 1000000ull) return false;   // < 8 ms of history: no reliable velocity
		float inv = 1.0e9f / (float)dtNs;
		vx = (s[newest].x - s[oldest].x) * inv;
		vy = (s[newest].y - s[oldest].y) * inv;
		return true;
	}
};
VelocityTracker g_vel = { {}, 0, 0 };

// Fling: the camera keeps the release velocity (world units/s) with exponential decay.
struct Fling { bool active; Real vx, vy; Real stopSpeed; Coord2D expect; };
Fling g_fling = { false, 0, 0, 0, { 0, 0 } };

void stopFling() { g_fling.active = false; }

void startFling(float fx, float fy, Uint64 now) {
	float vx, vy;
	if (!g_panAnchorValid || !g_vel.velocity(now, vx, vy)) return;
	const float ppm = pxPerMm();
	float speedPx = sqrtf(vx*vx + vy*vy);
	if (speedPx < FLING_MIN_MM_S * ppm) return;
	const float capPx = FLING_MAX_MM_S * ppm;
	if (speedPx > capPx) { vx *= capPx / speedPx; vy *= capPx / speedPx; speedPx = capPx; }
	// Project the finger velocity onto the anchor plane: how fast the ground under the finger
	// was moving. The camera then travels the opposite way, so the terrain keeps sliding with it.
	const float dtProbe = 1.0f / 60.0f;
	Real wx, wy;
	if (!screenDeltaToWorld(fx, fy, vx * dtProbe, vy * dtProbe, g_panAnchor.z, wx, wy)) return;
	const float mult = cameraSpeedMultiplier();
	g_fling.vx = -wx / dtProbe * mult;
	g_fling.vy = -wy / dtProbe * mult;
	const Real worldSpeed = sqrtf(g_fling.vx*g_fling.vx + g_fling.vy*g_fling.vy);
	if (!(worldSpeed > 0.0f)) return;
	const Real worldPerPx = worldSpeed / (speedPx * mult);   // conversion at the release point
	g_fling.stopSpeed = FLING_STOP_MM_S * ppm * worldPerPx;
	g_fling.expect = TheTacticalView->getPosition2D();
	g_fling.active = true;
}

void stepFling(float dt) {
	if (!g_fling.active) return;
	const Coord2D pos = TheTacticalView->getPosition2D();
	const Real speed = sqrtf(g_fling.vx*g_fling.vx + g_fling.vy*g_fling.vy);
	if (!(speed > 0.0f)) { stopFling(); return; }
	// The view clips the pivot into the map's camera constraints after we moved it. If it pushed
	// us back against the motion by most of a step, we are on the map edge: stop gliding.
	const Real pushX = pos.x - g_fling.expect.x, pushY = pos.y - g_fling.expect.y;
	const Real pushBack = -(pushX * g_fling.vx + pushY * g_fling.vy) / speed;
	if (pushBack > 0.5f * speed * dt + 0.5f) { stopFling(); return; }

	const Real dx = g_fling.vx * dt, dy = g_fling.vy * dt;
	if (!moveCameraBy(dx, dy)) { stopFling(); return; }
	g_fling.expect.x = pos.x + dx; g_fling.expect.y = pos.y + dy;

	const Real decay = expf(-dt / FLING_TAU_S);
	g_fling.vx *= decay; g_fling.vy *= decay;
	if (speed * decay < g_fling.stopSpeed) stopFling();
}

// ------------------------------------------------------------ pinch zoom --
// The height above ground follows the pinch multiplicatively (height = start / scale) through
// a short low-pass, in ln(height) so zooming in and out feel symmetric. Beyond a limit the
// excess is rubber-banded (asymptotic, never past ZOOM_RUBBER_LOG) and springs back on release.
bool   g_zoomActive = false;     // a pinch drives the height
bool   g_zoomSettling = false;   // after release: easing back inside the limits
float  g_zoomLogH = 0.0f;        // ln(height) the view is currently driven with
float  g_zoomRawLog0 = 0.0f;     // raw (un-rubbered) ln(height) at pinch start
float  g_zoomMinLog = 0.0f, g_zoomMaxLog = 0.0f;
Real   g_zoomMaxH = 0.0f;        // view's maximum height; setZoom() takes a fraction of it

inline float rubber(float excess) { return ZOOM_RUBBER_LOG * (1.0f - expf(-excess / ZOOM_RUBBER_LOG)); }
inline float rubberInverse(float over) {
	float r = 1.0f - over / ZOOM_RUBBER_LOG;
	if (r < 0.02f) r = 0.02f;
	return -ZOOM_RUBBER_LOG * logf(r);
}
inline float softLimit(float u, float lo, float hi) {
	if (u > hi) return hi + rubber(u - hi);
	if (u < lo) return lo - rubber(lo - u);
	return u;
}
// The raw value whose softLimit() is v: lets a pinch start from an over-zoomed height without a jump.
inline float softLimitInverse(float v, float lo, float hi) {
	if (v > hi) return hi + rubberInverse(v - hi);
	if (v < lo) return lo - rubberInverse(lo - v);
	return v;
}

void applyZoomHeight() {
	if (!TheTacticalView || !(g_zoomMaxH > 0.0f)) return;
	TheTacticalView->userSetZoom(expf(g_zoomLogH) / g_zoomMaxH);   // immediate: W3DView::setZoom sets both desired and actual
}

void beginPinchZoom() {
	g_zoomActive = false; g_zoomSettling = false;
	if (!TheTacticalView) return;
	const Real minH = TheTacticalView->getMinHeightAboveGround();
	g_zoomMaxH = TheTacticalView->getMaxHeightAboveGround();
	if (!(minH > 1.0f) || !(g_zoomMaxH > minH)) return;
	Real h = TheTacticalView->getCurrentHeightAboveGround();   // where the camera really is now
	if (!(h > 1.0f)) h = TheTacticalView->getHeightAboveGround();
	if (!(h > 1.0f)) return;
	g_zoomMinLog = logf(minH);
	g_zoomMaxLog = logf(g_zoomMaxH);
	g_zoomLogH = logf(h);
	g_zoomRawLog0 = softLimitInverse(g_zoomLogH, g_zoomMinLog, g_zoomMaxLog);
	g_zoomActive = true;
}

void endPinchZoom() {
	if (!g_zoomActive) return;
	g_zoomActive = false;
	g_zoomSettling = (g_zoomLogH > g_zoomMaxLog || g_zoomLogH < g_zoomMinLog);
}

// Spring back inside the limits after an over-zoom.
void settleZoom(float dt) {
	if (!g_zoomSettling) return;
	const float target = g_zoomLogH > g_zoomMaxLog ? g_zoomMaxLog : g_zoomMinLog;
	g_zoomLogH += (target - g_zoomLogH) * (1.0f - expf(-dt / ZOOM_SETTLE_TAU_S));
	if (fabsf(target - g_zoomLogH) < 0.002f) { g_zoomLogH = target; g_zoomSettling = false; }
	applyZoomHeight();
}

// One frame of an active pinch: low-pass the height toward the fingers' target and keep the
// ground under the pinch midpoint where it is. The camera sits at pivot + D * h (D a fixed
// direction, h the height above the pivot's ground, see W3DView::buildCameraPosition), so a
// height change scales the view about the pivot: the point under a pixel on the pivot's ground
// plane moves to Q' = C + (Q - C) * h1/h0. Moving the pivot by (Q - C) * (1 - h1/h0) cancels
// that exactly, and stays independent of the legacy 2-finger pan applied in the same frame.
void pinchZoomFrame(float dt, float cx, float cy, float pinchDist) {
	if (!g_zoomActive || !(g_startDist > 1.0f) || !(pinchDist > 1.0f)) return;
	const float target = softLimit(g_zoomRawLog0 - logf(pinchDist / g_startDist), g_zoomMinLog, g_zoomMaxLog);
	const float newLog = g_zoomLogH + (target - g_zoomLogH) * (1.0f - expf(-dt / ZOOM_TAU_S));
	if (fabsf(newLog - g_zoomLogH) < 1.0e-4f) return;

	const Real h0 = expf(g_zoomLogH), h1 = expf(newLog);
	const Coord3D pivot = TheTacticalView->getPosition();
	Coord3D q;
	if (h0 > 1.0f && pickPlane(cx, cy, pivot.z, q)) {
		const Real r = h1 / h0;
		moveCameraBy((q.x - pivot.x) * (1.0f - r), (q.y - pivot.y) * (1.0f - r));
	}
	g_zoomLogH = newLog;
	applyZoomHeight();
}

// ------------------------------------------------------- gesture logic --
// Camera mode: a finger that comes down on the battlefield itself (not on a HUD window, not
// while placing a building, not in a cutscene or the pause menu) drives the camera.
bool cameraModeAt(float px, float py) {
	if (!inLiveGame() || videoPlaying()) return false;
	if (TheInGameUI && (TheInGameUI->isQuitMenuVisible() || TheInGameUI->getPendingPlaceType() != nullptr)) return false;
	if (TheWindowManager) {
		ICoord2D s; toEngine(px, py, s);
		GameWindow *window = TheWindowManager->getWindowUnderCursor(s.x, s.y);
		// Same rule as W3DView::pickDrawable: an opaque window (or opaque parent) owns the touch.
		while (window) {
			if (!BitIsSet(window->winGetStatus(), WIN_STATUS_SEE_THRU)) return false;
			window = window->winGetParent();
		}
	}
	return true;
}

// ------------------------------------------------------------ smart touch --
// GeneralsX @feature Android smart touch 19/10/2026 (docs/MOBILE-UX-SPEC.md §7). The PC screens
// are laid out for a mouse: rows and dropdown arrows of 2–3 mm. When a finger lands on no
// interactive control, the press moves to the nearest interactive gadget within SMART_TOUCH_MM;
// when it lands on a dropdown's text, it moves to the dropdown's arrow (which opens the list). So
// every screen becomes tappable without touching its layout. Never in a battle unless the pause
// menu is up: there a miss is the ground.
const float SMART_TOUCH_MM = 4.0f;

inline bool interactiveStyle(UnsignedInt style) {
	return (style & (GWS_PUSH_BUTTON | GWS_RADIO_BUTTON | GWS_CHECK_BOX | GWS_VERT_SLIDER | GWS_HORZ_SLIDER
	               | GWS_SCROLL_LISTBOX | GWS_ENTRY_FIELD | GWS_TAB_CONTROL | GWS_COMBO_BOX)) != 0;
}
// A dropdown's text box: the arrow button beside it is what opens the list.
inline GameWindow *comboArrowFor(GameWindow *w) {
	GameWindow *parent = w ? w->winGetParent() : nullptr;
	if (parent && (parent->winGetInstanceData()->getStyle() & GWS_COMBO_BOX)
	    && (w->winGetInstanceData()->getStyle() & GWS_ENTRY_FIELD))
		return GadgetComboBoxGetDropDownButton(parent);
	return nullptr;
}
inline void windowCentre(GameWindow *w, Int &x, Int &y) {
	ICoord2D pos, size;
	w->winGetScreenPosition(&pos.x, &pos.y);
	w->winGetSize(&size.x, &size.y);
	x = pos.x + size.x / 2;
	y = pos.y + size.y / 2;
}
// The point of a window's rectangle nearest to (x, y), kept 2 px inside, and its squared distance.
void nearestPointIn(GameWindow *w, Int x, Int y, Int &nx, Int &ny, float &d2) {
	ICoord2D pos, size;
	w->winGetScreenPosition(&pos.x, &pos.y);
	w->winGetSize(&size.x, &size.y);
	const Int x0 = pos.x + 2, x1 = pos.x + size.x - 3, y0 = pos.y + 2, y1 = pos.y + size.y - 3;
	nx = x < x0 ? x0 : (x > x1 ? x1 : x);
	ny = y < y0 ? y0 : (y > y1 ? y1 : y);
	const float dx = (float)(nx - x), dy = (float)(ny - y);
	d2 = dx * dx + dy * dy;
}
struct NearestGadget { GameWindow *win; float d2; Int x, y; };
// Depth-first over a window and its children with the hit test's reachability rules: a hidden or
// disabled window and everything below it are out; a no-input window is skipped, its children not.
void nearestGadgetIn(GameWindow *w, Int x, Int y, NearestGadget &best) {
	const UnsignedInt st = w->winGetStatus();
	if (BitIsSet(st, WIN_STATUS_HIDDEN) || !BitIsSet(st, WIN_STATUS_ENABLED)) return;
	if (!BitIsSet(st, WIN_STATUS_NO_INPUT) && interactiveStyle(w->winGetInstanceData()->getStyle())) {
		Int nx, ny; float d2;
		nearestPointIn(w, x, y, nx, ny, d2);
		if (d2 < best.d2) { best.win = w; best.d2 = d2; best.x = nx; best.y = ny; }
	}
	for (GameWindow *c = w->winGetChild(); c; c = c->winGetNext()) nearestGadgetIn(c, x, y, best);
}
inline bool smartTouchActive() {
	if (videoPlaying() || !TheWindowManager) return false;
	return !inLiveGame() || (TheInGameUI && TheInGameUI->isQuitMenuVisible());
}
// Engine display coordinates -> window pixels (the inverse of toEngine).
void fromEngine(Int ex, Int ey, float &px, float &py) {
	float w, h; winSize(w, h);
	Int dw = (TheDisplay && TheDisplay->getWidth())  ? (Int)TheDisplay->getWidth()  : (Int)w;
	Int dh = (TheDisplay && TheDisplay->getHeight()) ? (Int)TheDisplay->getHeight() : (Int)h;
	Int pbX = 0, pbY = 0, pbW = (Int)w, pbH = (Int)h;
	if (TheDisplay && !TheDisplay->getViewportRect(pbX, pbY, pbW, pbH)) { pbX = 0; pbY = 0; pbW = (Int)w; pbH = (Int)h; }
	if (dw <= 0) dw = 1;
	if (dh <= 0) dh = 1;
	px = (float)pbX + (float)ex * (float)pbW / (float)dw;
	py = (float)pbY + (float)ey * (float)pbH / (float)dh;
}
// Moves a classic-mode press to where it was meant to land. Returns whether it moved.
bool smartTouchAdjust(float &px, float &py) {
	if (!smartTouchActive()) return false;
	ICoord2D s; toEngine(px, py, s);
	GameWindow *under = TheWindowManager->getWindowUnderCursor(s.x, s.y);
	GameWindow *target = nullptr;
	Int tx = s.x, ty = s.y;
	if (under) {
		if (GameWindow *arrow = comboArrowFor(under)) {
			target = arrow;
			windowCentre(arrow, tx, ty);
		} else if (interactiveStyle(under->winGetInstanceData()->getStyle())) {
			return false;   // already on a control
		}
	}
	if (!target) {
		float w, h; winSize(w, h);
		const float dw = (TheDisplay && TheDisplay->getWidth()) ? (float)TheDisplay->getWidth() : w;
		const float reach = mm(SMART_TOUCH_MM) * dw / (w > 0.0f ? w : 1.0f);   // in engine px
		NearestGadget best = { nullptr, reach * reach, s.x, s.y };
		if (GameWindow *modal = TheWindowManager->winGetModalWindow()) {
			nearestGadgetIn(modal, s.x, s.y, best);
		} else {
			for (GameWindow *top = TheWindowManager->winGetWindowList(); top; top = top->winGetNext())
				nearestGadgetIn(top, s.x, s.y, best);
		}
		if (!best.win) return false;
		target = best.win; tx = best.x; ty = best.y;
		if (GameWindow *arrow = comboArrowFor(target)) {
			target = arrow;
			windowCentre(arrow, tx, ty);
		}
	}
	fromEngine(tx, ty, px, py);
	return true;
}

// Keep synthesized drag coordinates off the engine's own 3 px screen-edge scroll band.
inline void clampInsideEdges(float &px, float &py) {
	float w, h; winSize(w, h);
	if (px < EDGE_GUARD_PX) px = EDGE_GUARD_PX; else if (px > w - 1 - EDGE_GUARD_PX) px = w - 1 - EDGE_GUARD_PX;
	if (py < EDGE_GUARD_PX) py = EDGE_GUARD_PX; else if (py > h - 1 - EDGE_GUARD_PX) py = h - 1 - EDGE_GUARD_PX;
}

// A camera-mode tap: a left click, or a left double click when it follows another tap closely.
void cameraTap(float x, float y, Uint64 now) {
	Uint8 clicks = 1;
	if (g_lastTapNs != 0 && now - g_lastTapNs <= DOUBLE_TAP_NS && dist2D(x, y, g_lastTapX, g_lastTapY) <= mm(DOUBLE_TAP_MM)) {
		clicks = 2;
		g_lastTapNs = 0;   // a third tap starts over
	} else {
		g_lastTapNs = now; g_lastTapX = x; g_lastTapY = y;
	}
	synthClick(x, y, clicks);
}

void endPrimary() {
	g_pm = PM_NONE;
	g_panAnchorValid = false;
}

void beginTwoFinger() {
	// g_tf[0] is the original finger; release its held left button if any.
	if (g_leftHeld) { synthButton(SDL_BUTTON_LEFT, false, g_tf[0].x, g_tf[0].y); g_leftHeld = false; }
	g_leftPending  = false;
	endPrimary();
	g_two          = true;
	g_twoMoved     = false;
	g_suppressLeft = true;
	g_startCx = (g_tf[0].x + g_tf[1].x) * 0.5f;
	g_startCy = (g_tf[0].y + g_tf[1].y) * 0.5f;
	g_lastCx  = g_startCx; g_lastCy = g_startCy;
	g_startDist  = dist2D(g_tf[0].x, g_tf[0].y, g_tf[1].x, g_tf[1].y);
	g_twoStartMs = SDL_GetTicks();
	if (inLiveGame()) beginPinchZoom();
}

void handleFingerDown(const SDL_TouchFingerEvent &tf) {
	float w, h; winSize(w, h);
	float px = tf.x * w, py = tf.y * h;
	const Uint64 now = SDL_GetTicksNS();
	stopFling();   // any new touch stops a glide
	bool firstFinger = (g_tfCount == 0);
	if (g_tfCount < 2) {
		g_tf[g_tfCount].id = tf.fingerID;
		g_tf[g_tfCount].sx = px; g_tf[g_tfCount].sy = py;
		g_tf[g_tfCount].x  = px; g_tf[g_tfCount].y  = py;
		g_tfCount++;
	}
	if (g_tfCount == 1) {
		if (firstFinger && videoPlaying() && onSkipButton(px, py)) {
			// press landed on the SKIP button during a cutscene — arm it, no select
			g_skipArmed = true; g_skipFingerId = tf.fingerID;
			g_leftPending = false; g_leftHeld = false;
			g_pm = PM_NONE;
		} else if (firstFinger && menuButtonActive() && onMenuButton(px, py)) {
			// press landed on the on-screen menu button — arm it, don't start a select/drag
			g_escArmed = true; g_escFingerId = tf.fingerID;
			g_leftPending = false; g_leftHeld = false;
			g_pm = PM_NONE;
		} else if (cameraModeAt(px, py)) {
			g_pm = PM_CAM_PENDING;
			g_primaryId = tf.fingerID;
			g_pSX = px; g_pSY = py; g_pDownNs = now;
			g_panAnchorValid = false;
			g_vel.reset(); g_vel.push(now, px, py);
			g_leftPending = false; g_leftHeld = false;
		} else {
			g_pm = PM_CLASSIC;
			g_primaryId = tf.fingerID;
			g_leftPending = true; g_leftHeld = false;
			g_leftId = tf.fingerID;
			g_leftRawX = px; g_leftRawY = py;
			g_leftSX = px; g_leftSY = py;
			smartTouchAdjust(g_leftSX, g_leftSY);   // menus: land on the nearest control
			g_leftOffX = g_leftSX - px; g_leftOffY = g_leftSY - py;
			g_leftDownMs = SDL_GetTicks();
		}
	} else if (g_tfCount == 2) {
		g_escArmed = false; g_skipArmed = false;   // a 2nd finger cancels the button arm
		if (g_pm == PM_CAM_BOX) {
			return;   // a second finger during box-select is ignored; the box carries on
		}
		beginTwoFinger();
	}
}

void handleFingerMotion(const SDL_TouchFingerEvent &tf) {
	int idx = findFinger(tf.fingerID);
	if (idx < 0) return;
	float w, h; winSize(w, h);
	float px = tf.x * w, py = tf.y * h;
	g_tf[idx].x = px; g_tf[idx].y = py;
	const Uint64 now = SDL_GetTicksNS();

	if (g_two && g_tfCount == 2) {
		// The camera work for two fingers happens once per frame in touchFrame();
		// here we only decide whether this is still a tap.
		float cx = (g_tf[0].x + g_tf[1].x) * 0.5f;
		float cy = (g_tf[0].y + g_tf[1].y) * 0.5f;
		float dist = dist2D(g_tf[0].x, g_tf[0].y, g_tf[1].x, g_tf[1].y);
		if (dist2D(cx, cy, g_startCx, g_startCy) > mm(TAP_SLOP_MM) || fabsf(dist - g_startDist) > mm(TAP_SLOP_MM))
			g_twoMoved = true;
		return;
	}

	if (tf.fingerID != g_primaryId || g_suppressLeft) return;

	switch (g_pm) {
	case PM_CLASSIC:
		// the smart-touch shift of the press stays on the whole drag, so a slider grabbed through
		// it follows the finger from where the press landed
		if (g_leftPending && !g_leftHeld) {
			if (dist2D(px, py, g_leftRawX, g_leftRawY) > mm(TAP_SLOP_MM)) {
				synthLeftDown(g_leftSX, g_leftSY);   // anchor the drag at the press point
				g_leftPending = false;
				synthMotion(px + g_leftOffX, py + g_leftOffY);
			}
		} else if (g_leftHeld) {
			synthMotion(px + g_leftOffX, py + g_leftOffY);
		}
		break;

	case PM_CAM_PENDING:
		g_vel.push(now, px, py);
		if (dist2D(px, py, g_pSX, g_pSY) > mm(TAP_SLOP_MM)) {
			// It is a drag: grab the ground under the finger and keep it there from now on.
			g_pm = PM_CAM_PAN;
			g_panAnchorValid = pickTerrain(px, py, g_panAnchor);
			if (!g_panAnchorValid && TheTacticalView) {
				g_panAnchorValid = pickPlane(px, py, TheTacticalView->getPosition().z, g_panAnchor);
			}
		}
		break;

	case PM_CAM_PAN:
		g_vel.push(now, px, py);   // the correction itself runs once per frame
		break;

	case PM_CAM_ARMED:
		if (dist2D(px, py, g_pSX, g_pSY) > mm(TAP_SLOP_MM)) {
			// Long-press then drag: a rubber-band box anchored at the press point.
			g_pm = PM_CAM_BOX;
			float ax = g_pSX, ay = g_pSY; clampInsideEdges(ax, ay);
			synthLeftDown(ax, ay);
			clampInsideEdges(px, py);
			synthMotion(px, py);
		}
		break;

	case PM_CAM_BOX:
		clampInsideEdges(px, py);
		synthMotion(px, py);
		break;

	case PM_NONE:
		break;
	}
}

void handleFingerUp(const SDL_TouchFingerEvent &tf) {
	int idx = findFinger(tf.fingerID);
	if (idx < 0) return;   // untracked (3rd+) finger — ignore
	float w, h; winSize(w, h);
	float px = tf.x * w, py = tf.y * h;
	const Uint64 now = SDL_GetTicksNS();

	// SKIP button: fire only if the finger lifts while still over it and a video is playing
	if (g_skipArmed && tf.fingerID == g_skipFingerId) {
		bool fire = onSkipButton(px, py) && videoPlaying();
		g_skipArmed = false;
		dropFinger(idx);
		if (g_tfCount == 0) { g_suppressLeft = false; g_leftPending = false; g_leftHeld = false; endPrimary(); }
		if (fire) { synthKey(SDL_SCANCODE_ESCAPE, true); synthKey(SDL_SCANCODE_ESCAPE, false); }
		return;
	}

	// on-screen menu button: fire only if the finger lifts while still over the button
	if (g_escArmed && tf.fingerID == g_escFingerId) {
		bool fire = onMenuButton(px, py) && menuButtonActive();
		g_escArmed = false;
		dropFinger(idx);
		if (g_tfCount == 0) { g_suppressLeft = false; g_leftPending = false; g_leftHeld = false; endPrimary(); }
		if (fire) ToggleQuitMenu();
		return;
	}

	if (g_two) {
		bool wasTap = (!g_twoMoved) && (SDL_GetTicks() - g_twoStartMs <= TWO_TAP_MAX_MS);
		g_two = false;
		endPinchZoom();
		if (wasTap && inLiveGame()) rightClick(g_startCx, g_startCy);   // deselect / cancel placement
		dropFinger(idx);
		if (g_tfCount == 0) { g_suppressLeft = false; g_leftPending = false; g_leftHeld = false; }
		return;
	}

	if (tf.fingerID == g_primaryId && !g_suppressLeft) {
		switch (g_pm) {
		case PM_CLASSIC:
			if (g_leftHeld) {
				releaseLeftHeld(px + g_leftOffX, py + g_leftOffY);
			} else if (g_leftPending) {
				// fast tap: finger lifted before the commit-defer — press now, and the
				// up is guaranteed to trail by >= CLICK_HOLD_MS so the click registers.
				synthClick(g_leftSX, g_leftSY, 1);
				g_leftPending = false;
			}
			break;
		case PM_CAM_PENDING:
			cameraTap(g_pSX, g_pSY, now);   // a tap is a click at the touch-down point
			break;
		case PM_CAM_PAN:
			g_vel.push(now, px, py);
			startFling(px, py, now);
			break;
		case PM_CAM_ARMED:
			synthClick(g_pSX, g_pSY, 1);    // long-press without a drag: just a (slow) tap
			break;
		case PM_CAM_BOX:
			clampInsideEdges(px, py);
			releaseLeftHeld(px, py);        // ends the rubber-band selection
			break;
		case PM_NONE:
			break;
		}
		endPrimary();
	}
	dropFinger(idx);
	if (g_tfCount == 0) { g_suppressLeft = false; g_escArmed = false; g_skipArmed = false; endPrimary(); }
}

// Run once per event-pump after draining events: release any due synthesized
// button-ups, commit a deferred classic left-down once the defer window elapses,
// and arm box-select once a camera-mode press has been held still long enough.
void touchTick() {
	drainUps();
	if (g_pm == PM_CLASSIC && g_leftPending && !g_leftHeld && g_tfCount == 1 && !g_suppressLeft) {
		if (SDL_GetTicks() - g_leftDownMs >= TAP_DEFER_MS) {
			synthLeftDown(g_leftSX, g_leftSY);
			g_leftPending = false;
		}
	}
	if (g_pm == PM_CAM_PENDING && g_tfCount == 1 && SDL_GetTicksNS() - g_pDownNs >= LONG_PRESS_NS) {
		g_pm = PM_CAM_ARMED;
		haptic(HAPTIC_TICK);
	}
}

// While box-selecting, a finger within EDGE_PAN_BAND_MM of a screen edge pans the camera at
// a steady screen speed (the box anchor is a screen point, so the selection grows with it).
void edgePanFrame(float dt, float fx, float fy) {
	float w, h; winSize(w, h);
	const float band = mm(EDGE_PAN_BAND_MM);
	float dirX = 0.0f, dirY = 0.0f;
	if (fx < band) dirX = -1.0f; else if (fx > w - band) dirX = 1.0f;
	if (fy < band) dirY = -1.0f; else if (fy > h - band) dirY = 1.0f;
	if (dirX == 0.0f && dirY == 0.0f) return;
	const float speedPx = EDGE_PAN_SCREENS_S * h * cameraSpeedMultiplier();
	Real wx, wy;
	if (!screenDeltaToWorld(w * 0.5f, h * 0.5f, dirX * speedPx * dt, dirY * speedPx * dt, TheTacticalView->getPosition().z, wx, wy)) return;
	moveCameraBy(wx, wy);
}

// Legacy 2-finger pan plus the anchored pinch zoom, once per frame.
void twoFingerFrame(float dt) {
	const float cx = (g_tf[0].x + g_tf[1].x) * 0.5f;
	const float cy = (g_tf[0].y + g_tf[1].y) * 0.5f;
	const float dist = dist2D(g_tf[0].x, g_tf[0].y, g_tf[1].x, g_tf[1].y);
	if (inLiveGame()) {
		pinchZoomFrame(dt, cx, cy, dist);
		const float sens = panSensitivity();   // Options > Scroll Speed slider (persisted)
		Coord2D off;
		off.x = -(cx - g_lastCx) * PAN_K * sens;
		off.y = -(cy - g_lastCy) * PAN_K * sens;
		if (off.x != 0.0f || off.y != 0.0f) TheTacticalView->userScrollBy(&off);
	}
	g_lastCx = cx; g_lastCy = cy;
}

// Once per rendered frame, before the game client updates the view: all camera motion that
// depends on time or on the last frame's camera transform happens here, exactly once.
void touchFrame() {
	static Uint64 prevNs = 0;
	const Uint64 now = SDL_GetTicksNS();
	float dt = prevNs ? (float)(now - prevNs) * 1.0e-9f : (1.0f / 60.0f);
	prevNs = now;
	if (dt < 0.001f) dt = 0.001f;
	if (dt > 0.05f) dt = 0.05f;

	if (!inLiveGame()) {
		// Leaving a battle (or not in one): nothing to drive, drop any carried-over camera state.
		stopFling();
		g_zoomActive = false; g_zoomSettling = false;
		g_panAnchorValid = false;
		return;
	}

	if (g_two && g_tfCount == 2) {
		twoFingerFrame(dt);
	} else if (g_pm == PM_CAM_PAN) {
		const int i = findFinger(g_primaryId);
		if (i >= 0) {
			g_vel.push(now, g_tf[i].x, g_tf[i].y);   // a still finger decays the velocity to zero
			if (g_panAnchorValid) keepAnchorUnderFinger(g_panAnchor, g_tf[i].x, g_tf[i].y);
		}
	} else if (g_pm == PM_CAM_BOX) {
		const int i = findFinger(g_primaryId);
		if (i >= 0) edgePanFrame(dt, g_tf[i].x, g_tf[i].y);
	}

	stepFling(dt);
	settleZoom(dt);
}

} // namespace
#endif // __ANDROID__

/**
 * Constructor: Initialize SDL3 game engine state
 */
SDL3GameEngine::SDL3GameEngine()
	: GameEngine(),
	  m_SDLWindow(nullptr),
	  m_IsInitialized(false),
	  m_IsActive(false),
	  m_IsTextInputActive(false),
	  m_TextInputFocusWindow(nullptr)
{
	fprintf(stderr, "DEBUG: SDL3GameEngine::SDL3GameEngine() created\n");
}

/**
 * Destructor: Cleanup SDL3 resources
 */
SDL3GameEngine::~SDL3GameEngine()
{
	if (m_SDLWindow && m_IsTextInputActive) {
		SDL_StopTextInput(m_SDLWindow);
		m_IsTextInputActive = false;
		m_TextInputFocusWindow = nullptr;
	}

	if (m_IsInitialized) {
		// Window cleanup is done in reset/shutdown
	}
	fprintf(stderr, "DEBUG: SDL3GameEngine::~SDL3GameEngine() destroyed\n");
}

/**
 * From GameEngine: init() - initialize subsystems
 * 
 * GeneralsX @bugfix felipebraz 16/02/2026
 * Simplified to follow fighter19 pattern - SDL3/Vulkan initialized in SDL3Main.cpp
 * before GameEngine is created. This init() only delegates to parent GameEngine::init().
 * ApplicationHWnd and TheSDL3Window are already set by main() before this is called.
 */
void SDL3GameEngine::init(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::init() starting\n");

	if (TheGlobalData && TheGlobalData->m_headless) {
		// GeneralsX @bugfix Copilot 17/05/2026 Allow headless replay path to initialize engine subsystems without an SDL window.
		fprintf(stderr, "INFO: SDL3GameEngine::init() headless mode - skipping SDL window binding\n");
		m_SDLWindow = nullptr;
		m_IsInitialized = true;
		m_IsActive = true;
		GameEngine::init();
		return;
	}

	// Verify window was created by SDL3Main.cpp
	extern SDL_Window* TheSDL3Window;
	extern HWND ApplicationHWnd;
	
	if (!TheSDL3Window || !ApplicationHWnd) {
		fprintf(stderr, "FATAL: SDL3 window not initialized before GameEngine::init()\n");
		fprintf(stderr, "FATAL: TheSDL3Window=%p, ApplicationHWnd=%p\n", TheSDL3Window, ApplicationHWnd);
		return;
	}

	// Store window reference locally
	m_SDLWindow = TheSDL3Window;
	m_IsInitialized = true;
	m_IsActive = true;

#if defined(__ANDROID__)
	// GeneralsX @feature Android touch → RTS controls: take full control of touch.
	// Disable SDL's automatic single-finger→mouse synthesis so we can distinguish
	// 1-finger (select) from 2-finger (pan / zoom / command) gestures ourselves.
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
	fprintf(stderr, "INFO: Android touch controls enabled (grab pan + fling, long-press box select, anchored pinch zoom)\n");
#endif

	fprintf(stderr, "INFO: SDL3GameEngine using pre-initialized window\n");

	// Call parent init to initialize game subsystems
	GameEngine::init();
}

/**
 * From GameEngine: reset() - reset system to starting state
 */
void SDL3GameEngine::reset(void)
{
	fprintf(stderr, "DEBUG: SDL3GameEngine::reset()\n");
	if (m_SDLWindow && m_IsTextInputActive) {
		SDL_StopTextInput(m_SDLWindow);
		m_IsTextInputActive = false;
		m_TextInputFocusWindow = nullptr;
	}
	GameEngine::reset();
}

/**
 * From GameEngine: update() - per-frame update
 */
void SDL3GameEngine::update(void)
{
	pollSDL3Events();
#if defined(__ANDROID__)
	// GeneralsX @feature Android touch → RTS controls: the per-frame camera work (grab pan,
	// fling, pinch, edge pan) runs exactly once per rendered frame, before the game client
	// updates the view. pollSDL3Events() also runs from serviceWindowsOS() mid-frame, so it
	// must not be the place for anything that integrates time or moves the camera.
	touchFrame();
#endif
	GameEngine::update();
}

/**
 * From GameEngine: execute() - main game loop
 */
void SDL3GameEngine::execute(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::execute() - entering main loop\n");
	GameEngine::execute();
	fprintf(stderr, "INFO: SDL3GameEngine::execute() - exited main loop\n");
}

/**
 * From GameEngine: serviceWindowsOS() - native OS service
 * On Linux, process SDL3 events
 */
void SDL3GameEngine::serviceWindowsOS(void)
{
	pollSDL3Events();
}

/**
 * Check if game has OS focus
 */
Bool SDL3GameEngine::isActive(void)
{
	return m_IsActive;
}

/**
 * Set OS focus status
 */
void SDL3GameEngine::setIsActive(Bool isActive)
{
	m_IsActive = isActive;
}

/**
 * Poll and process SDL3 events
 * Handles keyboard, mouse, window, and quit events
 */
void SDL3GameEngine::pollSDL3Events(void)
{
	if (!m_SDLWindow) {
		return;
	}

	updateTextInputState();

	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
			case SDL_EVENT_QUIT:
				m_quitting = true;
				break;

			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				m_quitting = true;
				break;

			case SDL_EVENT_WINDOW_FOCUS_GAINED:
				m_IsActive = true;
				if (TheMouse) {
					TheMouse->regainFocus();
					TheMouse->refreshCursorCapture();
				}
				break;

			case SDL_EVENT_WINDOW_FOCUS_LOST:
				m_IsActive = false;
				if (m_IsTextInputActive) {
					SDL_StopTextInput(m_SDLWindow);
					m_IsTextInputActive = false;
					m_TextInputFocusWindow = nullptr;
				}
				if (TheMouse) {
					TheMouse->loseFocus();
				}
				break;

			case SDL_EVENT_WINDOW_MOUSE_ENTER:
				if (TheMouse) {
					TheMouse->onCursorMovedInside();
				}
				break;

			case SDL_EVENT_WINDOW_MOUSE_LEAVE:
				if (TheMouse) {
					TheMouse->onCursorMovedOutside();
				}
				break;

			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP:
				// Fighter19 pattern: direct addSDLEvent() call
				// GeneralsX @refactor felipebraz 16/02/2026 Simplified event routing
				if (TheKeyboard) {
					SDL3Keyboard* keyboard = dynamic_cast<SDL3Keyboard*>(TheKeyboard);
					if (keyboard) {
						keyboard->addSDLEvent(&event);
					}
				}
				break;

			case SDL_EVENT_TEXT_INPUT:
				forwardTextInputEvent(event.text.text);
				break;

			case SDL_EVENT_MOUSE_MOTION:
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
			case SDL_EVENT_MOUSE_WHEEL:
				// Fighter19 pattern: direct addSDLEvent() call with raw SDL_Event
				// GeneralsX @refactor felipebraz 16/02/2026 Simplified event routing
				if (TheMouse) {
					SDL3Mouse* mouse = dynamic_cast<SDL3Mouse*>(TheMouse);
					if (mouse) {
						mouse->addSDLEvent(&event);
					}
				}
				break;

			case SDL_EVENT_WINDOW_RESIZED:
				handleWindowEvent(event.window);
				break;

#if defined(__ANDROID__)
			// GeneralsX @feature Android touch → RTS controls
			case SDL_EVENT_FINGER_DOWN:
				handleFingerDown(event.tfinger);
				break;
			case SDL_EVENT_FINGER_MOTION:
				handleFingerMotion(event.tfinger);
				break;
			case SDL_EVENT_FINGER_UP:
			case SDL_EVENT_FINGER_CANCELED:
				handleFingerUp(event.tfinger);
				break;
#endif

			default:
				// Ignore other events for now
				break;
		}

		updateTextInputState();
	}

#if defined(__ANDROID__)
	// Release due synthesized button-ups, commit a deferred classic left-down whose defer
	// window has elapsed, and arm box-select after a long press.
	touchTick();
#endif
}

// GeneralsX @bugfix felipebraz 01/04/2026 Enable SDL text input only while an entry gadget owns focus.
void SDL3GameEngine::updateTextInputState(void)
{
	if (!m_SDLWindow || !TheWindowManager) {
		return;
	}

	GameWindow* focusedWindow = TheWindowManager->winGetFocus();
	const Bool wantsTextInput =
		focusedWindow != nullptr && BitIsSet(focusedWindow->winGetStyle(), GWS_ENTRY_FIELD);

	if (wantsTextInput) {
		if (!m_IsTextInputActive) {
			if (SDL_StartTextInput(m_SDLWindow)) {
				m_IsTextInputActive = true;
			}
		}
		m_TextInputFocusWindow = focusedWindow;
	} else {
		if (m_IsTextInputActive) {
			SDL_StopTextInput(m_SDLWindow);
			m_IsTextInputActive = false;
		}
		m_TextInputFocusWindow = nullptr;
	}
}

// GeneralsX @bugfix felipebraz 01/04/2026 Forward SDL UTF-8 text input through existing GWM_IME_CHAR path.
void SDL3GameEngine::forwardTextInputEvent(const char* utf8Text)
{
	if (!utf8Text || !TheWindowManager) {
		return;
	}

	// GeneralsX @bugfix felipebraz 01/04/2026 Use tracked text-input focus window to keep SDL text delivery stable.
	GameWindow* targetWindow = m_TextInputFocusWindow;
	if (!targetWindow || !BitIsSet(targetWindow->winGetStyle(), GWS_ENTRY_FIELD)) {
		return;
	}

	const size_t textLength = strlen(utf8Text);
	size_t offset = 0;
	while (offset < textLength) {
		UnsignedInt codepoint = 0;
		if (!DecodeNextUtf8Codepoint(utf8Text, textLength, offset, codepoint)) {
			continue;
		}

		// GeneralsX @bugfix felipebraz 01/04/2026 Clamp IME char forwarding to BMP and reject UTF-16 surrogate range.
		if (codepoint == 0 || codepoint > 0x10FFFFU) {
			continue;
		}

		if (codepoint >= 0xD800U && codepoint <= 0xDFFFU) {
			continue;
		}

		if (codepoint > 0xFFFFU) {
			continue;
		}

		const WideChar wideCharacter = static_cast<WideChar>(codepoint);
		TheWindowManager->winSendInputMsg(targetWindow, GWM_IME_CHAR, static_cast<WindowMsgData>(wideCharacter), 0);
	}
}

/**
 * Handle keyboard event -dispatch to Keyboard manager
 * TheSuperHackers @build 10/02/2026 BenderAI - Phase 1.5 event wiring
 */
void SDL3GameEngine::handleKeyboardEvent(const SDL_KeyboardEvent& event)
{
	// Dispatch to SDL3Keyboard if available
	if (TheKeyboard) {
		SDL3Keyboard* sdlKeyboard = dynamic_cast<SDL3Keyboard*>(TheKeyboard);
		if (sdlKeyboard) {
			sdlKeyboard->addSDL3KeyEvent(event);
		}
	}
}

/**
 * Handle mouse motion event - dispatch to Mouse manager
 * TheSuperHackers @build 10/02/2026 BenderAI - Phase 1.5 event wiring
 */
void SDL3GameEngine::handleMouseMotionEvent(const SDL_MouseMotionEvent& event)
{
	// Dispatch to SDL3Mouse if available
	if (TheMouse) {
		SDL3Mouse* sdlMouse = dynamic_cast<SDL3Mouse*>(TheMouse);
		if (sdlMouse) {
			sdlMouse->addSDL3MouseMotionEvent(event);
		}
	}
}

/**
 * Handle mouse button event - dispatch to Mouse manager
 * TheSuperHackers @build 10/02/2026 BenderAI - Phase 1.5 event wiring
 */
void SDL3GameEngine::handleMouseButtonEvent(const SDL_MouseButtonEvent& event)
{
	// Dispatch to SDL3Mouse if available
	if (TheMouse) {
		SDL3Mouse* sdlMouse = dynamic_cast<SDL3Mouse*>(TheMouse);
		if (sdlMouse) {
			sdlMouse->addSDL3MouseButtonEvent(event);
		}
	}
}

/**
 * Handle mouse wheel event - dispatch to Mouse manager
 * TheSuperHackers @build 10/02/2026 BenderAI - Phase 1.5 event wiring
 */
void SDL3GameEngine::handleMouseWheelEvent(const SDL_MouseWheelEvent& event)
{
	// Dispatch to SDL3Mouse if available
	if (TheMouse) {
		SDL3Mouse* sdlMouse = dynamic_cast<SDL3Mouse*>(TheMouse);
		if (sdlMouse) {
			sdlMouse->addSDL3MouseWheelEvent(event);
		}
	}
}

/**
 * Handle window event (resize, etc.)
 */
void SDL3GameEngine::handleWindowEvent(const SDL_WindowEvent& event)
{
	// TODO: Phase 2 - Handle window resize, notify graphics subsystem
	// fprintf(stderr, "DEBUG: Window event (type=%d)\n", event.type);
}

/**
 * Factory Methods for GameEngine subsystems
 * TheSuperHackers @build felipebraz 13/02/2026
 * Implementations in .cpp to provide complete type definitions and avoid circular includes
 */

LocalFileSystem *SDL3GameEngine::createLocalFileSystem(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createLocalFileSystem() -> StdLocalFileSystem\n");
	return NEW StdLocalFileSystem;
}

ArchiveFileSystem *SDL3GameEngine::createArchiveFileSystem(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createArchiveFileSystem() -> StdBIGFileSystem\n");
	return NEW StdBIGFileSystem;
}

GameLogic *SDL3GameEngine::createGameLogic(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createGameLogic() -> W3DGameLogic\n");
	return NEW W3DGameLogic;
}

GameClient *SDL3GameEngine::createGameClient(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createGameClient() -> W3DGameClient\n");
	return NEW W3DGameClient;
}

ModuleFactory *SDL3GameEngine::createModuleFactory(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createModuleFactory() -> W3DModuleFactory\n");
	return NEW W3DModuleFactory;
}

ThingFactory *SDL3GameEngine::createThingFactory(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createThingFactory() -> W3DThingFactory\n");
	return NEW W3DThingFactory;
}

FunctionLexicon *SDL3GameEngine::createFunctionLexicon(void)
{
	fprintf(stderr, "INFO: SDL3GameEngine::createFunctionLexicon() -> W3DFunctionLexicon\n");
	return NEW W3DFunctionLexicon;
}

// GeneralsX @bugfix Copilot 15/04/2026 Match upstream GameEngine pure-virtual signature after sync.
Radar *SDL3GameEngine::createRadar(Bool dummy)
{
	// GeneralsX @bugfix fbraz 04/05/2026 Respect headless mode and create dummy radar.
	// Upstream reference: Win32GameEngine headless factory behavior, TheSuperHackers/GeneralsGameCode
	// https://github.com/TheSuperHackers/GeneralsGameCode
	if (dummy) {
		fprintf(stderr, "INFO: SDL3GameEngine::createRadar() -> RadarDummy (headless)\n");
		return NEW RadarDummy;
	}
	fprintf(stderr, "INFO: SDL3GameEngine::createRadar() -> W3DRadar\n");
	return NEW W3DRadar;
}

// GeneralsX @bugfix Copilot 24/03/2026 Match upstream GameEngine pure-virtual signature after sync.
ParticleSystemManager* SDL3GameEngine::createParticleSystemManager(Bool dummy)
{
	// GeneralsX @bugfix fbraz 04/05/2026 Respect headless mode and create dummy particle manager.
	if (dummy) {
		fprintf(stderr, "INFO: SDL3GameEngine::createParticleSystemManager() -> ParticleSystemManagerDummy (headless)\n");
		return NEW ParticleSystemManagerDummy;
	}
	fprintf(stderr, "INFO: SDL3GameEngine::createParticleSystemManager() -> W3DParticleSystemManager\n");
	return NEW W3DParticleSystemManager;
}

WebBrowser *SDL3GameEngine::createWebBrowser(void)
{
	// WebBrowser uses Windows COM (CComObject<W3DWebBrowser>)
	// Not available on Linux - return nullptr
	fprintf(stderr, "WARNING: WebBrowser not available on Linux platform\n");
	return nullptr;
}

/**
 * Factory method: AudioManager
 * Select audio backend based on compile flags
 * GeneralsX @bugfix Copilot 15/04/2026 Match upstream GameEngine pure-virtual signature after sync.
 */
AudioManager *SDL3GameEngine::createAudioManager(Bool dummy)
{
	(void)dummy;
	fprintf(stderr, "INFO: SDL3GameEngine::createAudioManager()\n");

#ifdef SAGE_USE_MINIAUDIO
	fprintf(stderr, "INFO: Creating MiniAudio audio backend\n");
	return new MiniAudioManager();
#elif defined(SAGE_USE_OPENAL)
	fprintf(stderr, "INFO: Creating OpenAL audio backend\n");
	return new OpenALAudioManager();
#else
	fprintf(stderr, "INFO: Audio backend not available (SAGE_USE_OPENAL/SAGE_USE_MINIAUDIO not defined)\n");
	fprintf(stderr, "WARNING: Falls back to parent implementation or silent mode\n");
	return GameEngine::createAudioManager();  // Call parent (may return stub)
#endif
}

#endif // !_WIN32

