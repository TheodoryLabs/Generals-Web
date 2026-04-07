/*
**  Command & Conquer Generals Zero Hour(tm)
**  Copyright 2025 Electronic Arts Inc.
**
**  GeneralsX Web Port — SDL2 → DirectInput/Win32 Input Translation
**
**  GeneralsX @feature WebPort 15/03/2026
*/

#ifndef EMSCRIPTEN_INPUT_H
#define EMSCRIPTEN_INPUT_H

#ifdef __EMSCRIPTEN__

/**
 * Pump all pending SDL2 events and route them:
 *   - Keyboard events → g_emscriptenKeyBuffer (for DirectInput stub in dinput.h)
 *   - Mouse events → TheWin32Mouse->addWin32Event() (for Win32Mouse)
 *   - Window events → handle resize, focus, etc.
 *
 * Must be called once per frame BEFORE TheGameEngine->update().
 */
void EmscriptenInput_PumpEvents();

/**
 * Route a Win32-style mouse event to TheWin32Mouse.
 * Called from EmscriptenInput_PumpEvents().
 * Implemented in EmscriptenMain.cpp (which has access to TheWin32Mouse).
 */
extern "C" void EmscriptenInput_RouteMouseEvent(unsigned int msg,
                                                 unsigned long wParam,
                                                 long lParam,
                                                 unsigned long time);

#endif // __EMSCRIPTEN__
#endif // EMSCRIPTEN_INPUT_H
