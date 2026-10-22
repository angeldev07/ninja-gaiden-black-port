/*
 * raster.h: software reference rasterizer (H7b).
 *
 * Just enough to show the game's 2D UI: triangles with vertices already in screen space
 * (XYZRHW), diffuse color, one texture stage with point sampling, alpha test and blending.
 * Perspective-correct interpolation (1/w). Pure code (no Windows, no global state), tested
 * in port/tests/raster_test.c.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct rs_vertex {
    float x, y, z, rhw; /* position in screen pixels and 1/w */
    float r, g, b, a;   /* diffuse color, 0..1 */
    float u, v;         /* texture coordinates (normalized or in texels, see rs_texture) */
} rs_vertex_t;

enum rs_tex_format {
    RS_TEX_A8R8G8B8 = 1, /* u32 0xAARRGGBB */
    RS_TEX_X8R8G8B8 = 2,
    RS_TEX_R5G6B5 = 3,
    RS_TEX_A8 = 4, /* alpha only: white color */
    RS_TEX_I8 = 5, /* index into a 256-color A8R8G8B8 palette */
};

typedef struct rs_texture {
    const uint8_t *data;
    const uint8_t *palette; /* RS_TEX_I8 */
    uint32_t width, height, pitch;
    int format;     /* RS_TEX_* (0 = no texture) */
    int swizzled;   /* NV2A Morton order (powers of two) */
    int normalized; /* 1: u, v in 0..1; 0: in texels (linear "rect" textures) */
} rs_texture_t;

typedef struct rs_target {
    uint8_t *pixels;
    uint32_t width, height, pitch;
    int bpp;                                 /* 4: A8R8G8B8, 2: R5G6B5 */
    int clip_x0, clip_y0, clip_x1, clip_y1;  /* inclusive-exclusive rectangle */
} rs_target_t;

/* Factors and functions with the NV2A GL values (0x0300 SRC_COLOR, 0x0302 SRC_ALPHA...). */
typedef struct rs_state {
    int blend;
    uint32_t sfactor, dfactor;
    int alpha_test;
    uint32_t alpha_func; /* 0x0200 NEVER .. 0x0207 ALWAYS */
    uint32_t alpha_ref;  /* 0..255 */
    int use_texture;     /* minimal combiner: texture x diffuse (or diffuse only) */
} rs_state_t;

/* Pixels written by the triangle (those that pass the alpha test). */
uint32_t rs_draw_triangle(const rs_target_t *t, const rs_texture_t *tex, const rs_state_t *st,
                          const rs_vertex_t *v0, const rs_vertex_t *v1, const rs_vertex_t *v2);

/* A8R8G8B8 color of the texture at (u, v), point sampling and clamped coordinates. */
uint32_t rs_sample(const rs_texture_t *tex, float u, float v);

/* Offset (in texels) of (x, y) in NV2A Morton order. */
uint32_t rs_swizzle_offset(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
