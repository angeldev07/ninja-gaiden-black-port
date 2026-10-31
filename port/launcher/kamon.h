/*
 * kamon.h: the launcher's emblem, an original silhouette drawn from scratch: a ninja in a leaping
 * lunge, sword raised back over the head, scarf trailing (not traced from any game artwork).
 *
 * The same geometry is used by port/res/make_icon.py for the app icon. Coordinates are on the unit
 * square: (0, 0) top left, (1, 1) bottom right, y grows downward.
 */
#pragma once

#include "ui.h"

/* Coverage 0..1 at (u, v) of the unit square. `aa` is the antialiasing width in unit coordinates
 * (one device pixel divided by the size); `thick` widens the strokes for tiny sizes (1 normally). */
float kamon_coverage(float u, float v, float aa, float thick);

/* Draws the emblem in the square (x, y, size, size), in logical pixels. */
void kamon_draw(int x, int y, int size, rgb_t col);
