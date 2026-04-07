/*
** GeneralsX Web Port — Main Loop Adapter for Emscripten
**
** PROBLEM:
** Native game engines use a blocking game loop:
**     while (running) {
**         ProcessInput();
**         UpdateGame();
**         Render();
**     }
**
** Browsers CANNOT do this — a blocking loop freezes the entire tab.
** The browser's event loop must run between frames to handle input,
** network, and rendering.
**
** SOLUTION:
** Emscripten provides emscripten_set_main_loop() which calls a function
** once per frame (synced to requestAnimationFrame). We split the game's
** main loop body into a single-frame callback.
**
** ARCHITECTURE:
**     Browser event loop
**         → requestAnimationFrame
**             → emscripten_main_loop_callback()
**                 → GameEngine::ProcessInput()
**                 → GameEngine::Update()
**                 → GameEngine::Render()
**             → returns to browser
**         → handles DOM events, network I/O, etc.
**         → next frame...
**
** INTEGRATION WITH GENERALSX:
** GeneralsX's main() in SDL3Main.cpp currently has a traditional game loop.
** This adapter wraps it: the game's per-frame logic is extracted into a
** callback function, and main() calls our Web_Main_Loop_Start() instead
** of entering a while loop.
**
** TIME MANAGEMENT:
** DX8/Windows games often use GetTickCount() or QueryPerformanceCounter.
** Emscripten maps these to performance.now() automatically. However, we
** provide additional timing utilities for frame rate control and delta
** time calculation.
**
** LICENSE: GPL-3.0
*/

#pragma once

#if defined(__EMSCRIPTEN__)

#include <emscripten.h>

// ============================================================================
// Frame callback type
// The game provides this function — it runs one frame of the game loop.
// Return true to keep running, false to request shutdown.
// ============================================================================
typedef bool (*WebFrameCallback)(float delta_time_ms);

// ============================================================================
// Main Loop Control
// ============================================================================
namespace WebMainLoop {

    // Start the Emscripten main loop
    // frame_cb: function to call each frame (must be non-blocking!)
    // target_fps: 0 = match display refresh rate (recommended)
    //             60 = cap at 60fps, etc.
    // simulate_infinite_loop: if true, main() never returns (normal for games)
    void Start(WebFrameCallback frame_cb, int target_fps = 0,
               bool simulate_infinite_loop = true);

    // Request a clean shutdown (game exit)
    // The next frame callback won't be called; cleanup runs instead.
    void Request_Shutdown();

    // Check if shutdown was requested
    bool Is_Shutdown_Requested();

    // Get time since the main loop started (milliseconds)
    double Get_Elapsed_Time_Ms();

    // Get the last frame's delta time (milliseconds)
    float Get_Delta_Time_Ms();

    // Get the current FPS (smoothed over ~0.5 seconds)
    float Get_FPS();

    // Get total frame count since loop start
    unsigned int Get_Frame_Count();

    // Pause/resume the main loop (e.g., when tab is hidden)
    void Pause();
    void Resume();
    bool Is_Paused();

    // Set a loading progress callback (called during async operations)
    typedef void (*LoadingProgressCallback)(float progress, const char* message);
    void Set_Loading_Callback(LoadingProgressCallback cb);

    // Run a loading phase before the game loop starts
    // This shows a loading screen while async operations complete.
    // load_func: called once to start async loading
    // ready_func: called each frame; return true when loading is done
    typedef void (*LoadStartCallback)();
    typedef bool (*LoadReadyCallback)();
    void Run_Loading_Phase(LoadStartCallback load_func,
                            LoadReadyCallback ready_func);
}

// ============================================================================
// Visibility Change Handling
// Automatically pauses the game when the browser tab is hidden.
// ============================================================================
namespace WebVisibility {
    // Register callbacks for tab visibility changes
    typedef void (*VisibilityCallback)(bool is_visible);
    void Set_Callback(VisibilityCallback cb);

    // Initialize visibility monitoring (call once at startup)
    void Init();
}

// ============================================================================
// Performance Timing Utilities
// ============================================================================
namespace WebTiming {
    // High-resolution timestamp (milliseconds, from performance.now())
    double Now_Ms();

    // Sleep-like yield (returns control to browser for N ms)
    // WARNING: This is NOT a real sleep — it yields the main thread.
    // Only use in async contexts (e.g., loading screens).
    void Yield_Ms(int ms);
}

#endif // __EMSCRIPTEN__
