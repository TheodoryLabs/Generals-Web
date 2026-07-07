# Postmortem: skirmish tab crashes on Chrome stable (2026-07-06/07)

## Symptom

The game booted and the main menu worked, but the browser tab died
("Aw, Snap") shortly after — most often when starting a skirmish match,
occasionally while idling in the menu. No JavaScript exception preceded the
crash; the renderer process was killed outright. The same binary had played
cleanly the day before.

## Root cause

A V8 garbage-collector bug in Chrome stable, amplified by the game's own
console logging.

1. **The browser.** Chrome 149 stable ships a compacting-GC evacuation bug
   (V8 "preserve indirect pointer tag during GC evacuation", fixed on the
   V8 release branches in late June / early July 2026). Chrome
   150.0.7871.47 fixed a *sibling* bug but still lacks this one — the fix
   lands in V8 15.0.245.14, one tag after the 15.0.245.13 that 150 stable
   shipped with. Any 150 respin carrying V8 >= 15.0.245.14 is clean.
   An extra trap: Chrome applies updates only on full relaunch, so a
   machine can show "150" in About while the *running* process is still
   the buggier 149.

2. **The amplifier (our side).** The Release binary still emitted trace
   logging at extreme volume (`[Blit_Char]`, `GX-TRACE:`,
   `TIMEGETTIME-RAW:`, per-event audio-bridge logs) — measured at tens of
   thousands of lines during a map load. Console messages are retained by
   the browser, ballooning the JS heap to 100–200 MB of short-lived
   strings within seconds. That churn forces frequent *compacting* full
   GCs — exactly the broken operation. The crash is probabilistic, which
   is why the same build "worked yesterday."

## How it was diagnosed

- Served binary was hash-compared (sha256) against the soak-verified
  v0.5.1-web release assets: byte-identical, ruling out a code regression.
- A "black box" logger was injected into the served page: console capture,
  `window.onerror`, and a 500 ms memory heartbeat streamed to the server, so
  the final moments survive a renderer crash. It captured three crashes:
  JS heap at 100–200 MB with heavy churn, no JS error, sudden silence — a
  GC/renderer death signature, not an engine fault. The page's user agent
  also revealed the running browser was still Chrome 149 despite 150 being
  installed on disk.
- Control run on relaunched Chrome 150 + `--js-flags=--no-compact` +
  muted console spam: clean boot-to-gameplay session, ended by user
  closing the tab.

## Fixes

- **Compile trace logging out of Release builds** (this repo). The spam is
  now gated to debug builds; Release binaries no longer generate the heap
  churn that triggers the bug (and gain the perf back).
- **Workaround for affected Chrome stable (149 / 150.0.7871.47):** launch
  with `--js-flags=--no-compact` (verified still supported in 150), or use
  a browser with the fix. Documented in README known-issues.
- **Watch:** retest without the flag once a Chrome 150 respin ships V8
  >= 15.0.245.14 (check chromiumdash `fetch_version`).

## Lessons

- "Release" builds must not ship debug-grade logging; log volume is not
  just a perf issue — it changes GC behavior enough to surface browser bugs.
- Crash triage for tab deaths needs telemetry that outlives the renderer:
  the beacon black box (server-side log + memory heartbeat) found in one
  run what DevTools couldn't.
- Always confirm the *running* browser version from the page (user agent),
  not from the installed app version.

---

## Part 2 (same day, later): the fuller truth

Continued live debugging falsified parts of the account above and sharpened
the rest. Corrections, in the spirit of honest engineering:

1. **The crash is probabilistic on every build.** The "known-good" v0.5.1
   binary also crashed at match start (roughly 1 launch in 4 on the affected
   machine); the log-gated v0.5.2 build crashed nearly every launch. Single
   clean runs prove nothing — only repeated trials count. Our one triumphant
   "it works!!" run early in the day changed three variables at once and led
   the investigation astray for hours.
2. **`--js-flags=--no-compact` is only a partial shield.** Crashes were
   reproduced with the flag verified active in the browser process. The V8
   fault is in GC *evacuation*, which also runs outside full-GC compaction
   (scavenger path), so disabling compaction merely lowers the odds.
3. **Why removing log spam made it worse:** the old build's enormous
   trace output accidentally *paced* the map-load fetch loop. Each `.big`
   read is a synchronous main-thread XHR that materializes the response as
   a JS string plus a copy loop; with logging compiled out, those
   allocations hit the young generation at maximum rate with no gaps —
   exactly the conditions under which the broken evacuation path fires.
   The log spam was a splint, not the disease. (The largest single reads —
   whole-file loads such as the localization archive at match start —
   produce multi-megabyte single-gulp strings; every recorded crash died on
   precisely that fetch.)
4. **The definitive experiment:** the identical binaries were driven to
   match start repeatedly, via an automated CDP harness, on Chrome for
   Testing 150.0.7871.46 (same pre-fix V8 as the affected stable Chrome)
   and on 149.0.7827.201 (post-fix V8). Crashes reproduce only on the
   pre-fix V8; on the fixed browser every build, including the "cursed"
   v0.5.2, plays flawlessly and noticeably faster.
5. **Instrumentation contaminates.** Our crash-diagnosis console logger
   (wrapping every console call during a log storm) measurably added young-
   generation allocations at the worst moment. Diagnosis tooling for GC-
   sensitive bugs must itself be allocation-light (final version: error
   hooks + a 2s heartbeat, no console wrapping).

**Engine-side hardening that ships as a result:** the VFS now (a) serves
small reads from cached 8MB readahead windows instead of issuing hundreds
of tiny synchronous XHRs, and (b) streams reads larger than 4MB in 4MB
sub-fetches written directly into the wasm heap, so no multi-megabyte JS
string is ever materialized in one gulp. This does not fix the browser's
GC — nothing on our side can — but it removes the port's worst-case
allocation pattern, cuts map-load time, and drops the crash odds on
affected Chrome builds substantially. Players on affected Chrome versions
should still prefer a fixed browser (see README Known Issues /
`scripts/play-web.sh`).
