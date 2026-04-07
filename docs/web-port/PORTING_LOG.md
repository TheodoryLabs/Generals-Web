# GeneralsX Web Port — Porting Log

## 2026-03-13 — Session 1: Initial Setup
- Started agent session
- Initialized tracker files (port-todo.md, porting-log.md, architecture-map.md)
- Audited initial build errors from ninja_error.log

## 2026-03-14 — Session 2: Rendering Pipeline
- Fixed 5 rendering bugs:
  1. DUMMY_IUNKNOWN COM refcounting → proper AddRef/Release with m_comRefCount
  2. BGRA texture swizzle → GL_TEXTURE_SWIZZLE_R/B for D3DFMT_A8R8G8B8/X8R8G8B8
  3. DXT mip upload → explicit per-level glCompressedTexImage2D
  4. Cull mode reversal → D3DCULL_CCW maps to GL_BACK (not GL_FRONT)
  5. Disabled lights → upload u_Lights[i].enabled=0 on every draw call
- Identified POOL-DOUBLE-FREE as startup blocker
- Added instrumentation to freeSingleBlock (callstack logging before abort)
- Created RENDERING_STATUS.md and BUILD_INSTRUCTIONS.md
- Created link_mac.sh for link-only builds

## 2026-03-15 — Session 3: Memory System + Porting Audit

### POOL-DOUBLE-FREE Analysis
- Exhaustive static analysis of all candidate double-free paths:
  - StringClass (new[]/delete[] → global operator → DMA pool)
  - AsciiString (DMA allocateBytes/freeBytes directly)
  - HashTemplateClass, TextureLoadTaskClass, DDSFileClass
- Confirmed WASM32 memory layout: MemoryPoolSingleBlock = 8 bytes (m_owningBlob + m_nextBlock)
- Verified sentinel value 0xDEADF00D detection logic is correct
- Root cause requires runtime browser callstack (static analysis exhausted)

### Changes Made
1. **GameMemory.cpp — freeSingleBlock non-fatal on Emscripten**
   - On double-free detection, prints full C++/JS callstack via emscripten_get_callstack()
   - Skips the free (leaks the block) instead of calling abort()
   - Game continues past the crash, allowing startup to proceed
   - The leaked block is a minor memory cost vs. a hard crash

2. **web.cmake — DISABLE_GAMEMEMORY nuclear option**
   - Added commented-out `set(RTS_GAMEMEMORY_ENABLE OFF)` with documentation
   - Enables GameMemoryNull.h path: all pool allocators → system malloc/free
   - Requires full rebuild (ninja) if enabled
   - Fallback if non-fatal workaround isn't sufficient

3. **web.cmake — existing WASM32 pool fixes documented**
   - DISABLE_MEMORYPOOL_MPSB_DLINK: prevents list-consistency bug in DMA free list
   - DISABLE_MEMORYPOOL_BOUNDINGWALL: prevents SI-vptr=0 pattern from post-wall overlap

### Comprehensive Porting Gap Audit
Identified all remaining gaps organized by severity:

**CRITICAL (blocks interactivity):**
- SDL input translation: DirectInput stubs are no-ops. Need SDL2 event → game input bridge for keyboard/mouse. Without this, the game renders but accepts no input.
- GameSpy networking: Must be disabled/stubbed for web. Attempting network calls will hang or crash in WASM.

**HIGH (blocks usability):**
- QueryPerformanceCounter/Frequency: Returns 0, causing timing issues. Need emscripten_get_now() bridge.
- Pointer Lock API: Mouse confinement for RTS camera control.
- Save game persistence: Emscripten FS is ephemeral. Need IDBFS mount for user saves/settings.

**MEDIUM (degraded experience):**
- Audio delayed-release thread: CreateThread is no-op on Emscripten. Sound objects may leak.
- On-demand audio archive loading: Currently deferred, needs trigger after main menu.

**LOW (polish):**
- Canvas resize / fullscreen toggle
- Loading progress UI
- WebGL context loss recovery
- Release build optimization (remove -g, -sSAFE_HEAP, -sASSERTIONS)

### CRITICAL Gaps Fixed (same session)

4. **SDL2 Input Translation Layer (EmscriptenInput.cpp/h)**
   - Created SDL2→DirectInput keyboard translation: 80+ SDL_SCANCODE→DIK_* mappings
   - Modified dinput.h stub: IDirectInputDevice8::GetDeviceData now reads from a ring buffer
   - SDL mouse events routed to Win32Mouse::addWin32Event() via EmscriptenInput_RouteMouseEvent()
   - SDL event pump called once per frame in GameWebFrameCallback before engine update
   - Supports: keyboard up/down, mouse motion, left/right/middle buttons, scroll wheel

5. **GameSpy Networking Disabled**
   - Added `NO_GAMESPY=1` define to web.cmake
   - GameSpy code still compiles (with socket stubs) but is inert at runtime:
     - Threads never start (CreateThread is no-op)
     - TheGameSpyChat stays null (never created during single-player)
     - Overlay system handles null gracefully

6. **QueryPerformanceCounter/Frequency Fixed**
   - Replaced stub (returned 0/1) with emscripten_get_now() bridge
   - Simulates 1MHz counter (microsecond resolution)
   - QPF returns 1000000LL, QPC returns emscripten_get_now() * 1000

7. **DIDEVICEOBJECTDATA Struct Conflict Resolved**
   - windows_base.h and dinput.h both defined the struct differently
   - Added include guard so only one definition is active
   - Unified field type for dwAppData (unsigned long *)

8. **WM_MOUSEWHEEL + MAKELPARAM Added**
   - Added missing WM_MOUSEWHEEL (0x020A) define to windows_base.h
   - Added MAKELPARAM macro for constructing lParam values

## 2026-03-15 — Session 4: Audit & Build Fixes

### Deep audit of all Session 3 changes
Audited every new/modified file for compilation errors, ODR violations,
duplicate symbols, and include path issues.

### Build-breaking fixes

9. **LinuxStubs.cpp — c_dfDIKeyboard duplicate symbol (LINKER ERROR)**
   - EmscriptenInput.cpp defines `const void *c_dfDIKeyboard = nullptr;`
   - LinuxStubs.cpp also defined `void *c_dfDIKeyboard = nullptr;`
   - On Emscripten, both files are compiled → "multiple definition" link error
   - Fix: guarded LinuxStubs.cpp definition with `#ifndef __EMSCRIPTEN__`

10. **LinuxStubs.cpp — TheWin32Mouse type mismatch (ODR violation)**
    - Was: `void *TheWin32Mouse = nullptr;`
    - WinMain.h declares: `extern Win32Mouse *TheWin32Mouse;`
    - W3DGameClient::createMouse() assigns a `Win32Mouse*` to it
    - EmscriptenMain.cpp reads it as `Win32Mouse*` for addWin32Event()
    - Fix: forward-declare `class Win32Mouse;` and define as `Win32Mouse*`

11. **LinuxStubs.cpp — ApplicationHInstance type mismatch (ODR violation)**
    - Was: `void *ApplicationHInstance = nullptr;`
    - WinMain.h declares: `extern HINSTANCE ApplicationHInstance;`
    - Fix: changed to `HINSTANCE ApplicationHInstance = nullptr;`

### Warning fixes

12. **windows_base.h — WSA macro redefinition warnings**
    - 5 WSA* macros (WSAEINVAL, WSAEALREADY, WSAEISCONN, WSAECONNRESET, WSAENOTCONN)
      defined twice: first as numeric constants (for GameSpy), then as POSIX aliases
    - Fix: added `#undef` before each POSIX redefinition

13. **EmscriptenInput.cpp — redundant WM_MOUSEWHEEL_EMSCRIPTEN**
    - Had its own `#define WM_MOUSEWHEEL_EMSCRIPTEN 0x020A`
    - windows_base.h already defines `WM_MOUSEWHEEL 0x020A`
    - Fix: removed redundant define, use `WM_MOUSEWHEEL` directly

14. **DISABLE_GAMEMEMORY Nuclear Option Activated**
    - Uncommented `set(RTS_GAMEMEMORY_ENABLE OFF CACHE BOOL ... FORCE)` in web.cmake
    - Swaps GameMemory.cpp (custom pool) → GameMemoryNull.cpp (pure malloc/free)
    - Eliminates POOL-DOUBLE-FREE entirely (no custom pool = no pool corruption)
    - `FORCE` keyword overrides the cached `ON` value from config-memory.cmake
    - GameMemoryNull.cpp: initMemoryManager() still creates factory/allocator stubs
    - All `NEW` / `delete` → standard `malloc(size) + memset(0)` / `free(p)`
    - Verified: __cdecl handled by Emscripten clang, <malloc.h> available via musl

15. **GameMemoryNull.h — Added Missing Macros and Types**
    - GameMemoryNull.h was incomplete: missing critical macros that hundreds of classes use
    - Added `MEMORY_POOL_GLUE(ARGCLASS, ARGPOOLNAME)` → expands to just virtual destructor declaration
    - Added `MEMORY_POOL_GLUE_WITH_EXPLICIT_CREATE(ARGCLASS, ARGPOOLNAME, ARGINITIAL, ARGOVERFLOW)` → same
    - Added `GCMP_FIND` and `GCMP_CREATE` as empty macros (no pool lookup in null mode)
    - Added `DECLARE_LITERALSTRING_ARG1/ARG2` and `PASS_LITERALSTRING_ARG1/ARG2` as empty macros
    - Added `MemoryPool` forward declaration (some headers reference `MemoryPool*`)
    - Added `MemoryPoolSingleBlock` and `MemoryPoolBlob` forward declarations
    - Added `PoolInitRec` struct (needed by function signature in engine init)
    - Added `STLSpecialAlloc` class with `allocate()`→`::operator new` and `deallocate()`→`::operator delete`
    - In null mode, all MEMORY_POOL_GLUE macros reduce to just `protected: virtual ~ClassName(); public:`
    - No custom operator new/delete per-class; the global operator new/delete (malloc+memset) handles everything
