# GeneralsX Web Port — TODO

## Legend
- [x] Done
- [~] Workaround in place (not root-caused)
- [ ] Not started

## Status (2026-05-05, after Session 12)

**The menu renders!** End-to-end pixels on screen for the first time.

Session 12 found, isolated, and fixed the black-canvas bug via in-engine
diagnostic counters: the d3d8 stub's `SetStreamSource` was being clobbered
by trailing null calls for streams 1+ in the engine's per-stream loop.
One-line fix in `emscripten_compat/d3d8.h`: `if (StreamNumber != 0) return 0;`.
After the fix the C&C Generals logo, "GENERALS / ZERO HOUR" title, and
the menu's game-asset thumbnails render to the canvas. See Session 12
final win in `porting-log.md` for the diagnostic methodology.

Other Session 12 wins:
- Music quit-suicide → `setQuitting(FALSE)` post-init
- Error overlay z-bleed → `showError` hides loading overlay
- `WebVisibility::Init` paused loop in iframes → disabled the callback
- iframe rAF throttled to 0Hz → switched main loop to setTimeout(60fps)
- Belt-and-braces per-frame clear of `m_breakTheMovie` / `m_loadScreenRender`
- Asset symlink + Range-aware HTTP server fixed

The port links and runs. Last successful builds:
- **`build-web/GeneralsMD/GeneralsZH.{html,js,wasm}` — May 5 2026 (debug,
  Session 11 verified)** — 30KB / 558KB / 91MB.
- **`build-web/GeneralsMD/GeneralsZH-rel.{html,js,wasm}` — May 5 2026
  (release, deployable)** — 11KB / 128KB / **17MB**. Produced via
  `bash link_mac.sh release`. Closure-minified JS, `-O3` wasm, no
  SAFE_HEAP / ASSERTIONS overhead.
- `build-html5/GeneralsMD/GeneralsZH.{html,js,wasm}` — Mar 31 2026 (older,
  predates Sessions 5–11).
- `GeneralsX/dist/GeneralsZH.{html,js,wasm}` — Apr 7 2026 (older).

Session 11 (May 5 2026) landed touch input, a click-to-start gesture gate, a
release-build profile, fixed two latent bugs caught by build verification
(broken EM_ASM in `GX_FlushIdbfs`, missing `EmscriptenInput.cpp.o` from
`link_mac.sh`'s MAIN_OBJS), and produced the first self-consistent debug
+ release artefacts since source diverged from the build outputs in
March/April.

Open work below is grouped by phase. Items at the top are the next ones to
tackle for a playable single-player session in the browser.

### Recommended next sprint (in priority order)
1. ~~**Loading progress bar (Polish)**~~ — done in Session 6 (May 4 2026). See
   `web_shell.html` + `GX_ReportProgress` in `EmscriptenMain.cpp`.
2. ~~**Pointer Lock + cursor visibility**~~ — done in Session 7 (May 4 2026).
   JS-side `canvas.requestPointerLock()` on right-mouse-down + xrel/yrel
   accumulation into a virtual cursor in `EmscriptenInput.cpp`; `SetCursor()`
   stub forwards to `EmscriptenInput_SetCanvasCursor`.
3. **Audio** — Session 8 landed the deferred archive mount (AudioZH /
   MusicZH / SpeechZH lazy-load via `DeferredAudio_Step` once the menu is
   on screen) and *implemented* the delayed-release drain
   (`Tick_Delayed_Release_Objects`), but the drain is dormant because
   `libwwaudio.a` is not in the web link line; the audio path is currently
   stubbed via `milesstub`. End-to-end audible audio is blocked on linking
   `z_wwaudio` and replacing the stub backend.
4. ~~**IndexedDB save persistence**~~ — done in Session 9 (May 4 2026).
   `/userdata` mounted as IDBFS via `Module.preRun` in `web_shell.html`;
   initial sync blocks `main()` via `addRunDependency`. Periodic + visibility
   + beforeunload flush via `GX_FlushIdbfs`. Engine `m_userDataDir` forced to
   `/userdata/` on Emscripten and `LocalFile::open` normalizes `\` → `/`.
5. ~~**Canvas resize handling + WebGL context-loss overlay**~~ — done in
   Session 10 (May 4 2026). HiDPI-aware resize observer in the shell ccalls
   `GX_OnCanvasResize`; `webglcontextlost` shows a graceful overlay with a
   reload button.
6. **POOL-DOUBLE-FREE root cause (Memory System)** — `DISABLE_GAMEMEMORY` keeps
   it dormant. Worth investigating only if we want to re-enable the custom pool
   for performance; not required for a working port.

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
- [x] Audio archives deferred loading (AudioZH.big, MusicZH.big, SpeechZH.big)
      — Session 8: state machine in `EmscriptenMain.cpp::DeferredAudio_Step`
      kicks 30 frames after engine "ready", mounts one archive per frame,
      calls `BigVFS::Mount_To_Emscripten_FS()` once at the end. Surfaces
      progress to `window.GeneralsX.setSecondaryProgress` (corner toast).

## Input System — CRITICAL
- [x] SDL2 → DirectInput translation layer (keyboard) — EmscriptenInput.cpp ring buffer → dinput.h GetDeviceData
- [x] SDL2 → DirectInput translation layer (mouse) — SDL2 → Win32Mouse::addWin32Event() via EmscriptenInput_RouteMouseEvent()
- [x] DIDEVICEOBJECTDATA struct conflict resolved (dinput.h vs windows_base.h include guard)
- [x] Pointer Lock API integration for mouse confinement — Session 7: JS-owned
      request on right-mouse-down (canvas.requestPointerLock), JS
      pointerlockchange → ccall EmscriptenInput_OnPointerLockChange. C++ side
      accumulates xrel/yrel into a virtual cursor while locked.
- [x] Touch input for mobile browsers — Session 11 (May 5 2026): SDL_FINGER*
      handlers in `EmscriptenInput.cpp`. 1 finger uses SDL's synthetic mouse
      events (tap = LMB, drag = LMB drag). 2 fingers engages an explicit
      RMB-drag camera pan at the centroid plus pinch-to-wheel zoom. Stale
      synthetic mouse events from SDL_TOUCH_MOUSEID are filtered while the
      2-finger gesture is active. CSS `touch-action: none` on the canvas
      stops the browser eating gestures.

## Networking — CRITICAL
- [x] Disable GameSpy networking for web builds (NO_GAMESPY=1 in web.cmake; inert at runtime)
- [x] Multiplayer lobby/matchmaking paths inert (threads never start, TheGameSpyChat stays null)

## Platform Stubs — HIGH
- [x] QueryPerformanceCounter / QueryPerformanceFrequency — emscripten_get_now() at 1MHz
- [x] WM_MOUSEWHEEL + MAKELPARAM added to windows_base.h
- [x] Save game / config persistence via IndexedDB (IDBFS) — Session 9
      (May 4 2026): IDBFS mounted at `/userdata` via Module.preRun; initial
      `FS.syncfs(true)` blocks main() via `addRunDependency` so saves load
      before the engine reads anything. Periodic 30s + visibilitychange +
      beforeunload flushes via `GX_FlushIdbfs`. `m_userDataDir` overridden
      to `/userdata/` on Emscripten in `GlobalData.cpp`. Backslash-to-slash
      path normalization added to `LocalFile::open` so engine-built save
      paths actually land under `/userdata`.
- [x] Cursor visibility / SetCursor via Emscripten API — Session 7: SetCursor()
      stub in windows_base.h now forwards to EmscriptenInput_SetCanvasCursor,
      which routes to window.GeneralsX.setCanvasCursor in the shell to set
      canvas.style.cursor = 'default' / 'none'.

## Audio — MEDIUM
- [~] Audio delayed-release thread workaround — Session 8: helper
      `WWAudioThreadsClass::Tick_Delayed_Release_Objects()` is implemented in
      `Core/Libraries/Source/WWVegas/WWAudio/Threads.{h,cpp}` but currently
      DORMANT: `libwwaudio.a` is not in `z_generals`'s link line on the web
      build (the audio path is stubbed via `milesstub`). When real WWAudio is
      wired up, link `z_wwaudio` into `z_generals` and call the helper once
      per frame from `GameWebFrameCallback`. The helper itself is correct and
      thread-safe; just gated on linkage.
- [x] On-demand audio archive mounting after main menu renders — Session 8:
      see Asset Pipeline above. AudioZH/MusicZH/SpeechZH start mounting ~500ms
      after the engine reports ready.
- [ ] Wire up real audio. The engine's audio code path is
      `MilesAudioManager` (3.4k LoC at
      `Core/GameEngineDevice/Source/MilesAudioDevice/MilesAudioManager.cpp`)
      → ~70 AIL_* functions → `libmilesstub.a` (480-line null shim from
      `_deps/miles-src/miles.c`, every function returns 0/null). Currently
      every call is a no-op so nothing plays.

      **The init gate** is at `MilesAudioManager::openDevice()` line 1429:
      ```
      AIL_startup();                                                 // returns void
      retval = AIL_quick_startup(useDigital, useMidi, rate, ...);    // returns 0 in stub
      AIL_quick_handles(&m_digitalHandle, ...);                      // *p stays null
      if (retval) buildProviderList(); else setOn(false, All);       // ← we hit the else
      ```
      `AIL_quick_startup` returns 0 → engine calls `setOn(false, All)` →
      `m_audioOn = false` → every subsequent `playAudioEvent` early-outs.

      **Phase 1 — minimal silent-but-believed-up bridge** (1–2 sessions):
      1. Replace `libmilesstub.a` with `libwebaudiobridge.a` (new directory
         `Core/Libraries/Source/WebAudioBridge/`). New file
         `web_audio_bridge.cpp` implements all ~100 AIL_* functions; most
         remain no-op, but `AIL_quick_startup` returns 1, `AIL_quick_handles`
         writes a non-null fake handle (e.g. `(HDIGDRIVER)1`), and
         `AIL_allocate_sample_handle` returns a non-null handle drawn
         from a small id pool.
      2. `cmake/web.cmake`: drop `libmilesstub` from `LINK_LIBRARIES` for
         the web target, add `libwebaudiobridge`. Mirror in
         `link_mac.sh::LINK_LIBRARIES`.
      3. Engine boots with `m_audioOn = true`; `playAudioEvent` runs its
         full code path. No actual sound, but every audio path executes
         without short-circuit. This shakes out any second-order bugs
         (uninitialized state, null derefs in the audio code paths) and
         gives a baseline to add real playback to incrementally.

      **Phase 2 — 2D one-shot sounds (UI clicks, hovers)** (1 session):
      Bridge the playback subset to Web Audio. The engine's per-sound flow
      is in `MilesAudioManager::playSample` line 2787:
      ```
      AIL_init_sample(sample);
      AIL_register_EOS_callback(sample, setSampleCompleted);
      // engine calls loadFileForRead(event) → fileBuffer points at WAV bytes
      AIL_set_sample_file(sample, fileBuffer, 0);
      AIL_start_sample(sample);
      ```
      Implementation:
      - Per-handle JS-side state (`Module.AILBridge.samples[id] = { … }`)
        with the WAV pointer + length, the EOS callback fnptr, gain/pan,
        and the active `AudioBufferSourceNode` if any.
      - `AIL_set_sample_file` copies the WAV bytes from heap memory into
        a JS `Uint8Array`, calls `audioContext.decodeAudioData(bytes)`
        (async — buffer is held until decoded; if `AIL_start_sample`
        fires before decode is done, queue the start until then).
      - `AIL_start_sample` creates an `AudioBufferSourceNode`, connects to
        a per-handle `GainNode` → `audioContext.destination`,
        `source.onended = () => Module.ccall(eosCallback, ...)`.
      - `AIL_stop_sample` calls `source.stop()`, the onended fires,
        engine sees the EOS callback.
      - `AIL_set_sample_volume_pan(s, vol, pan)` updates the GainNode and
        a StereoPannerNode in the chain.
      Asyncify is on for the link, so even if `decodeAudioData`'s promise
      needs to be awaited synchronously it can be done via
      `Asyncify.handleAsync`.

      **Phase 3 — streaming (music, speech)** (1 session):
      `AIL_open_stream`/`AIL_start_stream` for `MusicZH.big`/`SpeechZH.big`
      tracks. The engine's `MilesAudioManager::playStream` uses
      `AIL_set_file_callbacks` for streaming reads from BigVFS — the bridge
      can route those reads back to the C++ side and feed a chunked
      `AudioBufferSourceNode` chain or a `MediaElementAudioSourceNode`.

      **Phase 4 — 3D positional audio** (1–2 sessions):
      `AIL_3D_*` functions. Web Audio has `PannerNode` with
      `positionX/Y/Z` and falloff curves matching the AIL_set_3D_*
      semantics fairly well. The listener is an `AudioListener` on the
      `audioContext`. Setup roughly mirrors `MilesAudioManager::playSample3D`
      line 2809. WWAudio's separate 3D path
      (`Core/Libraries/Source/WWVegas/WWAudio`, compiled into
      `libwwaudio.a` but currently NOT linked into `z_generals`) can be
      enabled at the same time — just add `target_link_libraries(z_generals
      PRIVATE z_wwaudio)` in `Main/CMakeLists.txt` + the per-frame
      `Tick_Delayed_Release_Objects()` call documented in the comment block
      at the top of `EmscriptenMain.cpp`.

      **Click-to-start (Session 11)** already calls
      `Module.SDL2.audioContext.resume()` on the first user gesture, so
      the AudioContext is hot by the time the engine attempts its first
      sound. No autoplay-policy issues to work around.

## Polish — LOW
- [x] Canvas resize handling (fullscreen / responsive) — Session 10
      (May 4 2026): JS-side `ResizeObserver` + `window.resize` listener
      with 100ms debounce; updates `canvas.width/height = clientSize ×
      devicePixelRatio` for HiDPI sharpness. ccalls `GX_OnCanvasResize`
      which calls `SDL_SetWindowSize` + `emscripten_set_canvas_element_size`.
      Engine GUI re-layout NOT yet wired (the original game was fixed-res),
      but the framebuffer matches the canvas so CSS scaling is crisp.
- [x] Loading progress bar (BigVFS mount progress → DOM) — Session 6: custom
      `web_shell.html` with `window.GeneralsX.setProgress()` JS hook + 4 stages
      (download → boot → vfs → engine → ready). C++ side calls
      `GX_ReportProgress` from `EmscriptenMain.cpp` at every stage boundary.
- [x] Error overlay for WebGL context loss — Session 10: `webglcontextlost`
      listener on the canvas calls `e.preventDefault()` and shows the
      `#gx-error` overlay with a "Reload page" button. `webglcontextrestored`
      is also handled (logged but treated as non-recoverable since the engine
      isn't designed to re-upload all GL state).
- [x] wasm-opt optimization pass — Session 11 (May 5 2026): `link_mac.sh`
      now accepts `release` as `$1`. Release profile uses `-O3 -DNDEBUG`
      and `--closure 1`, drops `-g3 -sSAFE_HEAP=1 -sASSERTIONS=2`,
      writes to `GeneralsZH-rel.{html,js,wasm}` so debug + release artefacts
      coexist on disk. Asyncify stays on (it's a structural dep, not a
      diagnostic option). Default invocation `bash link_mac.sh` is
      unchanged (debug profile).
- [x] Click-to-start gesture gate — Session 11 (May 5 2026):
      `web_shell.html` tracks any pointerdown/keydown/touchstart since
      page load. If `setProgress('ready')` arrives without a prior
      gesture, the loading overlay reveals a "Begin" button instead of
      fading out; a click on it (or on the page) finishes the handoff.
      The first-gesture handler also calls
      `Module.SDL2.audioContext.resume()` so the audio path is unblocked
      whenever a real backend lands.
