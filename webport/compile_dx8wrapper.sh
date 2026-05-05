#!/usr/bin/env bash
# Recompile dx8wrapper.cpp standalone and replace its .o in libww3d2.a.
# Flags lifted from build-web/build.ninja's
# "build .../z_ww3d2.dir/dx8wrapper.cpp.o:" rule.
#
# Used to add diagnostic counters into DX8Wrapper::Draw without rebuilding
# the entire libz_ww3d2 library (which has many TUs and would take ~30 min).

set -euo pipefail

EMSDK_DIR="/Users/builduser/GeneralsX-build/emsdk"
BUILD_DIR="/Users/builduser/GeneralsX-build/build-web"
GMD_WW3D2="/Users/builduser/GeneralsX-build/GeneralsX/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2"
CORE_WW3D2="/Users/builduser/GeneralsX-build/GeneralsX/Core/Libraries/Source/WWVegas/WW3D2"
OBJ_DIR_FLAT="${BUILD_DIR}/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/CMakeFiles/z_ww3d2.dir"
OBJ_DIR_CORE="${OBJ_DIR_FLAT}/__/__/__/__/__/__/Core/Libraries/Source/WWVegas/WW3D2"
ARCHIVE="${BUILD_DIR}/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/libww3d2.a"

# All .cpp files we touch when instrumenting / fixing the d3d8 stub +
# FVF decoder + font rasterisation. They all live in the WW3D2 library
# (split between GeneralsMD/.../WW3D2 and Core/.../WW3D2 source trees),
# share the same compile flags, and need to be re-injected into libww3d2.a.
# Format: "src_root|cpp_name|obj_subdir"
SOURCES=(
    "${GMD_WW3D2}|dx8wrapper.cpp|${OBJ_DIR_FLAT}"
    "${GMD_WW3D2}|gles3_fvf.cpp|${OBJ_DIR_FLAT}"
    "${CORE_WW3D2}|render2dsentence.cpp|${OBJ_DIR_CORE}"
)
PCH_BASE="${BUILD_DIR}/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/CMakeFiles/z_ww3d2.dir/cmake_pch.hxx"
EMPP="${EMSDK_DIR}/upstream/emscripten/em++"
EMAR="${EMSDK_DIR}/upstream/emscripten/emar"

export EM_CONFIG="${EMSDK_DIR}/.emscripten"

DEFINES=(
    -DBUILD_STUBS -DDEBUG_CRASHING=1 -DDEBUG_LOGGING=1
    -DDISABLE_GAMEMEMORY=1 -DDISABLE_MEMORYPOOL_BOUNDINGWALL=1
    -DDISABLE_MEMORYPOOL_CHECKPOINTING=1 -DDISABLE_MEMORYPOOL_MPSB_DLINK=1
    -DDISABLE_MEMORYPOOL_STACKTRACE=1 -DNDEBUG -DNO_BINK=1 -DNO_D3D8=1
    -DNO_DXVK=1 -DNO_GAMESPY=1 -DNO_MILES=1 -DNO_WIN32=1 -DPLATFORM_WEB=1
    -DRTS_RELEASE -DRTS_ZEROHOUR=1 -DWIN32_LEAN_AND_MEAN -D_UNIX
)

FLAGS=(
    -g3 -O0 -g -std=c++20 -sUSE_WEBGL2=1 -sFULL_ES3=1 -fexceptions -g
    -s USE_SDL=2
    # PCH is stale (windows_base.h was edited for input layer); include the
    # header directly instead of using the pre-compiled version.
    -Xclang -include -Xclang "${PCH_BASE}"
)

INCLUDES=(
    -I"${BUILD_DIR}/Core/Libraries/Source/EABrowserEngine/.."
    -I"/Users/builduser/GeneralsX-build/GeneralsX/Core/Libraries/Source/WWVegas/WWLib"
    -I"/Users/builduser/GeneralsX-build/GeneralsX/GeneralsMD/Code/Libraries/Source/WWVegas"
    -I"/Users/builduser/GeneralsX-build/GeneralsX/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2"
    -I"/Users/builduser/GeneralsX-build/GeneralsX/Core/Libraries/Source/WWVegas"
    -I"/Users/builduser/GeneralsX-build/GeneralsX/Core/Libraries/Source/WWVegas/WW3D2"
    -I"/Users/builduser/GeneralsX-build/GeneralsX/Core/Libraries/Source/WWVegas/WWAudio"
    -I"/Users/builduser/GeneralsX-build/GeneralsX/Core/Libraries/Source/WWVegas/WWDebug"
    -I"/Users/builduser/GeneralsX-build/GeneralsX/Core/Libraries/Source/WWVegas/WWMath"
    -I"/Users/builduser/GeneralsX-build/GeneralsX/Core/Libraries/Source/WWVegas/WWSaveLoad"
    -I"/Users/builduser/GeneralsX-build/GeneralsX/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/emscripten_compat"
    -I"/Users/builduser/GeneralsX-build/GeneralsX/GeneralsMD/Code/CompatLib/Include"
    -I"${BUILD_DIR}/_deps/miles-src"
    -I"${BUILD_DIR}/_deps/miles-src/mss"
    -I"/Users/builduser/GeneralsX-build/GeneralsX/Dependencies/Utility"
    -I"/Users/builduser/GeneralsX-build/GeneralsX/Core/Libraries/Include"
    -I"/Users/builduser/GeneralsX-build/GeneralsX/resources/gitinfo"
)

cd "${BUILD_DIR}"

OBJS=()
for entry in "${SOURCES[@]}"; do
    IFS='|' read -r src_root cpp_name obj_dir <<< "${entry}"
    SRC="${src_root}/${cpp_name}"
    OBJ="${obj_dir}/${cpp_name}.o"
    OBJS+=("${OBJ}")
    echo "===== compiling ${cpp_name} ====="
    "${EMPP}" "${FLAGS[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" \
              -c "${SRC}" -o "${OBJ}"
    ls -l "${OBJ}"
done

echo "===== updating libww3d2.a archive ====="
# `r` = replace, `s` = update index. Replace each .o by basename match.
"${EMAR}" rs "${ARCHIVE}" "${OBJS[@]}"
ls -l "${ARCHIVE}"

echo "DONE."
