/*
 * texconv.c: conversión de texturas del NV2A para OpenGL (ver texconv.h).
 *
 * Es una traducción a C de ngb_tools.xpr, que es la referencia: la tabla de formatos
 * (formats.py: códigos, bits por píxel y campos de cada canal), el orden Morton
 * (swizzle.py: máscaras de bits de x e y repartidas alternando desde el bit bajo), la
 * ampliación de canales (pixels.py: replicar bits por debajo de 8, quedarse con el byte alto
 * por encima), la paleta D3DCOLOR (pixels.py) y S3TC (dxt.py: divisiones que truncan, DXT1
 * de 3 colores si c0 <= c1, DXT3/5 siempre de 4).
 *
 * Fuentes del formato: xemu (hw/xbox/nv2a/nv2a_regs.h: campos de SET_TEXTURE_FORMAT,
 * CONTROL1, IMAGE_RECT y PALETTE; pgraph/texture.c: tamaño de los niveles y cube maps con
 * caras alineadas a 128 bytes; pgraph/swizzle.c; pgraph/s3tc.c). Sin código copiado.
 */
#include "texconv.h"

#include <string.h>

/* Un canal: campo de `bits` bits desde `shift`, o la constante `value` si bits = 0. */
typedef struct chan {
    uint8_t shift, bits, value;
} chan;

typedef struct fmt_info {
    uint8_t code;
    const char *name;
    uint8_t bits_per_pixel;
    uint8_t linear;
    texconv_kind kind;
    chan r, g, b, a;
} fmt_info;

#define F(s, n) {(s), (n), 0}
#define K(v) {0, 0, (v)}
#define P(code, name, bpp, lin, r, g, b, a) {code, name, bpp, lin, TEXCONV_PACKED, r, g, b, a}
#define S(code, name, bpp, lin, kind) {code, name, bpp, lin, kind, K(0), K(0), K(0), K(0)}

static const fmt_info k_formats[] = {
    P(0x00, "L8", 8, 0, F(0, 8), F(0, 8), F(0, 8), K(255)),
    P(0x01, "AL8", 8, 0, F(0, 8), F(0, 8), F(0, 8), F(0, 8)),
    P(0x02, "A1R5G5B5", 16, 0, F(10, 5), F(5, 5), F(0, 5), F(15, 1)),
    P(0x03, "X1R5G5B5", 16, 0, F(10, 5), F(5, 5), F(0, 5), K(255)),
    P(0x04, "A4R4G4B4", 16, 0, F(8, 4), F(4, 4), F(0, 4), F(12, 4)),
    P(0x05, "R5G6B5", 16, 0, F(11, 5), F(5, 6), F(0, 5), K(255)),
    P(0x06, "A8R8G8B8", 32, 0, F(16, 8), F(8, 8), F(0, 8), F(24, 8)),
    P(0x07, "X8R8G8B8", 32, 0, F(16, 8), F(8, 8), F(0, 8), K(255)),
    S(0x0B, "P8", 8, 0, TEXCONV_PALETTE),
    S(0x0C, "DXT1", 4, 0, TEXCONV_DXT1),
    S(0x0E, "DXT3", 8, 0, TEXCONV_DXT3),
    S(0x0F, "DXT5", 8, 0, TEXCONV_DXT5),
    P(0x10, "LIN_A1R5G5B5", 16, 1, F(10, 5), F(5, 5), F(0, 5), F(15, 1)),
    P(0x11, "LIN_R5G6B5", 16, 1, F(11, 5), F(5, 6), F(0, 5), K(255)),
    P(0x12, "LIN_A8R8G8B8", 32, 1, F(16, 8), F(8, 8), F(0, 8), F(24, 8)),
    P(0x13, "LIN_L8", 8, 1, F(0, 8), F(0, 8), F(0, 8), K(255)),
    P(0x19, "A8", 8, 0, K(255), K(255), K(255), F(0, 8)),
    P(0x1A, "A8L8", 16, 0, F(0, 8), F(0, 8), F(0, 8), F(8, 8)),
    P(0x1B, "LIN_AL8", 8, 1, F(0, 8), F(0, 8), F(0, 8), F(0, 8)),
    P(0x1C, "LIN_X1R5G5B5", 16, 1, F(10, 5), F(5, 5), F(0, 5), K(255)),
    P(0x1D, "LIN_A4R4G4B4", 16, 1, F(8, 4), F(4, 4), F(0, 4), F(12, 4)),
    P(0x1E, "LIN_X8R8G8B8", 32, 1, F(16, 8), F(8, 8), F(0, 8), K(255)),
    P(0x1F, "LIN_A8", 8, 1, K(255), K(255), K(255), F(0, 8)),
    P(0x20, "LIN_A8L8", 16, 1, F(0, 8), F(0, 8), F(0, 8), F(8, 8)),
    S(0x27, "L6V5U5", 16, 0, TEXCONV_L6V5U5),
    S(0x28, "V8U8", 16, 0, TEXCONV_V8U8),
    P(0x32, "L16", 16, 0, F(0, 16), F(0, 16), F(0, 16), K(255)),
    P(0x35, "LIN_L16", 16, 1, F(0, 16), F(0, 16), F(0, 16), K(255)),
    S(0x37, "LIN_L6V5U5", 16, 1, TEXCONV_L6V5U5),
    P(0x38, "R5G5B5A1", 16, 0, F(11, 5), F(6, 5), F(1, 5), F(0, 1)),
    P(0x39, "R4G4B4A4", 16, 0, F(12, 4), F(8, 4), F(4, 4), F(0, 4)),
    P(0x3A, "A8B8G8R8", 32, 0, F(0, 8), F(8, 8), F(16, 8), F(24, 8)),
    P(0x3B, "B8G8R8A8", 32, 0, F(8, 8), F(16, 8), F(24, 8), F(0, 8)),
    P(0x3C, "R8G8B8A8", 32, 0, F(24, 8), F(16, 8), F(8, 8), F(0, 8)),
    P(0x3D, "LIN_R5G5B5A1", 16, 1, F(11, 5), F(6, 5), F(1, 5), F(0, 1)),
    P(0x3E, "LIN_R4G4B4A4", 16, 1, F(12, 4), F(8, 4), F(4, 4), F(0, 4)),
    P(0x3F, "LIN_A8B8G8R8", 32, 1, F(0, 8), F(8, 8), F(16, 8), F(24, 8)),
    P(0x40, "LIN_B8G8R8A8", 32, 1, F(8, 8), F(16, 8), F(24, 8), F(0, 8)),
    P(0x41, "LIN_R8G8B8A8", 32, 1, F(24, 8), F(16, 8), F(8, 8), F(0, 8)),
};

const char *texconv_error_text(int code) {
    switch (code) {
    case TEXCONV_ERR_ARGS: return "argumentos inválidos";
    case TEXCONV_ERR_FORMAT: return "formato sin conversión";
    case TEXCONV_ERR_DIMENSIONS: return "dimensiones no admitidas";
    case TEXCONV_ERR_SHORT: return "datos más cortos de lo que pide la textura";
    case TEXCONV_ERR_PALETTE: return "P8 sin paleta";
    default: return code >= 0 ? "ok" : "error desconocido";
    }
}

static const fmt_info *lookup(uint8_t code) {
    size_t i;
    for (i = 0; i < sizeof k_formats / sizeof k_formats[0]; i++) {
        if (k_formats[i].code == code) {
            return &k_formats[i];
        }
    }
    return NULL;
}

int texconv_palette_entries(uint32_t palette_word) {
    return 256 >> ((palette_word >> 2) & 3u);
}

/* ---- Morton --------------------------------------------------------------------------- */

static void morton_masks(int width, int height, uint32_t *mask_x, uint32_t *mask_y) {
    uint32_t mx = 0, my = 0, bit_out = 1;
    int bit;
    for (bit = 1; bit < width || bit < height; bit <<= 1) {
        if (bit < width) {
            mx |= bit_out;
            bit_out <<= 1;
        }
        if (bit < height) {
            my |= bit_out;
            bit_out <<= 1;
        }
    }
    *mask_x = mx;
    *mask_y = my;
}

/* Deposita los bits bajos de `value` en las posiciones a 1 de `mask` (pdep). */
static uint32_t deposit(uint32_t value, uint32_t mask) {
    uint32_t out = 0, bit;
    for (bit = 1; mask; bit <<= 1) {
        uint32_t lowest = mask & (~mask + 1u);
        if (value & bit) {
            out |= lowest;
        }
        mask &= mask - 1u;
    }
    return out;
}

uint32_t texconv_morton(int x, int y, int width, int height) {
    uint32_t mx, my;
    morton_masks(width, height, &mx, &my);
    return deposit((uint32_t)x, mx) | deposit((uint32_t)y, my);
}

/* ---- descripción ---------------------------------------------------------------------- */

static int is_dxt(texconv_kind kind) {
    return kind == TEXCONV_DXT1 || kind == TEXCONV_DXT3 || kind == TEXCONV_DXT5;
}

static size_t src_level_size(const texconv_desc *d, int w, int h) {
    if (d->compressed) {
        return (size_t)((w + 3) / 4) * (size_t)((h + 3) / 4) * (size_t)d->element_size;
    }
    if (d->linear) {
        return (size_t)d->pitch * (size_t)h;
    }
    return (size_t)w * (size_t)h * (size_t)d->element_size;
}

static size_t dst_level_size(const texconv_desc *d, int w, int h, size_t src_size) {
    if (d->upload_compressed) {
        return src_size;
    }
    if (d->kind == TEXCONV_V8U8) {
        return (size_t)w * (size_t)h * 2;
    }
    return (size_t)w * (size_t)h * 4;
}

static void set_gl_format(texconv_desc *d) {
    switch (d->kind) {
    case TEXCONV_DXT1:
    case TEXCONV_DXT3:
    case TEXCONV_DXT5:
        if (d->upload_compressed) {
            d->gl_internal = d->kind == TEXCONV_DXT1   ? TEXCONV_GL_COMPRESSED_DXT1
                             : d->kind == TEXCONV_DXT3 ? TEXCONV_GL_COMPRESSED_DXT3
                                                       : TEXCONV_GL_COMPRESSED_DXT5;
            return;
        }
        break;
    case TEXCONV_V8U8:
        d->gl_internal = TEXCONV_GL_RG8_SNORM;
        d->gl_format = TEXCONV_GL_RG;
        d->gl_type = TEXCONV_GL_BYTE;
        return;
    case TEXCONV_L6V5U5:
        d->gl_internal = TEXCONV_GL_RGBA8_SNORM;
        d->gl_format = TEXCONV_GL_RGBA;
        d->gl_type = TEXCONV_GL_BYTE;
        return;
    default:
        break;
    }
    d->gl_internal = TEXCONV_GL_RGBA8;
    d->gl_format = TEXCONV_GL_RGBA;
    d->gl_type = TEXCONV_GL_UNSIGNED_BYTE;
}

int texconv_describe(uint32_t format, uint32_t control1, uint32_t image_rect, int s3tc,
                     texconv_desc *out) {
    const fmt_info *info;
    int cube, dims, levels, fitting, k, w, h;
    size_t src_total = 0, dst_total = 0;
    if (!out) {
        return TEXCONV_ERR_ARGS;
    }
    memset(out, 0, sizeof *out);
    info = lookup((uint8_t)((format >> 8) & 0xFFu));
    if (!info) {
        return TEXCONV_ERR_FORMAT;
    }
    cube = (int)((format >> 2) & 1u);
    dims = (int)((format >> 4) & 0xFu);
    if (dims != 2) {
        return TEXCONV_ERR_DIMENSIONS; /* 3 = volumen: sin soporte */
    }
    out->nv_format = info->code;
    out->name = info->name;
    out->kind = info->kind;
    out->linear = info->linear;
    out->swizzled = !info->linear && !is_dxt(info->kind);
    out->compressed = is_dxt(info->kind);
    out->upload_compressed = out->compressed && s3tc;
    out->element_size = out->compressed ? 4 * info->bits_per_pixel * 4 / 8 : info->bits_per_pixel / 8;
    out->faces = cube ? TEXCONV_CUBE_FACES : 1;
    if (info->linear) {
        w = (int)(image_rect >> 16);
        h = (int)(image_rect & 0xFFFFu);
        out->pitch = (int)(control1 >> 16);
        levels = 1;
        if (cube || out->pitch < w * out->element_size) {
            return TEXCONV_ERR_DIMENSIONS;
        }
    } else {
        w = 1 << ((format >> 20) & 0xFu);
        h = 1 << ((format >> 24) & 0xFu);
        levels = (int)((format >> 16) & 0xFu);
        fitting = 1;
        while ((1 << fitting) <= (w > h ? w : h)) {
            fitting++;
        }
        levels = levels < fitting ? levels : fitting;
        levels = levels < 1 ? 1 : levels;
        if (cube && w != h) {
            return TEXCONV_ERR_DIMENSIONS;
        }
    }
    if (w < 1 || h < 1 || w > TEXCONV_MAX_SIZE || h > TEXCONV_MAX_SIZE) {
        return TEXCONV_ERR_DIMENSIONS;
    }
    out->width = w;
    out->height = h;
    out->levels = levels;
    set_gl_format(out);
    for (k = 0; k < levels; k++) {
        texconv_level *lv = &out->level[k];
        lv->width = (w >> k) > 0 ? (w >> k) : 1;
        lv->height = (h >> k) > 0 ? (h >> k) : 1;
        lv->src_offset = src_total;
        lv->src_size = src_level_size(out, lv->width, lv->height);
        lv->dst_offset = dst_total;
        lv->dst_size = dst_level_size(out, lv->width, lv->height, lv->src_size);
        src_total += lv->src_size;
        dst_total += lv->dst_size;
    }
    if (cube) {
        src_total = (src_total + TEXCONV_CUBE_FACE_ALIGN - 1) & ~(size_t)(TEXCONV_CUBE_FACE_ALIGN - 1);
    }
    out->face_src_stride = src_total;
    out->face_dst_stride = dst_total;
    out->src_size = src_total * (size_t)out->faces;
    out->dst_size = dst_total * (size_t)out->faces;
    return 0;
}

/* ---- conversión ----------------------------------------------------------------------- */

static uint8_t expand(uint32_t value, int bits) {
    uint32_t widened = 0;
    int filled = 0;
    if (bits >= 8) {
        return (uint8_t)(value >> (bits - 8));
    }
    while (filled < 8) {
        widened = (widened << bits) | value;
        filled += bits;
    }
    return (uint8_t)(widened >> (filled - 8));
}

static uint8_t channel(uint32_t word, const chan *c) {
    if (!c->bits) {
        return c->value;
    }
    return expand((word >> c->shift) & ((1u << c->bits) - 1u), c->bits);
}

static uint32_t read_element(const uint8_t *p, int size) {
    switch (size) {
    case 1: return p[0];
    case 2: return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
    default:
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
               ((uint32_t)p[3] << 24);
    }
}

/* round(|value| * num / den) con el signo de value (redondeo hacia arriba en el .5). */
static int8_t scale_signed(int value, int num, int den) {
    int magnitude = value < 0 ? -value : value;
    int scaled = (2 * num * magnitude + den) / (2 * den);
    return (int8_t)(value < 0 ? -scaled : scaled);
}

static int signed_field(uint32_t value, int bits) {
    return (value >> (bits - 1)) ? (int)value - (1 << bits) : (int)value;
}

static void convert_texel(const fmt_info *f, const texconv_desc *d, uint32_t word,
                          const uint8_t *palette, size_t palette_len, uint8_t *out) {
    switch (d->kind) {
    case TEXCONV_PALETTE:
        if (palette && (size_t)word * 4 + 4 <= palette_len) {
            const uint8_t *e = palette + 4 * word; /* D3DCOLOR: B, G, R, A en memoria */
            out[0] = e[2];
            out[1] = e[1];
            out[2] = e[0];
            out[3] = e[3];
        } else {
            memset(out, 0, 4);
        }
        break;
    case TEXCONV_V8U8:
        out[0] = (uint8_t)(word & 0xFFu);
        out[1] = (uint8_t)(word >> 8);
        break;
    case TEXCONV_L6V5U5:
        out[0] = (uint8_t)scale_signed(signed_field(word & 0x1Fu, 5), 127, 16);
        out[1] = (uint8_t)scale_signed(signed_field((word >> 5) & 0x1Fu, 5), 127, 16);
        out[2] = (uint8_t)scale_signed((int)(word >> 10), 127, 63);
        out[3] = 127;
        break;
    default:
        out[0] = channel(word, &f->r);
        out[1] = channel(word, &f->g);
        out[2] = channel(word, &f->b);
        out[3] = channel(word, &f->a);
        break;
    }
}

static void convert_uncompressed(const fmt_info *f, const texconv_desc *d, const texconv_level *lv,
                                 const uint8_t *src, const uint8_t *palette, size_t palette_len,
                                 uint8_t *dst) {
    int out_size = d->kind == TEXCONV_V8U8 ? 2 : 4;
    uint32_t mx = 0, my = 0;
    int x, y;
    if (d->swizzled) {
        morton_masks(lv->width, lv->height, &mx, &my);
    }
    for (y = 0; y < lv->height; y++) {
        uint32_t row = d->swizzled ? deposit((uint32_t)y, my) : 0;
        for (x = 0; x < lv->width; x++) {
            size_t pos = d->swizzled
                             ? (size_t)(row | deposit((uint32_t)x, mx)) * (size_t)d->element_size
                             : (size_t)y * (size_t)d->pitch + (size_t)x * (size_t)d->element_size;
            uint32_t word = read_element(src + pos, d->element_size);
            convert_texel(f, d, word, palette, palette_len,
                          dst + ((size_t)y * (size_t)lv->width + (size_t)x) * (size_t)out_size);
        }
    }
}

/* ---- S3TC en CPU (dxt.py) ------------------------------------------------------------- */

static void expand565(uint32_t c, int rgb[3]) {
    uint32_t r5 = c >> 11, g6 = (c >> 5) & 0x3Fu, b5 = c & 0x1Fu;
    rgb[0] = (int)(r5 << 3 | r5 >> 2);
    rgb[1] = (int)(g6 << 2 | g6 >> 4);
    rgb[2] = (int)(b5 << 3 | b5 >> 2);
}

static void color_block(const uint8_t *b, int dxt1, uint8_t texels[16][4]) {
    uint32_t c0 = (uint32_t)b[0] | ((uint32_t)b[1] << 8);
    uint32_t c1 = (uint32_t)b[2] | ((uint32_t)b[3] << 8);
    uint32_t idx = read_element(b + 4, 4);
    int pal[4][4], k, i;
    int four = !dxt1 || c0 > c1;
    expand565(c0, pal[0]);
    expand565(c1, pal[1]);
    for (k = 0; k < 3; k++) {
        if (four) {
            pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3;
            pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3;
        } else {
            pal[2][k] = (pal[0][k] + pal[1][k]) / 2;
            pal[3][k] = 0;
        }
    }
    pal[0][3] = pal[1][3] = pal[2][3] = 255;
    pal[3][3] = four ? 255 : 0;
    for (i = 0; i < 16; i++) {
        int c = (int)((idx >> (2 * i)) & 3u);
        for (k = 0; k < 4; k++) {
            texels[i][k] = (uint8_t)pal[c][k];
        }
    }
}

static void decode_block(texconv_kind kind, const uint8_t *b, uint8_t texels[16][4]) {
    int i;
    if (kind == TEXCONV_DXT1) {
        color_block(b, 1, texels);
        return;
    }
    color_block(b + 8, 0, texels);
    if (kind == TEXCONV_DXT3) {
        for (i = 0; i < 16; i++) {
            texels[i][3] = (uint8_t)(((b[i / 2] >> (4 * (i & 1))) & 0xFu) * 17u);
        }
    } else {
        int a0 = b[0], a1 = b[1], pal[8];
        uint64_t bits = 0;
        for (i = 0; i < 6; i++) {
            bits |= (uint64_t)b[2 + i] << (8 * i);
        }
        pal[0] = a0;
        pal[1] = a1;
        if (a0 > a1) {
            for (i = 1; i < 7; i++) pal[1 + i] = ((7 - i) * a0 + i * a1) / 7;
        } else {
            for (i = 1; i < 5; i++) pal[1 + i] = ((5 - i) * a0 + i * a1) / 5;
            pal[6] = 0;
            pal[7] = 255;
        }
        for (i = 0; i < 16; i++) {
            texels[i][3] = (uint8_t)pal[(int)((bits >> (3 * i)) & 7u)];
        }
    }
}

static void decompress_level(const texconv_desc *d, const texconv_level *lv, const uint8_t *src,
                             uint8_t *dst) {
    int bx, by, blocks_x = (lv->width + 3) / 4, blocks_y = (lv->height + 3) / 4;
    for (by = 0; by < blocks_y; by++) {
        for (bx = 0; bx < blocks_x; bx++) {
            uint8_t texels[16][4];
            int row, col;
            decode_block(d->kind, src + ((size_t)by * (size_t)blocks_x + (size_t)bx) * (size_t)d->element_size,
                         texels);
            for (row = 0; row < 4 && by * 4 + row < lv->height; row++) {
                for (col = 0; col < 4 && bx * 4 + col < lv->width; col++) {
                    size_t pos = ((size_t)(by * 4 + row) * (size_t)lv->width + (size_t)(bx * 4 + col)) * 4;
                    memcpy(dst + pos, texels[row * 4 + col], 4);
                }
            }
        }
    }
}

int texconv_convert(const uint8_t *src, size_t src_len, const uint8_t *palette, size_t palette_len,
                    const texconv_desc *desc, uint8_t *dst, size_t dst_len) {
    const fmt_info *f;
    int face, k;
    if (!src || !desc || !dst || !desc->levels) {
        return TEXCONV_ERR_ARGS;
    }
    f = lookup(desc->nv_format);
    if (!f) {
        return TEXCONV_ERR_FORMAT;
    }
    if (src_len < desc->src_size || dst_len < desc->dst_size) {
        return TEXCONV_ERR_SHORT;
    }
    if (desc->kind == TEXCONV_PALETTE && !palette) {
        return TEXCONV_ERR_PALETTE;
    }
    for (face = 0; face < desc->faces; face++) {
        const uint8_t *fsrc = src + desc->face_src_stride * (size_t)face;
        uint8_t *fdst = dst + desc->face_dst_stride * (size_t)face;
        for (k = 0; k < desc->levels; k++) {
            const texconv_level *lv = &desc->level[k];
            if (desc->upload_compressed) {
                memcpy(fdst + lv->dst_offset, fsrc + lv->src_offset, lv->src_size);
            } else if (desc->compressed) {
                decompress_level(desc, lv, fsrc + lv->src_offset, fdst + lv->dst_offset);
            } else {
                convert_uncompressed(f, desc, lv, fsrc + lv->src_offset, palette, palette_len,
                                     fdst + lv->dst_offset);
            }
        }
    }
    return 0;
}
