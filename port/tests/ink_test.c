/*
 * ink_test.c: tests of the launcher's procedural ink-wash landscape (launcher\ink.c).
 *
 * The picture is deterministic: same seed, same pixels. The checksum below pins the current
 * look; when the art is changed on purpose, update it (the failure message prints the new one).
 * The composition checks are coarse on purpose: a glowing middle sky, dark ranges at the bottom.
 * Timing: the full window at 200 % DPI must render within the spec's budget.
 * With INK_DUMP=file.bmp set, the 1240 x 700 picture is also written for review.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../launcher/ink.h"

static int g_fail;
#define CHECK(c)                                                                         \
    do {                                                                                 \
        if (!(c)) {                                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                          \
            g_fail++;                                                                    \
        }                                                                                \
    } while (0)

/* Checksum of the 256 x 144 render with INK_SEED (FNV-1a over the 0xRRGGBB words). */
#define INK_CHECKSUM 0x3eff9ce3u

static uint32_t fnv1a(const uint32_t *px, size_t n) {
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < n; i++) {
        h = (h ^ (px[i] & 0xffffff)) * 16777619u;
    }
    return h;
}

static double luma_rows(const canvas_t *c, int y0, int y1) {
    double sum = 0;
    int x, y;
    for (y = y0; y < y1; y++)
        for (x = 0; x < c->w; x++) {
            uint32_t p = c->px[(size_t)y * c->w + x];
            sum += 0.299 * ((p >> 16) & 255) + 0.587 * ((p >> 8) & 255) + 0.114 * (p & 255);
        }
    return sum / ((double)(y1 - y0) * c->w);
}

static canvas_t make(int w, int h) {
    canvas_t c;
    c.w = w;
    c.h = h;
    c.k = (float)w / 1240.0f;
    c.px = (uint32_t *)calloc((size_t)w * h, sizeof(uint32_t));
    return c;
}

static void dump_bmp(const char *path, const canvas_t *c) {
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    memset(&fh, 0, sizeof fh);
    memset(&ih, 0, sizeof ih);
    ih.biSize = sizeof ih;
    ih.biWidth = c->w;
    ih.biHeight = -c->h;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    fh.bfType = 0x4d42;
    fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + (DWORD)((size_t)c->w * c->h * 4);
    fwrite(&fh, sizeof fh, 1, f);
    fwrite(&ih, sizeof ih, 1, f);
    fwrite(c->px, 4, (size_t)c->w * c->h, f);
    fclose(f);
}

int main(void) {
    canvas_t a = make(256, 144), b = make(256, 144), big;
    size_t n = (size_t)256 * 144;
    LARGE_INTEGER f, t0, t1;
    double ms;
    uint32_t sum;

    ink_landscape(&a, INK_SEED);
    ink_landscape(&b, INK_SEED);
    CHECK(!memcmp(a.px, b.px, n * 4));
    ink_landscape(&b, INK_SEED + 1);
    CHECK(memcmp(a.px, b.px, n * 4) != 0);
    sum = fnv1a(a.px, n);
    if (sum != INK_CHECKSUM)
        printf("FAIL ink checksum 0x%08x (expected 0x%08x)\n", sum, INK_CHECKSUM), g_fail++;

    /* Composition: the sky glows in the middle; the nearest range darkens the bottom. */
    CHECK(luma_rows(&a, 0, 14) < luma_rows(&a, 65, 79));
    CHECK(luma_rows(&a, 130, 144) < luma_rows(&a, 65, 79));

    big = make(2480, 1400);
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    ink_landscape(&big, INK_SEED);
    QueryPerformanceCounter(&t1);
    ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
    printf("ink_test: 2480x1400 in %.0f ms\n", ms);
    CHECK(ms < 400.0);

    if (getenv("INK_DUMP")) {
        canvas_t w = make(1240, 700);
        ink_landscape(&w, INK_SEED);
        dump_bmp(getenv("INK_DUMP"), &w);
        free(w.px);
    }
    free(a.px);
    free(b.px);
    free(big.px);
    printf("ink_test: %s\n", g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}
