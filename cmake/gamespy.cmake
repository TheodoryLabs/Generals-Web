set(GS_OPENSSL FALSE)
set(GAMESPY_SERVER_NAME "server.cnc-online.net")

FetchContent_Declare(
    gamespy
    GIT_REPOSITORY https://github.com/TheAssemblyArmada/GamespySDK.git
    GIT_TAG        07e3d15c500415abc281efb74322ab6d9c857eb8
)

FetchContent_MakeAvailable(gamespy)

# GeneralsX @feature WebPort 09/03/2026 — Emscripten: GameSpy SDK needs __linux__ for _UNIX path
# The SDK's gsplatform.h checks __linux__ to define _UNIX (socket headers, time_t, etc.)
# Emscripten is POSIX-like but doesn't define __linux__; gsinterface propagates to all modules
if(EMSCRIPTEN)
    if(TARGET gsinterface)
        target_compile_definitions(gsinterface INTERFACE __linux__=1)
    endif()
endif()
