# Good First Issues — Seeded Task List

Twelve real, scoped tasks for new contributors, each anchored to actual code and
documentation in this repository. These are the in-repo copies of the issues seeded on
the [GitHub tracker](../../../../issues) at launch — check there for current status
before starting, and comment on the issue so work doesn't get duplicated.

Difficulty guide:

- **Easy** — one evening; mostly local, well-understood changes
- **Medium** — a weekend; requires building and verifying in-browser
- **Hard** — open-ended; for contributors who enjoy a real hunt

Before starting anything: build the project per [BUILDING.md](../../BUILDING.md) and
skim [PORTING_LOG.md](PORTING_LOG.md) — the engineering diary explains how most of
these situations came to be.

---

## 1. BigVFS: cache duplicate range fetches (Medium)

**Problem:** During boot, the same byte ranges of the same archives are fetched over
HTTP repeatedly — `INIZH.big` alone has been measured at roughly 120 fetches per boot,
costing an estimated 10–30 seconds of load time.

**Anchors:**
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/gles3_big_vfs.cpp` (the range-request VFS)
- `scripts/serve.py` (already sends long-lived `Cache-Control` for `.big` — the browser cache helps, but an in-engine cache removes the round-trips entirely)

**First steps:**
1. Instrument the fetch path in `gles3_big_vfs.cpp` to log `(archive, offset, length)` per request; count duplicates during one boot.
2. Add a small in-memory cache keyed by archive + byte range (or coalesce header/directory reads, which are the worst offenders).
3. Measure boot time before/after in Chrome DevTools (Network tab) and report numbers in the PR.

## 2. Audio: end-to-end verification of the Web Audio bridge (Medium)

**Problem:** `web_audio_bridge.cpp` (~49 KB) implements 2D sound, 3D positional audio,
and streaming, and the port TODO marks those phases complete — but no build has ever
been *verified* playing audio in a browser. Additionally, `WWAudio` has no CMake target
on the web path: only its `Threads.cpp` is compiled directly into the web target, so
the full wiring is unproven.

**Anchors:**
- `Core/Libraries/Source/WebAudioBridge/web_audio_bridge.cpp`
- `cmake/miles.cmake` (compiles the bridge as the `milesstub` target under Emscripten)
- `GeneralsMD/Code/Main/CMakeLists.txt` (line ~148: the direct `WWAudio/Threads.cpp` compile)
- [PORT_TODO.md](PORT_TODO.md) (audio section)

**First steps:**
1. Do a clean build, mount `AudioZH.big` / `MusicZH.big` / `SpeechZH.big` from your own game copy, and check the console for bridge initialization.
2. Test: menu click sounds, shell music, unit responses, 3D panning in a skirmish.
3. Document what works and what doesn't; file follow-up issues per gap. If nothing plays, the CMake wiring of WWAudio is the first suspect.

## 3. Shell map: fix unaligned reads in the map chunk parser (Hard)

**Problem:** The animated 3D main-menu background ("shell map") is opt-in behind
`?shellmap=1` because EA's map chunk parser performs unaligned memory reads that abort
under WebAssembly. Fixing the parser makes the menu look like the real game by default.

**Anchors:**
- `GeneralsMD/Code/Main/EmscriptenMain.cpp` (line ~808: the shellmap override and `?shellmap=1` query-string gate)
- The map chunk parsing code in the `GameEngine` data-chunk readers (search for `DataChunk`)

**First steps:**
1. Build, run with `?shellmap=1`, and capture the abort stack trace from the console.
2. Identify the struct reads that assume alignment; fix with `memcpy`-based reads or packed accessors.
3. Verify the shell map renders, then flip the default and remove the gate.

## 4. WebRTC transport on the virtual-UDP seam (Hard — help wanted)

**Problem:** Multiplayer. The engine-side seam already exists: virtual UDP send/receive
hooks (`GX_Network_EnableVirtual`, `GX_Network_PushPacket`, `GX_Network_SetVirtualIP`)
and a JS tap (`window.GeneralsX.onSendPacket`). What's missing is the JS/WebRTC
data-channel counterpart that carries those packets between two browsers.

**Anchors:**
- `Core/GameEngine/Source/GameNetwork/udp.cpp` (the virtual-UDP seam)
- `GeneralsMD/Code/Main/EmscriptenMain.cpp` (exported symbols)

**First steps:**
1. Wire a loopback harness first: two tabs on one machine exchanging packets via `BroadcastChannel`, no WebRTC yet.
2. Prove a LAN-lobby skirmish starts over the loopback path.
3. Swap the transport to an unreliable-mode `RTCDataChannel` with a minimal signaling story (manual SDP copy/paste is fine for v1).

## 5. POOL-DOUBLE-FREE: root-cause the latent allocator bug (Hard)

**Problem:** Early in the port, a double-free in the engine's custom pool allocator
aborted startup; it was masked by making `freeSingleBlock` non-fatal (it leaks the
block instead of aborting). The custom pool is now re-enabled in clean builds, so the
underlying bug deserves a real root cause.

**Anchors:**
- `Core/GameEngine/Source/Common/System/GameMemory.cpp` (`freeSingleBlock`, sentinel detection, the non-fatal Emscripten path)
- `cmake/web.cmake` (lines ~100–105: memory-pool configuration)
- [PORTING_LOG.md](PORTING_LOG.md) Sessions 2–4 (the full investigation history: `StringClass`/`AsciiString` candidates, WASM32 block layout, sentinel logic)

**First steps:**
1. Build with the pool enabled and capture the `POOL-DOUBLE-FREE` console callstack (the diagnostic prints a full JS+C trace before skipping the free).
2. Identify the owning type from the dumped block bytes (the instrumentation prints them as `StringClass`/`AsciiString` interpretations).
3. Fix the double delete; then remove the leak-instead-of-abort workaround.

## 6. Port the historic script-pipeline build truth into CMake (Medium)

**Problem:** The last hand-verified builds were produced by `link_mac.sh` with flags
that were only partially back-ported to CMake. Three places define Emscripten link
flags (`cmake/web.cmake`, `GeneralsMD/Code/Main/CMakeLists.txt`, `scripts/link_mac.sh`)
and they historically disagreed on exported functions, memory sizes, `-lidbfs.js`, and
the optimized release profile (`-O3 --closure 1 -sMALLOC=emmalloc`). Reconcile them so
CMake is the single source of truth, including a release profile.

**Anchors:**
- `scripts/link_mac.sh` (ground truth of the last script-built release)
- `cmake/web.cmake` and `GeneralsMD/Code/Main/CMakeLists.txt` (the two CMake layers)

**First steps:**
1. Diff the three flag sets; note that duplicate `-s` flags don't merge — the last one on the link line silently wins.
2. Move everything into one CMake location; add a release option (e.g. `-DGX_WEB_RELEASE=ON`).
3. Build both profiles clean, verify both in-browser, then retire `link_mac.sh` to an archive note.

## 7. Fix web.cmake's self-contradictory memory-pool comment (Easy)

**Problem:** In `cmake/web.cmake` (~lines 100–105), a comment block describing the
"NUCLEAR OPTION: bypass the custom memory pool" sits directly above
`set(RTS_GAMEMEMORY_ENABLE ON ...)` — which does the *opposite* (the cache string even
says "Disable GameMemory pool on web" while setting it `ON`). Pure comment/docs fix
with high confusion-prevention value.

**Anchors:**
- `cmake/web.cmake` (lines ~100–105)

**First steps:**
1. Rewrite the comment and cache-variable description to say what the code actually does now (pool enabled) and why.
2. Cross-check any docs in `docs/web-port/` that still describe the pool as disabled.

## 8. Re-enable visibility-pause with iframe detection (Easy)

**Problem:** `WebVisibility::Init()` is commented out in `EmscriptenMain.cpp` because
`document.hidden` is permanently `true` inside iframes, which froze the game when
embedded. The battery-friendly pause should come back for normal top-level tabs.

**Anchors:**
- `GeneralsMD/Code/Main/EmscriptenMain.cpp` (lines ~617–630: the disabled `WebVisibility::Init()` and the explanation)

**First steps:**
1. Gate the pause on being top-level: only honor `visibilitychange` when `window.self === window.top`.
2. Re-enable `WebVisibility::Init()`, verify pause/resume in a normal tab, and verify no freeze when the page is iframed.

## 9. Root-fix the music-check quit-suicide (Easy)

**Problem:** `GameEngine.cpp` line ~624 quits the game if `isMusicAlreadyLoaded()`
fails (originally a disc check). The web port defeats it with a blanket
`setQuitting(FALSE)` after init. Once audio archives mount and verify (issue 2), the
check should pass legitimately and the override should be removed.

**Anchors:**
- `GeneralsMD/Code/GameEngine/Source/Common/GameEngine.cpp` (line ~624)
- `GeneralsMD/Code/Main/EmscriptenMain.cpp` (lines ~648–656: the override and its comments)

**First steps:**
1. With audio archives mounted, log what `isMusicAlreadyLoaded()` actually returns and why.
2. Fix the underlying path (music asset visible to the audio system at check time).
3. Delete the `setQuitting(FALSE)` override and verify boot still succeeds — including the failure mode with *missing* audio archives (should degrade gracefully, not quit).

## 10. serve.py: add COOP/COEP headers and align the docs (Easy)

**Problem:** [SETUP.md](../../SETUP.md) tells users the bundled server sets
`Cross-Origin-Opener-Policy` headers and prints a port-8888 banner — but
`scripts/serve.py` sets neither COOP nor COEP, defaults to port 8000, and prints a
different message. Align the server and both docs (SETUP.md and BUILDING.md) on one
truthful story.

**Anchors:**
- `scripts/serve.py`
- `SETUP.md` (steps 3–4 and the SharedArrayBuffer troubleshooting entry)

**First steps:**
1. Add `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp` headers to `serve.py` (needed if/when SharedArrayBuffer is used; harmless otherwise) — or, if they break asset loading, fix the docs instead.
2. Make the startup banner and default port match what the docs promise.
3. Verify a full boot through the updated server in Chrome and Firefox.

## 11. Remove the boot-time VFS self-test probes (Easy)

**Problem:** `EmscriptenMain.cpp` runs a battery of diagnostic VFS probes on every
boot (`VFS-PROBE:` console lines, test reads of menu window files, etc.). They were
added during black-canvas triage with the note "remove once the menu renders." The
menu renders.

**Anchors:**
- `GeneralsMD/Code/Main/EmscriptenMain.cpp` (lines ~680–730: the `probes[]` array and `VFS-PROBE` logging)
- [PORTING_LOG.md](PORTING_LOG.md) (the triage sessions that added them)

**First steps:**
1. Delete (or compile-time-gate behind a `GX_VFS_DIAGNOSTICS` define) the probe block.
2. Verify boot-to-menu still works and the console is quieter.
3. Measure whether boot gets faster — each probe is a synchronous VFS read.

## 12. Engine GUI re-layout on canvas resize (Medium)

**Problem:** Framebuffer resizing works — `GX_OnCanvasResize` adjusts the GL viewport —
but the engine's GUI does not re-lay out: the original game was fixed-resolution, so
`.wnd` window layouts never re-initialize on the fly. Resizing the browser window
leaves UI elements misplaced/misscaled.

**Anchors:**
- `GeneralsMD/Code/Main/EmscriptenMain.cpp` (line ~268: `GX_OnCanvasResize`)
- [PORT_TODO.md](PORT_TODO.md) ("Polish — canvas resize handling")
- The `.wnd` layout system under `GeneralsMD/Code/GameEngine/` (`WindowLayout`, GameWindow manager)

**First steps:**
1. Trace what the engine does with resolution at startup (where `.wnd` layouts get instantiated against a resolution).
2. Investigate whether the in-game "resolution change" path (options menu) can be invoked from `GX_OnCanvasResize`.
3. Prototype with the main menu only; measure what breaks in-game.

---

*This list was seeded from the July 2026 pre-launch audit. When one of these lands,
update or remove its entry here and close the corresponding tracker issue.*
