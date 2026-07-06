# Bugs and Fixes

## Active Issues
None at the moment. Black-canvas was fixed in Session 12 (see Solved Issues).

## Last successful builds

- `build-web/GeneralsMD/GeneralsZH.{html,js,wasm}` — May 5 2026 (Session 12)
  (debug profile via `bash link_mac.sh`; ~31KB / 559KB / 91MB; includes
  Sessions 5–12 source edits; engine boots to main loop; the black canvas
  seen here was fixed later in Session 12 — see Solved Issues)
- `build-web/GeneralsMD/GeneralsZH-rel.{html,js,wasm}` — May 5 2026
  (release profile via `bash link_mac.sh release`; 11KB / 128KB / 17MB;
  closure-minified, `-O3`, no SAFE_HEAP/ASSERTIONS — deployable)
- `build-html5/GeneralsMD/GeneralsZH.{html,js,wasm}` — Mar 31 2026 12:29
  (older, predates Session 5+ edits)
- Distributed copy in `GeneralsX/dist/GeneralsZH.{html,js,wasm}` — Apr 7 2026 16:49
  (older, predates Session 5+ edits)

The link errors documented in `<build-root>/build.log` (Mar 13 2026) and `build_error.log` (Mar 10 2026) are **stale** — they predate the Session 4 fixes that landed on Mar 15 (and the Mar 31 build that succeeded). When picking up the project, always re-run the build before treating those logs as live.

## Latent / Watch
- **POOL-DOUBLE-FREE** — *update Jul 2026: the custom pool was re-enabled after the bounding-wall root cause was fixed (`web.cmake` sets `RTS_GAMEMEMORY_ENABLE ON`), so this watch item is live again.* Originally masked by `DISABLE_GAMEMEMORY`. All allocations go through `malloc`/`free`. Root cause was never identified; a future investigation would need the callstack `freeSingleBlock()` prints right before it skips the offending free.
- **WinMain.cpp not included on non-Windows** — `GeneralsMD/Code/Main/CMakeLists.txt` gates `WinMain.cpp` on `$<BOOL:${WIN32}>`, so `LinuxStubs.cpp` is the source of truth for `TheMessageTime`, `ApplicationHInstance`, and (on Linux) `c_dfDIKeyboard`. Keep the types in sync with `WinMain.h`.

## Solved Issues

### `BaseVertexIndex` not applied → wrong UVs / positions / no 3D (Session 12, May 5 2026)
- After the SetStreamSource fix landed pixels on screen, the menu's images appeared cut up incorrectly, in wrong positions, and the 3D scene was missing. Cause: WebGL2 / OpenGL ES 3.0 has no `glDrawElementsBaseVertex`, but the W3D engine's dynamic VB allocator returns a `VertexBufferOffset` (in vertices) and routes it through `SetIndices(BaseVertexIndex=offset)`. In real D3D8 the offset is added to every fetched index; the stub stored `base_vtx_index` but never applied it, so `glDrawElements` always read from offset 0 of the shared VBO. Every draw fetched whichever vertices the first allocation of the frame happened to write — explaining "wrong UVs + wrong positions + 3D missing" all at once.
- **Fix**: in `Emscripten_IDirect3DDevice8::_setup_vertex_attribs`, compute `base_vtx_index * stride` and pass it as a new `base_offset_bytes` parameter to `GLES3_FVFDecoder::Apply_Vertex_Attribs`. Inside the decoder, add the base offset to every per-attribute offset before passing to `glVertexAttribPointer`. This shifts the attribute base pointer forward so index 0 reads OUR first vertex.

### d3d8 stub `SetStreamSource` clobber → black canvas (Session 12, May 5 2026)
- The W3D engine's `DX8Wrapper::Apply_Render_State_Changes` calls `SetStreamSource(i, vb, ...)` for every stream in a `MAX_VERTEX_STREAMS` loop — stream 0 with the real VB, streams 1..N-1 with `nullptr`. The Emscripten d3d8 stub at `emscripten_compat/d3d8.h` only models a single bound stream via a single `current_vb` pointer, so the trailing null calls for streams 1+ kept clobbering stream 0. Result: every `DrawIndexedPrimitive` call short-circuited on `!current_vb` and no geometry ever reached the GPU. Confirmed via diagnostic counters: `SetStreamSource: 7952 (null=3976), dip:called=3976 no_vb=3976 drew=0` — exactly half the SetStreamSource calls were null.
- **Fix**: filter `Emscripten_IDirect3DDevice8::SetStreamSource` to stream 0 only — `if (StreamNumber != 0) return 0;`. The W3D path doesn't use multi-stream geometry. After fix: `dip:called=3976 no_vb=0 drew=3976`. Menu visuals (C&C Generals logo, asset icons) render correctly.

### Main loop frozen by visibility-pause + rAF throttling (Session 12, May 5 2026)
- Initial in-browser test showed the main loop ticking but only producing clears + viewport sets, no draws. Two compounding causes:
  - `WebVisibility::Init` registered an `emscripten_set_visibilitychange_callback` that calls `WebMainLoop::Pause()` whenever `document.hidden` flips true. In iframe embeds (Claude Preview, etc.) `document.hidden` is *always* true, so the loop's first guard short-circuited every frame.
  - Even after #1, the loop ran via `requestAnimationFrame` (because `WebMainLoop::Start(cb, 0, true)` passes `target_fps=0` which Emscripten maps to rAF). Chrome aggressively throttles rAF in iframes to ~0Hz when `document.hidden=true`.
- **Fix**: commented out `WebVisibility::Init();` in `EmscriptenMain.cpp::main()` and changed `WebMainLoop::Start(GameWebFrameCallback, 0, true)` to `... 60, true)`. Non-zero fps makes Emscripten use `setTimeout` (still throttled to ~1Hz in iframes but at least keeps ticking; full 60Hz on a top-level tab).

### Engine quit-suicide on missing music asset (Session 12, May 5 2026)
- First in-browser run aborted with `Aborted(native code called abort())` shortly after `GameEngine::init() complete`. Console traced this to `INFO: GameEngine requested quit — stopping main loop` immediately after `Window lost focus`. Root cause: `GameEngine.cpp:624` calls `if (!TheAudio->isMusicAlreadyLoaded()) setQuitting(TRUE);` and `isMusicAlreadyLoaded()` walks `m_allAudioEventInfo` for an `AT_Music` entry then verifies `TheFileSystem->doesFileExist(filename)`. With milesstub backing audio and `MusicZH.big` 404'ing, the check fails → engine kills itself before the first frame paints.
- **Fix**: `EmscriptenMain.cpp` now calls `TheGameEngine->setQuitting(FALSE)` immediately after `TheGameEngine->init()`. Override is a no-op once a real audio backend lands and `isMusicAlreadyLoaded` returns true on its own.

### Engine `setQuitting` causes abort in destruction path (Session 12)
- Side-effect of #1: when `m_quitting=TRUE` was set during init, the next frame entered the main loop's `getQuitting()` branch and `delete TheGameEngine` aborted somewhere in the subsystem destruction chain (root cause of the `abort()` we saw — the engine wasn't ready to be torn down). Clearing the quit flag post-init avoids this entirely.

### Asset symlink pointing at near-empty asset directory (Session 12)
- `build-web/GeneralsMD/assets` was symlinked to `Downloads/GeneralsZH-web/assets` which only contained `PatchINI.big`, `ShadersZH.big`, `shaders.big`, and an empty `deferred/`. The actual asset set lives at `Downloads/GeneralsZH-web-fresh/assets` (16 archives). BigVFS's startup mounts of `INI.big`, `Textures.big`, etc. were silently failing for ~80% of the archives. Re-pointed the symlink so the engine actually has data to mount.

### Python's stdlib SimpleHTTPRequestHandler doesn't honour Range (Session 12)
- BigVFS reads `.big` archive headers via short HTTP Range requests; Python 3.14's `SimpleHTTPRequestHandler` returns 200 with the full content even when given `Range: bytes=0-15`. Dropped a custom `serve.py` that parses the Range header, replies with 206 + Content-Range + the requested byte slice, and adds proper CORS / `application/wasm` MIME headers.

### Error-overlay bleed-through (Session 12, cosmetic)
- Engine-fault overlay used `rgba(10,0,0,0.92)` background, so the dimmed loading overlay (faded title + Begin button) bled through. Fixed by hiding the loading overlay (`overlay.style.display = 'none'`) inside `window.GeneralsX.showError()`.

### Closure compiler ADVANCED_OPTIMIZATIONS undeclared `allocate`/`ALLOC_NORMAL` (May 5 2026)
- `bash link_mac.sh release` failed at the closure pass: `JSC_UNDEFINED_VARIABLE: variable allocate is undeclared` / `variable ALLOC_NORMAL is undeclared`. Source: SDL2's `SDL_assert.c` emits an `EM_ASM` that calls `allocate(intArrayFromString(reply), "i8", ALLOC_NORMAL)` — those names are Emscripten's legacy allocator API and aren't declared in the closure-visible JS unless explicitly exported.
- **Fix**: appended `allocate,ALLOC_NORMAL,intArrayFromString` to the release-mode `EXPORTED_RUNTIME_METHODS` in `link_mac.sh`. Debug mode is unchanged (closure isn't run there). Also restructured the script so `EXPORTED_RUNTIME_METHODS` is per-mode rather than in the shared common block — repeated `-s X=…` flags don't merge, so the per-mode override would have been silently dropped if the common version came after.

### EM_ASM macro splitting on inline JS object literal (May 5 2026)
- `EmscriptenMain.cpp::GX_FlushIdbfs` (added Session 9) wrapped a JS body containing `Module.GX_idbfsState = { busy: false, pending: false };` in an `EM_ASM({ ... }, reason);` call. The C preprocessor splits macro args at top-level commas regardless of `{}` nesting, so the comma inside the object literal split the `code` argument in two and the JS body got parsed as C++ — failing with "use of undeclared identifier 'pending'", "unknown type 'st'", and a cascade of similar errors. The build never tripped over this in practice because Sessions 5–10 were all source-only edits and no rebuild was attempted between Session 9 (May 4) and Session 11 (May 5).
- **Fix**: wrap the body in extra parens — `EM_ASM(({ ... }), reason);` — the documented workaround in `<emscripten/em_asm.h>` (next to the `_EM_ASM_PREP_ARGS` definition).
- Audited the other 5 EM_ASM blocks in the file; they only use commas inside JS function-call parens, which the C preprocessor *does* respect, so they didn't need wrapping.

### link_mac.sh missing EmscriptenInput.cpp.o (May 5 2026)
- `link_mac.sh`'s `MAIN_OBJS` array listed `EmscriptenMain.cpp.o` and `LinuxStubs.cpp.o` but not `EmscriptenInput.cpp.o`. The actual ninja link line for `GeneralsZH.html` lists ALL THREE, and `EmscriptenInput.cpp.o` defines a critical chunk of the input layer (the SDL→DirectInput pump, `c_dfDIKeyboard`, the keyboard ring buffer). Running `link_mac.sh` as written would have failed with unresolved symbols.
- **Fix**: added `EmscriptenInput.cpp.o` to `MAIN_OBJS` with a comment explaining that it's not bundled into any `.a` so it must be passed directly. Confirmed link succeeds.

### Stale build.log triage (May 4 2026)
- The build error log under `<build-root>/build.log` reports `undefined symbol: TheMessageTime / ApplicationHInstance / c_dfDIKeyboard`. This is from a Mar 13 2026 ninja run, before the Session 4 stub additions. Verified the post-fix .o files and confirmed:
  - `LinuxStubs.cpp.o` defines `TheMessageTime`, `ApplicationHInstance`, `TheWin32Mouse` in `.bss.*` sections.
  - `EmscriptenInput.cpp.o` defines `c_dfDIKeyboard` in `.bss.c_dfDIKeyboard`.
  Both .o files are in the link line and the Mar 31 build linked them successfully.

### LinuxStubs.cpp guard cleanup (May 4 2026)
- The Win32 stub globals (`TheMessageTime`, `ApplicationHInstance`, `c_dfDIKeyboard`) were sitting **outside** the `#ifndef _WIN32` block at the bottom of `LinuxStubs.cpp`, which would have produced duplicate-symbol link errors on a native Windows build (where `WinMain.cpp` defines them). Moved them inside the guard.
- While moving them, fixed the type of `TheMessageTime` from `int` → `DWORD` to match the `extern DWORD TheMessageTime;` declaration in `Win32GameEngine.cpp`. Same 32-bit width on WASM32, but the new declaration matches the extern.
- Fixed the type of `c_dfDIKeyboard` (non-Emscripten branch) from `void *` → `const void *` to match `dinput.h`'s `extern const void *c_dfDIKeyboard;`.
