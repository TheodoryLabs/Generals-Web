<div align="center">

<img src="https://img.shields.io/badge/Platform-WebAssembly-654FF0?style=for-the-badge&logo=webassembly&logoColor=white"/>
<img src="https://img.shields.io/badge/Renderer-WebGL_2.0-990000?style=for-the-badge&logo=opengl&logoColor=white"/>
<img src="https://img.shields.io/badge/Compiler-Emscripten_3.1-blue?style=for-the-badge"/>
<img src="https://img.shields.io/badge/Status-Engine_Running-brightgreen?style=for-the-badge"/>
<img src="https://img.shields.io/badge/Org-TheodoryLabs-black?style=for-the-badge"/>

# Command & Conquer Web

### *C&C Generals Zero Hour — running in the browser via WebAssembly + WebGL 2.0*

*A full Emscripten port of the [GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) community project, built by [TheodoryLabs](https://github.com/TheodoryLabs).*

</div>

---

## What Is This?

This repository contains the complete **Emscripten/WebAssembly port** of Command & Conquer: Generals — Zero Hour, the classic 2003 real-time strategy game by EA/Westwood. The source is based on the community-maintained [TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) project, which modernized the original C++98 codebase to C++20.

This fork adds a full **web rendering and platform layer** — replacing DirectX 8 with a custom OpenGL ES 3.0 (WebGL 2.0) backend, bridging Win32 APIs to browser equivalents, and delivering the game as a `.wasm` + `.js` + `.html` bundle that runs in any modern browser.

**No plugin. No download. No Wine. Just the web.**

---

## Running the Game

> **Full setup guide with all steps and troubleshooting → [SETUP.md](SETUP.md)**

Quick summary:

1. Download `GeneralsZH.html`, `GeneralsZH.js`, `GeneralsZH.wasm`, and `serve.py` from the [latest Release](https://github.com/TheodoryLabs/command-and-conquer-web/releases/latest)
2. Create an `assets/` folder next to those files and copy your `.big` game files into it (from your Zero Hour install)
3. Run: `python3 serve.py`
4. Open **http://localhost:8888/GeneralsZH.html** in Chrome or Firefox

You must own a legal copy of C&C Generals Zero Hour to use the game assets. Available on [Steam](https://store.steampowered.com/bundle/39394).

---

## The Game

**Command & Conquer: Generals Zero Hour** is an expansion to C&C Generals released in 2003. It is a top-down real-time strategy game featuring:

- Three asymmetric factions: **USA**, **China**, and **GLA** (Global Liberation Army)
- 9 sub-faction generals, each with unique units, abilities, and strategies
- A W3D-based 3D engine with terrain deformation, dynamic lighting, and particle systems
- Large-scale battles with hundreds of units, campaign + skirmish modes

The game was originally built in Visual Studio 6 with C++98, targeting DirectX 8, DirectInput, DirectSound, and Win32 APIs throughout — making this a significant porting challenge.

---

## Web Port Architecture

The port replaces the Windows/DirectX layer with a browser-native stack while leaving core game logic completely untouched.

```
+----------------------------------------------+
|             Game Logic (C++)                  |
|     (GameEngine, Units, AI, Map, Physics)     |
|          UNCHANGED from original              |
+---------------------+------------------------+
                      |
           +----------+-----------+
           |  Platform Abstraction |
           |  (Emscripten stubs)   |
           +----------+-----------+
                      |
    +-----------------+-----------------+
    |                 |                 |
    v                 v                 v
+--------+    +-----------+    +----------+
|WebGL 2 |    | Main Loop |    |HTTP Fetch|
|Renderer|    |  (rAF)    |    |.big VFS  |
|(GLES3) |    |           |    |Range Req |
+--------+    +-----------+    +----------+
    |                 |
    v                 v
 Browser          SDL2 Input
 Canvas       (KB/Mouse -> DirectInput)
```

### Key Files

| File | Purpose |
|------|---------|
| `cmake/web.cmake` | Emscripten CMake toolchain — the single entry point for web builds |
| `GeneralsMD/Code/Main/EmscriptenMain.cpp` | Web entry point, replaces WinMain.cpp |
| `GeneralsMD/Code/Main/EmscriptenInput.cpp` | SDL2 to DirectInput/Win32Mouse bridge (80+ key mappings) |
| `GeneralsMD/Code/Main/LinuxStubs.cpp` | Platform symbol stubs |
| `.../WW3D2/emscripten_compat/` | Win32/DX8 stub headers (d3d8.h, dinput.h, windows.h, etc.) |
| `.../WW3D2/gles3_wrapper.cpp` | DirectX 8 fixed-function pipeline -> OpenGL ES 3.0 / WebGL 2.0 |
| `.../WW3D2/gles3_fvf.cpp` | Flexible Vertex Format decoder |
| `.../WW3D2/gles3_texture_utils.cpp` | Texture pipeline (DXT1/3/5, BGRA swizzle, mipmaps) |
| `.../WW3D2/gles3_big_vfs.cpp` | HTTP Range request VFS for .big archives |
| `.../WW3D2/gles3_mainloop.cpp` | emscripten_set_main_loop / requestAnimationFrame |
| `.../WW3D2/dx8_fixedfunction_shaders.cpp` | GLSL shader pair emulating DX8 fixed-function pipeline |

---

## How the Renderer Works

The original game uses DirectX 8's **fixed-function pipeline** — a legacy graphics API with configurable texture blending, per-vertex lighting, material colors, and fog, all without shaders. WebGL 2.0 has no fixed-function pipeline.

`gles3_wrapper.cpp` emulates it with a GLSL vertex+fragment shader pair that replicates the DX8 render state machine in real time:

- `SetRenderState` -> OpenGL state calls
- `SetTextureStageState` -> GLSL uniform uploads
- `DrawIndexedPrimitive` -> `glDrawElements`
- `CreateTexture` -> `glTexImage2D` with BGRA->RGBA swizzle
- `SetLight` / `LightEnable` -> GLSL light uniforms (8 lights)
- D3D fog (linear/exp/exp2) -> GLSL fog in fragment shader
- Alpha test -> `discard` in fragment shader

### Rendering Bugs Fixed

| Bug | Root Cause | Fix |
|-----|-----------|-----|
| All GPU resources leaked | `DUMMY_IUNKNOWN` AddRef/Release were no-ops | Proper COM refcounting (`m_comRefCount`) |
| Wrong texture colors (red/blue swapped) | DX8 stores textures as BGRA, WebGL expects RGBA | `GL_TEXTURE_SWIZZLE_R/B` per D3D format |
| DXT mipmaps missing | `glGenerateMipmap` on compressed textures is unreliable in WebGL2 | Explicit `glCompressedTexImage2D` per mip level |
| All geometry inside-out | D3DCULL_CCW maps to GL_BACK in OpenGL, not GL_FRONT | Fixed cull mode mapping |
| Stale lighting on disabled lights | `LightEnable(i, FALSE)` was not zeroing GLSL uniforms | Upload `u_Lights[i].enabled = 0` on every draw |

---

## Build Status

| Subsystem | Status |
|-----------|--------|
| WebGL 2.0 context + shaders | Working |
| Textures (TGA, DDS/DXT) | Working |
| Lighting, fog, alpha, blend | Working |
| BigVFS HTTP Range requests | Working |
| Engine initialization | Working |
| Keyboard + mouse input | Working |
| Timing (QueryPerformanceCounter) | Working |
| GameSpy networking | Disabled (NO_GAMESPY=1) |
| Custom memory pool | Disabled (system malloc/free) |
| Audio | Partial |
| Mouse pointer lock | Pending |
| Save/load persistence (IndexedDB) | Pending |

---

## Development Sessions

| Session | Date | Work |
|---------|------|------|
| 1 | Mar 13, 2026 | Build system setup, first compilation attempt, 500+ error audit |
| 2 | Mar 14, 2026 | Rendering pipeline — 5 critical rendering bugs fixed, first frame |
| 3 | Mar 15, 2026 | Memory system, SDL2 input bridge, platform stubs, GameSpy disabled |
| 4 | Mar 16, 2026 | Linker/ODR hardening, nuclear memory option, GameMemoryNull completion |

Full session-by-session details in [`porting-log.md`](../porting-log.md).

---

## Building From Source

### Prerequisites

- macOS or Linux
- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) 3.1.60+
- CMake 3.25+, Python 3
- ~16 GB RAM (for wasm-opt Asyncify pass)
- Original C&C Generals Zero Hour `.big` asset files

### Full Build (~20-40 min)

```bash
source /path/to/emsdk/emsdk_env.sh

emcmake cmake -B build-web \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DRTS_GAME=GeneralsMD \
  -DRTS_PLATFORM=web \
  -DCMAKE_PROJECT_INCLUDE=cmake/web.cmake

cd build-web && ninja
```

---

## Roadmap

- [ ] Mouse pointer lock (RTS camera confinement)
- [ ] Audio archive mounting (AudioZH.big, MusicZH.big, SpeechZH.big)
- [ ] Save/load persistence via IndexedDB (IDBFS)
- [ ] Release build (strip debug flags, smaller WASM)
- [ ] Canvas resize / fullscreen toggle
- [ ] Loading progress bar

---

## Legal

Command & Conquer: Generals and Zero Hour are trademarks of Electronic Arts Inc. Source code is released under **GPL-3.0** by EA as part of the community source release. See [LICENSE.md](LICENSE.md).

You must own a legal copy of C&C Generals Zero Hour to use the game assets. The `.big` files are **not** included.

---

<div align="center">

**TheodoryLabs** — Built with Emscripten, WebGL 2.0, and too much caffeine

</div>
