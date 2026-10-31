/*
 * kamon_test.c: the launcher's emblem (launcher\kamon.c), an original leaping-ninja silhouette,
 * as a coverage function on the unit square.
 */
#include <stdio.h>

#include "../launcher/kamon.h"

static int g_fail;
#define CHECK(c)                                                                         \
    do {                                                                                 \
        if (!(c)) {                                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                          \
            g_fail++;                                                                    \
        }                                                                                \
    } while (0)

int main(void) {
    const float aa = 0.004f;
    CHECK(kamon_coverage(0.60f, 0.29f, aa, 1.0f) > 0.99f); /* head */
    CHECK(kamon_coverage(0.55f, 0.46f, aa, 1.0f) > 0.99f); /* torso */
    CHECK(kamon_coverage(0.475f, 0.14f, aa, 1.0f) > 0.99f); /* the raised blade */
    CHECK(kamon_coverage(0.685f, 0.72f, aa, 1.0f) > 0.99f); /* front shin */
    CHECK(kamon_coverage(0.275f, 0.758f, aa, 1.0f) > 0.99f); /* back shin */
    CHECK(kamon_coverage(0.10f, 0.40f, aa, 1.0f) < 0.01f); /* empty sky */
    CHECK(kamon_coverage(0.85f, 0.60f, aa, 1.0f) < 0.01f);
    CHECK(kamon_coverage(0.50f, 0.95f, aa, 1.0f) < 0.01f); /* under the feet */
    /* Thicker strokes for tiny sizes cover more. */
    CHECK(kamon_coverage(0.475f, 0.17f, aa, 2.0f) >= kamon_coverage(0.475f, 0.17f, aa, 1.0f));
    printf("kamon_test: %s\n", g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}
