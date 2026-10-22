/*
 * texconv.h: NV2A textures -> data ready for glTexImage2D / glCompressedTexImage2D.
 *
 * Pure functions (no GL): `texconv_describe` interprets the state words of a texture stage
 * (the NV097 methods SET_TEXTURE_FORMAT, _CONTROL1 and _IMAGE_RECT) and says how it is
 * uploaded; `texconv_convert` does the CPU conversion (de-swizzle, palette, DXT without
 * S3TC, signed bump). The ground truth is `ngb_tools.xpr` (formats.py, swizzle.py,
 * pixels.py, dxt.py, bump.py): the tests compare against vectors generated with those
 * functions (tests/port/gen_texconv_vectors.py).
 *
 * Destination by format:
 *   - DXT1/3/5 with S3TC: the blocks as they are (GL_COMPRESSED_RGBA_S3TC_DXT*_EXT);
 *   - DXT without S3TC, packed and P8: RGBA8 (GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE), identical
 *     byte for byte to what ngb_tools.xpr exports;
 *   - V8U8: GL_RG8_SNORM (U, V) with the signed bytes as they are;
 *   - L6V5U5: GL_RGBA8_SNORM (U, V, L, 1): 5-bit U and V become round(x * 127 / 16) and the
 *     6-bit L becomes round(L * 127 / 63), in the B channel read by BUMPENVMAP_LUM.
 *
 * Layout of `dst`: face by face (6 if it is a cube map) and within each face level by level,
 * rows top to bottom with no padding. `level[i].dst_offset` is relative to the face.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define TEXCONV_MAX_LEVELS 13 /* up to 4096x4096 */
#define TEXCONV_MAX_SIZE 4096
#define TEXCONV_CUBE_FACES 6
#define TEXCONV_CUBE_FACE_ALIGN 128 /* each cube map face starts aligned to 128 bytes */

/* GL values `texconv_describe` can return (glcorearb.h, EXT_texture_compression_s3tc). */
#define TEXCONV_GL_BYTE 0x1400
#define TEXCONV_GL_UNSIGNED_BYTE 0x1401
#define TEXCONV_GL_RGBA 0x1908
#define TEXCONV_GL_RGBA8 0x8058
#define TEXCONV_GL_RG 0x8227
#define TEXCONV_GL_RG8_SNORM 0x8F95
#define TEXCONV_GL_RGBA8_SNORM 0x8F97
#define TEXCONV_GL_COMPRESSED_DXT1 0x83F1
#define TEXCONV_GL_COMPRESSED_DXT3 0x83F2
#define TEXCONV_GL_COMPRESSED_DXT5 0x83F3

typedef enum texconv_kind {
    TEXCONV_PACKED,   /* 8/16/32-bit word with ARGB/L fields */
    TEXCONV_PALETTE,  /* P8 */
    TEXCONV_DXT1,
    TEXCONV_DXT3,
    TEXCONV_DXT5,
    TEXCONV_V8U8,
    TEXCONV_L6V5U5,
} texconv_kind;

typedef struct texconv_level {
    int width, height;
    size_t src_offset, src_size; /* relative to the face */
    size_t dst_offset, dst_size;
} texconv_level;

typedef struct texconv_desc {
    uint8_t nv_format;      /* 8-bit color code */
    const char *name;       /* "A8R8G8B8", "DXT1"... (the ngb_tools.xpr one) */
    texconv_kind kind;
    int width, height, levels, faces;
    int swizzled, linear, compressed;
    int upload_compressed;  /* 1: dst are the DXT blocks (the driver has S3TC) */
    int element_size;       /* bytes per pixel or index; bytes per block if DXT */
    int pitch;              /* bytes per row (linear only) */
    unsigned gl_internal, gl_format, gl_type; /* gl_format and gl_type are 0 if compressed */
    size_t face_src_stride, face_dst_stride;
    size_t src_size, dst_size; /* all faces */
    texconv_level level[TEXCONV_MAX_LEVELS];
} texconv_desc;

enum texconv_error {
    TEXCONV_ERR_ARGS = -1,
    TEXCONV_ERR_FORMAT = -2,     /* unknown code or without conversion (depth, YUV...) */
    TEXCONV_ERR_DIMENSIONS = -3, /* volume texture, size 0 or larger than 4096 */
    TEXCONV_ERR_SHORT = -4,      /* `src` or `dst` shorter than the description says */
    TEXCONV_ERR_PALETTE = -5,    /* P8 without a palette */
};

/*
 * Describes a stage's texture. `format` is SET_TEXTURE_FORMAT (the Format field of an
 * XPR0); `control1` (SET_TEXTURE_CONTROL1, pitch in bits 16-31) and `image_rect`
 * (SET_TEXTURE_IMAGE_RECT, width in 16-31 and height in 0-15) only matter if linear.
 * `s3tc` = 1 if the driver supports GL_EXT_texture_compression_s3tc.
 */
int texconv_describe(uint32_t format, uint32_t control1, uint32_t image_rect, int s3tc,
                     texconv_desc *out);

/*
 * Converts `src` (desc->src_size bytes) into `dst` (desc->dst_size bytes). `palette` holds the
 * D3DCOLOR entries (B, G, R, A in memory) of a P8; missing ones give transparent black.
 */
int texconv_convert(const uint8_t *src, size_t src_len, const uint8_t *palette, size_t palette_len,
                    const texconv_desc *desc, uint8_t *dst, size_t dst_len);

/* Palette entries requested by SET_TEXTURE_PALETTE (bits 2-3: 256, 128, 64 or 32). */
int texconv_palette_entries(uint32_t palette_word);

/* Swizzled index (NV2A Morton order) of element (x, y) in a w x h image. */
uint32_t texconv_morton(int x, int y, int width, int height);

const char *texconv_error_text(int code);
