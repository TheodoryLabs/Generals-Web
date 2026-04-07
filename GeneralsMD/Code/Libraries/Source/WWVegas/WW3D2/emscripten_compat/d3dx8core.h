#ifndef __EMSCRIPTEN_D3DX8CORE_COMPAT_H
#define __EMSCRIPTEN_D3DX8CORE_COMPAT_H

#include "d3d8.h"
#include "d3d8types.h"

struct ID3DXBuffer {
  virtual ~ID3DXBuffer() {}
  virtual void *GetBufferPointer() = 0;
  virtual unsigned long GetBufferSize() = 0;
  virtual unsigned long Release() {
    delete this;
    return 0;
  }
};
typedef ID3DXBuffer *LPD3DXBUFFER;

inline HRESULT D3DXAssembleShader(const void *pSrcData, unsigned int SrcDataLen,
                                  DWORD Flags, LPD3DXBUFFER *ppConstants,
                                  LPD3DXBUFFER *ppCompiledShader,
                                  LPD3DXBUFFER *ppCompilationErrors) {
  // Pixel shader assembly is not supported on Emscripten/WebGL.
  // Return E_FAIL so callers skip the if(hr==0) branch and never
  // dereference the unset compiledShader pointer.
  if (ppCompiledShader) *ppCompiledShader = nullptr;
  return E_FAIL;
}

inline unsigned int D3DXGetFVFVertexSize(DWORD fvf) { return 32; }
inline HRESULT D3DXGetErrorStringA(HRESULT hr, char *pBuffer,
                                   size_t BufferLength) {
  if (pBuffer && BufferLength > 0) {
    strncpy(pBuffer, "DXError", BufferLength);
    pBuffer[BufferLength - 1] = '\0';
  }
  return 0; // S_OK
}

#endif // __EMSCRIPTEN_D3DX8CORE_COMPAT_H
