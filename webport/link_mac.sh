#!/usr/bin/env bash
# =============================================================================
# link_mac.sh — Link-only step for GeneralsX WebAssembly build (Mac)
#
# Run this from your Mac terminal.  It performs ONLY the final em++ link step,
# using the pre-compiled .o files and instrumented .a archives that are already
# in place.  It deliberately skips all CMake/ninja compile steps so that:
#   • No PCH is rebuilt (which would trigger a full multi-hour recompile)
#   • The instrumented GameMemory.cpp.o / assetmgr.cpp.o / texture.cpp.o
#     that are already baked into the archives are used as-is
#
# Output (debug, default):    build-web/GeneralsMD/GeneralsZH.{html,js,wasm}
# Output (release): build-web/GeneralsMD/GeneralsZH-rel.{html,js,wasm}
#
# Usage:
#   bash link_mac.sh           # debug build (current default)
#   bash link_mac.sh release   # optimised build for distribution
# =============================================================================

set -euo pipefail

# Build mode — pass `release` as $1 to switch from the default debug profile
# (asyncify + SAFE_HEAP + g3 + ASSERTIONS=2) to an optimisation-heavy profile
# suitable for distribution: -O3, no debug symbols, ASSERTIONS=0, JS minified
# via Closure. Release output is written to GeneralsZH-rel.{html,js,wasm} so
# both artefacts can coexist on disk.
MODE="${1:-debug}"
case "${MODE}" in
  debug|release) ;;
  *) echo "ERROR: unknown mode '${MODE}' — use 'debug' (default) or 'release'"
     exit 1 ;;
esac

BUILD_DIR="/Users/builduser/GeneralsX-build/build-web"
EMSDK_DIR="/Users/builduser/GeneralsX-build/emsdk"
EMPP="${EMSDK_DIR}/upstream/emscripten/em++"
LOG="${BUILD_DIR}/link_mac.${MODE}.log"

export EM_CONFIG="${EMSDK_DIR}/.emscripten"

echo "============================================="
echo " GeneralsX Mac Link Step (${MODE})"
echo " Build dir : ${BUILD_DIR}"
echo " em++      : ${EMPP}"
echo " Log       : ${LOG}"
echo "============================================="
echo ""

if [ ! -x "${EMPP}" ]; then
    echo "ERROR: em++ not found at ${EMPP}"
    echo "Make sure emsdk is installed at ${EMSDK_DIR}"
    exit 1
fi

cd "${BUILD_DIR}"

# Main translation-unit object files (already compiled, not rebuilt here).
# Match the ninja link line for z_generals exactly — EmscriptenInput.cpp.o is
# NOT bundled into any .a archive, so dropping it here produces unresolved
# symbols (EmscriptenInput_PumpEvents, c_dfDIKeyboard, the keyboard ring
# buffer globals, etc.).
MAIN_OBJS=(
    "GeneralsMD/Code/Main/CMakeFiles/z_generals.dir/EmscriptenMain.cpp.o"
    "GeneralsMD/Code/Main/CMakeFiles/z_generals.dir/EmscriptenInput.cpp.o"
    "GeneralsMD/Code/Main/CMakeFiles/z_generals.dir/LinuxStubs.cpp.o"
)

# Custom HTML shell with the GeneralsX progress UI. Points at the source-tree
# copy so the link picks up the latest version each time.
WEB_SHELL="/Users/builduser/GeneralsX-build/GeneralsX/GeneralsMD/Code/Main/web_shell.html"

# Common flags shared between debug and release. NOTE:
# EXPORTED_RUNTIME_METHODS is per-mode (not in COMMON_LINK_FLAGS) because the
# release build needs the legacy allocator helpers exported to satisfy
# Closure's ADVANCED_OPTIMIZATIONS pass — see the release block below.
COMMON_LINK_FLAGS=(
    -sUSE_WEBGL2=1 -sFULL_ES3=1
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
    -sINITIAL_MEMORY=268435456 -sALLOW_MEMORY_GROWTH=1
    -sMAXIMUM_MEMORY=536870912
    -sFETCH=1 -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=8388608
    -fexceptions -sDISABLE_EXCEPTION_CATCHING=0
    -sFORCE_FILESYSTEM=1 -sEXIT_RUNTIME=0
    -sWASM=1 -sENVIRONMENT=web
    -s USE_SDL=2
    -sEXPORTED_FUNCTIONS=_main,_EmscriptenInput_OnPointerLockChange,_GX_FlushIdbfs_Tab,_GX_OnCanvasResize
    --shell-file "${WEB_SHELL}"
)

if [ "${MODE}" = "release" ]; then
    # Distribution profile — optimisation, no debug symbols, no
    # safety/assertion overhead, minified JS via Closure. Asyncify is still on
    # because it's a structural dep (engine relies on emscripten_sleep) — we
    # just turn off SAFE_HEAP / ASSERTIONS / DEMANGLE which were diagnostic.
    #
    # `allocate,ALLOC_NORMAL,intArrayFromString` are the legacy Emscripten
    # allocator helpers; SDL2's SDL_assert.c emits an EM_ASM block that uses
    # them, and Closure ADVANCED_OPTIMIZATIONS errors out when it sees those
    # names without a declaration. Adding them to EXPORTED_RUNTIME_METHODS
    # keeps them defined and visible to the closure pass.
    FLAGS=( -O3 -DNDEBUG )
    LINK_FLAGS=(
        -O3
        -sASSERTIONS=0
        --closure 1
        -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS,UTF8ToString,allocate,ALLOC_NORMAL,intArrayFromString
        "${COMMON_LINK_FLAGS[@]}"
    )
    TARGET_BASE="GeneralsZH-rel"
else
    # Default debug profile — keeps the SAFE_HEAP / ASSERTIONS instrumentation
    # we use for triaging in-browser issues. Source maps and demangling on.
    FLAGS=( -O2 -g -DNDEBUG )
    LINK_FLAGS=(
        -g3 -sSAFE_HEAP=1 -sASSERTIONS=2 -sDEMANGLE_SUPPORT=1
        -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS,UTF8ToString
        "${COMMON_LINK_FLAGS[@]}"
    )
    TARGET_BASE="GeneralsZH"
fi

# Library order — exact copy from build.ninja LINK_LIBRARIES variable
LINK_LIBRARIES=(
    "Core/Libraries/Source/profile/libcore_profile.a"
    "GeneralsMD/Code/GameEngine/libz_gameengine.a"
    "GeneralsMD/Code/GameEngineDevice/libz_gameenginedevice.a"
    "GeneralsMD/Code/GameEngine/libz_gameengine.a"
    "GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/libww3d2.a"
    "Core/Libraries/Source/WWVegas/WWDebug/libwwdebug.a"
    "Core/Libraries/Source/WWVegas/WWLib/libwwlib.a"
    "Core/Libraries/Source/WWVegas/WWMath/libwwmath.a"
    "Core/Libraries/Source/WWVegas/WWSaveLoad/libwwsaveload.a"
    "GeneralsMD/Code/Libraries/Source/WWVegas/WWDownload/libwwdownload.a"
    "Core/Libraries/Source/Compression/libcompression.a"
    "resources/libresources.a"
    "libliblzhl.a"
    "Core/Libraries/Source/Compression/liblibzlib.a"
    "libgamespy.a"
    "libmilesstub.a"
    "libbinkstub.a"
)

TARGET="GeneralsMD/${TARGET_BASE}.html"

# Verify key inputs exist before starting the multi-minute link
echo "Checking inputs..."
for f in "${MAIN_OBJS[@]}" "${LINK_LIBRARIES[@]}"; do
    if [ ! -f "${f}" ]; then
        echo "  MISSING: ${f}"
        MISSING=1
    else
        echo "  OK: ${f}"
    fi
done
if [ "${MISSING:-0}" = "1" ]; then
    echo ""
    echo "ERROR: One or more inputs are missing.  Aborting."
    exit 1
fi

echo ""
echo "All inputs present.  Starting link (this will take several minutes due to wasm-opt asyncify)..."
echo ""

CMD=(
    "${EMPP}"
    "${FLAGS[@]}"
    "${LINK_FLAGS[@]}"
    "${MAIN_OBJS[@]}"
    -o "${TARGET}"
    "${LINK_LIBRARIES[@]}"
)

# Print the command to the log
echo "CMD: ${CMD[*]}" | tee "${LOG}"
echo "" | tee -a "${LOG}"

# Run and tee output to both terminal and log
if "${CMD[@]}" 2>&1 | tee -a "${LOG}"; then
    echo ""
    echo "============================================="
    echo " LINK SUCCEEDED"
    echo "============================================="
    for ext in .html .js .wasm; do
        p="${BUILD_DIR}/GeneralsMD/${TARGET_BASE}${ext}"
        if [ -f "${p}" ]; then
            sz=$(stat -f%z "${p}" 2>/dev/null || stat -c%s "${p}" 2>/dev/null)
            printf "  %-45s  %s bytes\n" "GeneralsMD/${TARGET_BASE}${ext}" "$(python3 -c "print(f'{${sz}:,}' if False else '{:,}'.format(${sz}))" 2>/dev/null || echo ${sz})"
        fi
    done
    echo ""
    echo "Deploy GeneralsMD/${TARGET_BASE}.{html,js,wasm} to your web server."
    echo "Open the browser console and look for:"
    echo "  POOL-ALLOC-32, POOL-FREE-32, TRACE: GT/TC-ctor/Set_Texture_Name"
    echo "  POOL-DOUBLE-FREE  <-- this will print the full callstack before aborting"
    echo "============================================="
else
    echo ""
    echo "============================================="
    echo " LINK FAILED  (see ${LOG})"
    echo "============================================="
    echo "Last 50 lines of output:"
    tail -50 "${LOG}"
    exit 1
fi
