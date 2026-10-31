/*
 * kamon.h: the launcher's emblem, an original crest drawn from scratch: a ring with two straight
 * swords crossed at +-45 degrees, tips up (a traditional motif, "maru ni chigai-ken").
 *
 * The same geometry is used by port/res/make_icon.py for the app icon. Coordinates are on the unit
 * disc: (0, 0) is the centre, 1 the outer radius, y grows downward.
 */
#pragma once

#include "ui.h"

/* Coverage 0..1 at (u, v). `aa` is the antialiasing width in unit coordinates (one device pixel
 * divided by the radius); `thick` scales the stroke widths (1 normally, more for tiny sizes). */
float kamon_coverage(float u, float v, float aa, float thick);

/* Draws the crest centred at (cx, cy) with radius r, in logical pixels. */
void kamon_draw(int cx, int cy, int r, rgb_t col);
