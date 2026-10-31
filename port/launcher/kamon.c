/*
 * kamon.c: geometry of the launcher's crest. See kamon.h. kamon_draw lives in ui.c (it writes the
 * back buffer); this file is pure maths so the shape is unit-tested.
 *
 * Each sword, in its own frame (a along the sword from tip to pommel, b across), in units of the
 * radius: a pointed tip from a = -0.84 to -0.72, the blade to 0.32 (half width 0.048), a guard to
 * 0.41 (half width 0.18) and the grip to 0.75 (half width 0.044). The ring is centred on 0.90 with
 * a half width of 0.05. `thick` widens the strokes (not the lengths) for tiny icon sizes.
 */
#include "kamon.h"

#include <math.h>

static float box_sd(float a, float b, float a0, float a1, float hw) {
    return fmaxf(fabsf(b) - hw, fmaxf(a0 - a, a - a1));
}

static float sword_sd(float u, float v, float s, float c, float thick) {
    float a = -u * s + v * c, b = u * c + v * s, d;
    float tip_hw = 0.048f * thick * (a + 0.84f) / 0.12f;
    d = fmaxf(fabsf(b) - tip_hw, fmaxf(-0.84f - a, a + 0.72f));             /* tip */
    d = fminf(d, box_sd(a, b, -0.72f, 0.32f, 0.048f * thick));              /* blade */
    d = fminf(d, box_sd(a, b, 0.32f, 0.32f + 0.09f * thick, 0.18f));        /* guard */
    d = fminf(d, box_sd(a, b, 0.32f + 0.09f * thick, 0.75f, 0.044f * thick)); /* grip */
    return d;
}

float kamon_coverage(float u, float v, float aa, float thick) {
    const float s45 = 0.70710678f;
    float d = fabsf(sqrtf(u * u + v * v) - 0.90f) - 0.05f * thick;
    float cov;
    d = fminf(d, sword_sd(u, v, s45, s45, thick));
    d = fminf(d, sword_sd(u, v, -s45, s45, thick));
    cov = 0.5f - d / aa;
    return cov < 0.0f ? 0.0f : cov > 1.0f ? 1.0f : cov;
}
