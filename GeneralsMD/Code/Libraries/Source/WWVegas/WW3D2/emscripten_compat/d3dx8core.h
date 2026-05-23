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

#ifndef D3DFVF_POSITION_MASK
#define D3DFVF_POSITION_MASK 0x40E
#endif

inline unsigned int D3DXGetFVFVertexSize(DWORD fvf) {
  unsigned int size = 0;

  // Position
  switch (fvf & D3DFVF_POSITION_MASK) {
    case D3DFVF_XYZ:    size += 3 * sizeof(float); break;    // 12 bytes
    case D3DFVF_XYZRHW: size += 4 * sizeof(float); break;    // 16 bytes (pre-transformed)
    case D3DFVF_XYZB1:  size += 4 * sizeof(float); break;    // 16 bytes (XYZ + 1 blend)
    case D3DFVF_XYZB2:  size += 5 * sizeof(float); break;    // 20 bytes
    case D3DFVF_XYZB3:  size += 6 * sizeof(float); break;    // 24 bytes
    case D3DFVF_XYZB4:
      if (fvf & D3DFVF_LASTBETA_UBYTE4)
        size += 6 * sizeof(float) + sizeof(unsigned int); // 3 blend floats + 1 DWORD index
      else
        size += 7 * sizeof(float); // 28 bytes
      break;
    case D3DFVF_XYZB5:  size += 8 * sizeof(float); break;
  }

  // Normal
  if (fvf & D3DFVF_NORMAL)
    size += 3 * sizeof(float);  // 12 bytes

  // Diffuse color
  if (fvf & D3DFVF_DIFFUSE)
    size += sizeof(unsigned int);  // 4 bytes (BGRA packed)

  // Specular color
  if (fvf & D3DFVF_SPECULAR)
    size += sizeof(unsigned int);  // 4 bytes (BGRA packed)

  // Texture coordinates
  unsigned int tex_count = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
  for (unsigned int i = 0; i < tex_count; i++) {
    unsigned int bits = (fvf >> (i * 2 + 16)) & 0x3;
    unsigned int dim = 2;
    switch (bits) {
      case D3DFVF_TEXTUREFORMAT1: dim = 1; break;
      case D3DFVF_TEXTUREFORMAT2: dim = 2; break;
      case D3DFVF_TEXTUREFORMAT3: dim = 3; break;
      case D3DFVF_TEXTUREFORMAT4: dim = 4; break;
    }
    size += dim * sizeof(float);
  }

  return size;
}
inline HRESULT D3DXGetErrorStringA(HRESULT hr, char *pBuffer,
                                   size_t BufferLength) {
  if (pBuffer && BufferLength > 0) {
    strncpy(pBuffer, "DXError", BufferLength);
    pBuffer[BufferLength - 1] = '\0';
  }
  return 0; // S_OK
}

#endif // __EMSCRIPTEN_D3DX8CORE_COMPAT_H
