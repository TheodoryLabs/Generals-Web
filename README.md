<![CDATA[<div align="center">

<img src="https://img.shields.io/badge/Platform-WebAssembly-654FF0?style=for-the-badge&logo=webassembly&logoColor=white"/>
<img src="https://img.shields.io/badge/Renderer-WebGL_2.0-990000?style=for-the-badge&logo=opengl&logoColor=white"/>
<img src="https://img.shields.io/badge/Compiler-Emscripten_3.1-blue?style=for-the-badge"/>
<img src="https://img.shields.io/badge/Status-Engine_Running-brightgreen?style=for-the-badge"/>
<img src="https://img.shields.io/badge/Org-TheodoryLabs-black?style=for-the-badge"/>

# Generals X — Web Port

### *Command & Conquer: Generals Zero Hour running in the browser via WebAssembly + WebGL 2.0*

*A full Emscripten port of the [GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) community project.*

</div>

---

## What Is This?

This repository contains the complete Emscripten/WebAssembly port of **Command & Conquer: Generals — Zero Hour**, the classic 2003 real-time strategy game by EA/Westwood. The source code is based on the community-maintained [TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) project, which reverse-engineered and modernized the original C++98 codebase to C++20.

This fork adds a full **web rendering and platform layer** — replacing DirectX 8 with a custom OpenGL ES 3.0 (WebGL 2.0) backend, bridging Win32 APIs to browser equivalents, and delivering the game as a single `.wasm` + `.js` + `.html` bundle that runs in any modern browser.

**No plugin. No download. No Wine. Just the web.**

---

## The Game

**Command & Conquer: Generals Zero Hour** is an expansion to C&C Generals released in 2003. It is a top-down real-time strategy game featuring:

- Three asymmetric factions: **USA**, **China**, and **GLA** (Global Liberation Army)
- 9 sub-faction generals, each with unique units, abilities, and strategies
- A W3D-based 3D engine with terrain deformation, dynamic lighting, and particle systems
- Large-scale battles with hundreds of units
- A campaign mode and skirmish/multiplayer support

The game was originally developed in Visual Studio 6 with C++98, targeting DirectX 8, DirectInput, DirectSound, and Windows-specific APIs throughout. This makes porting it to the web a significant architectural challenge.

---

## Web Port Architecture

The port replaces the Windows/DirectX layer with a browser-native stack while leaving the core game logic completely untouched. All platform-specific code is gated behind `#ifdef __EMSCRIPTEN__` or swapped via CMake.

```
┌─────────────────────────────────────────────────────────┐
│                   Game Logic (C++)                       │
│         (GameEngine, Units, AI, Map, Physics)            │
│              UNCHANGED from original                     │
└─────────────────────┬───────────────────────────────────┘
                       │
          ┌────────────▼────────────┐
          │   Platform Abstraction   │
          │  (Emscripten + stubs)    │
          └────────────┬────────────┘
                       │
    ┌──────────────────┼──────────────────┐
    │                  │                  │
    ▼                  ▼                  ▼
┌────────┐      ┌────────────┐    ┌─────────────┐
│WebGL 2 │      │  Emscripten│    │  HTTP Fetch │
│Renderer│      │  Main Loop │    │  (.big VFS) │
│(GLES3) │      │(rAF based) │    │  Range Req  │
└────────┘      └────────────┘    └─────────────┘
    │                  │
    ▼                  ▼
 Browser           SDL2 Input
 Canvas          (Keyboard/Mouse
                  → DirectInput
                    stubs)
```

### Key Components

#### 1. `cmake/web.cmake` — Emscripten Build Toolchain
The drop-in replacement for `cmake/dx8.cmake`. Configures the entire Emscripten pipeline:
- WebGL 2.0 flags (`-sUSE_WEBGL2=1 -sFULL_ES3=1`)
- Asyncify for blocking-style loads (`-sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=8MB`)
- Memory configuration (256MB initial, growable to 512MB)
- `FETCH=1` for HTTP Range requests to `.big` archives
- All platform defines: `NO_WIN32`, `NO_DXVK`, `NO_BINK`, `NO_MILES`, `NO_GAMESPY`
- Custom memory pool disabled (`RTS_GAMEMEMORY_ENABLE=OFF`) — uses system `malloc`/`free`

#### 2. `emscripten_compat/` — Win32 / DirectX Header Stubs
A directory of minimal stub headers that allow the game's Win32-dependent code to compile on Emscripten without modification:

| File | Replaces | Notes |
|------|----------|-------|
| `windows_base.h` | DXVK's `windows_base.h` | All Win32 types, HWND, HINSTANCE, LARGE_INTEGER, etc. |
| `d3d8.h` | DirectX 8 SDK | Full DX8 interface stubs + COM refcounting |
| `d3d8types.h` | DX8 type definitions | D3DFORMAT, D3DPOOL, D3DPRIMITIVETYPE etc. |
| `d3d8caps.h` | DX8 capabilities | D3DCAPS8 stub |
| `d3dx8.h` | D3DX utility library | Math + texture utilities stub |
| `dinput.h` | DirectInput 8 | Functional keyboard/mouse stub via SDL2 ring buffer |
| `dsound.h` | DirectSound 8 | No-op stub (audio routed via OpenAL/WebAudio) |
| `ddraw.h` | DirectDraw | Surface type stubs only |
| `windows.h` | Win32 headers | Includes `windows_base.h` |
| `mmsystem.h` | Windows multimedia | Timer stubs |
| `io.h` | POSIX I/O compat | |

#### 3. `gles3_wrapper.cpp/h` — DirectX 8 → WebGL 2.0 Translation Layer
The heart of the renderer. This replaces `dx8wrapper.cpp` entirely for web builds.

The original game uses DirectX 8's **fixed-function pipeline** — a legacy graphics API with configurable texture blending stages, per-vertex lighting, material colors, and fog, all without shaders. WebGL 2.0 has no fixed-function pipeline. This wrapper emulates it with a carefully crafted GLSL vertex+fragment shader pair that replicates the DX8 render state machine in real time.

Key translations:
- `IDirect3DDevice8::SetRenderState` → OpenGL state machine calls
- `IDirect3DDevice8::SetTextureStageState` → GLSL uniform uploads
- `IDirect3DDevice8::DrawIndexedPrimitive` → `glDrawElements`
- `IDirect3DDevice8::CreateTexture` → `glTexImage2D` with BGRA swizzle
- `IDirect3DDevice8::SetLight` / `LightEnable` → GLSL light uniforms (up to 8 lights)
- D3D fog (linear, exponential, exp²) → GLSL fog in fragment shader
- Alpha test → `discard` in fragment shader

Bugs fixed in this layer:
- **BGRA swizzle**: DX8 textures are stored BGRA; WebGL uses RGBA. Fixed with `GL_TEXTURE_SWIZZLE_R/B` per-texture.
- **DXT mipmaps**: DX8 allows explicit mip upload for compressed textures. `glGenerateMipmap` on compressed base is unreliable in WebGL2. Fixed with explicit `glCompressedTexImage2D` per level.
- **Cull mode reversal**: `D3DCULL_CCW` maps to `GL_BACK` (not `GL_FRONT`) in OpenGL. All geometry was inside-out before this fix.
- **COM refcounting**: Original `DUMMY_IUNKNOWN` macros were no-ops. All textures, vertex buffers, and index buffers leaked indefinitely. Fixed with proper `AddRef`/`Release` + `delete at 0`.
- **Disabled lights**: `LightEnable(i, FALSE)` wasn't uploading `u_Lights[i].enabled = 0`. Fixed to zero the uniform on every draw call.

#### 4. `gles3_fvf.cpp/h` — Flexible Vertex Format Decoder
DirectX 8 encodes vertex layouts as a bitmask called FVF (Flexible Vertex Format). This file decodes FVF codes at runtime to set up OpenGL vertex attribute arrays (`glVertexAttribPointer`). Supports positions, normals, diffuse/specular colors, and up to 8 texture coordinate sets.

#### 5. `gles3_texture_utils.cpp/h` — Texture Loading & Management
Handles the TGA and DDS texture loading pipeline for the web build. DDS textures with DXT1/DXT3/DXT5 compressed formats are uploaded with explicit mip-level enumeration. Manages the texture cache and COM-style refcounting.

#### 6. `gles3_big_vfs.cpp/h` — HTTP Range Request VFS
The game loads assets from `.big` archive files — a proprietary format containing thousands of game files (textures, models, sounds, INI scripts) in a single binary blob.

On the web, these archives are served over HTTP and accessed via **HTTP Range requests** — the browser only downloads the specific bytes needed for each asset, on demand. This avoids loading 1GB+ of assets at startup.

Implementation:
- C++ calls `Fetch_Range_JS()` (an `EM_ASYNC_JS` function) which uses the browser's `fetch()` API with `headers: {Range: 'bytes=X-Y'}`
- Asyncify allows the synchronous C++ file-read codepath to `await` the async fetch transparently
- Archives are mounted to Emscripten's in-memory filesystem at `/assets/`
- The BigFileSystem layer sees a normal filesystem; HTTP is completely transparent

#### 7. `gles3_mainloop.cpp/h` — Emscripten Main Loop
Replaces the Win32 message pump with `emscripten_set_main_loop()`. The browser calls `GameWebFrameCallback()` at every `requestAnimationFrame`, which:
1. Pumps SDL2 input events
2. Calls `TheGameEngine->update()` (game logic tick)
3. Calls `TheGameEngine->draw()` (renders the frame to the WebGL canvas)
4. Emscripten auto-swaps the WebGL backbuffer

#### 8. `EmscriptenMain.cpp` — Web Entry Point
Replaces `WinMain.cpp` for web builds. Responsibilities:
- Mounts all `.big` asset archives via HTTP Range VFS
- Creates the SDL2 window and WebGL context
- Initializes `Win32GameEngine` (the game's engine class, unchanged)
- Sets up `GameWebFrameCallback` as the Emscripten main loop
- Handles engine lifecycle (init → loop → quit)

#### 9. `EmscriptenInput.cpp/h` — Input Translation
Bridges SDL2 events to the game's Windows-style input system:
- **Keyboard**: SDL2 scancodes → DIK_* keycodes → ring buffer in `dinput.h` stub → `IDirectInputDevice8::GetDeviceData`
- **Mouse**: SDL2 mouse events → `Win32Mouse::addWin32Event()` with WM_MOUSEMOVE / WM_LBUTTONDOWN / WM_MOUSEWHEEL Win32 message codes
- **80+ SDL_SCANCODE → DIK_*** mappings covering the full keyboard

#### 10. `LinuxStubs.cpp` — Platform Symbol Stubs
Provides definitions for Win32 globals and functions that are referenced in the game codebase but have no implementation on non-Windows platforms: `ApplicationHInstance`, `TheWin32Mouse`, `QueryPerformanceCounter`, `QueryPerformanceFrequency`, D3DX math utilities, registry functions, etc.

---

## Build Status

| Subsystem | Status | Notes |
|-----------|--------|-------|
| WebGL 2.0 context | ✅ Working | SDL2 + OpenGL ES 3.0 |
| Vertex / fragment shaders | ✅ Working | Full DX8 fixed-function emulation |
| FVF vertex decode | ✅ Working | All 7 position types |
| Texture loading (TGA/DDS) | ✅ Working | BGRA swizzle, DXT mipmaps |
| COM refcounting | ✅ Working | Proper AddRef/Release on all D3D objects |
| Cull mode | ✅ Working | D3DCULL_CCW → GL_BACK |
| Lighting (8 lights) | ✅ Working | Per-draw uniform upload |
| Alpha test | ✅ Working | Emulated via `discard` in shader |
| Blend states | ✅ Working | Full DX8 blend factor mapping |
| Depth test / write | ✅ Working | |
| Fog (linear/exp/exp²) | ✅ Working | GLSL fog in fragment shader |
| Index buffer (16-bit) | ✅ Working | |
| BigVFS HTTP Range | ✅ Working | Asyncify + Fetch API |
| Engine initialization | ✅ Working | Mounts archives, starts game engine |
| Keyboard input | ✅ Working | SDL2 → DIK ring buffer |
| Mouse input | ✅ Working | SDL2 → Win32Mouse events |
| QueryPerformanceCounter | ✅ Working | `emscripten_get_now()` at 1MHz |
| GameSpy networking | ✅ Disabled | `NO_GAMESPY=1`, no-op at runtime |
| Custom memory pool | ✅ Disabled | System `malloc`/`free` via `GameMemoryNull` |
| POOL-DOUBLE-FREE | ✅ Eliminated | Root-cause: pool disabled entirely |
| Audio | ⚠️ Partial | OpenAL/WebAudio wired; audio archives not yet mounted |
| Mouse pointer lock | ⏳ Pending | Needed for RTS camera confinement |
| Save/load persistence | ⏳ Pending | IDBFS (IndexedDB) not yet wired |
| Audio archive mounting | ⏳ Pending | AudioZH.big, MusicZH.big deferred |
| Mobile/touch input | ⏳ Pending | Stretch goal |

---

## How It Was Built — Development Sessions

### Session 1 — March 13, 2026: Initial Setup
- Cloned GeneralsGameCode and initialized the web build environment
- Set up Emscripten SDK (emsdk) and CMake toolchain (`cmake/web.cmake`)
- Created initial `emscripten_compat/` header stubs
- First `ninja` invocation: catalogued all 500+ compilation errors
- Established tracker documents: `port-todo.md`, `porting-log.md`, `architecture-map.md`

### Session 2 — March 14, 2026: Rendering Pipeline
First successful engine initialization. Five critical rendering bugs fixed:
1. **COM refcounting leak** — `DUMMY_IUNKNOWN` AddRef/Release were no-ops; all GPU resources leaked
2. **BGRA texture swizzle** — DX8 stores textures as BGRA; WebGL expects RGBA; all colors were wrong
3. **DXT mipmap explicit upload** — WebGL2 `glGenerateMipmap` on compressed textures is unreliable; switched to explicit level upload
4. **Cull mode reversal** — DX8 handedness vs OpenGL handedness was inverted; all geometry was inside-out
5. **Light disable propagation** — Disabled lights weren't zeroing their GLSL uniforms; stale lighting values corrupted the scene

### Session 3 — March 15, 2026: Memory + Input + Platform
Resolved the `POOL-DOUBLE-FREE` startup crash, then completed the platform stub layer:
- **Memory**: Exhaustive static analysis of the custom pool allocator; added non-fatal workaround; root-caused to WASM32 pointer-size layout issues in the 32-byte DMA pool
- **Input**: Built the full SDL2 → DirectInput/Win32Mouse bridge (`EmscriptenInput.cpp`)
- **GameSpy**: Disabled at build time (`NO_GAMESPY=1`); all multiplayer code is inert
- **Timing**: Bridged `QueryPerformanceCounter/Frequency` to `emscripten_get_now()` at 1MHz resolution

### Session 4 — March 16, 2026: Hardening + Nuclear Memory Option
Deep audit of all Session 3 changes; found and fixed multiple build-breaking issues:
- **Duplicate symbol**: `c_dfDIKeyboard` defined in both `EmscriptenInput.cpp` and `LinuxStubs.cpp` → guarded with `#ifndef __EMSCRIPTEN__`
- **ODR violations**: `TheWin32Mouse` and `ApplicationHInstance` had wrong types in stubs → typed correctly
- **WSA macro redefinitions**: 5 GameSpy socket macros redefined twice → added `#undef` guards
- **Nuclear option activated**: `RTS_GAMEMEMORY_ENABLE=OFF` in `web.cmake` — completely replaces the custom pool allocator with system `malloc`/`free` via `GameMemoryNull.cpp`. This eliminated all pool-related corruption permanently.
- **GameMemoryNull.h completion**: Added all missing macros (`MEMORY_POOL_GLUE`, `GCMP_FIND`, `GCMP_CREATE`, `STLSpecialAlloc`, `PoolInitRec`, etc.) that hundreds of game classes depend on

---

## Repository Structure

```
generals-x-web/
├── cmake/
│   ├── web.cmake              ← Emscripten build toolchain (THE key file)
│   └── ...                    ← Other platform toolchains
├── GeneralsMD/
│   └── Code/
│       ├── Main/
│       │   ├── EmscriptenMain.cpp    ← Web entry point (replaces WinMain.cpp)
│       │   ├── EmscriptenInput.cpp   ← SDL2 → DirectInput bridge
│       │   ├── EmscriptenInput.h
│       │   └── LinuxStubs.cpp        ← Platform symbol stubs
│       └── Libraries/Source/WWVegas/WW3D2/
│           ├── emscripten_compat/    ← Win32/DX8 header stubs
│           │   ├── windows_base.h
│           │   ├── d3d8.h
│           │   ├── dinput.h
│           │   └── ...
│           ├── gles3_wrapper.cpp/h   ← DX8 → WebGL2 core renderer
│           ├── gles3_fvf.cpp/h       ← Vertex format decoder
│           ├── gles3_texture_utils.cpp/h ← Texture pipeline
│           ├── gles3_big_vfs.cpp/h   ← HTTP Range VFS
│           ├── gles3_mainloop.cpp/h  ← Emscripten main loop
│           └── dx8_fixedfunction_shaders.cpp ← GLSL shader source
├── docs/web-port/
│   ├── PORTING_LOG.md         ← Session-by-session development history
│   ├── RENDERING_STATUS.md    ← What renders, what doesn't, bugs fixed
│   ├── PORT_TODO.md           ← Remaining work (prioritized)
│   ├── BUILD_INSTRUCTIONS.md  ← How to build from source
│   └── ARCHITECTURE.md        ← This file's technical companion
├── dist/
│   ├── GeneralsZH.html        ← Browser shell
│   ├── GeneralsZH.js          ← Emscripten JS glue
│   └── GeneralsZH.wasm        ← The game (WebAssembly binary)
└── README_WEB.md              ← This file
```

---

## Building From Source

### Prerequisites
- **macOS** (Apple Silicon or Intel) or **Linux**
- **Emscripten SDK** 3.1.60+ (`emsdk install latest && emsdk activate latest`)
- **CMake** 3.25+
- **Python 3** (for emsdk and serve scripts)
- ~16 GB RAM (for `wasm-opt` Asyncify pass)
- The original **C&C Generals Zero Hour** game assets (`.big` files) — available via [Steam](https://store.steampowered.com/bundle/39394)

### Full Build (first time — 20–40 min)

```bash
# 1. Activate Emscripten
source /path/to/emsdk/emsdk_env.sh

# 2. Configure with web toolchain
emcmake cmake -B build-web \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/emscripten.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DRTS_GAME=GeneralsMD \
  -DRTS_PLATFORM=web \
  -DCMAKE_PROJECT_INCLUDE=cmake/web.cmake

# 3. Build
cd build-web && ninja
```

Output: `build-web/GeneralsMD/GeneralsZH.{html,js,wasm}`

### Link-Only Rebuild (fast — 5–15 min)
When source files haven't changed (only linker flags or stubs):

```bash
bash link_mac.sh
```

### Serving Locally

```bash
cd dist/
python3 serve.py
# Open http://localhost:8080/GeneralsZH.html
```

The server must:
- Serve `.wasm` with `Content-Type: application/wasm`
- Support `Range` request headers (for HTTP Range VFS)
- Set `Cross-Origin-Opener-Policy: same-origin` (for SharedArrayBuffer)
- Set `Cross-Origin-Embedder-Policy: require-corp`

### Asset Setup
Place your `.big` files in the `assets/` directory alongside the HTML:
```
assets/
  INI.big
  INIZH.big
  Textures.big
  TexturesZH.big
  W3D.big
  W3DZH.big
  Window.big
  WindowZH.big
  English.big
  EnglishZH.big
  Maps.big / MapsZH.big
  Terrain.big / TerrainZH.big
  PatchINI.big
  ShadersZH.big
  shaders.big
```

---

## Roadmap

### Immediate (next session)
- [ ] **Pointer Lock** — Confine mouse cursor to canvas for RTS camera drag
- [ ] **Audio archive mounting** — Mount `AudioZH.big`, `MusicZH.big`, `SpeechZH.big` after main menu
- [ ] **Save persistence** — Mount IndexedDB via `IDBFS` for save games and config

### Short term
- [ ] **Release build** — Remove `-g`, `-sSAFE_HEAP`, `-sASSERTIONS=2` for smaller/faster WASM
- [ ] **Canvas resize** — Handle browser window resize / fullscreen toggle gracefully
- [ ] **Loading progress** — BigVFS mount progress → DOM loading bar
- [ ] **WebGL context loss recovery** — Handle GPU context lost/restored events

### Long term
- [ ] **Mobile/touch input** — Touch events → RTS unit selection and camera
- [ ] **Multiplayer exploration** — WebSockets as transport layer for LAN-style play
- [ ] **Mod support** — Custom `.big` archives served from same origin

---

## Contributing

This is a TheodoryLabs project. If you want to contribute to the web port:

1. Understand the base project first: [TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode)
2. All web-specific changes are gated with `#ifdef __EMSCRIPTEN__` or `#ifdef PLATFORM_WEB`
3. The web toolchain is in `cmake/web.cmake` — this is the single entry point for web builds
4. New platform stubs go in `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/emscripten_compat/`
5. Run `ninja` from `build-web/` and capture browser console output to diagnose issues

For bugs, open an issue with:
- Browser + version
- Full browser console log
- Steps to reproduce

---

## Legal

Command & Conquer: Generals and Zero Hour are trademarks of Electronic Arts Inc.

The source code in this repository is released under the **GNU General Public License v3** by Electronic Arts Inc. as part of the community source release. See [LICENSE.md](LICENSE.md).

The web port modifications (all files prefixed `gles3_*`, `EmscriptenMain.cpp`, `EmscriptenInput.*`, `LinuxStubs.cpp`, `cmake/web.cmake`, `emscripten_compat/`) are also GPL-3.0.

You must own a legal copy of C&C Generals Zero Hour to use the game assets. The `.big` files are **not** included in this repository.

---

<div align="center">

**TheodoryLabs** · Built with Emscripten, WebGL 2.0, and too much caffeine

*"The engine initializes. The first frame is close."*

</div>
]]>