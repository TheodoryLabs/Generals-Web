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

/**
 * Set the canvas cursor visibility. Called from the SetCursor() stub in
 * windows_base.h whenever the engine asks Windows to show/hide the cursor.
 *
 *   visible == 0 → canvas style.cursor = 'none' (engine draws its own cursor)
 *   visible != 0 → canvas style.cursor = 'default'
 *
 * Implemented in EmscriptenInput.cpp via window.GeneralsX.setCanvasCursor.
 */
extern "C" void EmscriptenInput_SetCanvasCursor(int visible);

/**
 * Toggle SDL2 relative-mouse mode (which the Emscripten port implements via
 * the Pointer Lock API). When the lock is granted the system cursor is hidden
 * and the cursor cannot leave the canvas — instead each SDL_MOUSEMOTION event
 * carries `xrel` / `yrel` deltas that we accumulate into the virtual position
 * we feed to Win32Mouse. We use this for right-click camera pan so the user
 * can drag forever without hitting a screen edge.
 */
extern "C" void EmscriptenInput_SetRelativeMouseMode(int enabled);

/**
 * Update the cached canvas dimensions used by touch-input centroid math.
 * Called from GX_OnCanvasResize so two-finger pan maps SDL's normalized
 * (0..1) finger coords back to canvas pixels accurately.
 */
extern "C" void EmscriptenInput_UpdateCanvasSize(int w, int h);

#endif // __EMSCRIPTEN__
#endif // EMSCRIPTEN_INPUT_H
