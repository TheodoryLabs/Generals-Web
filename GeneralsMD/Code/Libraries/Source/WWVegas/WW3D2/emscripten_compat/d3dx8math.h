/*
**  GeneralsX Web Port — Minimal d3dx8math.h stub for Emscripten
**
**  Redirects to CompatLib's d3dx8math.h which provides GLM-backed
**  implementations of D3DX math functions.
**
**  GeneralsX @feature WebPort 09/03/2026
*/
#ifndef __EMSCRIPTEN_D3DX8MATH_H
#define __EMSCRIPTEN_D3DX8MATH_H

/* d3d8types.h provides the base types (D3DMATRIX, D3DVECTOR, etc.) */
#include "d3d8types.h"

/* ===================================================================
 * D3DXMATRIX — extends D3DMATRIX with operator overloads
 * =================================================================== */
#ifdef __cplusplus
struct D3DXMATRIX : public D3DMATRIX {
  D3DXMATRIX() {}
  D3DXMATRIX(float f11, float f12, float f13, float f14, float f21, float f22,
             float f23, float f24, float f31, float f32, float f33, float f34,
             float f41, float f42, float f43, float f44) {
    _11 = f11;
    _12 = f12;
    _13 = f13;
    _14 = f14;
    _21 = f21;
    _22 = f22;
    _23 = f23;
    _24 = f24;
    _31 = f31;
    _32 = f32;
    _33 = f33;
    _34 = f34;
    _41 = f41;
    _42 = f42;
    _43 = f43;
    _44 = f44;
  }

  /* Forward declare multiply — implemented after D3DXMatrixMultiply */
  D3DXMATRIX operator*(const D3DXMATRIX &other) const;
  D3DXMATRIX operator*=(const D3DXMATRIX &other);

  operator float *() { return &_11; }
  operator const float *() const { return &_11; }
};
#else
typedef D3DMATRIX D3DXMATRIX;
#endif

/* ===================================================================
 * D3DXVECTOR3 / D3DXVECTOR4
 * =================================================================== */
typedef D3DVECTOR D3DXVECTOR3;

#ifdef __cplusplus
struct D3DXVECTOR4 {
  D3DXVECTOR4() : x(0), y(0), z(0), w(0) {}
  D3DXVECTOR4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
  operator float *() { return &x; }
  operator const float *() const { return &x; }
  float x, y, z, w;
};
#else
typedef struct {
  float x, y, z, w;
} D3DXVECTOR4;
#endif

#define D3DX_PI 3.141592654f

/* ===================================================================
 * D3DX Math function declarations
 * =================================================================== */
#ifdef __cplusplus
extern "C" {
#endif

D3DXMATRIX *D3DXMatrixInverse(D3DXMATRIX *pOut, float *pDeterminant,
                              const D3DXMATRIX *pM);
D3DXMATRIX *D3DXMatrixScaling(D3DXMATRIX *pOut, float sx, float sy, float sz);
D3DXMATRIX *D3DXMatrixTranslation(D3DXMATRIX *pOut, float x, float y, float z);
D3DXMATRIX *D3DXMatrixMultiply(D3DXMATRIX *pOut, const D3DXMATRIX *pM1,
                               const D3DXMATRIX *pM2);
D3DXMATRIX *D3DXMatrixTranspose(D3DXMATRIX *pOut, const D3DXMATRIX *pM);
D3DXMATRIX *D3DXMatrixRotationZ(D3DXMATRIX *pOut, float angle);
D3DXVECTOR4 *D3DXVec3Transform(D3DXVECTOR4 *pOut, const D3DXVECTOR3 *pV,
                               const D3DXMATRIX *pM);
D3DXVECTOR4 *D3DXVec4Transform(D3DXVECTOR4 *pOut, const D3DXVECTOR4 *pV,
                               const D3DXMATRIX *pM);
float D3DXVec4Dot(const D3DXVECTOR4 *pV1, const D3DXVECTOR4 *pV2);

#ifdef __cplusplus
}
#endif

/* ===================================================================
 * Inline operator implementations (need D3DXMatrixMultiply)
 * =================================================================== */
#ifdef __cplusplus
inline D3DXMATRIX D3DXMATRIX::operator*(const D3DXMATRIX &other) const {
  D3DXMATRIX result;
  D3DXMatrixMultiply(&result, this, &other);
  return result;
}
inline D3DXMATRIX D3DXMATRIX::operator*=(const D3DXMATRIX &other) {
  D3DXMatrixMultiply(this, this, &other);
  return *this;
}
#endif

#endif /* __EMSCRIPTEN_D3DX8MATH_H */
