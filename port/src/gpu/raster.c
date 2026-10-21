/*
 * raster.c: rasterizador de referencia por software (ver raster.h).
 *
 * Reglas: centro de píxel en (x + 0.5, y + 0.5); un píxel entra si las tres funciones de
 * arista son >= 0 (con la orientación del triángulo normalizada). Las funciones de
 * mezcla, alfa y el swizzle siguen a xemu (hw/xbox/nv2a/pgraph, swizzle.c).
 */
#include "raster.h"

#include <math.h>
#include <string.h>

uint32_t rs_swizzle_offset(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    uint32_t off = 0, bit = 0, mx = 1, my = 1;
    while (mx < w || my < h) {
        if (mx < w) {
            if (x & mx)
                off |= 1u << bit;
            bit++;
            mx <<= 1;
        }
        if (my < h) {
            if (y & my)
                off |= 1u << bit;
            bit++;
            my <<= 1;
        }
    }
    return off;
}

static uint32_t clampu(float f, uint32_t max) {
    if (!(f > 0.0f))
        return 0;
    if (f >= (float)max)
        return max - 1;
    return (uint32_t)f;
}

uint32_t rs_sample(const rs_texture_t *tex, float u, float v) {
    uint32_t x, y, idx;
    size_t off;
    if (!tex || !tex->format || !tex->data || !tex->width || !tex->height)
        return 0xFFFFFFFFu;
    if (tex->normalized) {
        u *= (float)tex->width;
        v *= (float)tex->height;
    }
    x = clampu((float)floor(u), tex->width);
    y = clampu((float)floor(v), tex->height);
    idx = tex->swizzled ? rs_swizzle_offset(x, y, tex->width, tex->height) : 0;
    switch (tex->format) {
    case RS_TEX_A8R8G8B8:
    case RS_TEX_X8R8G8B8: {
        uint32_t c;
        off = tex->swizzled ? (size_t)idx * 4 : (size_t)y * tex->pitch + (size_t)x * 4;
        memcpy(&c, tex->data + off, 4);
        return tex->format == RS_TEX_X8R8G8B8 ? c | 0xFF000000u : c;
    }
    case RS_TEX_R5G6B5: {
        uint16_t c;
        off = tex->swizzled ? (size_t)idx * 2 : (size_t)y * tex->pitch + (size_t)x * 2;
        memcpy(&c, tex->data + off, 2);
        return 0xFF000000u | ((uint32_t)((c >> 11) & 0x1F) * 255 / 31) << 16 |
               ((uint32_t)((c >> 5) & 0x3F) * 255 / 63) << 8 | (uint32_t)(c & 0x1F) * 255 / 31;
    }
    case RS_TEX_A8:
        off = tex->swizzled ? idx : (size_t)y * tex->pitch + x;
        return ((uint32_t)tex->data[off] << 24) | 0x00FFFFFFu;
    case RS_TEX_I8: {
        uint32_t c = 0xFFFF00FFu; /* sin paleta: magenta */
        off = tex->swizzled ? idx : (size_t)y * tex->pitch + x;
        if (tex->palette)
            memcpy(&c, tex->palette + (size_t)tex->data[off] * 4, 4);
        return c;
    }
    default:
        return 0xFFFFFFFFu;
    }
}

static float blend_factor(uint32_t f, float src, float src_a, float dst, float dst_a) {
    switch (f) {
    case 0: return 0.0f;
    case 1: return 1.0f;
    case 0x300: return src;
    case 0x301: return 1.0f - src;
    case 0x302: return src_a;
    case 0x303: return 1.0f - src_a;
    case 0x304: return dst_a;
    case 0x305: return 1.0f - dst_a;
    case 0x306: return dst;
    case 0x307: return 1.0f - dst;
    default: return 1.0f;
    }
}

static int alpha_pass(uint32_t func, uint32_t a, uint32_t ref) {
    switch (func) {
    case 0x200: return 0;
    case 0x201: return a < ref;
    case 0x202: return a == ref;
    case 0x203: return a <= ref;
    case 0x204: return a > ref;
    case 0x205: return a != ref;
    case 0x206: return a >= ref;
    default: return 1;
    }
}

static float to_unit(uint32_t c, int shift) {
    return (float)((c >> shift) & 0xFF) / 255.0f;
}

static uint8_t to_byte(float f) {
    if (!(f > 0.0f))
        return 0;
    if (f >= 1.0f)
        return 255;
    return (uint8_t)(f * 255.0f + 0.5f);
}

static void read_dst(const rs_target_t *t, uint8_t *p, float d[4]) {
    if (t->bpp == 4) {
        uint32_t c;
        memcpy(&c, p, 4);
        d[0] = to_unit(c, 16);
        d[1] = to_unit(c, 8);
        d[2] = to_unit(c, 0);
        d[3] = to_unit(c, 24);
    } else {
        uint16_t c;
        memcpy(&c, p, 2);
        d[0] = (float)((c >> 11) & 0x1F) / 31.0f;
        d[1] = (float)((c >> 5) & 0x3F) / 63.0f;
        d[2] = (float)(c & 0x1F) / 31.0f;
        d[3] = 1.0f;
    }
}

static void write_dst(const rs_target_t *t, uint8_t *p, const float c[4]) {
    if (t->bpp == 4) {
        uint32_t v = (uint32_t)to_byte(c[3]) << 24 | (uint32_t)to_byte(c[0]) << 16 |
                     (uint32_t)to_byte(c[1]) << 8 | to_byte(c[2]);
        memcpy(p, &v, 4);
    } else {
        uint16_t v = (uint16_t)((to_byte(c[0]) >> 3) << 11 | (to_byte(c[1]) >> 2) << 5 |
                                (to_byte(c[2]) >> 3));
        memcpy(p, &v, 2);
    }
}

static float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

uint32_t rs_draw_triangle(const rs_target_t *t, const rs_texture_t *tex, const rs_state_t *st,
                          const rs_vertex_t *v0, const rs_vertex_t *v1, const rs_vertex_t *v2) {
    float area = edge(v0->x, v0->y, v1->x, v1->y, v2->x, v2->y), sign;
    float minx, maxx, miny, maxy, q0, q1, q2;
    int x0, x1, y0, y1, x, y;
    uint32_t written = 0;
    if (!t || !t->pixels || area == 0.0f || !(fabsf(area) < 1e9f))
        return 0;
    sign = area > 0.0f ? 1.0f : -1.0f;
    minx = fminf(v0->x, fminf(v1->x, v2->x));
    maxx = fmaxf(v0->x, fmaxf(v1->x, v2->x));
    miny = fminf(v0->y, fminf(v1->y, v2->y));
    maxy = fmaxf(v0->y, fmaxf(v1->y, v2->y));
    /* Se acota en float antes de convertir: un vértice fuera del rango de int sería UB. */
    minx = fmaxf(minx, (float)t->clip_x0);
    miny = fmaxf(miny, (float)t->clip_y0);
    maxx = fminf(maxx, (float)t->clip_x1);
    maxy = fminf(maxy, (float)t->clip_y1);
    if (!(minx <= maxx) || !(miny <= maxy))
        return 0;
    x0 = (int)floorf(minx);
    x1 = (int)ceilf(maxx);
    y0 = (int)floorf(miny);
    y1 = (int)ceilf(maxy);
    if (x0 < t->clip_x0) x0 = t->clip_x0;
    if (y0 < t->clip_y0) y0 = t->clip_y0;
    if (x1 > t->clip_x1) x1 = t->clip_x1;
    if (y1 > t->clip_y1) y1 = t->clip_y1;
    if (x1 > (int)t->width) x1 = (int)t->width;
    if (y1 > (int)t->height) y1 = (int)t->height;
    q0 = v0->rhw > 0.0f ? v0->rhw : 1.0f;
    q1 = v1->rhw > 0.0f ? v1->rhw : 1.0f;
    q2 = v2->rhw > 0.0f ? v2->rhw : 1.0f;
    for (y = y0; y < y1; y++) {
        float py = (float)y + 0.5f;
        for (x = x0; x < x1; x++) {
            float px = (float)x + 0.5f, w0, w1, w2, b0, b1, b2, qs, c[4], s[4];
            uint8_t *dst;
            w0 = edge(v1->x, v1->y, v2->x, v2->y, px, py) * sign;
            w1 = edge(v2->x, v2->y, v0->x, v0->y, px, py) * sign;
            w2 = edge(v0->x, v0->y, v1->x, v1->y, px, py) * sign;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                continue;
            /* Baricéntricas con corrección de perspectiva (pesos b_i * q_i). */
            b0 = w0 * q0;
            b1 = w1 * q1;
            b2 = w2 * q2;
            qs = b0 + b1 + b2;
            if (qs == 0.0f)
                continue;
            b0 /= qs;
            b1 /= qs;
            b2 /= qs;
            c[0] = v0->r * b0 + v1->r * b1 + v2->r * b2;
            c[1] = v0->g * b0 + v1->g * b1 + v2->g * b2;
            c[2] = v0->b * b0 + v1->b * b1 + v2->b * b2;
            c[3] = v0->a * b0 + v1->a * b1 + v2->a * b2;
            if (st->use_texture && tex && tex->format) {
                uint32_t texel = rs_sample(tex, v0->u * b0 + v1->u * b1 + v2->u * b2,
                                           v0->v * b0 + v1->v * b1 + v2->v * b2);
                c[0] *= to_unit(texel, 16);
                c[1] *= to_unit(texel, 8);
                c[2] *= to_unit(texel, 0);
                c[3] *= to_unit(texel, 24);
            }
            if (st->alpha_test && !alpha_pass(st->alpha_func, to_byte(c[3]), st->alpha_ref))
                continue;
            dst = t->pixels + (size_t)y * t->pitch + (size_t)x * (size_t)t->bpp;
            if (st->blend) {
                float d[4];
                int i;
                read_dst(t, dst, d);
                for (i = 0; i < 4; i++) {
                    float sa = c[3], da = d[3];
                    s[i] = c[i] * blend_factor(st->sfactor, c[i], sa, d[i], da) +
                           d[i] * blend_factor(st->dfactor, c[i], sa, d[i], da);
                }
                write_dst(t, dst, s);
            } else {
                write_dst(t, dst, c);
            }
            written++;
        }
    }
    return written;
}
