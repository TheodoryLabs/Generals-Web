#!/usr/bin/env bash

# Workspace root — set GX_BUILD_ROOT to your build workspace (contains build-web/, emsdk/, GeneralsX/).
: "${GX_BUILD_ROOT:=$(pwd)}"

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
# Output: build-web/GeneralsMD/GeneralsZH.{html,js,wasm}
# =============================================================================

set -euo pipefail

BUILD_DIR="${GX_BUILD_ROOT}/build-web"
EMSDK_DIR="${GX_BUILD_ROOT}/emsdk"
EMPP="${EMSDK_DIR}/upstream/emscripten/em++"
LOG="${BUILD_DIR}/link_mac.log"

export EM_CONFIG="${EMSDK_DIR}/.emscripten"

echo "============================================="
echo " GeneralsX Mac Link Step"
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

# Main translation-unit object files (already compiled, not rebuilt here)
MAIN_OBJS=(
    "GeneralsMD/Code/Main/CMakeFiles/z_generals.dir/EmscriptenMain.cpp.o"
    "GeneralsMD/Code/Main/CMakeFiles/z_generals.dir/LinuxStubs.cpp.o"
)

# Flags — exact copy from build.ninja (RelWithDebInfo)
FLAGS=(
    -O2 -g -DNDEBUG
)

LINK_FLAGS=(
    -g3 -sSAFE_HEAP=1 -sASSERTIONS=2 -sDEMANGLE_SUPPORT=1
    -sUSE_WEBGL2=1 -sFULL_ES3=1
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
    -sINITIAL_MEMORY=268435456 -sALLOW_MEMORY_GROWTH=1
    -sMAXIMUM_MEMORY=536870912
    -sFETCH=1 -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=8388608
    -fexceptions -sDISABLE_EXCEPTION_CATCHING=0
    -sFORCE_FILESYSTEM=1 -sEXIT_RUNTIME=0
    -sWASM=1 -sENVIRONMENT=web
    -s USE_SDL=2
)

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

TARGET="GeneralsMD/GeneralsZH.html"

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
        p="${BUILD_DIR}/GeneralsMD/GeneralsZH${ext}"
        if [ -f "${p}" ]; then
            sz=$(stat -f%z "${p}" 2>/dev/null || stat -c%s "${p}" 2>/dev/null)
            printf "  %-45s  %s bytes\n" "GeneralsMD/GeneralsZH${ext}" "$(python3 -c "print(f'{${sz}:,}' if False else '{:,}'.format(${sz}))" 2>/dev/null || echo ${sz})"
        fi
    done
    echo ""
    echo "Deploy GeneralsMD/GeneralsZH.{html,js,wasm} to your web server."
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
