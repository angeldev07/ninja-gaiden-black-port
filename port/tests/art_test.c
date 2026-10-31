/*
 * art_test.c: the launcher's backdrops (launcher\art.c) without game art.
 *
 * Regression: the hero's left-side darkening once mixed the ink toward the lacquer in unsigned
 * arithmetic, so ink darker than the lacquer wrapped around into bright magenta blocks. The
 * left third of the Play backdrop must stay dark and every pixel of both backdrops is written.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../launcher/art.h"

static int g_fail;
#define CHECK(c)                                                                         \
    do {                                                                                 \
        if (!(c)) {                                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                          \
            g_fail++;                                                                    \
        }                                                                                \
    } while (0)

static int max_channel(const canvas_t *c, int x0, int x1) {
    int x, y, m = 0;
    for (y = 0; y < c->h; y++)
        for (x = x0; x < x1; x++) {
            uint32_t p = c->px[(size_t)y * c->w + x];
            int ch[3] = {(int)((p >> 16) & 255), (int)((p >> 8) & 255), (int)(p & 255)}, i;
            for (i = 0; i < 3; i++)
                if (ch[i] > m)
                    m = ch[i];
        }
    return m;
}

static int unwritten(const canvas_t *c) {
    size_t i, n = (size_t)c->w * c->h;
    int bad = 0;
    for (i = 0; i < n; i++)
        bad += (c->px[i] & 0xffffff) == 0xababab;
    return bad;
}

int main(void) {
    canvas_t c;
    c.w = ART_W;
    c.h = ART_H;
    c.k = 1.0f;
    c.px = (uint32_t *)malloc(sizeof(uint32_t) * ART_W * ART_H);
    if (!c.px)
        return 1;
    memset(c.px, 0xAB, sizeof(uint32_t) * ART_W * ART_H);
    art_backdrop(&c, ART_HERO, 0);
    CHECK(unwritten(&c) == 0);
    CHECK(max_channel(&c, 0, ART_W / 3) < 48); /* the title side is dark lacquer with a trace of ink */
    memset(c.px, 0xAB, sizeof(uint32_t) * ART_W * ART_H);
    art_backdrop(&c, ART_INNER, 0);
    CHECK(unwritten(&c) == 0);
    CHECK(max_channel(&c, 0, ART_W) < 48);
    free(c.px);
    printf("art_test: %s\n", g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}
