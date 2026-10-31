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
    art_backdrop(&c, ART_HERO, 0, -1);
    CHECK(unwritten(&c) == 0);
    CHECK(max_channel(&c, 0, ART_W / 3) < 48); /* the title side is dark lacquer with a trace of ink */
    memset(c.px, 0xAB, sizeof(uint32_t) * ART_W * ART_H);
    art_backdrop(&c, ART_INNER, 0, -1);
    CHECK(unwritten(&c) == 0);
    CHECK(max_channel(&c, 0, ART_W) < 48);
    free(c.px);

    /* Window fit: the largest DPI (<= the system's) whose window fits the work area. Sizes round
     * like S() (MulDiv). */
    CHECK(art_fit_dpi(96, 1920, 1040, 16, 39) == 96);  /* 1080p at 100 %: fits as is */
    {
        int d = art_fit_dpi(144, 1920, 1008, 16, 39); /* 1080p laptop at 150 %: 1050 px tall does not fit */
        CHECK(d < 144 && (ART_H * d + 48) / 96 + 39 <= 1008 && (ART_H * (d + 1) + 48) / 96 + 39 > 1008);
    }
    {
        int d = art_fit_dpi(96, 1366, 728, 16, 31); /* 1366 x 768 at 100 % */
        CHECK(d < 96 && (ART_H * d + 48) / 96 + 31 <= 728);
    }
    CHECK(art_fit_dpi(192, 3840, 2100, 16, 39) == 192); /* 4K at 200 %: fits */
    CHECK(art_fit_dpi(96, 400, 300, 16, 31) == 72);     /* absurdly small: floor */
    /* Hero rotation: each hero holds 14 s, then a 1.2 s crossfade into the next, round and round. */
    {
        int cur, next;
        float mix;
        art_hero_at(0.0, 3, 14.0, 1.2, &cur, &next, &mix);
        CHECK(cur == 0 && next == 1 && mix == 0.0f);
        art_hero_at(14.6, 3, 14.0, 1.2, &cur, &next, &mix);
        CHECK(cur == 0 && next == 1 && mix > 0.45f && mix < 0.55f);
        art_hero_at(15.2 + 0.1, 3, 14.0, 1.2, &cur, &next, &mix);
        CHECK(cur == 1 && next == 2 && mix == 0.0f);
        art_hero_at(2 * 15.2 + 14.6, 3, 14.0, 1.2, &cur, &next, &mix);
        CHECK(cur == 2 && next == 0 && mix > 0.45f && mix < 0.55f); /* wraps to the first */
        art_hero_at(3 * 15.2 + 1.0, 3, 14.0, 1.2, &cur, &next, &mix);
        CHECK(cur == 0 && mix == 0.0f);
        art_hero_at(100.0, 1, 14.0, 1.2, &cur, &next, &mix); /* one hero: no fade */
        CHECK(cur == 0 && next == 0 && mix == 0.0f);
        art_hero_at(100.0, 0, 14.0, 1.2, &cur, &next, &mix); /* none: the ink */
        CHECK(cur == 0 && next == 0 && mix == 0.0f);
    }
    /* Without game art there is no hero to rotate. */
    {
        int list[ART_HERO_COUNT];
        CHECK(art_heroes(0, list) == 0);
    }
    printf("art_test: %s\n", g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}
