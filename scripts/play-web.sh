#!/bin/bash
# play-web.sh — serve the game and open it in a browser that is NOT affected
# by the Chrome 149/150-stable V8 garbage-collector crash bug (see
# docs/web-port/POSTMORTEM-2026-07-07-gc-crash.md and README "Known Issues").
#
# Usage:   ./scripts/play-web.sh [game-dir] [port]
#   game-dir: directory containing GeneralsZH.html/.js/.wasm and an assets/
#             symlink to your own game files (default: current directory)
#   port:     local port to serve on (default: 8000)
#
# The script serves the game with Range-request support and opens it in
# Chrome for Testing (a disposable, known-good Chromium build downloaded to
# ./.cft — its V8 contains the GC fix). Your regular browser and profile are
# never touched. Requires: python3, and npx (Node) for the one-time
# browser download.

set -euo pipefail

GAME_DIR="${1:-.}"
PORT="${2:-8000}"
# First Chrome-for-Testing stable line that ships the V8 GC-evacuation fix.
CFT_VERSION="149.0.7827.201"

cd "$GAME_DIR"
[ -f GeneralsZH.html ] || { echo "GeneralsZH.html not found in $GAME_DIR"; exit 1; }
[ -e assets ] || echo "WARNING: no assets/ directory or symlink here - the game needs your own game files."

# 1. Download the known-good browser once.
CFT_ROOT=".cft"
BIN=$(find "$CFT_ROOT" -type f -name "Google Chrome for Testing" -path "*MacOS*" 2>/dev/null | head -1 || true)
if [ -z "$BIN" ]; then
  BIN=$(find "$CFT_ROOT" -type f -name chrome -path "*chrome-linux*" 2>/dev/null | head -1 || true)
fi
if [ -z "$BIN" ]; then
  echo "Downloading Chrome for Testing $CFT_VERSION (one-time, ~150MB)..."
  npx --yes @puppeteer/browsers install "chrome@$CFT_VERSION" --path "$CFT_ROOT"
  BIN=$(find "$CFT_ROOT" -type f \( -name "Google Chrome for Testing" -path "*MacOS*" -o -name chrome -path "*chrome-linux*" \) | head -1)
fi
[ -n "$BIN" ] || { echo "Could not locate the downloaded browser."; exit 1; }

# 2. Serve with Range support (required for .big streaming).
if ! lsof -iTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1; then
  if [ -f serve.py ]; then
    (nohup python3 serve.py "$PORT" >/dev/null 2>&1 &)
  else
    echo "serve.py not found next to the game files - grab it from the release assets."
    exit 1
  fi
  sleep 1
fi

# 3. Launch with an isolated profile.
"$BIN" --user-data-dir="${TMPDIR:-/tmp}/gx-play-profile" \
       "http://localhost:$PORT/GeneralsZH.html" >/dev/null 2>&1 &

echo "Game launched on http://localhost:$PORT/GeneralsZH.html"
echo "(browser: Chrome for Testing $CFT_VERSION - unaffected by the stable-Chrome GC bug)"
