/*
 * art.c: the launcher's backdrops. See art.h.
 *
 * Play (ART_HERO): the hero image fills the window and is darkened toward the left, where the
 * title sits, and under the two bars. The game's key art is graded once at its own size, then
 * sampled bilinearly: scaled to 660 logical pixels tall, top at 40, its subject (52 % across
 * the crop) at x 870, the crop's left edge softened over 160 pixels.
 * Inner pages (ART_INNER): flat lacquer; in game art mode the title screen background graded
 * at 7 % in the left column.
 * Every frame, art_animate copies the cached backdrop and adds what moves: two drifting mist strips, the ink moon's breathing halo and, in game art mode, the ornament
 * ring turning at 5 % behind the page number. With t fixed it is a still picture.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "art.h"
#include "gameart.h"
#include "ink.h"

#define BG_R 19
#define BG_G 17
#define BG_B 15

static float smoothstepf(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

static uint32_t mix_bg(uint32_t p, float k) {
    /* int, not uint32_t: a channel darker than the lacquer must not wrap around */
    int r = (int)((float)BG_R + (float)((int)((p >> 16) & 255) - BG_R) * k + 0.5f);
    int g = (int)((float)BG_G + (float)((int)((p >> 8) & 255) - BG_G) * k + 0.5f);
    int b = (int)((float)BG_B + (float)((int)(p & 255) - BG_B) * k + 0.5f);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t mix(uint32_t a, uint32_t b, float t) {
    int r = (int)(((a >> 16) & 255) + ((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t + 0.5f);
    int g = (int)(((a >> 8) & 255) + ((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t + 0.5f);
    int bl = (int)((a & 255) + ((int)(b & 255) - (int)(a & 255)) * t + 0.5f);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

/* Bilinear sample of an 0xRRGGBB image (clamped). */
static uint32_t sample_rgb(const uint32_t *px, int w, int h, float u, float v) {
    int x0, y0, x1, y1;
    float fx, fy;
    if (u < 0.0f)
        u = 0.0f;
    if (v < 0.0f)
        v = 0.0f;
    if (u > (float)w - 1.001f)
        u = (float)w - 1.001f;
    if (v > (float)h - 1.001f)
        v = (float)h - 1.001f;
    x0 = (int)u;
    y0 = (int)v;
    x1 = x0 + 1 < w ? x0 + 1 : x0;
    y1 = y0 + 1 < h ? y0 + 1 : y0;
    fx = u - (float)x0;
    fy = v - (float)y0;
    return mix(mix(px[(size_t)y0 * w + x0], px[(size_t)y0 * w + x1], fx),
               mix(px[(size_t)y1 * w + x0], px[(size_t)y1 * w + x1], fx), fy);
}

static float sample_a(const uint8_t *a, int w, int h, float u, float v) {
    int x0, y0, x1, y1;
    float fx, fy, t, b;
    if (u < 0.0f || v < 0.0f || u > (float)w - 1.001f || v > (float)h - 1.001f)
        return 0.0f;
    x0 = (int)u;
    y0 = (int)v;
    x1 = x0 + 1;
    y1 = y0 + 1;
    fx = u - (float)x0;
    fy = v - (float)y0;
    t = a[(size_t)y0 * w + x0] + (a[(size_t)y0 * w + x1] - a[(size_t)y0 * w + x0]) * fx;
    b = a[(size_t)y1 * w + x0] + (a[(size_t)y1 * w + x1] - a[(size_t)y1 * w + x0]) * fx;
    return (t + (b - t) * fy) / 255.0f;
}

/* The image graded into the palette, at its own size (malloc'd). */
static uint32_t *graded(const ga_image_t *im) {
    size_t i, n = (size_t)im->w * im->h;
    uint32_t *g = (uint32_t *)malloc(n * sizeof(uint32_t));
    if (!g)
        return NULL;
    for (i = 0; i < n; i++) {
        uint32_t p = im->px[i];
        g[i] = ga_grade((uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p);
    }
    return g;
}

/* The game's emblem (the silhouette in its red glow over the logotype; black background) as
 * light added to the lacquer: 540 px tall, centred at x 910, top at 44. */
static void emblem_hero(canvas_t *c, const ga_image_t *im) {
    float s = 540.0f / (float)im->h, ox = 910.0f - 0.5f * (float)im->w * s, oy = 44.0f;
    int X, Y;
    for (Y = 0; Y < c->h; Y++) {
        float y = ((float)Y + 0.5f) / c->k, v = (y - oy) / s;
        for (X = 0; X < c->w; X++) {
            float x = ((float)X + 0.5f) / c->k, u = (x - ox) / s;
            int r = BG_R, g = BG_G, b = BG_B;
            if (u >= 0.0f && v >= 0.0f && u < (float)im->w && v < (float)im->h) {
                uint32_t p = sample_rgb(im->px, im->w, im->h, u, v);
                r += (int)((p >> 16) & 255);
                g += (int)((p >> 8) & 255);
                b += (int)(p & 255);
            }
            c->px[(size_t)Y * c->w + X] = ((uint32_t)(r > 255 ? 255 : r) << 16) |
                                          ((uint32_t)(g > 255 ? 255 : g) << 8) | (uint32_t)(b > 255 ? 255 : b);
        }
    }
}

static void hero(canvas_t *c, int game_art, int which) {
    const ga_image_t *im = !game_art                  ? NULL
                           : which == ART_HERO_RYU    ? gameart_get(GA_HERO_RYU)
                           : which == ART_HERO_AYANE  ? gameart_get(GA_HERO_AYANE)
                                                      : NULL;
    uint32_t *g = im ? graded(im) : NULL;
    float s = 660.0f / (im ? (float)im->h : 1.0f), ox = 0.0f, oy = 40.0f;
    int X, Y;
    if (g)
        ox = 870.0f - 0.52f * (float)im->w * s;
    else
        ink_landscape(c, INK_SEED);
    for (Y = 0; Y < c->h; Y++) {
        float y = ((float)Y + 0.5f) / c->k, t = y / ART_H;
        float ft = smoothstepf(0.0f, 0.16f, t) * (1.0f - 0.65f * smoothstepf(0.80f, 1.0f, t));
        for (X = 0; X < c->w; X++) {
            float x = ((float)X + 0.5f) / c->k, fl, k;
            uint32_t p;
            if (g) {
                float u = (x - ox) / s, v = (y - oy) / s;
                float fe = smoothstepf(ox, ox + 160.0f, x);
                p = (u >= 0.0f && u < (float)im->w && v >= 0.0f && v < (float)im->h)
                        ? sample_rgb(g, im->w, im->h, u, v)
                        : ((uint32_t)BG_R << 16) | ((uint32_t)BG_G << 8) | BG_B;
                fl = smoothstepf(0.30f, 0.66f, x / ART_W);
                k = fl * fe * ft;
            } else {
                p = c->px[(size_t)Y * c->w + X];
                fl = 0.12f + 0.88f * smoothstepf(0.18f, 0.62f, x / ART_W); /* ink keeps a trace */
                k = fl;
            }
            c->px[(size_t)Y * c->w + X] = mix_bg(p, k);
        }
    }
    free(g);
}

static void inner(canvas_t *c, int game_art) {
    const ga_image_t *bg = game_art ? gameart_get(GA_TITLEBG) : NULL;
    uint32_t *g = bg ? graded(bg) : NULL;
    const uint32_t lacquer = ((uint32_t)BG_R << 16) | ((uint32_t)BG_G << 8) | BG_B;
    int X, Y;
    for (Y = 0; Y < c->h; Y++) {
        float y = ((float)Y + 0.5f) / c->k;
        for (X = 0; X < c->w; X++) {
            float x = ((float)X + 0.5f) / c->k;
            uint32_t p = lacquer;
            /* A very soft warm glow behind the list, so the page is not dead flat. */
            float dx = (x - 760.0f) / 700.0f, dy = (y - 300.0f) / 420.0f;
            float glow = 1.0f - (dx * dx + dy * dy);
            if (glow > 0.0f)
                p = mix(p, 0x1a1714, glow * 0.8f);
            if (g && x < ART_SIDE_W && y > ART_TOP_H) {
                /* cover the column with the 511 x 409 crop, keeping its aspect */
                float sc = (float)(ART_H - ART_TOP_H) / (float)bg->h;
                float u = x / sc, v = (y - ART_TOP_H) / sc;
                float fade = 1.0f - smoothstepf(ART_SIDE_W - 80.0f, (float)ART_SIDE_W, x);
                p = mix(p, sample_rgb(g, bg->w, bg->h, u, v), 0.07f * fade);
            }
            c->px[(size_t)Y * c->w + X] = p;
        }
    }
    free(g);
}

void art_backdrop(canvas_t *c, int kind, int game_art, int which) {
    const ga_image_t *em = game_art && which == ART_HERO_EMBLEM ? gameart_get(GA_EMBLEM) : NULL;
    if (!c->px)
        return;
    if (kind != ART_HERO)
        inner(c, game_art);
    else if (em)
        emblem_hero(c, em);
    else
        hero(c, game_art, which);
}

int art_heroes(int game_art, int *list) {
    static const int asset[ART_HERO_COUNT] = {GA_EMBLEM, GA_HERO_RYU, GA_HERO_AYANE};
    int i, n = 0;
    for (i = 0; game_art && i < ART_HERO_COUNT; i++)
        if (gameart_get(asset[i]))
            list[n++] = i;
    return n;
}

void art_hero_at(double t, int n, double hold, double fade, int *cur, int *next, float *mix) {
    double period = hold + fade, in;
    long k;
    *cur = *next = 0;
    *mix = 0.0f;
    if (n <= 1 || period <= 0.0 || t < 0.0)
        return;
    k = (long)(t / period);
    in = t - (double)k * period;
    *cur = (int)(k % n);
    *next = (*cur + 1) % n;
    if (in > hold) {
        double m = (in - hold) / fade;
        *mix = (float)(m * m * (3.0 - 2.0 * m)); /* eased */
    }
}

/* ------------------------------------------------------------------ motion ---- */

#define PI_F 3.14159265f

static uint32_t towards(uint32_t p, int r, int g, int b, int a256) {
    int pr = (int)((p >> 16) & 255), pg = (int)((p >> 8) & 255), pb = (int)(p & 255);
    pr += ((r - pr) * a256) >> 8;
    pg += ((g - pg) * a256) >> 8;
    pb += ((b - pb) * a256) >> 8;
    return ((uint32_t)pr << 16) | ((uint32_t)pg << 8) | (uint32_t)pb;
}

/* A soft horizontal band of mist drifting at `speed` logical px/s. The horizontal density is
 * computed once per frame (two sines per column), the vertical profile once per row. */
static void mist_strip(canvas_t *c, float y0, float y1, float speed, float phase, double t) {
    int Y, X, Y0 = (int)(y0 * c->k), Y1 = (int)(y1 * c->k);
    float off = (float)fmod(t * speed, 100000.0);
    float *dens = (float *)malloc(sizeof(float) * (size_t)c->w);
    if (!dens)
        return;
    for (X = 0; X < c->w; X++) {
        float x = (float)X / c->k + off;
        dens[X] = 0.5f + 0.3f * sinf(x * 0.0061f + phase) + 0.2f * sinf(x * 0.0173f + phase * 2.1f);
    }
    if (Y0 < 0)
        Y0 = 0;
    if (Y1 > c->h)
        Y1 = c->h;
    for (Y = Y0; Y < Y1; Y++) {
        float v = ((float)Y / c->k - y0) / (y1 - y0), prof = sinf(PI_F * v);
        uint32_t *row = c->px + (size_t)Y * c->w;
        int scale = (int)(prof * prof * 0.10f * 256.0f * 256.0f); /* at most 10 %, x256 fixed point */
        for (X = 0; X < c->w; X++) {
            int a256 = (int)(dens[X] * (float)scale) >> 8;
            if (a256 > 0)
                row[X] = towards(row[X], 54, 49, 43, a256);
        }
    }
    free(dens);
}

void art_animate(canvas_t *frame, const canvas_t *a, const canvas_t *b, float mix, int kind, int game_art,
                 int ink, double t) {
    size_t i, n = (size_t)frame->w * frame->h;
    if (b && mix > 0.0f) { /* crossfade between two heroes */
        int m = (int)(mix * 256.0f);
        for (i = 0; i < n; i++)
            frame->px[i] = towards(a->px[i], (int)((b->px[i] >> 16) & 255), (int)((b->px[i] >> 8) & 255),
                                   (int)(b->px[i] & 255), m);
    } else {
        memcpy(frame->px, a->px, sizeof(uint32_t) * n);
    }
    if (kind == ART_HERO) {
        mist_strip(frame, ART_H * 0.58f, ART_H * 0.74f, 6.0f, 0.7f, t);
        mist_strip(frame, ART_H * 0.70f, ART_H * 0.86f, 10.0f, 2.3f, t);
        if (ink) { /* the moon's halo breathes: +-3 % over 8 s */
            float cx = ART_W * 0.76f * frame->k, cy = ART_H * 0.25f * frame->k;
            float r = ART_H * 0.062f * 5.5f * frame->k, f = 0.03f * sinf((float)t * 2.0f * PI_F / 8.0f);
            int X0 = (int)(cx - r), X1 = (int)(cx + r), Y0 = (int)(cy - r), Y1 = (int)(cy + r), X, Y;
            for (Y = Y0 < 0 ? 0 : Y0; Y < Y1 && Y < frame->h; Y++)
                for (X = X0 < 0 ? 0 : X0; X < X1 && X < frame->w; X++) {
                    float ddx = (float)X - cx, ddy = (float)Y - cy, d = sqrtf(ddx * ddx + ddy * ddy) / r;
                    if (d < 1.0f) {
                        uint32_t *p = &frame->px[(size_t)Y * frame->w + X];
                        int a256 = (int)(fabsf(f) * (1.0f - d) * 256.0f * 4.0f);
                        *p = f > 0 ? towards(*p, 239, 231, 214, a256 / 4) : towards(*p, BG_R, BG_G, BG_B, a256 / 4);
                    }
                }
        }
        return;
    }
    if (game_art) { /* the ornament ring, 200 px, one turn every 120 s */
        const ga_image_t *ring = gameart_get(GA_RING);
        float ang = (float)fmod(t, 120.0) / 120.0f * 2.0f * PI_F, ca = cosf(ang), sa = sinf(ang);
        float cx = 152.0f * frame->k, cy = 170.0f * frame->k, half = 100.0f * frame->k;
        int X, Y;
        if (!ring)
            return;
        for (Y = (int)(cy - half); Y < (int)(cy + half); Y++)
            for (X = (int)(cx - half); X < (int)(cx + half); X++) {
                float ux = ((float)X - cx) / half, uy = ((float)Y - cy) / half;
                float u = (ux * ca + uy * sa + 1.0f) * 0.5f * (float)(ring->w - 1);
                float v = (-ux * sa + uy * ca + 1.0f) * 0.5f * (float)(ring->h - 1);
                float cov = sample_a(ring->alpha, ring->w, ring->h, u, v);
                if (cov > 0.0f && X >= 0 && Y >= 0 && X < frame->w && Y < frame->h) {
                    uint32_t *p = &frame->px[(size_t)Y * frame->w + X];
                    *p = towards(*p, 239, 231, 214, (int)(cov * 0.05f * 256.0f));
                }
            }
    }
}

int art_fit_dpi(int dpi, int avail_w, int avail_h, int frame_w, int frame_h) {
    /* S() is MulDiv(v, dpi, 96): the same rounding as the window size the launcher asks for */
    while (dpi > 72 && (ART_W * dpi + 48) / 96 + frame_w > avail_w)
        dpi--;
    while (dpi > 72 && (ART_H * dpi + 48) / 96 + frame_h > avail_h)
        dpi--;
    return dpi;
}
