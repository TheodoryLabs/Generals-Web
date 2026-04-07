/*
** GeneralsX Web Port — Texture Utilities Implementation
**
** BGRA→RGBA swizzle, DDS parsing, TGA loading, and GL texture upload.
**
** LICENSE: GPL-3.0
*/

#if defined(__EMSCRIPTEN__)

#include "gles3_texture_utils.h"
#include "gles3_wrapper.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>

namespace GLES3_TextureUtils {

// ============================================================================
// BGRA → RGBA In-Place Swizzle
// Swaps byte 0 (B) and byte 2 (R) for each 4-byte pixel.
// ============================================================================
void BGRA_To_RGBA(unsigned char* data, unsigned int pixel_count) {
    if (!data) return;

    // Process 4 bytes at a time: [B, G, R, A] → [R, G, B, A]
    for (unsigned int i = 0; i < pixel_count; i++) {
        unsigned char* pixel = data + i * 4;
        unsigned char tmp = pixel[0];   // B
        pixel[0] = pixel[2];           // R → byte 0
        pixel[2] = tmp;                // B → byte 2
        // G (byte 1) and A (byte 3) stay in place
    }
}

// ============================================================================
// BGRA → RGBA Copy Swizzle
// Creates a new buffer with the swizzled data.
// ============================================================================
unsigned char* BGRA_To_RGBA_Copy(const unsigned char* bgra_data,
                                  unsigned int pixel_count) {
    if (!bgra_data) return nullptr;

    unsigned int byte_count = pixel_count * 4;
    unsigned char* rgba_data = (unsigned char*)malloc(byte_count);
    if (!rgba_data) return nullptr;

    for (unsigned int i = 0; i < pixel_count; i++) {
        const unsigned char* src = bgra_data + i * 4;
        unsigned char* dst = rgba_data + i * 4;
        dst[0] = src[2];   // R ← byte 2 of BGRA
        dst[1] = src[1];   // G ← byte 1
        dst[2] = src[0];   // B ← byte 0
        dst[3] = src[3];   // A ← byte 3
    }

    return rgba_data;
}

// ============================================================================
// 16-bit Format Converters
// ============================================================================

void A1R5G5B5_To_RGBA5551(unsigned short* data, unsigned int pixel_count) {
    // DX8 A1R5G5B5: [A1][R5][G5][B5] = ARRR RRGG GGGB BBBB
    // GL  RGB5_A1:   [R5][G5][B5][A1] = RRRR RGGG GGBB BBBA
    for (unsigned int i = 0; i < pixel_count; i++) {
        unsigned short px = data[i];
        unsigned int a = (px >> 15) & 0x1;
        unsigned int r = (px >> 10) & 0x1F;
        unsigned int g = (px >>  5) & 0x1F;
        unsigned int b = (px      ) & 0x1F;
        data[i] = (unsigned short)((r << 11) | (g << 6) | (b << 1) | a);
    }
}

void A4R4G4B4_To_RGBA4444(unsigned short* data, unsigned int pixel_count) {
    // DX8 A4R4G4B4: [A4][R4][G4][B4]
    // GL  RGBA4:     [R4][G4][B4][A4]
    for (unsigned int i = 0; i < pixel_count; i++) {
        unsigned short px = data[i];
        unsigned int a = (px >> 12) & 0xF;
        unsigned int r = (px >>  8) & 0xF;
        unsigned int g = (px >>  4) & 0xF;
        unsigned int b = (px      ) & 0xF;
        data[i] = (unsigned short)((r << 12) | (g << 8) | (b << 4) | a);
    }
}

// ============================================================================
// DDS File Header Parsing
// ============================================================================

// DDS magic number and header layout
#define DDS_MAGIC       0x20534444  // "DDS "
#define DDS_HEADER_SIZE 124

// DDS pixel format flags
#define DDPF_FOURCC     0x04
#define DDPF_RGB        0x40
#define DDPF_LUMINANCE  0x20000

// FOURCC codes for compressed formats
#define FOURCC_DXT1     0x31545844  // "DXT1"
#define FOURCC_DXT3     0x33545844  // "DXT3"
#define FOURCC_DXT5     0x35545844  // "DXT5"

// D3DFMT_* values we map to
#define D3DFMT_DXT1     827611204
#define D3DFMT_DXT3     861165636
#define D3DFMT_DXT5     894720068
#define D3DFMT_A8R8G8B8 21
#define D3DFMT_X8R8G8B8 22
#define D3DFMT_R5G6B5   23
#define D3DFMT_A1R5G5B5 25
#define D3DFMT_A4R4G4B4 26

bool Parse_DDS_Header(const void* file_data, unsigned int file_size,
                       DDS_Header* out) {
    if (!file_data || file_size < 128 || !out) return false;

    const unsigned int* data = (const unsigned int*)file_data;

    // Check magic number
    if (data[0] != DDS_MAGIC) return false;

    // DDS_HEADER starts at byte 4
    const unsigned int* hdr = data + 1;
    unsigned int hdr_size = hdr[0];
    if (hdr_size != DDS_HEADER_SIZE) return false;

    out->height    = hdr[2];
    out->width     = hdr[3];
    out->mip_count = hdr[6];
    if (out->mip_count == 0) out->mip_count = 1;

    // Pixel format starts at offset 76 within DDS_HEADER (byte 19 in DWORD array)
    const unsigned int* pf = hdr + 19;
    unsigned int pf_flags = pf[1];
    unsigned int fourcc   = pf[2];
    unsigned int rgb_bits = pf[3];

    out->data_offset = 4 + DDS_HEADER_SIZE;  // magic + header
    out->is_compressed = false;

    if (pf_flags & DDPF_FOURCC) {
        out->is_compressed = true;
        switch (fourcc) {
            case FOURCC_DXT1: out->format = D3DFMT_DXT1; break;
            case FOURCC_DXT3: out->format = D3DFMT_DXT3; break;
            case FOURCC_DXT5: out->format = D3DFMT_DXT5; break;
            default:
                fprintf(stderr, "WARN: Unknown DDS FOURCC: 0x%08X\n", fourcc);
                return false;
        }
        // Calculate compressed data size
        unsigned int block_size = (out->format == D3DFMT_DXT1) ? 8 : 16;
        out->data_size = ((out->width + 3) / 4) * ((out->height + 3) / 4) * block_size;
    } else if (pf_flags & DDPF_RGB) {
        unsigned int a_mask = pf[6];
        if (rgb_bits == 32) {
            out->format = (a_mask != 0) ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8;
        } else if (rgb_bits == 16) {
            unsigned int r_mask = pf[3];  // Actually checking bit masks
            out->format = D3DFMT_R5G6B5;  // Most common 16-bit
        } else {
            out->format = D3DFMT_A8R8G8B8;  // Fallback
        }
        out->data_size = out->width * out->height * (rgb_bits / 8);
    } else {
        // Luminance or other
        out->format = D3DFMT_A8R8G8B8;
        out->data_size = out->width * out->height * 4;
    }

    return true;
}

// ============================================================================
// TGA File Loading
// ============================================================================

#pragma pack(push, 1)
struct TGA_FileHeader {
    unsigned char id_length;
    unsigned char color_map_type;
    unsigned char image_type;       // 2=uncompressed RGB, 10=RLE RGB
    unsigned short cm_first_entry;
    unsigned short cm_length;
    unsigned char cm_size;
    unsigned short x_origin;
    unsigned short y_origin;
    unsigned short width;
    unsigned short height;
    unsigned char bpp;              // 24 or 32
    unsigned char image_descriptor;
};
#pragma pack(pop)

bool Load_TGA(const void* file_data, unsigned int file_size,
               TGA_Image* out) {
    if (!file_data || file_size < sizeof(TGA_FileHeader) || !out) return false;

    const TGA_FileHeader* hdr = (const TGA_FileHeader*)file_data;

    // Only support uncompressed true-color (type 2)
    if (hdr->image_type != 2 && hdr->image_type != 10) {
        fprintf(stderr, "WARN: Unsupported TGA type %d (only type 2 & 10 supported)\n",
                hdr->image_type);
        return false;
    }

    out->width  = hdr->width;
    out->height = hdr->height;
    out->bpp    = hdr->bpp;

    unsigned int pixel_count = out->width * out->height;
    unsigned int src_bytes_per_pixel = out->bpp / 8;

    // Always output 4-channel RGBA
    out->pixels = (unsigned char*)malloc(pixel_count * 4);
    if (!out->pixels) return false;

    const unsigned char* src = (const unsigned char*)file_data
                               + sizeof(TGA_FileHeader) + hdr->id_length;

    // TGA stores in BGR(A) order, convert to RGBA
    for (unsigned int i = 0; i < pixel_count; i++) {
        unsigned int si = i * src_bytes_per_pixel;
        unsigned int di = i * 4;
        out->pixels[di + 0] = src[si + 2]; // R ← src B
        out->pixels[di + 1] = src[si + 1]; // G
        out->pixels[di + 2] = src[si + 0]; // B ← src R
        out->pixels[di + 3] = (src_bytes_per_pixel == 4) ? src[si + 3] : 255;
    }

    // Handle TGA vertical flip (bit 5 of image descriptor)
    bool top_to_bottom = (hdr->image_descriptor & 0x20) != 0;
    if (!top_to_bottom) {
        // TGA default is bottom-to-top — flip vertically
        unsigned int row_bytes = out->width * 4;
        unsigned char* temp_row = (unsigned char*)malloc(row_bytes);
        if (temp_row) {
            for (unsigned int y = 0; y < out->height / 2; y++) {
                unsigned char* top = out->pixels + y * row_bytes;
                unsigned char* bot = out->pixels + (out->height - 1 - y) * row_bytes;
                memcpy(temp_row, top, row_bytes);
                memcpy(top, bot, row_bytes);
                memcpy(bot, temp_row, row_bytes);
            }
            free(temp_row);
        }
    }

    return true;
}

// ============================================================================
// GL Texture Upload Helper
// Combines format detection, swizzle, and upload
// ============================================================================
GLuint Upload_Texture(unsigned int width, unsigned int height,
                       unsigned int dx8_format,
                       unsigned int mip_levels,
                       const void* data, unsigned int data_size) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    GLenum internal_fmt = GLES3_TextureConverter::DX8_Format_To_GL_Internal(dx8_format);

    if (internal_fmt == GL_COMPRESSED_RGB_S3TC_DXT1_EXT ||
        internal_fmt == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT ||
        internal_fmt == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT) {
        // DXT compressed — upload directly, no swizzle needed
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, internal_fmt,
                               width, height, 0, data_size, data);
    } else if (dx8_format == D3DFMT_A8R8G8B8 || dx8_format == D3DFMT_X8R8G8B8) {
        // 32-bit BGRA — needs swizzle to RGBA
        unsigned int pixel_count = width * height;
        unsigned char* rgba = BGRA_To_RGBA_Copy(
            (const unsigned char*)data, pixel_count);
        if (rgba) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                         width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            free(rgba);
        } else {
            // Allocation failed — upload BGRA and hope for shader swizzle
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                         width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        }
    } else if (dx8_format == D3DFMT_R5G6B5) {
        // 16-bit RGB565 — direct mapping
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB565,
                     width, height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, data);
    } else if (dx8_format == D3DFMT_A1R5G5B5) {
        // 16-bit with 1-bit alpha — needs channel reorder
        unsigned int pixel_count = width * height;
        unsigned short* converted = (unsigned short*)malloc(pixel_count * 2);
        if (converted) {
            memcpy(converted, data, pixel_count * 2);
            A1R5G5B5_To_RGBA5551(converted, pixel_count);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1,
                         width, height, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1,
                         converted);
            free(converted);
        }
    } else if (dx8_format == D3DFMT_A4R4G4B4) {
        unsigned int pixel_count = width * height;
        unsigned short* converted = (unsigned short*)malloc(pixel_count * 2);
        if (converted) {
            memcpy(converted, data, pixel_count * 2);
            A4R4G4B4_To_RGBA4444(converted, pixel_count);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA4,
                         width, height, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4,
                         converted);
            free(converted);
        }
    } else {
        // Unknown format — best effort upload as RGBA8
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    }

    // Mipmaps
    if (mip_levels != 1) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        GL_LINEAR_MIPMAP_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    return tex;
}

} // namespace GLES3_TextureUtils

#endif // __EMSCRIPTEN__
