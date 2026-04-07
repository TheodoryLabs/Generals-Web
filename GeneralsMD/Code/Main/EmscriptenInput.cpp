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
#include <cstdio>
#include <cstring>

#include "EmscriptenInput.h"

// windows_base.h provides WM_* message defines
#include "windows_base.h"

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

// c_dfDIKeyboard — referenced by DirectInputKeyboard but never meaningfully
// used on Emscripten (SetDataFormat is a no-op stub).
const void *c_dfDIKeyboard = nullptr;

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
            }
            break;
        }

        // ------------------------------------------------------------------
        // Mouse events → Win32Mouse event buffer via addWin32Event()
        // ------------------------------------------------------------------
        case SDL_MOUSEMOTION: {
            long lp = (long)((event.motion.x & 0xFFFF) |
                             ((event.motion.y & 0xFFFF) << 16));
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
                long lp = (long)((event.button.x & 0xFFFF) |
                                 ((event.button.y & 0xFFFF) << 16));
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
                long lp = (long)((event.button.x & 0xFFFF) |
                                 ((event.button.y & 0xFFFF) << 16));
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
            long lp = (long)((mx & 0xFFFF) | ((my & 0xFFFF) << 16));
            EmscriptenInput_RouteMouseEvent(WM_MOUSEWHEEL, wp, lp,
                                            SDL_GetTicks());
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
                break;
            case SDL_WINDOWEVENT_FOCUS_LOST:
                fprintf(stderr, "INFO: Window lost focus\n");
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
