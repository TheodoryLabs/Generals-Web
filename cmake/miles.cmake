FetchContent_Declare(
    miles
    GIT_REPOSITORY https://github.com/TheSuperHackers/miles-sdk-stub.git
    GIT_TAG        6e32700d7ba4b4713a03bf1f5ffc3b0ac8d17264
    # Guard mss.h's WAVEFORMAT against the emscripten_compat definition —
    # see cmake/patch-miles-waveformat.cmake. GeneralsX @build WebPort 2026-07-07
    PATCH_COMMAND ${CMAKE_COMMAND} -DMILES_MSS_H=<SOURCE_DIR>/mss/mss.h -P ${CMAKE_SOURCE_DIR}/cmake/patch-miles-waveformat.cmake
)

# GeneralsX @feature WebPort 09/03/2026 — Emscripten: miles-sdk-stub creates a SHARED library
# which Emscripten doesn't support. We only need the mss.h header, not the actual library.
# Fetch the source but don't run its CMakeLists; create a header-only INTERFACE target instead.
if(EMSCRIPTEN)
    FetchContent_Populate(miles)
    if(NOT TARGET milesstub)
        add_library(milesstub STATIC ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WebAudioBridge/web_audio_bridge.cpp)
        target_include_directories(milesstub PUBLIC
            ${miles_SOURCE_DIR}
            ${miles_SOURCE_DIR}/mss
            ${CMAKE_SOURCE_DIR}/Core/GameEngine/Include
            ${CMAKE_SOURCE_DIR}/Core/Libraries/Include
            ${CMAKE_SOURCE_DIR}/Dependencies/Utility
        )
        target_compile_definitions(milesstub PUBLIC BUILD_STUBS)
    endif()
    if(NOT TARGET milescleanup)
        add_library(milescleanup STATIC ${miles_SOURCE_DIR}/cleanup.c)
        target_link_libraries(milesstub PUBLIC milescleanup)
    endif()
else()
    FetchContent_MakeAvailable(miles)
endif()
