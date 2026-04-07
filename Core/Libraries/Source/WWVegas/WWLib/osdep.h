/*
** osdep.h — OS dependency abstraction for UNIX / Linux / Emscripten
**
** Provides Windows-style string/memory helpers that are absent on POSIX platforms.
** TheSuperHackers @build BenderAI 11/02/2026
** GeneralsX @feature WebPort 09/03/2026 — Emscripten guard on strupr/strrev
*/
#pragma once

#include <cstring>
#include <cctype>

// strupr — uppercase string in-place.
// POSIX does not define strupr; Emscripten provides it via <compat/string.h>.
// GeneralsX @feature WebPort 09/03/2026 — Only define if not already provided by compat headers.
#if !defined(strupr) && !defined(__EMSCRIPTEN__)
static inline char *strupr(char *str)
{
    if (!str) return nullptr;
    for (char *p = str; *p; ++p)
        *p = (char)toupper((unsigned char)*p);
    return str;
}
#endif

// strrev — reverse string in-place (not available on POSIX).
#if !defined(strrev) && !defined(__EMSCRIPTEN__)
static inline char *strrev(char *str)
{
    if (!str) return nullptr;
    size_t len = strlen(str);
    for (size_t i = 0; i < len / 2; i++) {
        char tmp = str[i];
        str[i] = str[len - 1 - i];
        str[len - 1 - i] = tmp;
    }
    return str;
}
#endif

// stricmp / strnicmp — case-insensitive compare (POSIX has strcasecmp).
#if !defined(stricmp)
#  include <strings.h>
#  define stricmp  strcasecmp
#  define strnicmp strncasecmp
#endif

// ============================================================
// MSVC extension compatibility for non-MSVC compilers
// (GCC, Clang, Emscripten)
// ============================================================

// __int64 / _int64: wwprofile.h already handles these under #ifdef _UNIX
// We only need __forceinline which is safe as a macro define
#if !defined(_MSC_VER) && !defined(__forceinline)
#  define __forceinline inline __attribute__((always_inline))
#endif

// _stricmp / _strnicmp: MSVC names for case-insensitive compare
#if !defined(_stricmp)
#  if defined(__EMSCRIPTEN__) || defined(__linux__) || defined(__APPLE__)
#    include <strings.h>
#    define _stricmp  strcasecmp
#    define _strnicmp strncasecmp
#    define stricmp   strcasecmp
#    define strnicmp  strncasecmp
#  endif
#endif

