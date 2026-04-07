/*
**  GeneralsX Web Port — Minimal ddraw.h stub for Emscripten
**
**  Provides the bare minimum DirectDraw constants used by ddsfile.cpp.
**
**  GeneralsX @feature WebPort 09/03/2026
*/

#ifndef __EMSCRIPTEN_DDRAW_H
#define __EMSCRIPTEN_DDRAW_H

/* DDSCAPS2 flags used by DDS file loading */
#define DDSCAPS2_CUBEMAP        0x00000200L
#define DDSCAPS2_VOLUME         0x00200000L

/* DDSCAPS2 cubemap face flags (may be needed by DDS loaders) */
#define DDSCAPS2_CUBEMAP_POSITIVEX  0x00000400L
#define DDSCAPS2_CUBEMAP_NEGATIVEX  0x00000800L
#define DDSCAPS2_CUBEMAP_POSITIVEY  0x00001000L
#define DDSCAPS2_CUBEMAP_NEGATIVEY  0x00002000L
#define DDSCAPS2_CUBEMAP_POSITIVEZ  0x00004000L
#define DDSCAPS2_CUBEMAP_NEGATIVEZ  0x00008000L

/* DDSD flags for surface description */
#define DDSD_CAPS               0x00000001L
#define DDSD_HEIGHT             0x00000002L
#define DDSD_WIDTH              0x00000004L
#define DDSD_PITCH              0x00000008L
#define DDSD_PIXELFORMAT        0x00001000L
#define DDSD_MIPMAPCOUNT        0x00020000L
#define DDSD_LINEARSIZE         0x00080000L
#define DDSD_DEPTH              0x00800000L

/* DDPF pixel format flags */
#define DDPF_ALPHAPIXELS        0x00000001L
#define DDPF_FOURCC             0x00000004L
#define DDPF_RGB                0x00000040L
#define DDPF_LUMINANCE          0x00020000L

#endif /* __EMSCRIPTEN_DDRAW_H */
