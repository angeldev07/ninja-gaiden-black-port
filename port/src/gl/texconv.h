/*
 * texconv.h: texturas del NV2A -> datos listos para glTexImage2D / glCompressedTexImage2D.
 *
 * Funciones puras (sin GL): `texconv_describe` interpreta las palabras de estado de una
 * etapa de textura (los métodos NV097 SET_TEXTURE_FORMAT, _CONTROL1 e _IMAGE_RECT) y dice
 * cómo se sube; `texconv_convert` hace la conversión en CPU (des-swizzle, paleta, DXT sin
 * S3TC, relieve con signo). La referencia de verdad es `ngb_tools.xpr` (formats.py,
 * swizzle.py, pixels.py, dxt.py, bump.py): los tests comparan con vectores generados con
 * esas funciones (tests/port/gen_texconv_vectors.py).
 *
 * Destino según el formato:
 *   - DXT1/3/5 con S3TC: los bloques tal cual (GL_COMPRESSED_RGBA_S3TC_DXT*_EXT);
 *   - DXT sin S3TC, empaquetados y P8: RGBA8 (GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE), idéntico
 *     byte a byte a lo que exporta ngb_tools.xpr;
 *   - V8U8: GL_RG8_SNORM (U, V) con los bytes con signo tal cual;
 *   - L6V5U5: GL_RGBA8_SNORM (U, V, L, 1): U y V de 5 bits pasan a round(x * 127 / 16) y L
 *     de 6 bits a round(L * 127 / 63), en el canal B que lee BUMPENVMAP_LUM.
 *
 * Disposición de `dst`: cara a cara (6 si es cube map) y en cada cara nivel a nivel, filas
 * de arriba abajo sin relleno. `level[i].dst_offset` es relativo a la cara.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define TEXCONV_MAX_LEVELS 13 /* hasta 4096x4096 */
#define TEXCONV_MAX_SIZE 4096
#define TEXCONV_CUBE_FACES 6
#define TEXCONV_CUBE_FACE_ALIGN 128 /* cada cara del cube map empieza alineada a 128 bytes */

/* Valores de GL que puede devolver `texconv_describe` (glcorearb.h, EXT_texture_compression_s3tc). */
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
    TEXCONV_PACKED,   /* palabra de 8/16/32 bits con campos ARGB/L */
    TEXCONV_PALETTE,  /* P8 */
    TEXCONV_DXT1,
    TEXCONV_DXT3,
    TEXCONV_DXT5,
    TEXCONV_V8U8,
    TEXCONV_L6V5U5,
} texconv_kind;

typedef struct texconv_level {
    int width, height;
    size_t src_offset, src_size; /* relativos a la cara */
    size_t dst_offset, dst_size;
} texconv_level;

typedef struct texconv_desc {
    uint8_t nv_format;      /* código de color de 8 bits */
    const char *name;       /* "A8R8G8B8", "DXT1"... (el de ngb_tools.xpr) */
    texconv_kind kind;
    int width, height, levels, faces;
    int swizzled, linear, compressed;
    int upload_compressed;  /* 1: dst son los bloques DXT (el driver tiene S3TC) */
    int element_size;       /* bytes por píxel o índice; bytes por bloque si es DXT */
    int pitch;              /* bytes por fila (solo lineales) */
    unsigned gl_internal, gl_format, gl_type; /* gl_format y gl_type son 0 si es comprimida */
    size_t face_src_stride, face_dst_stride;
    size_t src_size, dst_size; /* todas las caras */
    texconv_level level[TEXCONV_MAX_LEVELS];
} texconv_desc;

enum texconv_error {
    TEXCONV_ERR_ARGS = -1,
    TEXCONV_ERR_FORMAT = -2,     /* código desconocido o sin conversión (profundidad, YUV...) */
    TEXCONV_ERR_DIMENSIONS = -3, /* textura de volumen, tamaño 0 o mayor de 4096 */
    TEXCONV_ERR_SHORT = -4,      /* `src` o `dst` más cortos de lo que dice la descripción */
    TEXCONV_ERR_PALETTE = -5,    /* P8 sin paleta */
};

/*
 * Describe la textura de una etapa. `format` es SET_TEXTURE_FORMAT (el campo Format de un
 * XPR0); `control1` (SET_TEXTURE_CONTROL1, pitch en los bits 16-31) e `image_rect`
 * (SET_TEXTURE_IMAGE_RECT, ancho en 16-31 y alto en 0-15) solo cuentan si es lineal.
 * `s3tc` = 1 si el driver admite GL_EXT_texture_compression_s3tc.
 */
int texconv_describe(uint32_t format, uint32_t control1, uint32_t image_rect, int s3tc,
                     texconv_desc *out);

/*
 * Convierte `src` (desc->src_size bytes) en `dst` (desc->dst_size bytes). `palette` son las
 * entradas D3DCOLOR (B, G, R, A en memoria) de una P8; las que falten dan negro transparente.
 */
int texconv_convert(const uint8_t *src, size_t src_len, const uint8_t *palette, size_t palette_len,
                    const texconv_desc *desc, uint8_t *dst, size_t dst_len);

/* Entradas de paleta que pide SET_TEXTURE_PALETTE (bits 2-3: 256, 128, 64 o 32). */
int texconv_palette_entries(uint32_t palette_word);

/* Índice swizzled (orden Morton de la NV2A) del elemento (x, y) en una imagen w x h. */
uint32_t texconv_morton(int x, int y, int width, int height);

const char *texconv_error_text(int code);
