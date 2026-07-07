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
