/*
 * art.h: the launcher's backdrops (docs/target/launcher-laca-design.md).
 *
 * Everything is composed into a plain 0xRRGGBB pixel buffer ("canvas") once per DPI and cached
 * by ui.c. The hero of the Play page is either the procedural ink landscape (ink.c, our own art)
 * or, in game art mode, the key art read from the user's copy (gameart.c) graded into the
 * lacquer palette. Inner pages are flat lacquer, with the game's title background faintly in the
 * left column in game art mode. Coordinates are logical (96 dpi); `k` scales them to device pixels.
 */
#pragma once

#include <stdint.h>

#define ART_W 1240
#define ART_H 700
#define ART_TOP_H 64   /* top bar */
#define ART_BAR_Y 588  /* Play page bottom bar */
#define ART_SIDE_W 310 /* left column of the inner pages */

enum art_kind { ART_HERO, ART_INNER };

typedef struct canvas {
    uint32_t *px; /* w * h pixels, 0xRRGGBB */
    int w, h;
    float k; /* device pixels per logical pixel */
} canvas_t;

/* Paints a whole backdrop. game_art: use the user's game art where it is available. */
void art_backdrop(canvas_t *c, int kind, int game_art);

/* One frame: `cached` (a backdrop of the same size) plus what moves at time t (seconds). */
void art_animate(canvas_t *frame, const canvas_t *cached, int kind, int game_art, double t);

/* The largest DPI, not above `dpi` and not below 72, at which the window (ART_W x ART_H logical plus
 * a frame of frame_w x frame_h device pixels) fits in avail_w x avail_h device pixels. */
int art_fit_dpi(int dpi, int avail_w, int avail_h, int frame_w, int frame_h);
