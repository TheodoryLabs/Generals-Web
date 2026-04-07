FetchContent_Declare(
    bink
    GIT_REPOSITORY https://github.com/TheSuperHackers/bink-sdk-stub.git
    GIT_TAG        180fc4620ed72fd700347ab837a5271fd0259901
)

# GeneralsX @feature WebPort 09/03/2026 — Same pattern as miles: header-only on Emscripten
if(EMSCRIPTEN)
    FetchContent_Populate(bink)
    if(NOT TARGET binkstub)
        add_library(binkstub STATIC ${bink_SOURCE_DIR}/bink.c)
        target_include_directories(binkstub PUBLIC
            ${bink_SOURCE_DIR}
        )
        target_compile_definitions(binkstub PUBLIC BINKDLL)
        target_compile_options(binkstub PUBLIC "-fdeclspec")
    endif()
else()
    FetchContent_MakeAvailable(bink)
endif()
