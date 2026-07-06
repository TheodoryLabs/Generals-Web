# ============================================================================
# GeneralsX Web Port — CMake Integration
#
# Drop-in replacement for cmake/dx8.cmake when building for Emscripten.
# Instead of fetching DXVK or DirectX SDK, this configures the OpenGL ES 3.0
# wrapper that targets WebGL 2.0.
#
# GeneralsX @feature WebPort 09/03/2026
# ============================================================================

# CMAKE_PROJECT_INCLUDE re-runs this file at every subproject's project() call
# (gamespy, Generals, GeneralsMD). The flags and targets below must only be
# created once, at the top level — guard against re-entry.
# GeneralsX @build WebPort 2026-07-06
if(GENERALSX_WEB_CMAKE_INCLUDED)
    return()
endif()
set(GENERALSX_WEB_CMAKE_INCLUDED TRUE)

message(STATUS "=== Configuring GeneralsX Web Port (Emscripten + WebGL 2.0) ===")

# ============================================================================
# 1. Emscripten Compiler Flags
# ============================================================================

# WebGL 2.0 = OpenGL ES 3.0
add_compile_options(-sUSE_WEBGL2=1 -sFULL_ES3=1)
add_link_options(-sUSE_WEBGL2=1 -sFULL_ES3=1)
add_link_options(-sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2)

# OpenAL → WebAudio
# GeneralsX @build BenderAI 09/03/2026 — In Emscripten 5.x, OpenAL is always
# built-in (the USE_OPENAL port was removed). No flag needed.
# add_link_options(-sUSE_OPENAL=1)

# Memory: 256MB initial, grow to 512MB
add_link_options(
    -sINITIAL_MEMORY=268435456
    -sALLOW_MEMORY_GROWTH=1
    -sMAXIMUM_MEMORY=536870912
)

# Fetch API (for HTTP Range requests to .big archives)
add_link_options(-sFETCH=1)

# ASYNCIFY disabled to optimize performance (Fetch_Range_JS uses synchronous XHR and emscripten_sleep yields are commented out)
add_link_options(-sASYNCIFY=0)

# Enable C++ Exception catching so we can see what is crashing
add_compile_options(-fexceptions -g)
add_link_options(-fexceptions -sDISABLE_EXCEPTION_CATCHING=0)

# Debugging and Source Maps
# DEMANGLE_SUPPORT was removed in Emscripten 4.x+ (stacks demangle by default)
# Debug diagnostics only outside the Release distribution profile —
# -g keeps full DWARF in the wasm (5x size) and assertions cost frames.
# GeneralsX @build WebPort 2026-07-07
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    add_link_options(-g -sSAFE_HEAP=1 -sASSERTIONS=2)
else()
    add_link_options(-g0 -sASSERTIONS=0)
endif()

# Filesystem support
add_link_options(-sFORCE_FILESYSTEM=1)

# Don't exit runtime (game loop runs via requestAnimationFrame)
add_link_options(-sEXIT_RUNTIME=0)

# WebAssembly output
add_link_options(-sWASM=1 -sENVIRONMENT=web)

# SDL3 for Emscripten: must be static, no shared libs
set(SDL_SHARED OFF CACHE BOOL "No shared libs on Emscripten" FORCE)
set(SDL_STATIC ON CACHE BOOL "Use static SDL3 on Emscripten" FORCE)
set(SDL_WAYLAND OFF CACHE BOOL "No Wayland on Emscripten" FORCE)
set(SDL_X11 OFF CACHE BOOL "No X11 on Emscripten" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "No Vulkan on Emscripten" FORCE)
set(SDL_OPENGL OFF CACHE BOOL "No desktop OpenGL on Emscripten" FORCE)
set(SDL_OPENGLES ON CACHE BOOL "Enable OpenGL ES for Emscripten" FORCE)

# ============================================================================
# 2. Build Defines
# ============================================================================

add_definitions(-DPLATFORM_WEB=1)
add_definitions(-DNO_DXVK=1)
add_definitions(-DNO_WIN32=1)
add_definitions(-DNO_BINK=1)
add_definitions(-DNO_MILES=1)

# Disable GameSpy networking — sockets and multiplayer not supported in WASM.
# Game code guarded by #ifndef NO_GAMESPY will be compiled out.
add_definitions(-DNO_GAMESPY=1)

# The doubly-linked MPSB fast-path is enabled by default to allow O(1) removals.
# add_definitions(-DDISABLE_MEMORYPOOL_MPSB_DLINK=1)

# Disable MEMORYPOOL_BOUNDINGWALL debug feature: in WASM32 (4-byte pointers),
# the post-wall fill/check of a freed pool block overlaps with the immediately
# adjacent block's vtable pointer, zeroing it (SI-vptr=0 pattern). This cascades
# into DynamicMemoryAllocator free-list corruption where all new() calls return
# the same address, ultimately causing RuntimeError: function signature mismatch
# in virtual dispatch. Boundary-wall checking is a native-debug aid only.
add_definitions(-DDISABLE_MEMORYPOOL_BOUNDINGWALL=1)

# NUCLEAR OPTION: Bypass the custom memory pool entirely and use system
# malloc/free. Uses the GameMemoryNull.h path which replaces all pool
# allocators with standard heap allocations. Slightly slower but eliminates all
# pool-related corruption (POOL-DOUBLE-FREE, MPSB dlink, bounding wall).
# Requires a full rebuild: delete build-web/CMakeCache.txt then ninja.
set(RTS_GAMEMEMORY_ENABLE ON CACHE BOOL "Disable GameMemory pool on web" FORCE)

# Force SAGE_USE_OPENAL so WWAudio uses MilesStub.h instead of mss.h
# Emscripten provides OpenAL→WebAudio via -sUSE_OPENAL=1
set(SAGE_USE_OPENAL ON CACHE BOOL "Force OpenAL on Emscripten" FORCE)

# ============================================================================
# 3. GLES3 Wrapper Sources
# ============================================================================

# Source files are in WW3D2 directory (alongside DX8 wrapper sources)
set(WW3D2_DIR ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2)

set(GLES3_WEB_SOURCES
    ${WW3D2_DIR}/gles3_wrapper.cpp
    ${WW3D2_DIR}/gles3_fvf.cpp
    ${WW3D2_DIR}/gles3_texture_utils.cpp
    ${WW3D2_DIR}/gles3_big_vfs.cpp
    ${WW3D2_DIR}/gles3_mainloop.cpp
    ${WW3D2_DIR}/dx8_fixedfunction_shaders.cpp
)

set(GLES3_WEB_HEADERS
    ${WW3D2_DIR}/gles3_wrapper.h
    ${WW3D2_DIR}/gles3_fvf.h
    ${WW3D2_DIR}/gles3_texture_utils.h
    ${WW3D2_DIR}/gles3_big_vfs.h
    ${WW3D2_DIR}/gles3_mainloop.h
)

# Create INTERFACE library for include paths
add_library(gles3_web INTERFACE)
target_include_directories(gles3_web INTERFACE ${WW3D2_DIR})

# Emscripten entry point
set(EMSCRIPTEN_MAIN_SOURCE
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/Main/EmscriptenMain.cpp
)

# ============================================================================
# 4. d3d8lib Replacement Target
#
# On Windows, d3d8lib provides DirectX 8 headers.
# On Linux, d3d8lib provides DXVK headers.
# On Web, we create a stub d3d8lib that provides our GLES3 headers instead.
# ============================================================================

if(NOT TARGET d3d8lib)
    add_library(d3d8lib INTERFACE)
    target_include_directories(d3d8lib INTERFACE
        # Emscripten compat: provides windows_base.h stub (normally from DXVK)
        ${WW3D2_DIR}/emscripten_compat
        # CompatLib headers: Win32 type/function stubs needed by game code
        # On Linux, these come via CompatLib STATIC library; on Emscripten we just need headers
        ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/CompatLib/Include
    )
    target_compile_definitions(d3d8lib INTERFACE
        PLATFORM_WEB=1
        NO_DXVK=1
        NO_D3D8=1
    )
endif()

# Also create d3d8 and d3dx8 as stub targets if they don't exist
# (some link targets reference these directly)
if(NOT TARGET d3d8)
    add_library(d3d8 INTERFACE)
endif()
if(NOT TARGET d3dx8)
    add_library(d3dx8 INTERFACE)
endif()

message(STATUS "Web port sources: ${GLES3_WEB_SOURCES}")
message(STATUS "Web port ready for build")
