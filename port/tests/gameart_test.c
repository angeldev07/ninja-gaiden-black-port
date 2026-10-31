/*
 * gameart_test.c: the launcher's game art grade, logo mask and loader lifecycle (launcher\gameart.c).
 * No game data: the loader is pointed at folders without demo.afs and must fall back cleanly.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../launcher/gameart.h"

static int g_fail;
#define CHECK(c)                                                                         \
    do {                                                                                 \
        if (!(c)) {                                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                          \
            g_fail++;                                                                    \
        }                                                                                \
    } while (0)

static int near_rgb(uint32_t a, uint32_t b, int tol) {
    int i;
    for (i = 0; i < 24; i += 8) {
        int d = (int)((a >> i) & 255) - (int)((b >> i) & 255);
        if (d < -tol || d > tol)
            return 0;
    }
    return 1;
}

static void wait_ready(void) {
    int i;
    for (i = 0; i < 500 && !gameart_ready(); i++)
        Sleep(10);
}

int main(void) {
    uint32_t c;
    int i;
    wchar_t dir[MAX_PATH];

    /* Grade: black stays lacquer, white becomes 86 % cream, greys stay neutral-warm. */
    CHECK(ga_grade(0, 0, 0) == 0x13110f);
    CHECK(near_rgb(ga_grade(255, 255, 255), 0xcec7b8, 2));
    c = ga_grade(128, 128, 128);
    CHECK(abs((int)((c >> 16) & 255) - (int)((c >> 8) & 255)) <= 8);
    /* Saturated red moves toward vermilion; skin-like orange keeps little red push. */
    c = ga_grade(255, 0, 0);
    CHECK(((c >> 16) & 255) > 2 * ((c >> 8) & 255));
    CHECK(near_rgb(c, 0x632019, 3));
    /* Monotonic in luminance. */
    for (i = 1; i < 256; i += 15)
        CHECK((ga_grade((uint8_t)i, (uint8_t)i, (uint8_t)i) & 0xff00) >=
              (ga_grade((uint8_t)(i - 1), (uint8_t)(i - 1), (uint8_t)(i - 1)) & 0xff00));

    /* Logo mask: solid letters stay, soft glow (low alpha) goes, saturated red glow is cut down. */
    CHECK(ga_logo_mask(10, 10, 10, 255) >= 250);
    CHECK(ga_logo_mask(10, 10, 10, 120) == 0);
    CHECK(ga_logo_mask(200, 40, 40, 255) < 128);
    CHECK(ga_logo_mask(230, 230, 235, 255) >= 250); /* chrome edge of a letter */

    /* Subtitle trim: letters on rows 1-4, a near-empty row 5 (one stray pixel), a subtitle on
     * rows 6-8 right under it (one variant's BLACK texture does this): only rows 0-4 stay. */
    {
        uint8_t a[8 * 12];
        ga_image_t im;
        memset(a, 0, sizeof a);
        memset(a + 8 * 1, 255, 8 * 4);
        a[8 * 5 + 7] = 30;
        memset(a + 8 * 6 + 4, 220, 4);
        memset(a + 8 * 7 + 4, 220, 4);
        memset(a + 8 * 8 + 4, 220, 4);
        im.w = 8;
        im.h = 12;
        im.px = NULL;
        im.alpha = a;
        ga_trim_subtitle(&im);
        CHECK(im.h == 5);
        /* no subtitle: unchanged */
        memset(a + 8 * 5, 0, 8 * 7);
        im.h = 12;
        ga_trim_subtitle(&im);
        CHECK(im.h == 12);
    }

    /* Loader: no game, a restart while loading, and stop. Everything stays NULL. */
    GetTempPathW(MAX_PATH, dir);
    gameart_start(dir);
    gameart_start(L"C:\\definitely\\not\\here");
    wait_ready();
    CHECK(gameart_ready());
    for (i = 0; i < GA_COUNT; i++)
        CHECK(gameart_get(i) == NULL);
    gameart_stop();
    CHECK(gameart_get(GA_EMBLEM) == NULL);

    printf("gameart_test: %s\n", g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}
