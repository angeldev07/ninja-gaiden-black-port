/*
 * gameart.h: the launcher's optional game art, read at runtime from the user's own copy.
 *
 * A worker thread loads demo.afs (demo.spr and titlebg.spr), decodes the textures the launcher
 * uses through port/src/gl/texconv.c and crops them to their sprite rectangles. Nothing is
 * written to disk and nothing is shipped: the art only ever exists in this process's memory.
 * Any asset that is missing or does not match the expected format and size is left NULL and
 * the launcher falls back to its own art (docs/target/launcher-laca-design.md).
 *
 * Reference prototype of the grade and the logo mask: tools/proto/gameart_proto.py.
 */
#pragma once

#include <stdint.h>

enum ga_asset { GA_HERO, GA_LOGO, GA_LOGO_BLACK, GA_TITLEBG, GA_RING, GA_COUNT };

typedef struct ga_image {
    int w, h;
    uint32_t *px;   /* 0xRRGGBB, ungraded (NULL for masks) */
    uint8_t *alpha; /* 0..255 coverage for masks and alpha textures (NULL otherwise) */
} ga_image_t;

/* The lacquer grade of one pixel: a duotone from bg to 86 % cream on a smoothstep of the
 * luminance, 14 % of the original chroma kept, saturated reds pulled toward vermilion. */
uint32_t ga_grade(uint8_t r, uint8_t g, uint8_t b);

/* Logo letterform mask from a logo pixel: keeps the letters, drops the soft red glow. */
uint8_t ga_logo_mask(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

/* Starts (or restarts) loading from `game_dir` (the folder holding demo.afs). hero_index picks
 * the key art (0 Ryu, 1 Ayane, modulo the ones found). Cancels and joins a previous load. When
 * done, posts `msg` to `hwnd` if one was given with gameart_notify. */
void gameart_start(const wchar_t *game_dir, int hero_index);
void gameart_notify(void *hwnd, unsigned msg);
/* 1 once the current load has finished (whatever it found). */
int gameart_ready(void);
/* The asset, or NULL if it is not available (yet). Valid until the next gameart_start/stop. */
const ga_image_t *gameart_get(int which);
/* Number of key arts found in the last load (for rotating hero_index). */
int gameart_hero_count(void);
/* Cancels a load and frees everything. */
void gameart_stop(void);
