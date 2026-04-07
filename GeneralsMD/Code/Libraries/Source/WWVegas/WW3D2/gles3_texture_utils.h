/*
** GeneralsX Web Port — Texture Utilities
**
** DX8 textures use BGRA byte order (Blue in byte 0, Green in byte 1,
** Red in byte 2, Alpha in byte 3). OpenGL ES 3.0 / WebGL 2.0 expects
** RGBA byte order for glTexImage2D uploads.
**
** This module provides:
**   1. CPU-side BGRA→RGBA byte swizzle for texture uploads
**   2. DDS header parsing for compressed texture loading
**   3. TGA loader (common format in Generals asset pipeline)
**
** PERFORMANCE NOTE:
** The BGRA swizzle runs during texture upload (not per-frame), so it only
** affects loading times. For a typical Generals session, texture uploads
** happen during map load and unit creation. The swizzle adds ~2ms per
** 1024x1024 texture on modern hardware — negligible compared to the
** decompression and GPU upload time.
**
** DXT/S3TC compressed textures do NOT need swizzling — the BGRA encoding
** is baked into the compression format and handled by the GPU's S3TC
** decoder (via WEBGL_compressed_texture_s3tc extension).
**
** LICENSE: GPL-3.0
*/

#pragma once

#if defined(__EMSCRIPTEN__)

#include <GLES3/gl3.h>

namespace GLES3_TextureUtils {

// ========================================================================
// BGRA → RGBA Byte Swizzle
// Converts DX8's BGRA pixel data to GL's RGBA in-place.
// Only needed for uncompressed 32-bit textures (D3DFMT_A8R8G8B8, X8R8G8B8).
// ========================================================================

// Swizzle in-place: swap R and B bytes for each pixel
// data: pointer to raw pixel data (modified in place)
// pixel_count: total number of pixels (width * height)
void BGRA_To_RGBA(unsigned char* data, unsigned int pixel_count);

// Swizzle with copy: creates a new buffer with RGBA data
// Returns malloc'd buffer — caller must free()
// Returns nullptr on allocation failure
unsigned char* BGRA_To_RGBA_Copy(const unsigned char* bgra_data,
                                  unsigned int pixel_count);

// ========================================================================
// 16-bit Format Converters
// DX8 uses some 16-bit formats with different bit layouts than GL
// ========================================================================

// A1R5G5B5 → GL_RGB5_A1 (swap bit order)
void A1R5G5B5_To_RGBA5551(unsigned short* data, unsigned int pixel_count);

// A4R4G4B4 → GL_RGBA4 (reorder channels)
void A4R4G4B4_To_RGBA4444(unsigned short* data, unsigned int pixel_count);

// ========================================================================
// DDS File Header Parsing
// Used to load .dds textures from game assets
// ========================================================================

struct DDS_Header {
    unsigned int width;
    unsigned int height;
    unsigned int mip_count;
    unsigned int format;        // D3DFMT_* value
    bool is_compressed;         // true for DXT1/3/5
    unsigned int data_offset;   // byte offset to pixel data
    unsigned int data_size;     // total pixel data size
};

// Parse a DDS file header
// Returns true if valid DDS, fills out header struct
bool Parse_DDS_Header(const void* file_data, unsigned int file_size,
                       DDS_Header* out_header);

// ========================================================================
// TGA File Loading
// Generals uses TGA for some UI textures and loading screens
// ========================================================================

struct TGA_Image {
    unsigned int width;
    unsigned int height;
    unsigned int bpp;           // bits per pixel (24 or 32)
    unsigned char* pixels;      // RGBA pixel data (malloc'd, caller frees)
};

// Load a TGA file, converting to RGBA
// Returns true on success, fills out image struct
bool Load_TGA(const void* file_data, unsigned int file_size,
               TGA_Image* out_image);

// ========================================================================
// GL Texture Upload Helper
// Combines format detection, swizzle, and upload into one call
// ========================================================================

// Create a GL texture from raw DX8-format pixel data
// Handles BGRA swizzle, format conversion, mipmap generation
// dx8_format: D3DFMT_* value (21=A8R8G8B8, 22=X8R8G8B8, etc.)
GLuint Upload_Texture(unsigned int width, unsigned int height,
                       unsigned int dx8_format,
                       unsigned int mip_levels,
                       const void* data, unsigned int data_size);

} // namespace GLES3_TextureUtils

#endif // __EMSCRIPTEN__
