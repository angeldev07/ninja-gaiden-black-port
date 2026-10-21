/*
 * raster.h: rasterizador de referencia por software (H7b).
 *
 * Lo justo para ver la UI 2D del juego: triángulos con vértices ya en pantalla (XYZRHW),
 * color difuso, una etapa de textura con muestreo por punto, prueba de alfa y mezcla.
 * Interpolación con corrección de perspectiva (1/w). Es código puro (sin Windows ni
 * estado global) y se prueba en port/tests/raster_test.c.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct rs_vertex {
    float x, y, z, rhw; /* posición en píxeles de pantalla y 1/w */
    float r, g, b, a;   /* color difuso, 0..1 */
    float u, v;         /* coordenadas de textura (normalizadas o en texels, ver rs_texture) */
} rs_vertex_t;

enum rs_tex_format {
    RS_TEX_A8R8G8B8 = 1, /* u32 0xAARRGGBB */
    RS_TEX_X8R8G8B8 = 2,
    RS_TEX_R5G6B5 = 3,
    RS_TEX_A8 = 4, /* solo alfa: color blanco */
    RS_TEX_I8 = 5, /* índice a una paleta de 256 colores A8R8G8B8 */
};

typedef struct rs_texture {
    const uint8_t *data;
    const uint8_t *palette; /* RS_TEX_I8 */
    uint32_t width, height, pitch;
    int format;     /* RS_TEX_* (0 = sin textura) */
    int swizzled;   /* orden Morton de la NV2A (potencias de dos) */
    int normalized; /* 1: u, v en 0..1; 0: en texels (texturas "rect" lineales) */
} rs_texture_t;

typedef struct rs_target {
    uint8_t *pixels;
    uint32_t width, height, pitch;
    int bpp;                                 /* 4: A8R8G8B8, 2: R5G6B5 */
    int clip_x0, clip_y0, clip_x1, clip_y1;  /* rectángulo inclusivo-exclusivo */
} rs_target_t;

/* Factores y funciones con los valores GL del NV2A (0x0300 SRC_COLOR, 0x0302 SRC_ALPHA...). */
typedef struct rs_state {
    int blend;
    uint32_t sfactor, dfactor;
    int alpha_test;
    uint32_t alpha_func; /* 0x0200 NEVER .. 0x0207 ALWAYS */
    uint32_t alpha_ref;  /* 0..255 */
    int use_texture;     /* combinador mínimo: textura x difuso (o solo difuso) */
} rs_state_t;

/* Píxeles escritos por el triángulo (los que pasan la prueba de alfa). */
uint32_t rs_draw_triangle(const rs_target_t *t, const rs_texture_t *tex, const rs_state_t *st,
                          const rs_vertex_t *v0, const rs_vertex_t *v1, const rs_vertex_t *v2);

/* Color A8R8G8B8 de la textura en (u, v), muestreo por punto y coordenadas acotadas. */
uint32_t rs_sample(const rs_texture_t *tex, float u, float v);

/* Offset (en texels) de (x, y) en orden Morton de la NV2A. */
uint32_t rs_swizzle_offset(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
