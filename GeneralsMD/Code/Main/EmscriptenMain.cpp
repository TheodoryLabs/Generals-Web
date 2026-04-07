/*
**  Command & Conquer Generals Zero Hour(tm)
**  Copyright 2025 Electronic Arts Inc.
**
**  This program is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  GeneralsX Web Port — Emscripten Entry Point
*/

#if defined(__EMSCRIPTEN__)

// SYSTEM INCLUDES
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <emscripten.h>
#include <emscripten/html5.h>

// USER INCLUDES
#include "Common/CommandLine.h"
#include "Common/CriticalSection.h"
#include "Common/Debug.h"
#include "Common/FramePacer.h"
#include "Common/GameEngine.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/version.h"
#include "Lib/BaseType.h"

// Web port includes
#include "gles3_big_vfs.h"
#include "gles3_mainloop.h"
#include "gles3_wrapper.h"

// SDL input translation
#include "EmscriptenInput.h"

// Win32GameEngine
#include "Win32Device/Common/Win32GameEngine.h"

// Win32Mouse
#include "Win32Device/GameClient/Win32Mouse.h"

// CRITICAL SECTIONS
static CriticalSection critSec1;
static CriticalSection critSec2;
static CriticalSection critSec3;
static CriticalSection critSec4;
static CriticalSection critSec5;

// GLOBAL COMMAND LINE ARGUMENTS
int __argc = 0;
char **__argv = nullptr;

// GLOBAL WINDOW HANDLE
HWND ApplicationHWnd = nullptr;

// GLOBAL SDL3 WINDOW
SDL_Window *TheSDL3Window = nullptr;

// GAME TEXT FILE PATHS
const Char *g_csfFile = "data/%s/generals.csf";
const Char *g_strFile = "data/Generals.str";

extern bool GLES3_Init(void *hwnd, bool lite);
extern Win32Mouse *TheWin32Mouse;

GameEngine *CreateGameEngine(void) {
  fprintf(stderr, "INFO: CreateGameEngine() — Win32GameEngine for WebGL 2.0\n");
  return NEW Win32GameEngine();
}

extern "C" void EmscriptenInput_RouteMouseEvent(unsigned int msg,
                                                 unsigned long wParam,
                                                 long lParam,
                                                 unsigned long time) {
  if (TheWin32Mouse) {
    TheWin32Mouse->addWin32Event((UINT)msg, (WPARAM)wParam, (LPARAM)lParam,
                                  (DWORD)time);
  }
}

// Global state tracking for init phase
static bool g_GameEngineInitialized = false;

static bool GameWebFrameCallback(float /*delta_time_ms*/) {
  EmscriptenInput_PumpEvents();

  if (!g_GameEngineInitialized) {
    // Force an initial clear to black so the first frame renders IMMEDIATELY.
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // SDL_GL_SwapWindow(TheSDL3Window); // Emscripten does this automatically!
    return true; // Keep looping.
  }

  if (!TheGameEngine) {
    return false;
  }

  if (TheGameEngine->getQuitting()) {
    fprintf(stderr, "INFO: GameEngine requested quit — stopping main loop\n");
    delete TheFramePacer;
    TheFramePacer = nullptr;
    delete TheGameEngine;
    TheGameEngine = nullptr;
    return false; 
  }

  // Temporary debug log
  static int frame_tick = 0;
  if (frame_tick % 60 == 0) {
      fprintf(stderr, "INFO: GameWebFrameCallback ticking... %d\n", frame_tick);
  }
  frame_tick++;

  TheGameEngine->update();

  if (TheFramePacer) {
    TheFramePacer->update();
  }

  return true;
}

// EMSCRIPTEN_ASYNC_API to safely wrap the heavy init
EM_ASYNC_JS(void, ExecuteHeavyInitialization, (), {
    console.log("INFO: JS delegating to GameEngine::init()...");
});

void AsyncEngineInit() {
  fprintf(stderr, "INFO: Initializing GameEngine in async context...\n");
  TheGameEngine = CreateGameEngine();
  TheGameEngine->init(); // This function contains emscripten_sleep(0) calls via Asyncify
  g_GameEngineInitialized = true;
  fprintf(stderr, "INFO: Async GameEngine::init() complete\n");
}

int main(int argc, char *argv[]) {
  __argc = argc;
  __argv = argv;

  fprintf(stderr, "=================================================\n");
  fprintf(stderr, " Command & Conquer Generals: Zero Hour (Web)\n");
  fprintf(stderr, " Emscripten + WebGL 2.0 Build\n");
  fprintf(stderr, "=================================================\n\n");

  try {
    TheAsciiStringCriticalSection = &critSec1;
    TheUnicodeStringCriticalSection = &critSec2;
    TheDmaCriticalSection = &critSec3;
    TheMemoryPoolCriticalSection = &critSec4;
    TheDebugLogCriticalSection = &critSec5;

    initMemoryManager();

    TheVersion = NEW Version;
    CommandLine::parseCommandLineForStartup();

    fprintf(stderr, "INFO: Initializing SDL2 (Emscripten) Video ...\n");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
      fprintf(stderr, "FATAL: SDL2 Video init failed: %s\n", SDL_GetError());
      return 1;
    }
    fprintf(stderr, "INFO: Initializing SDL2 (Emscripten) Audio ...\n");
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
      fprintf(stderr, "WARN: SDL2 Audio init failed: %s\n", SDL_GetError());
    }

    fprintf(stderr, "INFO: Creating SDL3 OpenGL ES 3.0 window (→ WebGL 2.0) ...\n");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    
    int canvas_width = 1024;
    int canvas_height = 768;
    emscripten_get_canvas_element_size("#canvas", &canvas_width, &canvas_height);
    
    const Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    TheSDL3Window = SDL_CreateWindow(
        "Command & Conquer Generals: Zero Hour", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, canvas_width, canvas_height, windowFlags);
    if (!TheSDL3Window) {
      fprintf(stderr, "FATAL: SDL_CreateWindow failed: %s\n", SDL_GetError());
      SDL_Quit();
      return 1;
    }
        
    SDL_GLContext gl_context = SDL_GL_CreateContext(TheSDL3Window);
    if (!gl_context) {
      fprintf(stderr, "FATAL: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
      SDL_DestroyWindow(TheSDL3Window);
      SDL_Quit();
      return 1;
    }
    SDL_GL_MakeCurrent(TheSDL3Window, gl_context);

    ApplicationHWnd = (HWND)(void *)TheSDL3Window;
    fprintf(stderr, "INFO: SDL3 + WebGL 2.0 context ready\n");
    
    if (!GLES3_Init(nullptr, false)) {
      fprintf(stderr, "FATAL: GLES3_Init failed\n");
      return 1;
    }
    
    WebVisibility::Init();

    // 1. CLEAR FRAME 0 IMMEDIATELY BEFORE ANY FILE I/O OR INITS
    fprintf(stderr, "INFO: Rendering first frame before VFS init...\n");
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    // SDL_GL_SwapWindow(TheSDL3Window); // Emscripten hates sync swap outside main loop

    TheFramePacer = new FramePacer();
    TheFramePacer->enableFramesPerSecondLimit(TRUE);

    // Asset loading: mount .big archives via HTTP into the Emscripten VFS
    // We do this before the game engine starts, but theoretically it could also be async.
    fprintf(stderr, "INFO: Initializing BigVFS asset system...\n");
    const char *asset_base_url = "assets/";
    if (BigVFS::Init(asset_base_url)) {
      static const char *const startup_archives[] = {
          "INI.big", "Textures.big", "W3D.big", "maps.big", "Window.big",
          "English.big", "shaders.big",
          "INIZH.big", "TexturesZH.big", "W3DZH.big", "MapsZH.big", "WindowZH.big",
          "EnglishZH.big", "ShadersZH.big", "TerrainZH.big",
          nullptr};
      for (int i = 0; startup_archives[i] != nullptr; ++i) {
        BigVFS::Mount_Archive(startup_archives[i]);
      }
      BigVFS::Mount_To_Emscripten_FS();
    } else {
      fprintf(stderr, "WARN: BigVFS::Init failed\n");
    }

    // 2. Initialize the GameEngine synchronously (takes a few seconds)
    fprintf(stderr, "INFO: Starting synchronous GameEngine initialization...\n");
    TheGameEngine = CreateGameEngine();
    TheGameEngine->init();
    g_GameEngineInitialized = true;
    fprintf(stderr, "INFO: GameEngine::init() complete\n");

    // 3. Start the main event loop
    // This provides the JS runtime immediate return of "main" 
    fprintf(stderr, "INFO: Starting Emscripten main loop (GameWebFrameCallback) ...\n");
    WebMainLoop::Start(GameWebFrameCallback, 0, true);

  } catch (const std::exception &e) {
    fprintf(stderr, "FATAL: Exception during init: %s\n", e.what());
    return 1;
  } catch (...) {
    fprintf(stderr, "FATAL: Unknown exception during init\n");
    return 1;
  }

  return 0; // Exits cleanly, transferring control entirely to browser
}

#endif // __EMSCRIPTEN__

