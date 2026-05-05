# GeneralsX Web Port — Rendering & Build Status
## Updated: 2026-03-15

---

## Session 3 Changes (2026-03-15)

### POOL-DOUBLE-FREE — Non-Fatal Workaround
The POOL-DOUBLE-FREE crash that blocked startup has been made **non-fatal** on Emscripten:
- `freeSingleBlock()` now prints the full C++/JS callstack via `emscripten_get_callstack()`
- Instead of `abort()`, the double-free is **skipped** (the block is leaked but the game continues)
- This allows startup to proceed past the crash point

**The root cause is still unknown.** The callstack printed on next run will identify the exact code path. Once identified, the proper fix is to prevent the double-free from occurring.

### DISABLE_GAMEMEMORY Nuclear Option
If the non-fatal workaround is insufficient (e.g., too many double-frees cause excessive leaks or cascading corruption), a nuclear option is ready in `cmake/web.cmake`:
```cmake
set(RTS_GAMEMEMORY_ENABLE OFF CACHE BOOL "Disable GameMemory pool on web" FORCE)
```
This routes all allocations through system `malloc`/`free`, completely bypassing the custom pool system. Requires a full rebuild.

---

## Bugs Fixed (Session 2 — 2026-03-14)

### 1. `DUMMY_IUNKNOWN` — no-op COM refcounting (d3d8.h)
**Was:** `AddRef()` returned 0 (no-op); `Release()` returned 0 (no-op).
Every D3D texture, vertex buffer, and index buffer created via `CreateTexture` /
`CreateVertexBuffer` / `CreateIndexBuffer` was **never freed**, leaking all GL
resources for the lifetime of the game.
**Fix:** Replaced all three macros with proper COM refcounting
(`++m_comRefCount` / `--; delete at 0`). Each concrete class now carries
`ULONG m_comRefCount = 1`.

### 2. BGRA texture colour swizzle (gles3_wrapper.cpp — `Create_Texture`)
**Was:** All uncompressed textures uploaded as `GL_RGBA/GL_UNSIGNED_BYTE` regardless of D3D format. BGRA pixels had swapped red/blue channels.
**Fix:** Per-format swizzle via `GL_TEXTURE_SWIZZLE_R/B` for A8R8G8B8, X8R8G8B8, L8, A8L8, A8.

### 3. DXT mip-level upload (d3d8.h — `Upload_To_GL`)
**Was:** Only mip level 0 uploaded + `glGenerateMipmap` on compressed base (unreliable in WebGL2).
**Fix:** Explicit per-level `glCompressedTexImage2D` for DXT textures with LevelCount > 1.

### 4. Cull-mode reversal (gles3_wrapper.cpp — `CullMode_To_GL`)
**Was:** D3DCULL_CCW → GL_FRONT (wrong), D3DCULL_CW → GL_BACK (wrong). All geometry inside-out.
**Fix:** D3DCULL_CCW → GL_BACK, D3DCULL_CW → GL_FRONT.

### 5. Disabled lights staying active (gles3_wrapper.cpp — `Apply_Uniforms`)
**Was:** `LightEnable(i, FALSE)` didn't upload `u_Lights[i].enabled = 0`.
**Fix:** Added else branch to zero the uniform for disabled lights.

---

## Rendering Path Status

| Component | Status |
|-----------|--------|
| WebGL2 context creation | ✓ |
| VAO + default GL state | ✓ |
| FVF vertex attribute decode | ✓ |
| Vertex shader (transform + lighting + fog) | ✓ |
| Fragment shader (texture stages, alpha test, fog) | ✓ |
| BGRA vertex colour swizzle in shader | ✓ |
| BGRA texture colour swizzle | ✓ |
| Texture refcounting | ✓ |
| DXT mip upload | ✓ |
| Index buffer (16-bit) | ✓ |
| Cull mode mapping | ✓ |
| Light enable/disable | ✓ |
| Depth test / depth write | ✓ |
| Blend state | ✓ |
| Alpha test (emulated in shader) | ✓ |
| BigVFS (.big archive HTTP range requests) | ✓ |
| TGA / DDS texture loading pipeline | ✓ |
| **POOL-DOUBLE-FREE** | ⚠️ non-fatal workaround (needs root-cause fix) |
| **SDL input → game input bridge** | ✓ SDL2→DirectInput keyboard + Win32Mouse events |
| **GameSpy networking disabled** | ✓ NO_GAMESPY=1 define (inert at runtime) |
| **QueryPerformanceCounter** | ✓ emscripten_get_now() at 1MHz |

---

## Next Steps (Priority Order)

1. **Run the build** and capture the POOL-DOUBLE-FREE callstack from browser console
2. **Pointer Lock** — mouse confinement for RTS camera
3. **Save persistence** — IndexedDB via IDBFS
4. **Audio on-demand loading** — mount AudioZH.big etc. after main menu
5. **Release build optimization** — remove -g, -sSAFE_HEAP, -sASSERTIONS

---

## Build Instructions
See `BUILD_INSTRUCTIONS.md` for full details.

**Session 3 added new source files (EmscriptenInput.cpp) and modified headers (dinput.h, windows_base.h).
A FULL rebuild via `ninja` is required — `link_mac.sh` only re-links and will NOT compile new/changed files.**

```bash
cd /Users/builduser/GeneralsX-build/build-web
ninja                     # full recompile + link (10–20 min)
```

Output: `build-web/GeneralsMD/GeneralsZH.{html,js,wasm}`

After building, serve the files and open in Chrome with DevTools Console open to capture the POOL-DOUBLE-FREE callstack.
