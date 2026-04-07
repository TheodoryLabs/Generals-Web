#ifndef __EMSCRIPTEN_WINDOWS_COMPAT_H
#define __EMSCRIPTEN_WINDOWS_COMPAT_H

#include "windows_base.h"
#include <cmath>
#include <string.h>

#ifndef _isnan
#define _isnan std::isnan
#endif
#ifndef _finite
#define _finite std::isfinite
#endif

#ifndef lstrcpyn
#define lstrcpyn strncpy
#endif

#ifndef _strdup
#define _strdup strdup
#endif

#ifndef strcmpi
#define strcmpi strcasecmp
#endif

#ifndef stricmp
#define stricmp strcasecmp
#endif

#ifndef lstrcat
#define lstrcat strcat
#endif
#ifndef lstrcpy
#define lstrcpy strcpy
#endif
#ifndef lstrcmp
#define lstrcmp strcmp
#endif
#ifndef lstrlen
#define lstrlen strlen
#endif

#ifndef HIWORD
#define HIWORD(l) ((unsigned short)((((unsigned long)(l)) >> 16) & 0xffff))
#endif
#ifndef LOWORD
#define LOWORD(l) ((unsigned short)(((unsigned long)(l)) & 0xffff))
#endif

inline void GetCurrentDirectory(int, char *) {}
inline int GetFileAttributes(const char *) { return -1; }

#ifndef HFONT
typedef void *HFONT;
#endif
#ifndef HBITMAP
typedef void *HBITMAP;
#endif
#ifndef HDC
typedef void *HDC;
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef LPBYTE
typedef unsigned char *LPBYTE;
#endif

#endif // __EMSCRIPTEN_WINDOWS_COMPAT_H
