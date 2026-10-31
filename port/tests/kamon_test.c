/*
 * kamon_test.c: the launcher's crest (launcher\kamon.c) as a coverage function on the unit disc.
 * A ring of radius 0.90 and two straight swords crossed at +-45 degrees, tips up.
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
    const float aa = 0.01f; /* antialiasing width in unit coordinates */
    /* The swords cross at the centre. */
    CHECK(kamon_coverage(0.0f, 0.0f, aa, 1.0f) > 0.99f);
    /* The ring, on the axes. */
    CHECK(kamon_coverage(0.90f, 0.0f, aa, 1.0f) > 0.99f);
    CHECK(kamon_coverage(0.0f, -0.90f, aa, 1.0f) > 0.99f);
    /* Empty between ring and blades, and outside the ring. */
    CHECK(kamon_coverage(0.60f, 0.0f, aa, 1.0f) < 0.01f);
    CHECK(kamon_coverage(0.0f, 0.60f, aa, 1.0f) < 0.01f);
    CHECK(kamon_coverage(0.99f, 0.0f, aa, 1.0f) < 0.01f);
    /* A blade near its tip: rotated 45 degrees from "up" toward +x, so up-right (y grows down). */
    CHECK(kamon_coverage(0.50f, -0.50f, aa, 1.0f) > 0.99f);
    CHECK(kamon_coverage(-0.50f, -0.50f, aa, 1.0f) > 0.99f);
    /* The guards sit below the centre on both diagonals. */
    CHECK(kamon_coverage(-0.26f, 0.26f, aa, 1.0f) > 0.99f);
    /* Thicker strokes for tiny sizes cover more. */
    CHECK(kamon_coverage(0.0f, -0.96f, aa, 1.8f) > kamon_coverage(0.0f, -0.96f, aa, 1.0f));
    printf("kamon_test: %s\n", g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}
