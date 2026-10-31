/*
 * kamon.c: geometry of the launcher's emblem. See kamon.h. kamon_draw lives in ui.c (it writes the
 * back buffer); this file is pure maths so the shape is unit-tested.
 *
 * The silhouette is a union of tapered capsules (segment a-b, radius ra at a and rb at b) on the unit
 * square. port/res/make_icon.py carries the same table; keep both in step.
 */
#include "kamon.h"

#include <math.h>

static const float PARTS[][6] = {
    {0.600f, 0.290f, 0.066f, 0.600f, 0.290f, 0.066f}, /* head */
    {0.555f, 0.285f, 0.024f, 0.300f, 0.215f, 0.006f}, /* scarf, upper tail */
    {0.555f, 0.310f, 0.018f, 0.330f, 0.345f, 0.005f}, /* scarf, lower tail */
    {0.585f, 0.370f, 0.060f, 0.505f, 0.555f, 0.050f}, /* torso */
    {0.615f, 0.395f, 0.032f, 0.745f, 0.245f, 0.026f}, /* front arm, up to the hands */
    {0.545f, 0.405f, 0.029f, 0.715f, 0.265f, 0.024f}, /* back arm */
    {0.745f, 0.235f, 0.017f, 0.815f, 0.262f, 0.015f}, /* grip */
    {0.715f, 0.195f, 0.012f, 0.758f, 0.282f, 0.012f}, /* guard */
    {0.735f, 0.228f, 0.013f, 0.215f, 0.055f, 0.004f}, /* blade, raised back over the head */
    {0.545f, 0.565f, 0.050f, 0.705f, 0.615f, 0.042f}, /* front thigh */
    {0.705f, 0.615f, 0.040f, 0.665f, 0.815f, 0.031f}, /* front shin */
    {0.665f, 0.815f, 0.028f, 0.755f, 0.840f, 0.019f}, /* front foot */
    {0.475f, 0.585f, 0.048f, 0.355f, 0.700f, 0.039f}, /* back thigh */
    {0.355f, 0.700f, 0.037f, 0.195f, 0.815f, 0.028f}, /* back shin */
    {0.195f, 0.815f, 0.024f, 0.150f, 0.770f, 0.015f}, /* back foot */
};

static float capsule_sd(float x, float y, const float *p, float thick) {
    float dx = p[3] - p[0], dy = p[4] - p[1], l2 = dx * dx + dy * dy, t = 0.0f, px, py;
    if (l2 > 0.0f) {
        t = ((x - p[0]) * dx + (y - p[1]) * dy) / l2;
        t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    }
    px = p[0] + dx * t;
    py = p[1] + dy * t;
    return sqrtf((x - px) * (x - px) + (y - py) * (y - py)) - (p[2] + (p[5] - p[2]) * t) * thick;
}

float kamon_coverage(float u, float v, float aa, float thick) {
    float d = 9.0f, cov;
    size_t i;
    for (i = 0; i < sizeof PARTS / sizeof PARTS[0]; i++) {
        float di = capsule_sd(u, v, PARTS[i], thick);
        if (di < d)
            d = di;
    }
    cov = 0.5f - d / aa;
    return cov < 0.0f ? 0.0f : cov > 1.0f ? 1.0f : cov;
}
