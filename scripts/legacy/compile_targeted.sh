#!/usr/bin/env bash

# Workspace root — set GX_BUILD_ROOT to your build workspace (contains build-web/, emsdk/, GeneralsX/).
: "${GX_BUILD_ROOT:=$(pwd)}"

# Targeted compile of just the .o files in z_generals.dir/ that we modify in
# the Mac workflow (EmscriptenMain.cpp.o, EmscriptenInput.cpp.o, LinuxStubs.cpp.o).
# Avoids running ninja for the whole tree (which would re-run CMake and pull
# the entire WWAudio/WW3D2/GameEngine compile graph, taking hours).
#
# Flags lifted verbatim from build-web/build.ninja's
# "build .../EmscriptenInput.cpp.o:" rule.
#
# Usage: bash compile_targeted.sh [--syntax-only]

set -euo pipefail

SYNTAX_ONLY=0
if [ "${1:-}" = "--syntax-only" ]; then
    SYNTAX_ONLY=1
fi

EMSDK_DIR="${GX_BUILD_ROOT}/emsdk"
BUILD_DIR="${GX_BUILD_ROOT}/build-web"
SRC_DIR="${GX_BUILD_ROOT}/GeneralsX/GeneralsMD/Code/Main"
EMPP="${EMSDK_DIR}/upstream/emscripten/em++"
PCH_BASE="${BUILD_DIR}/GeneralsMD/Code/Main/CMakeFiles/z_generals.dir/cmake_pch.hxx"

export EM_CONFIG="${EMSDK_DIR}/.emscripten"

DEFINES=(
    -DBINKDLL -DBUILD_STUBS -DDEBUG_CRASHING=1 -DDEBUG_LOGGING=1
    -DDISABLE_GAMEMEMORY=1 -DDISABLE_MEMORYPOOL_BOUNDINGWALL=1
    -DDISABLE_MEMORYPOOL_CHECKPOINTING=1
    -DDISABLE_MEMORYPOOL_STACKTRACE=1 -DNDEBUG -DNO_BINK=1 -DNO_D3D8=1
    -DNO_DXVK=1 -DNO_GAMESPY=1 -DNO_MILES=1 -DNO_WIN32=1 -DPLATFORM_WEB=1
    -DRTS_RELEASE -DRTS_ZEROHOUR=1 -DZ_PREFIX -D_UNIX
)

FLAGS=(
    -g3 -O0 -g -std=c++20 -sUSE_WEBGL2=1 -sFULL_ES3=1 -fexceptions -g
    -sUSE_SDL=2 -fdeclspec -Winvalid-pch
    -Xclang -include-pch -Xclang "${PCH_BASE}.pch"
    -Xclang -include -Xclang "${PCH_BASE}"
)

INCLUDES=(
    -I"${SRC_DIR}"
    -I"${BUILD_DIR}/GeneralsMD/Code/Main"
    -I"${SRC_DIR}/../CompatLib/Include"
    -I"${GX_BUILD_ROOT}/GeneralsX/Core/Libraries/Source/profile"
    -I"${GX_BUILD_ROOT}/GeneralsX/GeneralsMD/Code/GameEngine/Include"
    -I"${GX_BUILD_ROOT}/GeneralsX/Core/GameEngine/Include"
    -I"${GX_BUILD_ROOT}/GeneralsX/Core/Libraries/Source/Compression"
    -I"${BUILD_DIR}/_deps/lzhl-src/CompLibHeader"
    -I"${BUILD_DIR}/_deps/lzhl-src/CompLibHeader/.."
    -I"${BUILD_DIR}/Core/Libraries/Source/Compression/_deps/zlib-1.1.4-src/ZLib"
    -I"${BUILD_DIR}/Core/Libraries/Source/EABrowserDispatch/.."
    -I"${GX_BUILD_ROOT}/GeneralsX/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2"
    -I"${GX_BUILD_ROOT}/GeneralsX/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/emscripten_compat"
    -I"${GX_BUILD_ROOT}/GeneralsX/GeneralsMD/Code/CompatLib/Include"
    -I"${BUILD_DIR}/_deps/gamespy-src/include"
    -I"${BUILD_DIR}/_deps/gamespy-src/include/gamespy"
    -I"${GX_BUILD_ROOT}/GeneralsX/GeneralsMD/Code/Libraries/Source/WWVegas"
    -I"${GX_BUILD_ROOT}/GeneralsX/Core/Libraries/Source/WWVegas"
    -I"${GX_BUILD_ROOT}/GeneralsX/Core/Libraries/Source/WWVegas/WW3D2"
    -I"${GX_BUILD_ROOT}/GeneralsX/Core/Libraries/Source/WWVegas/WWAudio"
    -I"${GX_BUILD_ROOT}/GeneralsX/Core/Libraries/Source/WWVegas/WWDebug"
    -I"${GX_BUILD_ROOT}/GeneralsX/Core/Libraries/Source/WWVegas/WWLib"
    -I"${GX_BUILD_ROOT}/GeneralsX/Core/Libraries/Source/WWVegas/WWMath"
    -I"${GX_BUILD_ROOT}/GeneralsX/Core/Libraries/Source/WWVegas/WWSaveLoad"
    -I"${GX_BUILD_ROOT}/GeneralsX/GeneralsMD/Code/GameEngineDevice/Include"
    -I"${GX_BUILD_ROOT}/GeneralsX/Core/GameEngineDevice/Include"
    -I"${BUILD_DIR}/_deps/bink-src"
    -I"${BUILD_DIR}/_deps/miles-src"
    -I"${BUILD_DIR}/_deps/miles-src/mss"
    -I"${GX_BUILD_ROOT}/GeneralsX/Dependencies/Utility"
    -I"${GX_BUILD_ROOT}/GeneralsX/Core/Libraries/Include"
    -I"${GX_BUILD_ROOT}/GeneralsX/resources/gitinfo"
)

cd "${BUILD_DIR}"

SOURCES=(
    "EmscriptenInput.cpp"
    "EmscriptenMain.cpp"
)

for src in "${SOURCES[@]}"; do
    SRC="${SRC_DIR}/${src}"
    OBJ="GeneralsMD/Code/Main/CMakeFiles/z_generals.dir/${src}.o"
    if [ "${SYNTAX_ONLY}" = "1" ]; then
        echo "===== syntax-check ${src} ====="
        "${EMPP}" -fsyntax-only "${FLAGS[@]}" "${DEFINES[@]}" \
                  "${INCLUDES[@]}" "${SRC}"
    else
        echo "===== compiling ${src} → ${OBJ} ====="
        "${EMPP}" "${FLAGS[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" \
                  -c "${SRC}" -o "${OBJ}"
        ls -l "${OBJ}"
    fi
done

echo ""
echo "DONE."
