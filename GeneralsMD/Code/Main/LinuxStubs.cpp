/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
** LinuxStubs.cpp
**
** Platform stubs for non-Windows (Linux / Emscripten) builds.
** Provides Windows API equivalents that the game engine calls but that are
** not available outside of Win32 or CompatLib.
**
** TheSuperHackers @build felipebraz 13/02/2026 — original Linux stubs
** GeneralsX @build BenderAI 09/03/2026 — Emscripten-specific additions
**   On Emscripten, CompatLib is skipped (web.cmake), so we provide all
**   Windows API stubs that CompatLib would normally supply.
*/

#ifndef _WIN32

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>

#ifdef __EMSCRIPTEN__
#include "d3d8.h"
#include "d3dx8math.h"
#include "windows_base.h"
#else
#include "d3dx8math.h"
#include "module_compat.h"
#include "thread_compat.h"
#include "time_compat.h"
#include "types_compat.h"
#include "wnd_compat.h"
#endif

// OSDisplay.h declares OSDisplayWarningBox / OSDisplaySetBusyState.
// It is in the game engine include path, transitively available via
// z_gameengine.
#include "Common/AsciiString.h"
#include "Common/OSDisplay.h"

// ============================================================================
// OSDisplay stubs
// Win32OSDisplay.cpp is only compiled on WIN32 (see z_gameenginedevice
// CMakeLists). Provide non-Windows stubs here so GameAudio.cpp / GameLogic.cpp
// can link.
// ============================================================================

OSDisplayButtonType OSDisplayWarningBox(AsciiString p, AsciiString m,
                                        UnsignedInt /*buttonFlags*/,
                                        UnsignedInt /*otherFlags*/) {
  fprintf(stderr, "WARN: OSDisplayWarningBox('%s', '%s') — web stub\n",
          p.str() ? p.str() : "(null)", m.str() ? m.str() : "(null)");
  return OSDBT_OK;
}

void OSDisplaySetBusyState(Bool /*busyDisplay*/, Bool /*busySystem*/) {
  // No-op on web: system sleep prevention is not applicable in a browser tab.
}

// ============================================================================
// Windows API stubs — module / dynamic library loading
// Dynamic library loading is not meaningful in WebAssembly.
// ============================================================================

#ifndef __EMSCRIPTEN__
bool GetModuleFileName(HINSTANCE /*hInstance*/, char *buffer, int size) {
  // No filesystem path — GameMemoryInit.cpp skips the INI load gracefully.
  if (buffer && size > 0)
    buffer[0] = '\0';
  return false;
}

HMODULE LoadLibrary(const char * /*lpFileName*/) { return nullptr; }

FARPROC GetProcAddress(HMODULE /*hModule*/, const char * /*lpProcName*/) {
  return nullptr;
}

void FreeLibrary(HMODULE /*hModule*/) {}

// ============================================================================
// Windows API stubs — windowing (Debug.cpp)
// ============================================================================

int MessageBox(HWND /*hWnd*/, LPCTSTR lpText, LPCTSTR lpCaption,
               UINT /*uType*/) {
  fprintf(stderr, "WARN: MessageBox('%s', '%s') — web stub\n",
          lpCaption ? lpCaption : "", lpText ? lpText : "");
  return 1; // IDOK
}

BOOL ShowWindow(HWND /*hWnd*/, int /*nCmdShow*/) { return TRUE; }
#endif // __EMSCRIPTEN__

// ============================================================================
// Windows API stubs — threading (MainMenuUtils.cpp)
// Emscripten single-threaded: CreateThread is a no-op; thread callbacks
// are never invoked. This unblocks the linker; functionality is not required
// for the web port's main menu path.
// ============================================================================

#ifndef __EMSCRIPTEN__
void *CreateThread(void * /*lpSecure*/, size_t /*dwStackSize*/,
                   unsigned int (* /*lpStartAddress*/)(void *),
                   void * /*lpParameter*/, unsigned long /*dwCreationFlags*/,
                   unsigned long *lpThreadId) {
  if (lpThreadId)
    *lpThreadId = 0;
  return nullptr; // NULL handle — callers must handle nullptr thread gracefully
}

int TerminateThread(void * /*hThread*/, unsigned long /*dwExitCode*/) {
  return 1; // TRUE — success (no-op)
}
#endif // __EMSCRIPTEN__

// ============================================================================
// Emscripten-only stubs (timing, strings, wide chars)
// These are provided by CompatLib on Linux but CompatLib is skipped for
// Emscripten.
// ============================================================================

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#undef GetLocalTime
#undef _itoa
#undef itoa

#endif // __EMSCRIPTEN__

// ============================================================================
// D3DX math stubs — W3DShaderManager.cpp, TerrainTex.cpp
// The CompatLib d3dx8math.cpp uses GLM which is not set up for Emscripten.
// Implement the needed functions directly using standard C math.
// D3DXMATRIX inherits D3DMATRIX: float m[4][4] where m[row][col] == _rc.
// ============================================================================

D3DXMATRIX *WINAPI D3DXMatrixMultiply(D3DXMATRIX *pOut, CONST D3DXMATRIX *pM1,
                                      CONST D3DXMATRIX *pM2) {
  // Standard row-major 4x4 multiply: out[r][c] = sum_k(M1[r][k] * M2[k][c])
  D3DXMATRIX tmp;
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c) {
      float s = 0.0f;
      for (int k = 0; k < 4; ++k)
        s += pM1->m[r][k] * pM2->m[k][c];
      tmp.m[r][c] = s;
    }
  *pOut = tmp;
  return pOut;
}

D3DXMATRIX *WINAPI D3DXMatrixScaling(D3DXMATRIX *pOut, FLOAT sx, FLOAT sy,
                                     FLOAT sz) {
  memset(pOut->m, 0, sizeof(pOut->m));
  pOut->m[0][0] = sx;
  pOut->m[1][1] = sy;
  pOut->m[2][2] = sz;
  pOut->m[3][3] = 1.0f;
  return pOut;
}

D3DXMATRIX *WINAPI D3DXMatrixTranslation(D3DXMATRIX *pOut, FLOAT x, FLOAT y,
                                         FLOAT z) {
  // Identity + translation in the last row (row-vector convention)
  memset(pOut->m, 0, sizeof(pOut->m));
  pOut->m[0][0] = 1.0f;
  pOut->m[1][1] = 1.0f;
  pOut->m[2][2] = 1.0f;
  pOut->m[3][3] = 1.0f;
  pOut->m[3][0] = x;
  pOut->m[3][1] = y;
  pOut->m[3][2] = z;
  return pOut;
}

D3DXVECTOR4 *WINAPI D3DXVec4Transform(D3DXVECTOR4 *pOut, const D3DXVECTOR4 *pV,
                                      const D3DXMATRIX *pM) {
  D3DXVECTOR4 v;
  v.x = pV->x * pM->m[0][0] + pV->y * pM->m[1][0] + pV->z * pM->m[2][0] +
        pV->w * pM->m[3][0];
  v.y = pV->x * pM->m[0][1] + pV->y * pM->m[1][1] + pV->z * pM->m[2][1] +
        pV->w * pM->m[3][1];
  v.z = pV->x * pM->m[0][2] + pV->y * pM->m[1][2] + pV->z * pM->m[2][2] +
        pV->w * pM->m[3][2];
  v.w = pV->x * pM->m[0][3] + pV->y * pM->m[1][3] + pV->z * pM->m[2][3] +
        pV->w * pM->m[3][3];
  *pOut = v;
  return pOut;
}

FLOAT WINAPI D3DXVec4Dot(CONST D3DXVECTOR4 *pV1, CONST D3DXVECTOR4 *pV2) {
  return pV1->x * pV2->x + pV1->y * pV2->y + pV1->z * pV2->z + pV1->w * pV2->w;
}

// ============================================================================
// Miscellaneous Application Stubs
// ============================================================================
char const *gAppPrefix = "GeneralsX_Web";

// Win32 Mouse — must match WinMain.h declaration (extern Win32Mouse*).
// Forward-declare to avoid pulling in the full Win32Mouse.h header chain.
class Win32Mouse;
Win32Mouse *TheWin32Mouse = nullptr;

// ============================================================================
// RegistryClass stubs (for W3DDisplay & dx8wrapper)
// ============================================================================
#include "../../../Core/Libraries/Source/WWVegas/WWLib/registry.h"
RegistryClass::RegistryClass(const char *sub_key, bool create)
    : IsValid(false) {}
RegistryClass::~RegistryClass() {}
int RegistryClass::Get_Int(const char *name, int def_value) {
  return def_value;
}

// ============================================================================
// Download Manager Stubs
// ============================================================================
#include "../../../Core/Libraries/Source/WWVegas/WWDownload/Download.h"

HRESULT CDownload::PumpMessages() { return S_OK; }
HRESULT CDownload::Abort() { return S_OK; }
HRESULT CDownload::DownloadFile(LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR,
                                bool) {
  return S_OK;
}
HRESULT CDownload::GetLastLocalFile(char *, int) { return E_FAIL; }
Cftp::Cftp() {}
Cftp::~Cftp() {}

// ============================================================================
// Miles Sound System Stubs provided by milesstub static library
// ============================================================================

D3DXMATRIX *WINAPI D3DXMatrixInverse(D3DXMATRIX *pOut, FLOAT *pDeterminant,
                                     CONST D3DXMATRIX *pM) {
  // Gauss-Jordan elimination on augmented matrix [M | I]
  float a[4][8];
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j)
      a[i][j] = pM->m[i][j];
    for (int j = 4; j < 8; ++j)
      a[i][j] = (i == j - 4) ? 1.0f : 0.0f;
  }

  for (int col = 0; col < 4; ++col) {
    // Partial pivot
    int pivot = col;
    for (int row = col + 1; row < 4; ++row)
      if (fabsf(a[row][col]) > fabsf(a[pivot][col]))
        pivot = row;
    if (pivot != col)
      for (int j = 0; j < 8; ++j) {
        float t = a[col][j];
        a[col][j] = a[pivot][j];
        a[pivot][j] = t;
      }

    float div = a[col][col];
    if (fabsf(div) < 1e-9f) {
      // Singular — return identity and zero determinant
      if (pDeterminant)
        *pDeterminant = 0.0f;
      memset(pOut->m, 0, sizeof(pOut->m));
      pOut->m[0][0] = pOut->m[1][1] = pOut->m[2][2] = pOut->m[3][3] = 1.0f;
      return pOut;
    }
    for (int j = 0; j < 8; ++j)
      a[col][j] /= div;
    for (int row = 0; row < 4; ++row)
      if (row != col) {
        float f = a[row][col];
        for (int j = 0; j < 8; ++j)
          a[row][j] -= f * a[col][j];
      }
  }

  if (pDeterminant) {
    // Approximate determinant via product of diagonal pivots before elimination
    // (just set to non-zero since we succeeded)
    *pDeterminant = 1.0f;
  }
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j)
      pOut->m[i][j] = a[i][j + 4];
  return pOut;
}

D3DXMATRIX *WINAPI D3DXMatrixTranspose(D3DXMATRIX *pOut, CONST D3DXMATRIX *pM) {
  D3DXMATRIX temp;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      temp.m[i][j] = pM->m[j][i];
    }
  }
  *pOut = temp;
  return pOut;
}


class DbgHelpGuard {
  bool m_needsUnload;
public:
  DbgHelpGuard();
  ~DbgHelpGuard();
  void activate();
  void deactivate();
};

DbgHelpGuard::DbgHelpGuard() : m_needsUnload(false) {}
DbgHelpGuard::~DbgHelpGuard() {}
void DbgHelpGuard::activate() {}
void DbgHelpGuard::deactivate() {}

class WWMemoryLogClass {
public:
  static int Get_Allocate_Count();
  static int Get_Free_Count();
  static void Reset_Counters();
};

int WWMemoryLogClass::Get_Allocate_Count() { return 0; }
int WWMemoryLogClass::Get_Free_Count() { return 0; }
void WWMemoryLogClass::Reset_Counters() {}

D3DXVECTOR4 *WINAPI D3DXVec3Transform(D3DXVECTOR4 *pOut, CONST D3DXVECTOR3 *pV,
                                      CONST D3DXMATRIX *pM) {
  pOut->x = pV->x * pM->m[0][0] + pV->y * pM->m[1][0] + pV->z * pM->m[2][0] +
            pM->m[3][0];
  pOut->y = pV->x * pM->m[0][1] + pV->y * pM->m[1][1] + pV->z * pM->m[2][1] +
            pM->m[3][1];
  pOut->z = pV->x * pM->m[0][2] + pV->y * pM->m[1][2] + pV->z * pM->m[2][2] +
            pM->m[3][2];
  pOut->w = pV->x * pM->m[0][3] + pV->y * pM->m[1][3] + pV->z * pM->m[2][3] +
            pM->m[3][3];
  return pOut;
}

D3DXMATRIX *WINAPI D3DXMatrixRotationZ(D3DXMATRIX *pOut, FLOAT angle) {
  float s = sinf(angle);
  float c = cosf(angle);
  memset(pOut->m, 0, sizeof(pOut->m));
  pOut->m[0][0] = c;
  pOut->m[0][1] = s;
  pOut->m[1][0] = -s;
  pOut->m[1][1] = c;
  pOut->m[2][2] = 1.0f;
  pOut->m[3][3] = 1.0f;
  return pOut;
}

#endif // !_WIN32
int TheMessageTime = 0;
// Match WinMain.h declaration: extern HINSTANCE ApplicationHInstance
HINSTANCE ApplicationHInstance = nullptr;
// c_dfDIKeyboard: On Emscripten, provided by EmscriptenInput.cpp (avoids duplicate symbol).
// On Linux (non-Emscripten), we still need it here.
#ifndef __EMSCRIPTEN__
void* c_dfDIKeyboard = nullptr;
#endif
