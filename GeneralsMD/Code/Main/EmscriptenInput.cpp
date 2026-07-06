/*
**  Command & Conquer Generals Zero Hour(tm)
**  Copyright 2025 Electronic Arts Inc.
**
**  GeneralsX Web Port — SDL2 → DirectInput/Win32 Input Translation
**
**  GeneralsX @feature WebPort 15/03/2026
*/

#ifdef __EMSCRIPTEN__

#include <SDL.h>
#include <cmath>  // sqrtf for two-finger pinch distance
#include <cstdio>
#include <cstring>
#include <emscripten.h>

#include "EmscriptenInput.h"

// windows_base.h provides WM_* message defines
#include "windows_base.h"

// Access to TheDisplay for logical resolution dimensions
#include "GameClient/Display.h"

// dinput.h provides EmscriptenDIKEntry, EMSCRIPTEN_DINPUT_BUFFER_SIZE,
// and the extern declarations for the key buffer globals.
#include "dinput.h"

// ============================================================================
// Keyboard ring buffer definition
// (declared extern in dinput.h, defined here)
// ============================================================================

EmscriptenDIKEntry g_emscriptenKeyBuffer[EMSCRIPTEN_DINPUT_BUFFER_SIZE];
int g_emscriptenKeyReadIdx = 0;
int g_emscriptenKeyWriteIdx = 0;
static bool g_emscriptenKeysPressed[256] = { false };

// c_dfDIKeyboard — referenced by DirectInputKeyboard but never meaningfully
// used on Emscripten (SetDataFormat is a no-op stub).
const void *c_dfDIKeyboard = nullptr;

// ============================================================================
// Cursor visibility + Pointer Lock state
//
// The system cursor on the canvas is controlled via CSS, set from JS through
// the window.GeneralsX hook in web_shell.html. The C++ side just calls down
// here whenever the engine wants to show/hide the cursor, and the JS side
// maps that to canvas.style.cursor.
//
// Pointer Lock is wired up via SDL2's relative-mouse-mode (which the
// Emscripten port of SDL2 implements by calling
// emscripten_request_pointerlock() under the hood). We engage it on
// right-mouse-button-down — the canonical RTS camera-pan gesture — and
// release on right-mouse-button-up.
//
// While the lock is held, SDL_MOUSEMOTION events carry deltas in xrel/yrel
// (since the cursor cannot physically move). We accumulate those deltas into
// a virtual position and feed THAT to Win32Mouse, so the engine still
// receives ordinary "absolute" WM_MOUSEMOVE coordinates — keeping the
// existing camera-pan delta math working unchanged.
// ============================================================================

static int ScaleMouseX(int x);
static int ScaleMouseY(int y);

static int g_lastCursorVisible = -1;        // -1 = unknown, 0 = hidden, 1 = visible
static bool g_relativeMouseActive = false;  // mirrors SDL_GetRelativeMouseMode
static int g_virtualMouseX = 0;
static int g_virtualMouseY = 0;
static int g_canvasW = 1024;
static int g_canvasH = 768;

extern "C" void EmscriptenInput_SetCanvasCursor(int visible) {
  // Cheap dedupe: don't fire EM_ASM if the state hasn't changed. Win32Mouse
  // calls SetCursor() on every frame for the active cursor, so this matters.
  const int normalized = visible ? 1 : 0;
  if (normalized == g_lastCursorVisible) return;
  g_lastCursorVisible = normalized;

  EM_ASM({
    if (typeof window !== 'undefined' && window.GeneralsX &&
        typeof window.GeneralsX.setCanvasCursor === 'function') {
      window.GeneralsX.setCanvasCursor(!!$0);
    } else {
      // Fallback: touch the canvas style directly. Safe on the default shell.
      var c = document.getElementById('canvas');
      if (c) c.style.cursor = $0 ? 'default' : 'none';
    }
  }, normalized);
}

// The pointer-lock REQUEST has to happen from within the browser's user-
// gesture stack — and SDL2-Emscripten queues input events to drain them from
// requestAnimationFrame, so by the time we see SDL_MOUSEBUTTONDOWN here we
// are no longer in that stack. The shell registers its own canvas mousedown
// listener that calls canvas.requestPointerLock() directly. From C++ we just
// listen to the resulting state change (via JS pointerlockchange handler) by
// having the JS side call this function back into us.
//
// EmscriptenInput_OnPointerLockChange is exported (EXPORTED_FUNCTIONS) so the
// shell can ccall it; we keep its signature trivially C-friendly. Note: this
// is currently a hint to the C++ side, since we use the locked state to
// decide whether to accumulate xrel/yrel into a virtual position.
extern "C" void EmscriptenInput_SetRelativeMouseMode(int enabled) {
  // Kept for source compatibility; prefer JS-side requestPointerLock(). If a
  // caller still uses this entry point (e.g., a non-shell build) we forward
  // to SDL_SetRelativeMouseMode, which on Emscripten attempts pointer lock.
  // The browser may decline; we don't treat that as fatal.
  const bool want = enabled != 0;
  if (want == g_relativeMouseActive) return;
  if (SDL_SetRelativeMouseMode(want ? SDL_TRUE : SDL_FALSE) != 0) {
    fprintf(stderr, "WARN: SDL_SetRelativeMouseMode(%d) failed: %s "
                    "(harmless if the JS shell handles pointer lock)\n",
            (int)want, SDL_GetError());
  }
  // Don't gate g_relativeMouseActive on this — JS-side pointerlockchange is
  // the authoritative source. EmscriptenInput_OnPointerLockChange flips the
  // flag based on the actual document.pointerLockElement state.
}

extern "C" EMSCRIPTEN_KEEPALIVE
void EmscriptenInput_OnPointerLockChange(int locked) {
  const bool want = locked != 0;
  if (want == g_relativeMouseActive) return;
  g_relativeMouseActive = want;
  if (want) {
    // Snapshot the current cursor position so the engine sees a continuous
    // coordinate stream when we cross from absolute → delta mode.
    int sx = 0, sy = 0;
    SDL_GetMouseState(&sx, &sy);
    // Scale sx and sy from logical SDL window dimensions to canvas attributes space
    g_virtualMouseX = ScaleMouseX(sx);
    g_virtualMouseY = ScaleMouseY(sy);
  }
  fprintf(stderr, "INFO: pointer lock %s\n", want ? "engaged" : "released");
}

extern "C" void EmscriptenInput_UpdateCanvasSize(int w, int h) {
  if (w > 0) g_canvasW = w;
  if (h > 0) g_canvasH = h;
}

// ============================================================================
// Touch input — multi-finger gestures translated to Win32 mouse messages
//
// The whole input stack downstream of this file is mouse-shaped, so we don't
// add a parallel touch path; instead we synthesise mouse messages from SDL
// finger events. Gestures supported:
//
//   1 finger  — pass-through tap/drag. SDL's built-in
//               SDL_HINT_TOUCH_MOUSE_EVENTS handles this for us, so we just
//               watch the finger count and don't synthesise our own events.
//   2 fingers — right-button drag for camera pan. We cancel the synthetic
//               LMB-down (if any) the moment the 2nd finger lands, then emit a
//               WM_RBUTTONDOWN at the two-finger centroid; finger motion ticks
//               WM_MOUSEMOVE at the new centroid, and the pinch distance
//               delta drives WM_MOUSEWHEEL. On lift, WM_RBUTTONUP and we
//               return to single-finger mode.
//
// While in "2-finger mode" we set g_suppressSyntheticMouse so the
// SDL_MOUSEBUTTON*/SDL_MOUSEMOTION handlers below ignore the synthetic events
// SDL is still firing for the underlying touch ids — otherwise the engine
// would see both the right-button drag (from us) and a stray left-button
// down/up (from SDL's compat path).
// ============================================================================
static int g_activeFingerCount = 0;
static bool g_twoFingerMode = false;
static bool g_suppressSyntheticMouse = false;
static int g_twoFingerLastCentroidX = 0;
static int g_twoFingerLastCentroidY = 0;
static float g_twoFingerLastPinch = 0.0f;
static bool g_lmbDownFromSyntheticTouch = false;

static int ScaleMouseX(int x) {
  int targetW = 800;
  if (TheDisplay != nullptr) {
    targetW = TheDisplay->getWidth();
  }
  if (g_relativeMouseActive) {
    if (x < 0) return 0;
    if (x > targetW - 1) return targetW - 1;
    return x;
  }
  int sdlW = g_canvasW;
  extern SDL_Window *TheSDL3Window;
  if (TheSDL3Window) {
    int sdlH = 0;
    SDL_GetWindowSize(TheSDL3Window, &sdlW, &sdlH);
  }
  if (sdlW > 0) {
    x = (int)std::round((double)x * targetW / sdlW);
  }
  if (x < 0) return 0;
  if (x > targetW - 1) return targetW - 1;
  return x;
}

static int ScaleMouseY(int y) {
  int targetH = 600;
  if (TheDisplay != nullptr) {
    targetH = TheDisplay->getHeight();
  }
  if (g_relativeMouseActive) {
    if (y < 0) return 0;
    if (y > targetH - 1) return targetH - 1;
    return y;
  }
  int sdlH = g_canvasH;
  extern SDL_Window *TheSDL3Window;
  if (TheSDL3Window) {
    int sdlW = 0;
    SDL_GetWindowSize(TheSDL3Window, &sdlW, &sdlH);
  }
  if (sdlH > 0) {
    y = (int)std::round((double)y * targetH / sdlH);
  }
  if (y < 0) return 0;
  if (y > targetH - 1) return targetH - 1;
  return y;
}

extern "C" int EmscriptenInput_GetVirtualMouseX() {
  return g_virtualMouseX;
}

extern "C" int EmscriptenInput_GetVirtualMouseY() {
  return g_virtualMouseY;
}

static int Touch_NormalizedToPixelX(float nx) {
  if (nx < 0.f) nx = 0.f;
  if (nx > 1.f) nx = 1.f;
  return (int)(nx * (float)(g_canvasW - 1));
}

static int Touch_NormalizedToPixelY(float ny) {
  if (ny < 0.f) ny = 0.f;
  if (ny > 1.f) ny = 1.f;
  return (int)(ny * (float)(g_canvasH - 1));
}

static void Touch_QueryCentroidAndPinch(int *cxOut, int *cyOut,
                                         float *pinchOut) {
  // SDL_GetNumTouchDevices() can be > 1 on some hardware; we walk all of them
  // and pick the first device with active fingers. In practice on the web
  // there is exactly one touch device (the canvas).
  int numDevices = SDL_GetNumTouchDevices();
  for (int d = 0; d < numDevices; ++d) {
    SDL_TouchID tid = SDL_GetTouchDevice(d);
    int n = SDL_GetNumTouchFingers(tid);
    if (n <= 0) continue;
    float sx = 0.f, sy = 0.f;
    int counted = 0;
    SDL_Finger *f0 = nullptr, *f1 = nullptr;
    for (int i = 0; i < n && counted < 2; ++i) {
      SDL_Finger *f = SDL_GetTouchFinger(tid, i);
      if (!f) continue;
      sx += f->x;
      sy += f->y;
      if (counted == 0) f0 = f;
      else if (counted == 1) f1 = f;
      ++counted;
    }
    if (counted == 0) continue;
    float cx = sx / (float)counted;
    float cy = sy / (float)counted;
    *cxOut = Touch_NormalizedToPixelX(cx);
    *cyOut = Touch_NormalizedToPixelY(cy);
    if (counted >= 2 && f0 && f1) {
      float dx = (f1->x - f0->x) * (float)g_canvasW;
      float dy = (f1->y - f0->y) * (float)g_canvasH;
      *pinchOut = sqrtf(dx * dx + dy * dy);
    } else {
      *pinchOut = 0.f;
    }
    return;
  }
  *cxOut = g_virtualMouseX;
  *cyOut = g_virtualMouseY;
  *pinchOut = 0.f;
}

static long Touch_PackXY(int x, int y) {
  int sx = ScaleMouseX(x);
  int sy = ScaleMouseY(y);
  return (long)((sx & 0xFFFF) | ((sy & 0xFFFF) << 16));
}

static void Touch_BeginTwoFingerMode() {
  // Cancel any synthetic LMB drag that the first-finger touch initiated.
  // SDL_HINT_TOUCH_MOUSE_EVENTS=1 means SDL fired a WM_LBUTTONDOWN under us;
  // emit the matching up so the engine doesn't see a stuck button.
  if (g_lmbDownFromSyntheticTouch) {
    long lp = Touch_PackXY(g_virtualMouseX, g_virtualMouseY);
    EmscriptenInput_RouteMouseEvent(WM_LBUTTONUP, 0, lp, SDL_GetTicks());
    g_lmbDownFromSyntheticTouch = false;
  }
  int cx, cy;
  float pinch;
  Touch_QueryCentroidAndPinch(&cx, &cy, &pinch);
  g_twoFingerLastCentroidX = cx;
  g_twoFingerLastCentroidY = cy;
  g_twoFingerLastPinch = pinch;
  g_virtualMouseX = cx;
  g_virtualMouseY = cy;
  // Move first, then press — the engine binds the down-event to the cursor's
  // current position, so we want it on the centroid not the last LMB pos.
  long lp = Touch_PackXY(cx, cy);
  EmscriptenInput_RouteMouseEvent(WM_MOUSEMOVE, 0, lp, SDL_GetTicks());
  EmscriptenInput_RouteMouseEvent(WM_RBUTTONDOWN, 0, lp, SDL_GetTicks());
  g_twoFingerMode = true;
  g_suppressSyntheticMouse = true;
  fprintf(stderr, "INFO: touch — 2-finger mode begin at %d,%d\n", cx, cy);
}

static void Touch_EndTwoFingerMode() {
  if (!g_twoFingerMode) return;
  long lp = Touch_PackXY(g_twoFingerLastCentroidX, g_twoFingerLastCentroidY);
  EmscriptenInput_RouteMouseEvent(WM_RBUTTONUP, 0, lp, SDL_GetTicks());
  g_twoFingerMode = false;
  g_twoFingerLastPinch = 0.0f;
  // Keep suppressing synthetic mouse for one more event tick so a stale
  // LBUTTONUP from the lifted finger doesn't reach the engine. The next
  // FINGERDOWN re-evaluates.
  g_suppressSyntheticMouse = (g_activeFingerCount > 0);
  fprintf(stderr, "INFO: touch — 2-finger mode end\n");
}

// Pinch step in pixels — distance change required to emit one wheel notch.
// 30px ≈ a comfortable pinch on a phone, ≈ a finger-width on a tablet.
static const float kPinchStepPx = 30.0f;

// ============================================================================
// SDL2 Scancode → DirectInput DIK_* Translation Table
//
// SDL scancodes are USB HID scancodes. DirectInput uses its own scan codes
// which happen to match IBM PC AT keyboard scan codes (Set 1).
// ============================================================================

static unsigned char sdlScancodeToDIK(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_ESCAPE:    return 0x01; // DIK_ESCAPE
    case SDL_SCANCODE_1:         return 0x02;
    case SDL_SCANCODE_2:         return 0x03;
    case SDL_SCANCODE_3:         return 0x04;
    case SDL_SCANCODE_4:         return 0x05;
    case SDL_SCANCODE_5:         return 0x06;
    case SDL_SCANCODE_6:         return 0x07;
    case SDL_SCANCODE_7:         return 0x08;
    case SDL_SCANCODE_8:         return 0x09;
    case SDL_SCANCODE_9:         return 0x0A;
    case SDL_SCANCODE_0:         return 0x0B;
    case SDL_SCANCODE_MINUS:     return 0x0C;
    case SDL_SCANCODE_EQUALS:    return 0x0D;
    case SDL_SCANCODE_BACKSPACE: return 0x0E;
    case SDL_SCANCODE_TAB:       return 0x0F;
    case SDL_SCANCODE_Q:         return 0x10;
    case SDL_SCANCODE_W:         return 0x11;
    case SDL_SCANCODE_E:         return 0x12;
    case SDL_SCANCODE_R:         return 0x13;
    case SDL_SCANCODE_T:         return 0x14;
    case SDL_SCANCODE_Y:         return 0x15;
    case SDL_SCANCODE_U:         return 0x16;
    case SDL_SCANCODE_I:         return 0x17;
    case SDL_SCANCODE_O:         return 0x18;
    case SDL_SCANCODE_P:         return 0x19;
    case SDL_SCANCODE_LEFTBRACKET:  return 0x1A;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
    case SDL_SCANCODE_RETURN:    return 0x1C;
    case SDL_SCANCODE_LCTRL:     return 0x1D;
    case SDL_SCANCODE_A:         return 0x1E;
    case SDL_SCANCODE_S:         return 0x1F;
    case SDL_SCANCODE_D:         return 0x20;
    case SDL_SCANCODE_F:         return 0x21;
    case SDL_SCANCODE_G:         return 0x22;
    case SDL_SCANCODE_H:         return 0x23;
    case SDL_SCANCODE_J:         return 0x24;
    case SDL_SCANCODE_K:         return 0x25;
    case SDL_SCANCODE_L:         return 0x26;
    case SDL_SCANCODE_SEMICOLON: return 0x27;
    case SDL_SCANCODE_APOSTROPHE: return 0x28;
    case SDL_SCANCODE_GRAVE:     return 0x29;
    case SDL_SCANCODE_LSHIFT:    return 0x2A;
    case SDL_SCANCODE_BACKSLASH: return 0x2B;
    case SDL_SCANCODE_Z:         return 0x2C;
    case SDL_SCANCODE_X:         return 0x2D;
    case SDL_SCANCODE_C:         return 0x2E;
    case SDL_SCANCODE_V:         return 0x2F;
    case SDL_SCANCODE_B:         return 0x30;
    case SDL_SCANCODE_N:         return 0x31;
    case SDL_SCANCODE_M:         return 0x32;
    case SDL_SCANCODE_COMMA:     return 0x33;
    case SDL_SCANCODE_PERIOD:    return 0x34;
    case SDL_SCANCODE_SLASH:     return 0x35;
    case SDL_SCANCODE_RSHIFT:    return 0x36;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x37;
    case SDL_SCANCODE_LALT:      return 0x38;
    case SDL_SCANCODE_SPACE:     return 0x39;
    case SDL_SCANCODE_CAPSLOCK:  return 0x3A;
    case SDL_SCANCODE_F1:        return 0x3B;
    case SDL_SCANCODE_F2:        return 0x3C;
    case SDL_SCANCODE_F3:        return 0x3D;
    case SDL_SCANCODE_F4:        return 0x3E;
    case SDL_SCANCODE_F5:        return 0x3F;
    case SDL_SCANCODE_F6:        return 0x40;
    case SDL_SCANCODE_F7:        return 0x41;
    case SDL_SCANCODE_F8:        return 0x42;
    case SDL_SCANCODE_F9:        return 0x43;
    case SDL_SCANCODE_F10:       return 0x44;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45;
    case SDL_SCANCODE_SCROLLLOCK: return 0x46;
    case SDL_SCANCODE_KP_7:      return 0x47;
    case SDL_SCANCODE_KP_8:      return 0x48;
    case SDL_SCANCODE_KP_9:      return 0x49;
    case SDL_SCANCODE_KP_MINUS:  return 0x4A;
    case SDL_SCANCODE_KP_4:      return 0x4B;
    case SDL_SCANCODE_KP_5:      return 0x4C;
    case SDL_SCANCODE_KP_6:      return 0x4D;
    case SDL_SCANCODE_KP_PLUS:   return 0x4E;
    case SDL_SCANCODE_KP_1:      return 0x4F;
    case SDL_SCANCODE_KP_2:      return 0x50;
    case SDL_SCANCODE_KP_3:      return 0x51;
    case SDL_SCANCODE_KP_0:      return 0x52;
    case SDL_SCANCODE_KP_PERIOD: return 0x53;
    case SDL_SCANCODE_NONUSBACKSLASH: return 0x56;
    case SDL_SCANCODE_F11:       return 0x57;
    case SDL_SCANCODE_F12:       return 0x58;
    case SDL_SCANCODE_KP_ENTER:  return 0x9C;
    case SDL_SCANCODE_RCTRL:     return 0x9D;
    case SDL_SCANCODE_KP_DIVIDE: return 0xB5;
    case SDL_SCANCODE_PRINTSCREEN: return 0xB7;
    case SDL_SCANCODE_RALT:      return 0xB8;
    case SDL_SCANCODE_HOME:      return 0xC7;
    case SDL_SCANCODE_UP:        return 0xC8;
    case SDL_SCANCODE_PAGEUP:    return 0xC9;
    case SDL_SCANCODE_LEFT:      return 0xCB;
    case SDL_SCANCODE_RIGHT:     return 0xCD;
    case SDL_SCANCODE_END:       return 0xCF;
    case SDL_SCANCODE_DOWN:      return 0xD0;
    case SDL_SCANCODE_PAGEDOWN:  return 0xD1;
    case SDL_SCANCODE_INSERT:    return 0xD2;
    case SDL_SCANCODE_DELETE:    return 0xD3;
    default:                     return 0x00; // KEY_NONE
    }
}

// ============================================================================
// SDL2 event pump — called once per frame from GameWebFrameCallback
// ============================================================================

// WM_MOUSEWHEEL (0x020A) is defined in windows_base.h

void EmscriptenInput_PumpEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {

        // ------------------------------------------------------------------
        // Keyboard events → DirectInput ring buffer
        // ------------------------------------------------------------------
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            // Skip auto-repeat for key-down (DirectInput doesn't generate
            // repeats; the Keyboard base class handles them internally)
            if (event.type == SDL_KEYDOWN && event.key.repeat)
                break;

            unsigned char dik = sdlScancodeToDIK(event.key.keysym.scancode);
            if (dik != 0x00) {
                int idx = g_emscriptenKeyWriteIdx &
                          (EMSCRIPTEN_DINPUT_BUFFER_SIZE - 1);
                g_emscriptenKeyBuffer[idx].dwOfs = dik;
                g_emscriptenKeyBuffer[idx].dwData =
                    (event.type == SDL_KEYDOWN) ? 0x80 : 0x00;
                g_emscriptenKeyBuffer[idx].dwTimeStamp = SDL_GetTicks();
                g_emscriptenKeyWriteIdx++;
                g_emscriptenKeysPressed[dik] = (event.type == SDL_KEYDOWN);
            }
            break;
        }

        // ------------------------------------------------------------------
        // Mouse events → Win32Mouse event buffer via addWin32Event()
        //
        // We track a "virtual" mouse position that mirrors the canvas cursor
        // when not pointer-locked, and accumulates xrel/yrel deltas while
        // locked. The engine sees regular absolute WM_MOUSEMOVE coords either
        // way, so its camera-pan delta math works unchanged.
        // ------------------------------------------------------------------
        case SDL_MOUSEMOTION: {
            // Skip synthetic mouse motion that SDL generates from the first
            // touch finger while we're driving a 2-finger gesture ourselves.
            if (g_suppressSyntheticMouse &&
                event.motion.which == (Uint32)SDL_TOUCH_MOUSEID) {
                break;
            }
            int x, y;
            if (g_relativeMouseActive) {
                g_virtualMouseX += event.motion.xrel;
                g_virtualMouseY += event.motion.yrel;
                if (g_virtualMouseX < 0) g_virtualMouseX = 0;
                if (g_virtualMouseY < 0) g_virtualMouseY = 0;
                int maxW = g_canvasW > 0 ? (g_canvasW - 1) : 799;
                int maxH = g_canvasH > 0 ? (g_canvasH - 1) : 599;
                if (g_virtualMouseX > maxW) g_virtualMouseX = maxW;
                if (g_virtualMouseY > maxH) g_virtualMouseY = maxH;
                x = g_virtualMouseX;
                y = g_virtualMouseY;
            } else {
                x = event.motion.x;
                y = event.motion.y;
                g_virtualMouseX = x;
                g_virtualMouseY = y;
            }
            int sx = ScaleMouseX(x);
            int sy = ScaleMouseY(y);
            long lp = (long)((sx & 0xFFFF) | ((sy & 0xFFFF) << 16));
            EmscriptenInput_RouteMouseEvent(WM_MOUSEMOVE, 0, lp,
                                            SDL_GetTicks());
            break;
        }

        case SDL_MOUSEBUTTONDOWN: {
            unsigned int msg = 0;
            switch (event.button.button) {
            case SDL_BUTTON_LEFT:   msg = WM_LBUTTONDOWN; break;
            case SDL_BUTTON_RIGHT:  msg = WM_RBUTTONDOWN; break;
            case SDL_BUTTON_MIDDLE: msg = WM_MBUTTONDOWN; break;
            }
            if (msg) {
                // Synthetic mouse from a touch is ignored while we drive a
                // 2-finger gesture ourselves; track LMB-from-touch separately
                // so Touch_BeginTwoFingerMode can synthesise the matching up.
                const bool isSyntheticTouch =
                    event.button.which == (Uint32)SDL_TOUCH_MOUSEID;
                if (isSyntheticTouch && msg == WM_LBUTTONDOWN) {
                    g_lmbDownFromSyntheticTouch = true;
                }
                if (g_suppressSyntheticMouse && isSyntheticTouch) break;
                // The pointer-lock request itself happens in the shell's
                // canvas.mousedown listener (which is in the user-gesture
                // stack). The resulting pointerlockchange callback flips
                // g_relativeMouseActive via EmscriptenInput_OnPointerLockChange.
                int x = event.button.x;
                int y = event.button.y;
                if (g_relativeMouseActive) {
                    x = g_virtualMouseX;
                    y = g_virtualMouseY;
                }
                int sx = ScaleMouseX(x);
                int sy = ScaleMouseY(y);
                long lp = (long)((sx & 0xFFFF) | ((sy & 0xFFFF) << 16));
                EmscriptenInput_RouteMouseEvent(msg, 0, lp, SDL_GetTicks());
            }
            break;
        }

        case SDL_MOUSEBUTTONUP: {
            unsigned int msg = 0;
            switch (event.button.button) {
            case SDL_BUTTON_LEFT:   msg = WM_LBUTTONUP; break;
            case SDL_BUTTON_RIGHT:  msg = WM_RBUTTONUP; break;
            case SDL_BUTTON_MIDDLE: msg = WM_MBUTTONUP; break;
            }
            if (msg) {
                const bool isSyntheticTouch =
                    event.button.which == (Uint32)SDL_TOUCH_MOUSEID;
                if (isSyntheticTouch && msg == WM_LBUTTONUP) {
                    g_lmbDownFromSyntheticTouch = false;
                }
                if (g_suppressSyntheticMouse && isSyntheticTouch) break;
                // While pointer-locked, SDL's event.button.x/y is whatever
                // the browser captured at lock entry — not meaningful. Use
                // the accumulated virtual coord so the up-event lands at the
                // same place the move events were tracking.
                int x = event.button.x;
                int y = event.button.y;
                if (g_relativeMouseActive) {
                    x = g_virtualMouseX;
                    y = g_virtualMouseY;
                }
                int sx = ScaleMouseX(x);
                int sy = ScaleMouseY(y);
                long lp = (long)((sx & 0xFFFF) | ((sy & 0xFFFF) << 16));
                EmscriptenInput_RouteMouseEvent(msg, 0, lp, SDL_GetTicks());
            }
            break;
        }

        case SDL_MOUSEWHEEL: {
            // WM_MOUSEWHEEL: wParam high word = delta (WHEEL_DELTA = 120)
            int delta = event.wheel.y * 120;
            unsigned long wp = (unsigned long)((delta & 0xFFFF) << 16);
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            int smx = ScaleMouseX(mx);
            int smy = ScaleMouseY(my);
            long lp = (long)((smx & 0xFFFF) | ((smy & 0xFFFF) << 16));
            EmscriptenInput_RouteMouseEvent(WM_MOUSEWHEEL, wp, lp,
                                            SDL_GetTicks());
            break;
        }

        // ------------------------------------------------------------------
        // Touch events — see comment block at top of file. Single finger is
        // pass-through (SDL synthesises mouse events for it). Two fingers
        // engage RMB-drag camera pan + pinch-to-wheel zoom. We rely on
        // SDL_GetNumTouchFingers / SDL_GetTouchFinger to compute centroids
        // rather than tracking ids ourselves — fewer corner cases on lift.
        // ------------------------------------------------------------------
        case SDL_FINGERDOWN: {
            ++g_activeFingerCount;
            if (g_activeFingerCount == 2 && !g_twoFingerMode) {
                Touch_BeginTwoFingerMode();
            } else if (g_activeFingerCount > 2 && g_twoFingerMode) {
                // 3rd finger lands while 2-finger gesture is active. Ignore;
                // we keep the 2-finger drag going. Re-centroid would jump.
            } else if (g_activeFingerCount == 1) {
                // First finger — let SDL's synthetic mouse path handle it.
                g_suppressSyntheticMouse = false;
            }
            break;
        }

        case SDL_FINGERMOTION: {
            if (g_twoFingerMode) {
                int cx, cy;
                float pinch;
                Touch_QueryCentroidAndPinch(&cx, &cy, &pinch);
                if (cx != g_twoFingerLastCentroidX ||
                    cy != g_twoFingerLastCentroidY) {
                    long lp = Touch_PackXY(cx, cy);
                    EmscriptenInput_RouteMouseEvent(WM_MOUSEMOVE, 0, lp,
                                                    SDL_GetTicks());
                    g_twoFingerLastCentroidX = cx;
                    g_twoFingerLastCentroidY = cy;
                    g_virtualMouseX = cx;
                    g_virtualMouseY = cy;
                }
                // Pinch → wheel notches. Accumulator-style: each kPinchStepPx
                // of distance change emits one notch and resets the baseline.
                if (g_twoFingerLastPinch > 0.0f && pinch > 0.0f) {
                    float diff = pinch - g_twoFingerLastPinch;
                    if (diff >= kPinchStepPx || diff <= -kPinchStepPx) {
                        int notches = (int)(diff / kPinchStepPx);
                        if (notches != 0) {
                            int delta = notches * 120;
                            unsigned long wp =
                                (unsigned long)((delta & 0xFFFF) << 16);
                            long lp = Touch_PackXY(cx, cy);
                            EmscriptenInput_RouteMouseEvent(
                                WM_MOUSEWHEEL, wp, lp, SDL_GetTicks());
                            g_twoFingerLastPinch +=
                                (float)notches * kPinchStepPx;
                        }
                    }
                } else if (pinch > 0.0f) {
                    g_twoFingerLastPinch = pinch;
                }
            }
            // 1-finger motion: SDL emits synthetic SDL_MOUSEMOTION; nothing
            // to do here.
            break;
        }

        case SDL_FINGERUP: {
            if (g_activeFingerCount > 0) --g_activeFingerCount;
            if (g_twoFingerMode && g_activeFingerCount < 2) {
                Touch_EndTwoFingerMode();
            }
            if (g_activeFingerCount == 0) {
                // All fingers off — re-enable synthetic mouse path so the
                // next touch's tap-translation reaches the engine.
                g_suppressSyntheticMouse = false;
                g_lmbDownFromSyntheticTouch = false;
                g_twoFingerLastPinch = 0.0f;
            }
            break;
        }

        // ------------------------------------------------------------------
        // Window events
        // ------------------------------------------------------------------
        case SDL_WINDOWEVENT:
            switch (event.window.event) {
            case SDL_WINDOWEVENT_RESIZED:
                fprintf(stderr, "INFO: Window resized to %dx%d\n",
                        event.window.data1, event.window.data2);
                // Track canvas dimensions so the virtual-mouse clamp stays
                // accurate during a Pointer Lock drag.
                g_canvasW = event.window.data1 > 0 ? event.window.data1 : 1024;
                g_canvasH = event.window.data2 > 0 ? event.window.data2 : 768;
                break;
            case SDL_WINDOWEVENT_FOCUS_LOST:
                fprintf(stderr, "INFO: Window lost focus - releasing keys\n");
                // Release all currently pressed keys to avoid stuck keys
                for (int dik = 0; dik < 256; ++dik) {
                    if (g_emscriptenKeysPressed[dik]) {
                        int idx = g_emscriptenKeyWriteIdx &
                                  (EMSCRIPTEN_DINPUT_BUFFER_SIZE - 1);
                        g_emscriptenKeyBuffer[idx].dwOfs = dik;
                        g_emscriptenKeyBuffer[idx].dwData = 0x00;
                        g_emscriptenKeyBuffer[idx].dwTimeStamp = SDL_GetTicks();
                        g_emscriptenKeyWriteIdx++;
                        g_emscriptenKeysPressed[dik] = false;
                    }
                }
                // Drop pointer lock proactively if focus moves out of the
                // canvas (the browser would do this for us, but our state
                // mirror would otherwise be stale).
                if (g_relativeMouseActive) {
                    EmscriptenInput_SetRelativeMouseMode(0);
                }
                break;
            case SDL_WINDOWEVENT_FOCUS_GAINED:
                fprintf(stderr, "INFO: Window gained focus\n");
                break;
            }
            break;

        case SDL_QUIT:
            fprintf(stderr, "INFO: SDL_QUIT received\n");
            break;
        }
    }
}

#endif // __EMSCRIPTEN__
