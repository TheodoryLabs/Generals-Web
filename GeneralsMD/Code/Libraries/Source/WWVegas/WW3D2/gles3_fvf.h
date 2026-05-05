/*
** GeneralsX Web Port — FVF (Flexible Vertex Format) Decoder
**
** DX8's Flexible Vertex Format encodes the vertex layout as a bitmask.
** This module decodes FVF flags and sets up OpenGL ES 3.0 vertex attribute
** pointers (glVertexAttribPointer) to match the expected shader inputs.
**
** DX8 FVF vertex component order (strict, defined by Microsoft):
**   1. Position      (XYZ)        — 3 floats, 12 bytes
**   2. Blend weights (XYZB1-4)    — 1-4 floats (rarely used in Generals)
**   3. Normal        (NORMAL)     — 3 floats, 12 bytes
**   4. Diffuse color (DIFFUSE)    — 1 DWORD (BGRA packed), 4 bytes
**   5. Specular color(SPECULAR)   — 1 DWORD (BGRA packed), 4 bytes
**   6. Tex coords    (TEX1-8)     — 2-4 floats per set
**
** Our shaders expect these vertex attribute locations:
**   layout(location = 0) in vec3 a_Position;
**   layout(location = 1) in vec3 a_Normal;
**   layout(location = 2) in vec4 a_Color;       (D3DFVF_DIFFUSE)
**   layout(location = 3) in vec2 a_TexCoord0;
**   layout(location = 4) in vec2 a_TexCoord1;
**   layout(location = 5) in vec2 a_TexCoord2;
**   layout(location = 6) in vec2 a_TexCoord3;
**
** LICENSE: GPL-3.0
*/

#pragma once

#if defined(__EMSCRIPTEN__)

#include <GLES3/gl3.h>

// ============================================================================
// DX8 FVF Flag Constants
// These match the values from d3d8types.h / d3d9types.h
// ============================================================================

// Position flags
#define D3DFVF_XYZ              0x002   // x, y, z (untransformed)
#define D3DFVF_XYZRHW           0x004   // x, y, z, rhw (transformed)
#define D3DFVF_XYZB1            0x006   // x, y, z + 1 blend weight
#define D3DFVF_XYZB2            0x008
#define D3DFVF_XYZB3            0x00A
#define D3DFVF_XYZB4            0x00C
#define D3DFVF_XYZB5            0x00E

// Normal
#define D3DFVF_NORMAL           0x010

// Vertex colors
#define D3DFVF_DIFFUSE          0x040
#define D3DFVF_SPECULAR         0x080

// Texture coordinate count (encoded in bits 8-11)
#define D3DFVF_TEX0             0x000
#define D3DFVF_TEX1             0x100
#define D3DFVF_TEX2             0x200
#define D3DFVF_TEX3             0x300
#define D3DFVF_TEX4             0x400
#define D3DFVF_TEX5             0x500
#define D3DFVF_TEX6             0x600
#define D3DFVF_TEX7             0x700
#define D3DFVF_TEX8             0x800

// Mask for the texture count bits
#define D3DFVF_TEXCOUNT_MASK    0xF00
#define D3DFVF_TEXCOUNT_SHIFT   8

// Position mask
#define D3DFVF_POSITION_MASK    0x40E

// Blend weight flags
#define D3DFVF_LASTBETA_UBYTE4  0x1000

// Per-texture-coordinate dimension macros (D3DFVF_TEXCOORDSIZE*)
// Encoded in bits 16-31, 2 bits per texture coordinate set
// 00 = 2D (default), 01 = 3D, 10 = 4D, 11 = 1D
#define D3DFVF_TEXTUREFORMAT1   3   // 1D
#define D3DFVF_TEXTUREFORMAT2   0   // 2D (default)
#define D3DFVF_TEXTUREFORMAT3   1   // 3D
#define D3DFVF_TEXTUREFORMAT4   2   // 4D

// Macros to encode/decode per-texture-unit coordinate sizes
#define D3DFVF_TEXCOORDSIZE1(CoordIndex) (D3DFVF_TEXTUREFORMAT1 << (CoordIndex*2 + 16))
#define D3DFVF_TEXCOORDSIZE2(CoordIndex) (D3DFVF_TEXTUREFORMAT2 << (CoordIndex*2 + 16))
#define D3DFVF_TEXCOORDSIZE3(CoordIndex) (D3DFVF_TEXTUREFORMAT3 << (CoordIndex*2 + 16))
#define D3DFVF_TEXCOORDSIZE4(CoordIndex) (D3DFVF_TEXTUREFORMAT4 << (CoordIndex*2 + 16))

// Maximum texture coordinate sets
#define D3DDP_MAXTEXCOORD       8

// DX8_FVF_* are defined as enum values in dx8fvf.h — do NOT redefine them
// here as macros or they will stomp the enum identifiers via the preprocessor.

// ============================================================================
// GLES3_FVFDecoder — Decodes FVF bitmask into vertex attribute setup
// ============================================================================
class GLES3_FVFDecoder {
public:
    // Decode an FVF bitmask and compute byte offsets for each component
    void Decode(unsigned int fvf);

    // Set up glVertexAttribPointer calls for the decoded FVF.
    // Must be called while the correct VBO is bound.
    //
    // `base_offset_bytes` is added to every per-attribute offset before
    // it's passed to glVertexAttribPointer. This emulates DX8's
    // SetIndices(BaseVertexIndex) behaviour — the engine's dynamic VB
    // allocator returns a VertexBufferOffset (in vertices) that names the
    // first vertex of THIS draw's allocation inside the shared VBO. Since
    // WebGL2/GLES3 has no `glDrawElementsBaseVertex`, we compensate by
    // sliding the attribute base pointer forward by base_offset_bytes
    // (= base_vtx_index * stride) so index 0 reads OUR first vertex.
    void Apply_Vertex_Attribs(unsigned int stride,
                              unsigned int base_offset_bytes = 0) const;

    // Disable all vertex attributes (call when switching VBOs)
    static void Disable_All_Attribs();

    // Compute the total vertex size in bytes for a given FVF
    static unsigned int Get_FVF_Vertex_Size(unsigned int fvf);

    // Get number of texture coordinate sets from FVF
    static unsigned int Get_Tex_Count(unsigned int fvf);

    // Get the dimension of a specific texture coordinate set (1-4 floats)
    static unsigned int Get_Tex_Coord_Size(unsigned int fvf, unsigned int index);

    // Accessors for computed offsets
    unsigned int Get_Location_Offset() const   { return location_offset; }
    unsigned int Get_Normal_Offset() const     { return normal_offset; }
    unsigned int Get_Diffuse_Offset() const    { return diffuse_offset; }
    unsigned int Get_Specular_Offset() const   { return specular_offset; }
    unsigned int Get_Tex_Offset(unsigned int n) const;
    unsigned int Get_FVF_Size() const          { return fvf_size; }
    unsigned int Get_FVF() const               { return fvf; }

    // Query which components are present
    bool Has_Position() const  { return (fvf & D3DFVF_POSITION_MASK) != 0; }
    bool Has_Normal() const    { return (fvf & D3DFVF_NORMAL) != 0; }
    bool Has_Diffuse() const   { return (fvf & D3DFVF_DIFFUSE) != 0; }
    bool Has_Specular() const  { return (fvf & D3DFVF_SPECULAR) != 0; }

    // Vertex attribute locations (matching shader layout locations)
    static constexpr int ATTR_POSITION  = 0;
    static constexpr int ATTR_NORMAL    = 1;
    static constexpr int ATTR_COLOR     = 2;
    static constexpr int ATTR_TEXCOORD0 = 3;
    static constexpr int ATTR_TEXCOORD1 = 4;
    static constexpr int ATTR_TEXCOORD2 = 5;
    static constexpr int ATTR_TEXCOORD3 = 6;

private:
    unsigned int fvf = 0;
    unsigned int fvf_size = 0;

    // Byte offsets for each component within the vertex
    unsigned int location_offset = 0;
    unsigned int blend_offset = 0;
    unsigned int normal_offset = 0;
    unsigned int diffuse_offset = 0;
    unsigned int specular_offset = 0;
    unsigned int texcoord_offset[D3DDP_MAXTEXCOORD] = {0};
};

#endif // __EMSCRIPTEN__
