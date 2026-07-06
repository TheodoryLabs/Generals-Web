# Guard the WAVEFORMAT/PCMWAVEFORMAT definitions in the fetched miles-sdk-stub
# mss.h: the pinned stub defines them unconditionally, and on Emscripten our
# emscripten_compat/windows_base.h also defines WAVEFORMAT (guarded by
# _WAVEFORMAT_DEFINED). Any TU that sees both headers fails with a redefinition
# error. Applied as a FetchContent PATCH_COMMAND; idempotent.
# GeneralsX @build WebPort 2026-07-07
if(NOT DEFINED MILES_MSS_H)
    message(FATAL_ERROR "pass -DMILES_MSS_H=<path to mss.h>")
endif()
file(READ "${MILES_MSS_H}" _mss)
if(NOT _mss MATCHES "_WAVEFORMAT_DEFINED")
    string(REPLACE
        "typedef struct WAVEFORMAT\n{"
        "#ifndef _WAVEFORMAT_DEFINED\n#define _WAVEFORMAT_DEFINED\ntypedef struct WAVEFORMAT\n{"
        _mss "${_mss}")
    string(REPLACE
        "} PCMWAVEFORMAT;"
        "} PCMWAVEFORMAT;\n#endif /* _WAVEFORMAT_DEFINED */"
        _mss "${_mss}")
    file(WRITE "${MILES_MSS_H}" "${_mss}")
    message(STATUS "miles-sdk-stub: mss.h WAVEFORMAT guard applied")
else()
    message(STATUS "miles-sdk-stub: mss.h WAVEFORMAT guard already present")
endif()
