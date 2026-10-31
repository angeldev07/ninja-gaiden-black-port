/*
 * ink.h: the launcher's default hero art, a procedural ink-wash night landscape (original).
 *
 * Port of tools/proto/inkland.py: four ranges of ridged-fBm mountains whose ink is darkest at the
 * crest and fades into mist, air perspective, two mist bands, a pale moon, paper fibre and a
 * vignette. Deterministic for a given seed. The picture is laid out in logical coordinates
 * (1240 x 700 scaled by canvas->k), so it looks the same at every DPI.
 */
#pragma once

#include <stdint.h>

#include "art.h"

#define INK_SEED 7u

/* Fills the whole canvas. */
void ink_landscape(canvas_t *c, uint32_t seed);
