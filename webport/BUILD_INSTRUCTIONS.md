# GeneralsX — Link-Only Build Instructions (Mac)

## What this does

Runs *only* the final `em++` link step on your Mac.  All C++ compilation is
already done — the pre-compiled `.o` files and instrumented `.a` archives are
already in `build-web/`.  This avoids triggering a full multi-hour recompile
and produces a new `GeneralsZH.{html,js,wasm}` that contains the
double-free diagnostic instrumentation.

The instrumented build will print to the browser console:

- `POOL-ALLOC-32 block=0x…` — every 32-byte DMA pool allocation
- `POOL-FREE-32  block=0x…` — every 32-byte DMA pool free
- `TRACE: GT …` / `TRACE: TC-ctor …` / `TRACE: Set_Texture_Name …` — texture
  loading steps
- **`POOL-DOUBLE-FREE pool=… callstack: …`** — fires right before `abort()`
  and prints a full JS+C stack trace identifying the bug

---

## Prerequisites

- emsdk already installed at
  `/Users/builduser/GeneralsX-build/emsdk`
  (you've been building with it, so it should be there)
- ~16 GB free RAM — wasm-opt asyncify is memory-hungry

---

## Step 1 — Run the link script

Open Terminal, then:

```bash
cd /Users/builduser/GeneralsX-build
bash link_mac.sh
```

This will take **5–15 minutes** (dominated by `wasm-opt` asyncify
instrumentation).  Progress is logged to `build-web/link_mac.log`.

When it finishes successfully you'll see:

```
=============================================
 LINK SUCCEEDED
=============================================
  GeneralsMD/GeneralsZH.html    …  bytes
  GeneralsMD/GeneralsZH.js      …  bytes
  GeneralsMD/GeneralsZH.wasm    …  bytes
```

---

## Step 2 — Deploy

Copy the three output files to your web server:

```
build-web/GeneralsMD/GeneralsZH.html
build-web/GeneralsMD/GeneralsZH.js
build-web/GeneralsMD/GeneralsZH.wasm
```

The server must serve `.wasm` with `Content-Type: application/wasm` and
support HTTP Range requests for the `.big` VFS to work.

---

## Step 3 — Collect the crash log

1. Open the page in Chrome/Firefox with DevTools open (Console tab).
2. Let the game start loading.  The console will be noisy with `POOL-ALLOC-32`
   and `POOL-FREE-32` lines — that's expected.
3. When the double-free hits you'll see something like:

```
POOL-DOUBLE-FREE pool=dmaPool_32 userdata=0x12345678 next=0xABCDEF01 usedInBlob=1
  raw bytes [0..15]: 41 72 74 00 ...
  as-StringClass (skip 8B hdr): Art...
  as-AsciiString (skip 4B hdr): ...
POOL-DOUBLE-FREE callstack:
  at freeSingleBlock (GeneralsZH.wasm:...)
  at operator delete[] (...)
  at StringClass::~StringClass (...)
  ...
```

4. **Copy the entire POOL-DOUBLE-FREE block** (including the callstack) and
   paste it into the chat.  That's all that's needed to identify the bug.

---

## If the link fails

Check the last 100 lines of `build-web/link_mac.log`:

```bash
tail -100 /Users/builduser/GeneralsX-build/build-web/link_mac.log
```

Common causes:
- **OOM / SIGKILL**: close other apps and retry; wasm-opt needs ~12 GB
- **Missing .o file**: the two `EmscriptenMain.cpp.o` / `LinuxStubs.cpp.o`
  inputs must exist in `build-web/GeneralsMD/Code/Main/CMakeFiles/z_generals.dir/`

---

## DO NOT run `ninja` directly

Running `ninja` would notice the missing PCH files for `z_gameengine` and
`z_ww3d2`, rebuild them, then consider all `.o` files stale and trigger a
complete multi-hour recompile — overwriting the instrumented objects.

Use `link_mac.sh` only.
