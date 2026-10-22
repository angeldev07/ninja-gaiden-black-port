/*
 * art.h: original procedural artwork of the launcher (nothing here comes from the game).
 *
 * Everything is drawn into a plain 0xRRGGBB pixel buffer ("canvas") from code: a red mist
 * behind the hero, horizontal speed lines, a brushed-chrome wordmark made of our own
 * geometric slanted letters, and an abstract emblem (a faceted shuriken crossed by a slash).
 * Coordinates are logical (96 dpi); `k` scales them to device pixels.
 */
#pragma once

#include <stdint.h>

#define ART_W 1240
#define ART_H 700
#define ART_SIDE_W 264
#define ART_FOOT_H 88
#define ART_HERO_H 330

enum art_kind { ART_HERO, ART_INNER };

typedef struct canvas {
    uint32_t *px; /* w * h pixels, 0xRRGGBB */
    int w, h;
    float k; /* device pixels per logical pixel */
} canvas_t;

/* Paints the whole window background: sidebar with the logo, the main area (hero banner with
 * the big wordmark and emblem for ART_HERO; a quieter mist for ART_INNER) and the footer. */
void art_backdrop(canvas_t *c, int kind);

/* Pieces, reusable for other surfaces. */
void art_emblem(canvas_t *c, float cx, float cy, float radius, int slash);
void art_wordmark(canvas_t *c, const char *text, float x, float y, float height, float tracking,
                  int red_style);
/* Brushed-chrome (or red steel) colour for a vertical position t in 0..1. */
void art_chrome(float t, int red_style, float *r, float *g, float *b);
/* Width (logical) the wordmark will take. */
float art_wordmark_width(const char *text, float height, float tracking);
