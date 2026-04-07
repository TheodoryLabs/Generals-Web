FetchContent_Declare(
    miles
    GIT_REPOSITORY https://github.com/TheSuperHackers/miles-sdk-stub.git
    GIT_TAG        6e32700d7ba4b4713a03bf1f5ffc3b0ac8d17264
)

# GeneralsX @feature WebPort 09/03/2026 — Emscripten: miles-sdk-stub creates a SHARED library
# which Emscripten doesn't support. We only need the mss.h header, not the actual library.
# Fetch the source but don't run its CMakeLists; create a header-only INTERFACE target instead.
if(EMSCRIPTEN)
    FetchContent_Populate(miles)
    if(NOT TARGET milesstub)
        add_library(milesstub STATIC ${miles_SOURCE_DIR}/miles.c)
        target_include_directories(milesstub PUBLIC
            ${miles_SOURCE_DIR}
            ${miles_SOURCE_DIR}/mss
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
