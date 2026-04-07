# GeneralsX Web Port — TODO

## Legend
- [x] Done
- [~] Workaround in place (not root-caused)
- [ ] Not started

---

## Build & Compilation
- [x] Emscripten CMake toolchain (cmake/web.cmake)
- [x] GLES3 wrapper sources (gles3_wrapper.cpp, gles3_fvf.cpp, gles3_texture_utils.cpp)
- [x] BigVFS HTTP range-request VFS (gles3_big_vfs.cpp)
- [x] EmscriptenMain.cpp entry point + WebMainLoop
- [x] LinuxStubs.cpp — D3DX math, OSDisplay, Registry, Download stubs
- [x] emscripten_compat/ headers (d3d8.h, windows_base.h, etc.)
- [x] WWDownload #ifdef __EMSCRIPTEN__ guards (Download.cpp, FTP.cpp, registry.cpp)
- [x] HRESULT typedef conflict resolved (d3d8types.h defers to windows_base.h)
- [x] d3d8lib / d3d8 / d3dx8 stub INTERFACE targets in web.cmake
- [x] MilesStub + BinkStub static libraries
- [x] SDL2 static build for Emscripten (SDL_SHARED OFF, SDL_OPENGLES ON)
- [x] link_mac.sh — link-only script for Mac builds

## Rendering (D3D8 → WebGL 2.0)
- [x] WebGL 2.0 context creation (SDL2 + OpenGL ES 3.0)
- [x] VAO + default GL state initialization
- [x] FVF vertex attribute decode (positions 0–6)
- [x] Vertex shader (transform + lighting + fog)
- [x] Fragment shader (texture stages, alpha test, fog)
- [x] BGRA vertex colour swizzle in shader (a_Color.bgra)
- [x] BGRA texture colour swizzle (GL_TEXTURE_SWIZZLE)
- [x] COM refcounting fix (DUMMY_IUNKNOWN → proper AddRef/Release)
- [x] DXT mip-level explicit upload (no glGenerateMipmap on compressed)
- [x] Cull mode mapping fix (D3DCULL_CCW → GL_BACK)
- [x] Light enable/disable uniform upload
- [x] Index buffer (16-bit)
- [x] Depth test / depth write
- [x] Blend state
- [x] Alpha test (emulated in fragment shader)
- [x] TGA / DDS texture loading pipeline

## Memory System
- [x] DISABLE_GAMEMEMORY nuclear option ENABLED — bypasses custom pool, uses malloc/free
- [x] DISABLE_MEMORYPOOL_MPSB_DLINK (doubly-linked fast-path disabled for WASM32)
- [x] DISABLE_MEMORYPOOL_BOUNDINGWALL (boundary-wall debug feature disabled for WASM32)
- [~] POOL-DOUBLE-FREE — eliminated by DISABLE_GAMEMEMORY (root cause: WASM32 pointer-size layout)

## Asset Pipeline
- [x] BigVFS::Init / Mount_Archive / Mount_To_Emscripten_FS
- [x] Startup archives: INI, Textures, W3D, maps, Window, English, shaders (base + ZH)
- [ ] Audio archives deferred loading (AudioZH.big, MusicZH.big, SpeechZH.big)

## Input System — CRITICAL
- [x] SDL2 → DirectInput translation layer (keyboard) — EmscriptenInput.cpp ring buffer → dinput.h GetDeviceData
- [x] SDL2 → DirectInput translation layer (mouse) — SDL2 → Win32Mouse::addWin32Event() via EmscriptenInput_RouteMouseEvent()
- [x] DIDEVICEOBJECTDATA struct conflict resolved (dinput.h vs windows_base.h include guard)
- [ ] Pointer Lock API integration for mouse confinement
- [ ] Touch input for mobile browsers (stretch goal)

## Networking — CRITICAL
- [x] Disable GameSpy networking for web builds (NO_GAMESPY=1 in web.cmake; inert at runtime)
- [x] Multiplayer lobby/matchmaking paths inert (threads never start, TheGameSpyChat stays null)

## Platform Stubs — HIGH
- [x] QueryPerformanceCounter / QueryPerformanceFrequency — emscripten_get_now() at 1MHz
- [x] WM_MOUSEWHEEL + MAKELPARAM added to windows_base.h
- [ ] Save game / config persistence via IndexedDB (IDBFS)
- [ ] Cursor visibility / SetCursor via Emscripten API

## Audio — MEDIUM
- [ ] Audio delayed-release thread workaround (single-threaded Emscripten, thread never runs)
- [ ] On-demand audio archive mounting after main menu renders

## Polish — LOW
- [ ] Canvas resize handling (fullscreen / responsive)
- [ ] Loading progress bar (BigVFS mount progress → DOM)
- [ ] Error overlay for WebGL context loss
- [ ] wasm-opt optimization pass (currently debug build with -g -sSAFE_HEAP=1)
