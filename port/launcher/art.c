/*
 * art.c: original procedural artwork of the launcher. See art.h.
 *
 * The letters are our own: every glyph is a short list of polygons on a 10-unit-high grid,
 * sheared to lean forward and cut with sharp chamfers. They are rasterised with exact
 * horizontal coverage and 6 sub-rows per pixel, then lit like brushed chrome (a vertical
 * steel ramp with a dark horizon line, bevel shading from the blurred mask, a dark outline
 * and a thin red glow around it).
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "art.h"

#define PI_F 3.14159265f
#define SHEAR 0.21f
#define SUBROWS 6

/* ---------------------------------------------------------------- small helpers ---- */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void add_px(canvas_t *c, int x, int y, float r, float g, float b) {
    uint32_t *p;
    int R, G, B;
    if ((unsigned)x >= (unsigned)c->w || (unsigned)y >= (unsigned)c->h)
        return;
    p = &c->px[(size_t)y * c->w + x];
    R = (int)((*p >> 16) & 255) + (int)(r + 0.5f);
    G = (int)((*p >> 8) & 255) + (int)(g + 0.5f);
    B = (int)(*p & 255) + (int)(b + 0.5f);
    *p = ((uint32_t)(R > 255 ? 255 : R) << 16) | ((uint32_t)(G > 255 ? 255 : G) << 8) |
         (uint32_t)(B > 255 ? 255 : B);
}

static void mix_px(canvas_t *c, int x, int y, float r, float g, float b, float a) {
    uint32_t *p;
    float R, G, B;
    if ((unsigned)x >= (unsigned)c->w || (unsigned)y >= (unsigned)c->h || a <= 0.0f)
        return;
    if (a > 1.0f)
        a = 1.0f;
    p = &c->px[(size_t)y * c->w + x];
    R = ((*p >> 16) & 255) * (1 - a) + r * a;
    G = ((*p >> 8) & 255) * (1 - a) + g * a;
    B = (*p & 255) * (1 - a) + b * a;
    *p = ((uint32_t)(R + 0.5f) << 16) | ((uint32_t)(G + 0.5f) << 8) | (uint32_t)(B + 0.5f);
}

static void fill_dev(canvas_t *c, int x0, int y0, int x1, int y1, float r, float g, float b) {
    int x, y;
    uint32_t v = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > c->w)
        x1 = c->w;
    if (y1 > c->h)
        y1 = c->h;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            c->px[(size_t)y * c->w + x] = v;
}

static uint32_t g_rng = 12345u;
static float frand(void) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return (float)(g_rng >> 8) / 16777216.0f;
}

static float hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xffff) / 65535.0f;
}

static float vnoise(float x, float y) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy, a, b, c, d;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    a = hash2(ix, iy);
    b = hash2(ix + 1, iy);
    c = hash2(ix, iy + 1);
    d = hash2(ix + 1, iy + 1);
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}

static float fbm(float x, float y) {
    return 0.5f * vnoise(x, y) + 0.3f * vnoise(2 * x + 7.1f, 2 * y + 3.7f) +
           0.2f * vnoise(4 * x + 1.3f, 4 * y + 9.2f);
}

/* ------------------------------------------------------------------------ mist ---- */

/* Additive elliptical mist with a ragged edge. Logical centre and radii; the clip box is
 * logical too. */
static void haze(canvas_t *c, float cx, float cy, float rx, float ry, float cr, float cg, float cb,
                 float strength, float clip_x0, float clip_y0, float clip_x1, float clip_y1) {
    float k = c->k;
    int x0 = (int)floorf((cx - rx) * k), x1 = (int)ceilf((cx + rx) * k);
    int y0 = (int)floorf((cy - ry) * k), y1 = (int)ceilf((cy + ry) * k), x, y;
    if (x0 < (int)(clip_x0 * k))
        x0 = (int)(clip_x0 * k);
    if (y0 < (int)(clip_y0 * k))
        y0 = (int)(clip_y0 * k);
    if (x1 > (int)(clip_x1 * k))
        x1 = (int)(clip_x1 * k);
    if (y1 > (int)(clip_y1 * k))
        y1 = (int)(clip_y1 * k);
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) {
            float lx = x / k, ly = y / k, dx = (lx - cx) / rx, dy = (ly - cy) / ry;
            float d2 = dx * dx + dy * dy, f, n;
            if (d2 >= 1.0f)
                continue;
            f = 1.0f - d2;
            f = f * f;
            n = 0.45f + 1.1f * fbm(lx / 70.0f + cx * 0.013f, ly / 55.0f + cy * 0.017f);
            f *= strength * n;
            add_px(c, x, y, cr * f, cg * f, cb * f);
        }
}

/* A horizontal speed line that swells in the middle and fades at both ends. */
static void speed_line(canvas_t *c, float x0, float x1, float y, float half, float inten, float r,
                       float g, float b) {
    float k = c->k;
    int px0 = (int)(x0 * k), px1 = (int)(x1 * k), x, dy;
    int rows = (int)ceilf(half * k + 2.0f);
    for (x = px0; x < px1; x++) {
        float u = (float)(x - px0) / (float)(px1 - px0 > 1 ? px1 - px0 : 1);
        float prof = powf(sinf(PI_F * u), 0.8f);
        float ht = half * k * prof;
        for (dy = -rows; dy <= rows; dy++) {
            float cov = clampf(ht + 0.5f - fabsf((float)dy), 0.0f, 1.0f) * prof * inten;
            if (cov > 0.002f)
                add_px(c, x, (int)(y * k) + dy, r * cov, g * cov, b * cov);
        }
    }
}

/* ---------------------------------------------------------------------- shapes ---- */

/* Triangle with colours interpolated from its vertices (4x4 supersampled edge). */
static void tri_grad(canvas_t *c, const float *p, const float (*col)[3], float alpha) {
    float ax = p[0], ay = p[1], bx = p[2], by = p[3], cx = p[4], cy = p[5];
    float den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    int x0 = (int)floorf(fminf(ax, fminf(bx, cx))), x1 = (int)ceilf(fmaxf(ax, fmaxf(bx, cx)));
    int y0 = (int)floorf(fminf(ay, fminf(by, cy))), y1 = (int)ceilf(fmaxf(ay, fmaxf(by, cy))), x, y;
    int sx, sy;
    if (fabsf(den) < 1e-6f)
        return;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            int hits = 0;
            float w0, w1, w2, px = x + 0.5f, py = y + 0.5f;
            for (sy = 0; sy < 4; sy++)
                for (sx = 0; sx < 4; sx++) {
                    float qx = x + (sx + 0.5f) / 4, qy = y + (sy + 0.5f) / 4;
                    float u = ((by - cy) * (qx - cx) + (cx - bx) * (qy - cy)) / den;
                    float v = ((cy - ay) * (qx - cx) + (ax - cx) * (qy - cy)) / den;
                    hits += (u >= 0 && v >= 0 && u + v <= 1);
                }
            if (!hits)
                continue;
            w0 = clampf(((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / den, 0.0f, 1.0f);
            w1 = clampf(((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / den, 0.0f, 1.0f);
            w2 = clampf(1.0f - w0 - w1, 0.0f, 1.0f);
            mix_px(c, x, y, col[0][0] * w0 + col[1][0] * w1 + col[2][0] * w2,
                   col[0][1] * w0 + col[1][1] * w1 + col[2][1] * w2,
                   col[0][2] * w0 + col[1][2] * w1 + col[2][2] * w2, alpha * hits / 16.0f);
        }
}

static void line_aa(canvas_t *c, float ax, float ay, float bx, float by, float w, float r, float g,
                    float b, float alpha) {
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, rad = w * 0.5f;
    int x0 = (int)floorf(fminf(ax, bx) - rad - 1), x1 = (int)ceilf(fmaxf(ax, bx) + rad + 1);
    int y0 = (int)floorf(fminf(ay, by) - rad - 1), y1 = (int)ceilf(fmaxf(ay, by) + rad + 1), x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            float px = x + 0.5f - ax, py = y + 0.5f - ay;
            float t = l2 > 0 ? clampf((px * dx + py * dy) / l2, 0, 1) : 0;
            float qx = px - dx * t, qy = py - dy * t;
            float d = sqrtf(qx * qx + qy * qy) - rad;
            mix_px(c, x, y, r, g, b, clampf(0.5f - d, 0, 1) * alpha);
        }
}

static void disc(canvas_t *c, float cx, float cy, float r, float cr, float cg, float cb, float a) {
    int x, y;
    for (y = (int)floorf(cy - r - 1); y <= (int)ceilf(cy + r + 1); y++)
        for (x = (int)floorf(cx - r - 1); x <= (int)ceilf(cx + r + 1); x++) {
            float d = hypotf(x + 0.5f - cx, y + 0.5f - cy) - r;
            mix_px(c, x, y, cr, cg, cb, clampf(0.5f - d, 0, 1) * a);
        }
}

static void ring(canvas_t *c, float cx, float cy, float r, float w, float cr, float cg, float cb,
                 float a) {
    int x, y;
    float out = r + w * 0.5f;
    for (y = (int)floorf(cy - out - 1); y <= (int)ceilf(cy + out + 1); y++)
        for (x = (int)floorf(cx - out - 1); x <= (int)ceilf(cx + out + 1); x++) {
            float d = fabsf(hypotf(x + 0.5f - cx, y + 0.5f - cy) - r) - w * 0.5f;
            mix_px(c, x, y, cr, cg, cb, clampf(0.5f - d, 0, 1) * a);
        }
}

/* A katana-like slash: razor thin at both ends, swelling in the middle, white-hot core,
 * red halo. Device coordinates. */
static void slash(canvas_t *c, float ax, float ay, float bx, float by, float maxw, float k) {
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, halo = 16.0f * k;
    int x0 = (int)floorf(fminf(ax, bx) - halo), x1 = (int)ceilf(fmaxf(ax, bx) + halo);
    int y0 = (int)floorf(fminf(ay, by) - halo), y1 = (int)ceilf(fmaxf(ay, by) + halo), x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            float px = x + 0.5f - ax, py = y + 0.5f - ay;
            float t = clampf((px * dx + py * dy) / l2, 0, 1);
            float qx = px - dx * t, qy = py - dy * t, d = sqrtf(qx * qx + qy * qy);
            float w = 0.5f * maxw * k * powf(sinf(PI_F * t), 0.55f);
            float cov = clampf(0.5f + w - d, 0, 1), hot;
            float gl = expf(-(d / (halo * 0.5f)) * (d / (halo * 0.5f))) * powf(sinf(PI_F * t), 0.5f);
            if (gl > 0.01f)
                add_px(c, x, y, 220 * gl * 0.55f, 18 * gl * 0.55f, 26 * gl * 0.55f);
            if (cov > 0.0f) {
                hot = clampf(1.0f - d / (w + 0.01f), 0, 1);
                mix_px(c, x, y, 255 * hot + 235 * (1 - hot), 245 * hot + 40 * (1 - hot),
                       235 * hot + 40 * (1 - hot), cov);
            }
        }
}

void art_emblem(canvas_t *c, float lcx, float lcy, float lR, int with_slash) {
    float k = c->k, cx = lcx * k, cy = lcy * k, R = lR * k;
    int i, side;
    ring(c, cx, cy, R * 1.16f, 1.4f * k, 200, 28, 36, 0.55f);
    ring(c, cx, cy, R * 1.24f, 1.0f * k, 150, 18, 26, 0.35f);
    for (i = 0; i < 4; i++) {
        float tip = (45.0f + 90.0f * i) * PI_F / 180.0f;
        float tx = cx + R * cosf(tip), ty = cy - R * sinf(tip);
        for (side = -1; side <= 1; side += 2) {
            float ang = tip + side * (side > 0 ? 0.80f : 0.72f);
            float ir = R * (side > 0 ? 0.30f : 0.40f);
            float ix = cx + ir * cosf(ang), iy = cy - ir * sinf(ang);
            float pts[6] = {cx, cy, tx, ty, ix, iy};
            float col[3][3];
            static const float lit_tip[3] = {246, 249, 253}, lit_in[3] = {176, 184, 200},
                               lit_c[3] = {24, 26, 34};
            static const float dk_tip[3] = {196, 204, 216}, dk_in[3] = {52, 56, 70},
                               dk_c[3] = {8, 8, 12};
            int j;
            for (j = 0; j < 3; j++) {
                col[0][j] = side > 0 ? lit_c[j] : dk_c[j];
                col[1][j] = side > 0 ? lit_tip[j] : dk_tip[j];
                col[2][j] = side > 0 ? lit_in[j] : dk_in[j];
            }
            tri_grad(c, pts, (const float(*)[3])col, 1.0f);
            line_aa(c, tx, ty, ix, iy, 1.3f * k, 255, 70, 70, 0.75f);
        }
    }
    disc(c, cx, cy, R * 0.15f, 190, 24, 32, 1.0f);
    disc(c, cx, cy, R * 0.10f, 6, 4, 6, 1.0f);
    ring(c, cx, cy, R * 0.15f, 1.2f * k, 255, 150, 150, 0.8f);
    if (with_slash)
        slash(c, cx - R * 1.55f, cy + R * 0.85f, cx + R * 1.55f, cy - R * 0.85f, 5.0f, k);
}

/* ---------------------------------------------------------------------- glyphs ---- */

/* Pairs of floats on a grid 10 units high. (-1,-1) starts another contour of the same shape
 * (even-odd, used for counters), (-2,-2) starts another shape (union), (-3,-3) ends. */
#define NEXT_CONTOUR -1, -1
#define NEXT_SHAPE -2, -2
#define END_GLYPH -3, -3

static const float G_N[] = {0, 0, 2.4f, 0, 2.4f, 10, 0, 10, NEXT_SHAPE, 5.2f, 0, 7.6f, 0, 7.6f,
                            10, 5.2f, 10, NEXT_SHAPE, 0, 0, 2.9f, 0, 7.6f, 10, 4.7f, 10, END_GLYPH};
static const float G_I[] = {0, 0, 2.7f, 0, 2.7f, 10, 0, 10, END_GLYPH};
static const float G_J[] = {4.4f, 0, 7.0f, 0, 7.0f, 8.2f, 5.2f, 10, 0, 10, 0, 7.8f, 4.4f, 7.8f,
                            END_GLYPH};
static const float G_A[] = {3.1f, 0, 5.3f, 0, 2.3f, 10, 0, 10, NEXT_SHAPE, 3.1f, 0, 5.3f, 0, 8.4f,
                            10, 6.1f, 10, NEXT_SHAPE, 1.3f, 6.4f, 7.1f, 6.4f, 7.6f, 8.2f, 0.8f,
                            8.2f, END_GLYPH};
static const float G_G[] = {1.7f, 0, 7.6f, 0, 7.2f, 2.1f, 2.4f, 2.1f, 2.4f, 7.9f, 5.2f, 7.9f,
                            5.2f, 5.9f, 7.6f, 5.9f, 7.6f, 8.3f, 5.9f, 10, 1.7f, 10, 0, 8.3f,
                            0, 1.7f, END_GLYPH};
static const float G_D[] = {0, 0, 5.4f, 0, 7.8f, 2.4f, 7.8f, 7.6f, 5.4f, 10, 0, 10, NEXT_CONTOUR,
                            2.4f, 2.2f, 4.7f, 2.2f, 5.4f, 2.9f, 5.4f, 7.1f, 4.7f, 7.8f, 2.4f,
                            7.8f, END_GLYPH};
static const float G_E[] = {0, 0, 6.6f, 0, 6.2f, 2.1f, 2.4f, 2.1f, 2.4f, 4.0f, 5.6f, 4.0f,
                            5.2f, 6.0f, 2.4f, 6.0f, 2.4f, 7.9f, 6.9f, 7.9f, 6.6f, 10, 0, 10,
                            END_GLYPH};
static const float G_B[] = {0, 0, 5.4f, 0, 7.4f, 1.8f, 7.4f, 4.1f, 6.4f, 5.0f, 7.6f, 5.9f, 7.6f,
                            8.2f, 5.6f, 10, 0, 10, NEXT_CONTOUR, 2.4f, 2.1f, 4.6f, 2.1f, 5.0f,
                            2.5f, 5.0f, 3.6f, 4.6f, 3.9f, 2.4f, 3.9f, NEXT_CONTOUR, 2.4f, 6.1f,
                            4.8f, 6.1f, 5.2f, 6.5f, 5.2f, 7.6f, 4.8f, 7.9f, 2.4f, 7.9f, END_GLYPH};
static const float G_L[] = {0, 0, 2.4f, 0, 2.4f, 7.9f, 6.2f, 7.9f, 6.2f, 10, 0, 10, END_GLYPH};
static const float G_C[] = {1.7f, 0, 7.2f, 0, 6.8f, 2.1f, 2.4f, 2.1f, 2.4f, 7.9f, 6.8f, 7.9f,
                            7.2f, 10, 1.7f, 10, 0, 8.3f, 0, 1.7f, END_GLYPH};
static const float G_K[] = {0, 0, 2.4f, 0, 2.4f, 10, 0, 10, NEXT_SHAPE, 2.4f, 3.7f, 5.0f, 0, 7.8f,
                            0, 3.8f, 5.0f, 2.4f, 6.4f, NEXT_SHAPE, 2.4f, 5.3f, 3.9f, 4.2f, 7.9f,
                            10, 5.2f, 10, END_GLYPH};

typedef struct glyph {
    char ch;
    float w;
    const float *d;
} glyph_t;

static const glyph_t k_glyphs[] = {
    {'N', 7.6f, G_N}, {'I', 2.7f, G_I}, {'J', 7.0f, G_J}, {'A', 8.4f, G_A}, {'G', 7.6f, G_G},
    {'D', 7.8f, G_D}, {'E', 6.9f, G_E}, {'B', 7.6f, G_B}, {'L', 6.2f, G_L}, {'C', 7.2f, G_C},
    {'K', 7.9f, G_K},
};
#define SPACE_UNITS 4.0f

static const glyph_t *find_glyph(char ch) {
    size_t i;
    for (i = 0; i < sizeof k_glyphs / sizeof k_glyphs[0]; i++)
        if (k_glyphs[i].ch == ch)
            return &k_glyphs[i];
    return NULL;
}

float art_wordmark_width(const char *text, float height, float tracking) {
    float s = height / 10.0f, w = 0;
    for (; *text; text++) {
        const glyph_t *g = find_glyph(*text);
        w += (g ? g->w : SPACE_UNITS) * s + tracking * s;
    }
    return w + 10.0f * SHEAR * s;
}

typedef struct edge {
    float x0, y0, x1, y1;
    int shape;
} edge_t;

#define MAX_EDGES 96

/* Turns one glyph into edges in mask pixels. (ox, oy) is the glyph's top-left in the mask. */
static int glyph_edges(const glyph_t *g, float ox, float oy, float s, edge_t *e) {
    const float *p = g->d;
    int n = 0, shape = 0;
    float sx[24], sy[24];
    int np = 0, i;
    for (;; p += 2) {
        int end = p[0] == -3.0f, split = p[0] == -1.0f || p[0] == -2.0f || end;
        if (!split) {
            sx[np] = ox + (p[0] + (10.0f - p[1]) * SHEAR) * s;
            sy[np] = oy + p[1] * s;
            np++;
            continue;
        }
        for (i = 0; i < np && n < MAX_EDGES; i++) {
            int j = (i + 1) % np;
            e[n].x0 = sx[i];
            e[n].y0 = sy[i];
            e[n].x1 = sx[j];
            e[n].y1 = sy[j];
            e[n].shape = shape;
            n++;
        }
        np = 0;
        if (p[0] == -2.0f)
            shape++;
        if (end)
            break;
    }
    return n;
}

static int cmp_float(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

/* Rasterises one glyph into the 8-bit mask (max-combined with what is already there). */
static void raster_glyph(uint8_t *mask, int mw, int mh, const edge_t *e, int ne, float gx0,
                         float gx1, float gy0, float gy1) {
    int r, s, sh, x, nshape = 0, i;
    float *cov = (float *)calloc((size_t)mw, sizeof(float));
    float *tmp = (float *)calloc((size_t)mw, sizeof(float));
    int ya = (int)floorf(gy0), yb = (int)ceilf(gy1);
    if (!cov || !tmp) {
        free(cov);
        free(tmp);
        return;
    }
    for (i = 0; i < ne; i++)
        if (e[i].shape + 1 > nshape)
            nshape = e[i].shape + 1;
    if (ya < 0)
        ya = 0;
    if (yb > mh)
        yb = mh;
    for (r = ya; r < yb; r++) {
        memset(cov, 0, (size_t)mw * sizeof(float));
        for (s = 0; s < SUBROWS; s++) {
            float y = r + (s + 0.5f) / SUBROWS;
            memset(tmp, 0, (size_t)mw * sizeof(float));
            for (sh = 0; sh < nshape; sh++) {
                float xs[64];
                int nx = 0, k;
                for (i = 0; i < ne; i++) {
                    if (e[i].shape != sh)
                        continue;
                    if ((e[i].y0 <= y && e[i].y1 > y) || (e[i].y1 <= y && e[i].y0 > y)) {
                        float t = (y - e[i].y0) / (e[i].y1 - e[i].y0);
                        if (nx < 64)
                            xs[nx++] = e[i].x0 + t * (e[i].x1 - e[i].x0);
                    }
                }
                qsort(xs, (size_t)nx, sizeof(float), cmp_float);
                for (k = 0; k + 1 < nx; k += 2) {
                    float xa = clampf(xs[k], 0, (float)mw), xb = clampf(xs[k + 1], 0, (float)mw);
                    int p0 = (int)floorf(xa), p1 = (int)floorf(xb);
                    for (x = p0; x <= p1 && x < mw; x++) {
                        float lo = xa > x ? xa : (float)x, hi = xb < x + 1 ? xb : (float)(x + 1);
                        if (hi > lo)
                            tmp[x] += hi - lo;
                    }
                }
            }
            for (x = (int)gx0 < 0 ? 0 : (int)gx0; x < mw && x <= (int)ceilf(gx1); x++)
                cov[x] += (tmp[x] > 1.0f ? 1.0f : tmp[x]) / SUBROWS;
        }
        for (x = (int)gx0 < 0 ? 0 : (int)gx0; x < mw && x <= (int)ceilf(gx1); x++) {
            int v = (int)(cov[x] * 255.0f + 0.5f);
            uint8_t *m = &mask[(size_t)r * mw + x];
            if (v > 255)
                v = 255;
            if (v > *m)
                *m = (uint8_t)v;
        }
    }
    free(cov);
    free(tmp);
}

/* One pass of a box blur on both axes (a, tmp: w * h bytes). */
static void blur_u8(uint8_t *a, uint8_t *tmp, int w, int h, int r) {
    int x, y, d;
    if (r < 1)
        return;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int sum = 0;
            for (d = -r; d <= r; d++) {
                int xx = x + d;
                if (xx >= 0 && xx < w)
                    sum += a[(size_t)y * w + xx];
            }
            tmp[(size_t)y * w + x] = (uint8_t)(sum / (2 * r + 1));
        }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int sum = 0;
            for (d = -r; d <= r; d++) {
                int yy = y + d;
                if (yy >= 0 && yy < h)
                    sum += tmp[(size_t)yy * w + x];
            }
            a[(size_t)y * w + x] = (uint8_t)(sum / (2 * r + 1));
        }
}

typedef struct stop {
    float t, r, g, b;
} stop_t;

/* Brushed chrome: bright sky, dark horizon, bright ground reflection. */
static const stop_t k_chrome[] = {{0.00f, 252, 253, 255}, {0.26f, 178, 186, 200}, {0.45f, 92, 99, 114},
                                  {0.50f, 18, 20, 28},    {0.53f, 140, 150, 168}, {0.78f, 214, 220, 230},
                                  {1.00f, 120, 128, 144}};
/* The same shape in red steel for the secondary word. */
static const stop_t k_red[] = {{0.00f, 255, 150, 130}, {0.26f, 230, 56, 48},  {0.45f, 138, 10, 18},
                               {0.50f, 34, 0, 6},      {0.53f, 200, 40, 40},  {0.78f, 255, 98, 78},
                               {1.00f, 160, 20, 26}};

static void ramp(const stop_t *s, int n, float t, float *r, float *g, float *b) {
    int i;
    t = clampf(t, 0, 1);
    for (i = 0; i + 1 < n; i++)
        if (t <= s[i + 1].t) {
            float u = (t - s[i].t) / (s[i + 1].t - s[i].t);
            *r = s[i].r + (s[i + 1].r - s[i].r) * u;
            *g = s[i].g + (s[i + 1].g - s[i].g) * u;
            *b = s[i].b + (s[i + 1].b - s[i].b) * u;
            return;
        }
    *r = s[n - 1].r;
    *g = s[n - 1].g;
    *b = s[n - 1].b;
}

void art_chrome(float t, int red_style, float *r, float *g, float *b) {
    ramp(red_style ? k_red : k_chrome, 7, t, r, g, b);
}

void art_wordmark(canvas_t *c, const char *text, float lx, float ly, float lheight, float tracking,
                  int red_style) {
    float k = c->k, s = lheight * k / 10.0f, track = tracking * s;
    int pad = (int)(lheight * k * 0.45f) + 8;
    int mw = (int)ceilf(art_wordmark_width(text, lheight, tracking) * k) + 2 * pad;
    int mh = (int)ceilf(lheight * k) + 2 * pad, x, y, rg;
    uint8_t *mask = (uint8_t *)calloc((size_t)mw * mh, 1);
    uint8_t *glow = (uint8_t *)malloc((size_t)mw * mh);
    uint8_t *tight = (uint8_t *)malloc((size_t)mw * mh);
    uint8_t *soft = (uint8_t *)malloc((size_t)mw * mh);
    uint8_t *tmp = (uint8_t *)malloc((size_t)mw * mh);
    float pen = (float)pad;
    const char *p;
    int bx = (int)floorf(lx * k) - pad, by = (int)floorf(ly * k) - pad;
    if (!mask || !glow || !tight || !soft || !tmp)
        goto done;
    for (p = text; *p; p++) {
        const glyph_t *g = find_glyph(*p);
        edge_t e[MAX_EDGES];
        if (!g) {
            pen += SPACE_UNITS * s + track;
            continue;
        }
        {
            int ne = glyph_edges(g, pen, (float)pad, s, e);
            raster_glyph(mask, mw, mh, e, ne, pen - 1.0f, pen + (g->w + 10 * SHEAR) * s + 2.0f,
                         (float)pad - 1.0f, (float)pad + 10.0f * s + 1.0f);
        }
        pen += g->w * s + track;
    }
    memcpy(glow, mask, (size_t)mw * mh);
    memcpy(soft, mask, (size_t)mw * mh);
    memcpy(tight, mask, (size_t)mw * mh);
    {
        int r1 = (int)(lheight * k * 0.14f) + 2;
        blur_u8(glow, tmp, mw, mh, r1);
        blur_u8(glow, tmp, mw, mh, r1);
    }
    blur_u8(soft, tmp, mw, mh, (int)(1.6f * k) + 1);
    blur_u8(tight, tmp, mw, mh, (int)(0.9f * k) + 1);
    for (y = 0; y < mh; y++)
        for (x = 0; x < mw; x++) {
            size_t i = (size_t)y * mw + x;
            float m = mask[i] / 255.0f, g1 = fminf(1.0f, glow[i] / 255.0f * 2.6f);
            float gs = soft[i] / 255.0f, gt = tight[i] / 255.0f;
            float cr = 0, cg = 0, cb = 0, t, bev, dx, dy;
            int X = bx + x, Y = by + y;
            if (red_style)
                g1 *= 1.15f;
            add_px(c, X, Y, 200 * g1 * g1 * 0.75f, 10 * g1 * g1, 18 * g1 * g1);
            add_px(c, X, Y, 255 * gs * (1 - m) * 0.9f, 48 * gs * (1 - m), 52 * gs * (1 - m));
            mix_px(c, X, Y, 6, 3, 5, fminf(1.0f, gt * 2.2f) * (1 - m) * 0.85f);
            if (mask[i] == 0)
                continue;
            t = ((float)y - pad) / (10.0f * s);
            ramp(red_style ? k_red : k_chrome, 7, t, &cr, &cg, &cb);
            dx = (float)((x + 1 < mw ? soft[i + 1] : 0) - (x > 0 ? soft[i - 1] : 0));
            dy = (float)((y + 1 < mh ? soft[i + mw] : 0) - (y > 0 ? soft[i - mw] : 0));
            bev = (dx + dy) * 0.5f;
            cr += bev * 0.55f;
            cg += bev * 0.55f;
            cb += bev * 0.6f;
            rg = (int)(hash2(0, y + by) * 8.0f) - 4; /* brushed streaks: one value per row */
            cr += (float)rg;
            cg += (float)rg;
            cb += (float)rg;
            mix_px(c, X, Y, clampf(cr, 0, 255), clampf(cg, 0, 255), clampf(cb, 0, 255), m);
        }
done:
    free(mask);
    free(glow);
    free(tight);
    free(soft);
    free(tmp);
}

/* ------------------------------------------------------------------ backdrops ---- */

static void shade_rows(canvas_t *c, float ly0, float ly1, float x0, float x1, float f0, float f1) {
    int y, x, ya = (int)(ly0 * c->k), yb = (int)(ly1 * c->k);
    int xa = (int)(x0 * c->k), xb = (int)(x1 * c->k);
    for (y = ya; y < yb && y < c->h; y++) {
        float f = f0 + (f1 - f0) * (float)(y - ya) / (float)(yb - ya > 0 ? yb - ya : 1);
        int ff = (int)(f * 256.0f);
        for (x = xa; x < xb && x < c->w; x++) {
            uint32_t v = c->px[(size_t)y * c->w + x];
            uint32_t r = (((v >> 16) & 255) * ff) >> 8, g = (((v >> 8) & 255) * ff) >> 8,
                     b = ((v & 255) * ff) >> 8;
            c->px[(size_t)y * c->w + x] = (r << 16) | (g << 8) | b;
        }
    }
}

static void hero_lines(canvas_t *c, int inner) {
    int i, n = inner ? 9 : 44;
    g_rng = 20041u + (uint32_t)inner;
    for (i = 0; i < n; i++) {
        float y = inner ? 8.0f + frand() * 60.0f : 18.0f + frand() * 300.0f;
        float x0 = (float)ART_SIDE_W + (inner ? 330.0f : 10.0f) + frand() * (inner ? 420.0f : 560.0f);
        float len = (inner ? 160.0f : 130.0f) + frand() * (inner ? 380.0f : 560.0f);
        float half = 0.3f + frand() * (inner ? 0.6f : 1.1f);
        float inten = 0.22f + frand() * (inner ? 0.35f : 0.65f);
        speed_line(c, x0, x0 + len, y, half, inten, 235, 22, 30);
        if (frand() > 0.78f)
            speed_line(c, x0 + len * 0.2f, x0 + len * 0.8f, y, half * 0.4f, inten, 255, 170, 160);
    }
}

static void sidebar_art(canvas_t *c) {
    float k = c->k;
    fill_dev(c, 0, 0, (int)(ART_SIDE_W * k), c->h, 5, 5, 8);
    haze(c, 40, 700, 300, 230, 130, 6, 10, 0.42f, 0, 0, ART_SIDE_W, ART_H);
    haze(c, 130, 0, 230, 170, 100, 4, 8, 0.30f, 0, 0, ART_SIDE_W, ART_H);
    /* glowing red edge */
    {
        int y;
        int xe = (int)((ART_SIDE_W - 1) * k);
        for (y = 0; y < c->h; y++) {
            float f = 0.45f + 0.55f * sinf(PI_F * (float)y / (float)c->h);
            int d;
            for (d = 0; d < (int)(7 * k) + 1; d++) {
                float a = f * expf(-d / (2.4f * k));
                add_px(c, xe - d, y, 150 * a * 0.35f, 8 * a * 0.35f, 14 * a * 0.35f);
            }
            mix_px(c, xe, y, 120, 14, 20, 0.7f * f);
            mix_px(c, xe + 1, y, 18, 18, 24, 1.0f);
        }
    }
    art_emblem(c, 50.0f, 50.0f, 24.0f, 1);
    art_wordmark(c, "NINJA GAIDEN", 90.0f, 26.0f, 17.0f, 0.9f, 0);
    art_wordmark(c, "BLACK", 90.0f, 52.0f, 15.0f, 3.2f, 1);
}

static void footer_art(canvas_t *c) {
    float k = c->k;
    int fy = (int)((ART_H - ART_FOOT_H) * k), x, d;
    fill_dev(c, (int)(ART_SIDE_W * k), fy, c->w, c->h, 6, 6, 9);
    haze(c, 760, 760, 620, 160, 120, 6, 10, 0.30f, ART_SIDE_W, ART_H - ART_FOOT_H, ART_W, ART_H);
    for (x = (int)(ART_SIDE_W * k); x < c->w; x++) {
        float u = (float)(x - (int)(ART_SIDE_W * k)) / (float)(c->w - (int)(ART_SIDE_W * k));
        float f = powf(sinf(PI_F * u), 0.6f);
        for (d = 0; d < (int)(9 * k); d++) {
            float a = f * expf(-d / (2.8f * k)) * 0.22f;
            add_px(c, x, fy - 1 - d, 200 * a, 12 * a, 20 * a);
        }
        mix_px(c, x, fy, 190, 22, 30, 0.8f * f);
    }
}

void art_backdrop(canvas_t *c, int kind) {
    float k = c->k;
    int x, y;
    for (y = 0; y < c->h; y++) {
        uint32_t v;
        float t = (float)y / (float)c->h;
        int base = (int)(11 - 4 * t);
        v = ((uint32_t)base << 16) | ((uint32_t)base << 8) | (uint32_t)(base + 3);
        for (x = 0; x < c->w; x++)
            c->px[(size_t)y * c->w + x] = v;
    }
    if (kind == ART_HERO) {
        haze(c, 780, 150, 640, 290, 105, 5, 9, 0.55f, ART_SIDE_W, 0, ART_W, ART_H);
        haze(c, 1060, 150, 330, 250, 255, 30, 34, 0.55f, ART_SIDE_W, 0, ART_W, ART_H);
        haze(c, 520, 110, 420, 90, 150, 6, 12, 0.36f, ART_SIDE_W, 0, ART_W, ART_H);
        hero_lines(c, 0);
        shade_rows(c, 190, 372, ART_SIDE_W, ART_W, 1.0f, 0.38f);
        shade_rows(c, 372, ART_H, ART_SIDE_W, ART_W, 0.38f, 0.38f);
        art_emblem(c, 1062.0f, 150.0f, 108.0f, 1);
        art_wordmark(c, "NINJA GAIDEN", 316.0f, 56.0f, 70.0f, 1.0f, 0);
        art_wordmark(c, "BLACK", 320.0f, 150.0f, 50.0f, 4.0f, 1);
        speed_line(c, 640.0f, 960.0f, 176.0f, 1.1f, 0.9f, 255, 36, 44);
        speed_line(c, 700.0f, 900.0f, 181.0f, 0.5f, 0.8f, 255, 190, 180);
    } else {
        haze(c, 1130, 20, 560, 250, 105, 5, 9, 0.78f, ART_SIDE_W, 0, ART_W, ART_H);
        haze(c, 1180, 0, 300, 150, 240, 24, 30, 0.34f, ART_SIDE_W, 0, ART_W, ART_H);
        hero_lines(c, 1);
        shade_rows(c, 100, 330, ART_SIDE_W, ART_W, 1.0f, 0.5f);
        shade_rows(c, 330, ART_H, ART_SIDE_W, ART_W, 0.5f, 0.5f);
    }
    (void)k;
    sidebar_art(c);
    footer_art(c);
}
