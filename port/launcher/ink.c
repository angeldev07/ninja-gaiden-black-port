/*
 * ink.c: procedural ink-wash night landscape. See ink.h; the reference is tools/proto/inkland.py.
 *
 * Speed: the picture is computed on a grid of at most 1240 x 700 (the logical window) and
 * scaled up bilinearly to the device size, then the paper grain is added at device resolution.
 * Per pixel only the nearest range that covers it is shaded (plus the one behind on its
 * antialiased edge), so the fBm work is about one band's worth instead of four. Rows are split
 * across up to 8 threads; each row only depends on the shared, read-only tables.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ink.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LW 1240.0f
#define LH 700.0f
#define NBANDS 4

static float g_perm[1024];

static float smoothf(float t) { return t * t * (3.0f - 2.0f * t); }
static float mixf(float a, float b, float t) { return a + (b - a) * t; }
static float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

static void seed_perm(uint32_t a) {
    int i;
    for (i = 0; i < 1024; i++) { /* mulberry32 */
        uint32_t t;
        a += 0x6D2B79F5u;
        t = a;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + ((t ^ (t >> 7)) * (t | 61u));
        g_perm[i] = (float)((t ^ (t >> 14)) & 0xFFFFFFFFu) / 4294967296.0f;
    }
}

static float hash2(int ix, int iy) {
    uint32_t h = (uint32_t)ix * 73u + (uint32_t)iy * 151u + ((uint32_t)(ix * iy) % 977u);
    return g_perm[h & 1023u];
}

static float vnoise1(float x, int salt) {
    float fi = floorf(x), f = smoothf(x - fi);
    int i = (int)fi;
    float a = g_perm[(uint32_t)(i * 31 + salt) & 1023u], b = g_perm[(uint32_t)((i + 1) * 31 + salt) & 1023u];
    return a + (b - a) * f;
}

static float vnoise2(float x, float y) {
    float fx0 = floorf(x), fy0 = floorf(y);
    int ix = (int)fx0, iy = (int)fy0;
    float fx = smoothf(x - fx0), fy = smoothf(y - fy0);
    float a = hash2(ix, iy), b = hash2(ix + 1, iy), c = hash2(ix, iy + 1), d = hash2(ix + 1, iy + 1);
    float top = a + (b - a) * fx;
    return top + ((c + (d - c) * fx) - top) * fy;
}

static float ridged1(float x, int salt) {
    float s = 0.0f, amp = 1.0f, f = 1.0f, norm = 0.0f;
    int o;
    for (o = 0; o < 5; o++) {
        float n = vnoise1(x * f, salt + o * 97), r = 1.0f - fabsf(2.0f * n - 1.0f);
        s += amp * r * r;
        norm += amp;
        amp *= 0.5f;
        f *= 2.02f;
    }
    return s / norm;
}

static float fbm2(float x, float y, int octaves) {
    float s = 0.0f, amp = 1.0f, f = 1.0f, norm = 0.0f;
    int o;
    for (o = 0; o < octaves; o++) {
        s += amp * vnoise2(x * f, y * f);
        norm += amp;
        amp *= 0.5f;
        f *= 2.02f;
    }
    return s / norm;
}

/* Warm lacquer night (0..255 per channel). */
static const float BG[3] = {19, 17, 15}, SKY_HI[3] = {40, 36, 32}, MIST[3] = {54, 49, 43};

static const struct band {
    float base, hs;
    int salt;
    float crest[3], fade;
} BANDS[NBANDS] = {
    {0.50f, 0.20f, 11, {34, 31, 28}, 70.0f},
    {0.59f, 0.18f, 23, {26, 23, 21}, 60.0f},
    {0.70f, 0.16f, 37, {18, 16, 14}, 55.0f},
    {0.84f, 0.12f, 41, {11, 10, 9}, 45.0f},
};

/* Colour of range `b` at logical (x, y), given its silhouette height s (logical y). */
static void band_ink(const struct band *b, float x, float y, float s, float out[3]) {
    float depth = y - s < 0.0f ? 0.0f : y - s;
    float ink = expf(-depth / b->fade);
    float tex = 0.75f + 0.5f * fbm2(x / 70.0f + (float)b->salt, y / 150.0f, 4);
    float macro = smoothf(clamp01(fbm2(x / 420.0f + (float)b->salt * 3.0f, y / 300.0f, 2) * 1.8f - 0.35f));
    float k = fminf(1.0f, ink * tex * (0.55f + 0.45f * macro));
    int i;
    for (i = 0; i < 3; i++)
        out[i] = mixf(MIST[i], b->crest[i], k);
}

typedef struct job {
    /* grid pass */
    uint32_t *grid;
    const float *sil;
    int gw, gh;
    /* device pass */
    canvas_t *c;
    int y0, y1;
} job_t;

static void grid_rows(const job_t *j) {
    uint32_t *px = j->grid;
    const float *sil = j->sil;
    int gw = j->gw, gh = j->gh, b, gx, gy;
    float sx = LW / (float)gw, sy = LH / (float)gh;
    const float mx = LW * 0.76f, my = LH * 0.25f, mr = LH * 0.062f;
    for (gy = j->y0; gy < j->y1; gy++) {
        float y = ((float)gy + 0.5f) * sy, t = y / LH;
        float sky = 1.0f - fminf(1.0f, fabsf(t - 0.48f) * 2.1f);
        float mband = fmaxf(0.0f, 1.0f - fabsf(t - 0.66f) / 0.05f) * 0.22f +
                      fmaxf(0.0f, 1.0f - fabsf(t - 0.79f) / 0.04f) * 0.16f;
        for (gx = 0; gx < gw; gx++) {
            float x = ((float)gx + 0.5f) * sx, c[3], dx = x - mx, dy = y - my;
            float dist = sqrtf(dx * dx + dy * dy), halo, cov, edge, ink[3];
            int i, top = -1;
            for (i = 0; i < 3; i++)
                c[i] = mixf(BG[i], SKY_HI[i], sky * 0.9f);
            halo = fmaxf(0.0f, 1.0f - dist / (mr * 5.5f));
            halo = halo * halo * 0.35f;
            c[0] += halo * 30.0f;
            c[1] += halo * 27.0f;
            c[2] += halo * 22.0f;
            if (dist < mr + 1.5f) {
                float tex = 0.92f + 0.08f * fbm2(x / 18.0f, y / 18.0f, 3);
                float mc = fminf(1.0f, mr + 1.5f - dist) * 0.32f;
                c[0] = mixf(c[0], 150.0f * tex, mc);
                c[1] = mixf(c[1], 138.0f * tex, mc);
                c[2] = mixf(c[2], 120.0f * tex, mc);
            }
            /* The nearest range whose (noisy) edge is above this pixel hides those behind it,
             * except on its 1 px antialiased edge, where the one behind shows through. */
            for (b = NBANDS - 1; b >= 0; b--)
                if (y >= sil[(size_t)b * gw + gx] - 2.0f) {
                    top = b;
                    break;
                }
            for (b = top >= 1 ? top - 1 : top; b >= 0 && b <= top; b++) {
                float s = sil[(size_t)b * gw + gx];
                edge = s + (vnoise2(x / 3.0f, y / 3.0f) - 0.5f) * 2.2f;
                if (y < edge - 1.0f)
                    continue;
                cov = fminf(1.0f, y - edge + 1.0f);
                band_ink(&BANDS[b], x, y, s, ink);
                for (i = 0; i < 3; i++)
                    c[i] = mixf(c[i], ink[i], cov);
            }
            if (mband > 0.0f) {
                float m = mband * (0.6f + 0.4f * vnoise2(x / 160.0f, 3.0f));
                for (i = 0; i < 3; i++)
                    c[i] = mixf(c[i], MIST[i], m);
            }
            px[(size_t)gy * gw + gx] = ((uint32_t)fminf(255.0f, fmaxf(0.0f, c[0])) << 16) |
                                       ((uint32_t)fminf(255.0f, fmaxf(0.0f, c[1])) << 8) |
                                       (uint32_t)fminf(255.0f, fmaxf(0.0f, c[2]));
        }
    }
}

static float *silhouettes(int gw) {
    float sx = LW / (float)gw;
    float *sil = (float *)malloc(sizeof(float) * NBANDS * (size_t)gw);
    float *tmp = (float *)malloc(sizeof(float) * (size_t)gw);
    int b, gx, pass;
    if (!sil || !tmp) {
        free(sil);
        free(tmp);
        return NULL;
    }
    for (b = 0; b < NBANDS; b++) {
        float *s = sil + (size_t)b * gw;
        for (gx = 0; gx < gw; gx++) {
            float x = ((float)gx + 0.5f) * sx;
            s[gx] = LH * BANDS[b].base - LH * BANDS[b].hs * (0.35f + 0.95f * x / LW) *
                                             (ridged1(x / 340.0f, BANDS[b].salt) * 1.7f - 0.3f);
        }
        for (pass = 0; pass < 2; pass++) {
            for (gx = 0; gx < gw; gx++)
                tmp[gx] = (s[gx > 0 ? gx - 1 : 0] + 2.0f * s[gx] + s[gx < gw - 1 ? gx + 1 : gw - 1]) / 4.0f;
            memcpy(s, tmp, sizeof(float) * (size_t)gw);
        }
    }
    free(tmp);
    return sil;
}

static uint32_t lerp_px(uint32_t a, uint32_t b, int t256) {
    uint32_t r = (((a >> 16) & 255) * (uint32_t)(256 - t256) + ((b >> 16) & 255) * (uint32_t)t256) >> 8;
    uint32_t g = (((a >> 8) & 255) * (uint32_t)(256 - t256) + ((b >> 8) & 255) * (uint32_t)t256) >> 8;
    uint32_t bl = ((a & 255) * (uint32_t)(256 - t256) + (b & 255) * (uint32_t)t256) >> 8;
    return (r << 16) | (g << 8) | bl;
}

static void device_rows(const job_t *j) {
    canvas_t *c = j->c;
    uint32_t *grid = j->grid;
    int gw = j->gw, gh = j->gh, x, y;
    /* Scale up (bilinear), then paper fibre and the vignette at device resolution. */
    for (y = j->y0; y < j->y1; y++) {
        float fy = ((float)y + 0.5f) * (float)gh / (float)c->h - 0.5f;
        int y0 = fy < 0.0f ? 0 : (int)fy, y1 = y0 + 1 < gh ? y0 + 1 : gh - 1;
        int ty = (int)((fy - (float)y0) * 256.0f);
        float vy = ((float)y / (float)c->h - 0.5f) * 1.6f;
        if (ty < 0)
            ty = 0;
        for (x = 0; x < c->w; x++) {
            uint32_t p;
            float lx = ((float)x + 0.5f) * LW / (float)c->w, ly = ((float)y + 0.5f) * LH / (float)c->h;
            float vx = ((float)x / (float)c->w - 0.5f) * 1.6f, vig = 1.0f - 0.28f * (vx * vx + vy * vy);
            float fib;
            int ch, out = 0;
            if (grid != c->px) {
                float fx = ((float)x + 0.5f) * (float)gw / (float)c->w - 0.5f;
                int x0 = fx < 0.0f ? 0 : (int)fx, x1 = x0 + 1 < gw ? x0 + 1 : gw - 1;
                int tx = (int)((fx - (float)x0) * 256.0f);
                if (tx < 0)
                    tx = 0;
                p = lerp_px(lerp_px(grid[(size_t)y0 * gw + x0], grid[(size_t)y0 * gw + x1], tx),
                            lerp_px(grid[(size_t)y1 * gw + x0], grid[(size_t)y1 * gw + x1], tx), ty);
            } else {
                p = grid[(size_t)y * gw + x];
            }
            fib = (hash2(x * 7 + 3, y * 13 + 5) - 0.5f) * 3.0f + (vnoise2(lx / 40.0f, ly * 1.3f) - 0.5f) * 2.0f;
            for (ch = 16; ch >= 0; ch -= 8) {
                float v = ((float)((p >> ch) & 255) + fib) * vig;
                int iv = v < 0.0f ? 0 : v > 255.0f ? 255 : (int)v;
                out |= iv << ch;
            }
            c->px[(size_t)y * c->w + x] = (uint32_t)out;
        }
    }
}

static DWORD WINAPI job_thread(LPVOID p) {
    job_t *j = (job_t *)p;
    if (j->c)
        device_rows(j);
    else
        grid_rows(j);
    return 0;
}

/* Runs fn over rows [0, rows) split across threads (inline when only one is available). */
static void run_jobs(void (*fn)(const job_t *), const job_t *base, int rows) {
    SYSTEM_INFO si;
    HANDLE th[8];
    job_t jobs[8];
    int n, i, started = 0;
    GetSystemInfo(&si);
    n = (int)si.dwNumberOfProcessors;
    n = n < 1 ? 1 : n > 8 ? 8 : n;
    if (rows < n * 8)
        n = 1;
    for (i = 0; i < n; i++) {
        jobs[i] = *base;
        jobs[i].y0 = rows * i / n;
        jobs[i].y1 = rows * (i + 1) / n;
    }
    for (i = 1; i < n; i++) {
        th[i] = CreateThread(NULL, 0, job_thread, &jobs[i], 0, NULL);
        if (!th[i])
            fn(&jobs[i]);
        else
            started |= 1 << i;
    }
    fn(&jobs[0]);
    for (i = 1; i < n; i++)
        if (started & (1 << i)) {
            WaitForSingleObject(th[i], INFINITE);
            CloseHandle(th[i]);
        }
}

void ink_landscape(canvas_t *c, uint32_t seed) {
    int gw = c->w < (int)LW ? c->w : (int)LW, gh = c->h < (int)LH ? c->h : (int)LH;
    uint32_t *grid;
    job_t base;
    if (!c->px || c->w <= 0 || c->h <= 0)
        return;
    seed_perm(seed);
    grid = (gw == c->w && gh == c->h) ? c->px : (uint32_t *)malloc(sizeof(uint32_t) * (size_t)gw * gh);
    if (!grid)
        return;
    memset(&base, 0, sizeof base);
    base.grid = grid;
    base.gw = gw;
    base.gh = gh;
    base.sil = silhouettes(gw);
    if (!base.sil) {
        if (grid != c->px)
            free(grid);
        return;
    }
    run_jobs(grid_rows, &base, gh);
    free((void *)base.sil);
    base.c = c;
    run_jobs(device_rows, &base, c->h);
    if (grid != c->px)
        free(grid);
}
