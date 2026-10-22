/*
 * raster_test.c: software rasterizer tests with hand-computed expected values.
 * Built and run by port\build.cmd; exits with code 1 if anything fails.
 */
#include <stdio.h>
#include <string.h>

#include "../src/gpu/raster.h"

static int g_failures, g_checks;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        g_checks++;                                                                 \
        if (!(cond)) {                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

static uint32_t g_fb[8 * 8];

static rs_target_t target(void) {
    rs_target_t t;
    memset(&t, 0, sizeof t);
    t.pixels = (uint8_t *)g_fb;
    t.width = t.height = 8;
    t.pitch = 8 * 4;
    t.bpp = 4;
    t.clip_x1 = t.clip_y1 = 8;
    return t;
}

static rs_vertex_t vtx(float x, float y, float r, float g, float b, float a, float u, float v) {
    rs_vertex_t o;
    o.x = x;
    o.y = y;
    o.z = 0.0f;
    o.rhw = 1.0f;
    o.r = r;
    o.g = g;
    o.b = b;
    o.a = a;
    o.u = u;
    o.v = v;
    return o;
}

static uint32_t px(int x, int y) {
    return g_fb[y * 8 + x];
}

/* Triangle (0,0) (4,0) (0,4): the centers with x + y + 1 <= 4 are in -> 4+3+2+1 = 10. */
static void test_triangle_coverage(void) {
    rs_target_t t = target();
    rs_state_t st;
    rs_vertex_t a = vtx(0, 0, 1, 1, 1, 1, 0, 0), b = vtx(4, 0, 1, 1, 1, 1, 0, 0);
    rs_vertex_t c = vtx(0, 4, 1, 1, 1, 1, 0, 0);
    uint32_t n;
    memset(g_fb, 0, sizeof g_fb);
    memset(&st, 0, sizeof st);
    n = rs_draw_triangle(&t, NULL, &st, &a, &b, &c);
    CHECK(n == 10);
    CHECK(px(0, 0) == 0xFFFFFFFFu && px(3, 0) == 0xFFFFFFFFu && px(0, 3) == 0xFFFFFFFFu);
    CHECK(px(4, 0) == 0 && px(3, 1) == 0 && px(1, 3) == 0);
    /* Same triangle with the other vertex order: same coverage. */
    memset(g_fb, 0, sizeof g_fb);
    CHECK(rs_draw_triangle(&t, NULL, &st, &a, &c, &b) == 10);
    /* Clipping: with clip_x1 = 2 only columns 0 and 1 remain (4 + 3 -> 2 + 2 + 2 + 1). */
    memset(g_fb, 0, sizeof g_fb);
    t.clip_x1 = 2;
    CHECK(rs_draw_triangle(&t, NULL, &st, &a, &b, &c) == 7);
}

/* Interpolated color: in a uniform-color quad (two triangles) the result is exact. */
static void test_flat_color_quad(void) {
    rs_target_t t = target();
    rs_state_t st;
    rs_vertex_t a = vtx(0, 0, 1, 0, 0, 1, 0, 0), b = vtx(8, 0, 1, 0, 0, 1, 0, 0);
    rs_vertex_t c = vtx(0, 8, 1, 0, 0, 1, 0, 0), d = vtx(8, 8, 1, 0, 0, 1, 0, 0);
    memset(g_fb, 0, sizeof g_fb);
    memset(&st, 0, sizeof st);
    CHECK(rs_draw_triangle(&t, NULL, &st, &a, &b, &c) + rs_draw_triangle(&t, NULL, &st, &b, &d, &c) >= 64);
    CHECK(px(0, 0) == 0xFFFF0000u && px(7, 7) == 0xFFFF0000u && px(7, 0) == 0xFFFF0000u);
}

/* A8R8G8B8 2x2 texture: each quadrant of an 8x8 quad takes its texel. */
static void test_texture_2x2(void) {
    static const uint32_t texels[4] = {0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu};
    rs_target_t t = target();
    rs_texture_t tex;
    rs_state_t st;
    rs_vertex_t a = vtx(0, 0, 1, 1, 1, 1, 0, 0), b = vtx(8, 0, 1, 1, 1, 1, 1, 0);
    rs_vertex_t c = vtx(0, 8, 1, 1, 1, 1, 0, 1), d = vtx(8, 8, 1, 1, 1, 1, 1, 1);
    memset(&tex, 0, sizeof tex);
    tex.data = (const uint8_t *)texels;
    tex.width = tex.height = 2;
    tex.pitch = 8;
    tex.format = RS_TEX_A8R8G8B8;
    tex.normalized = 1;
    memset(&st, 0, sizeof st);
    st.use_texture = 1;
    CHECK(rs_sample(&tex, 0.25f, 0.25f) == 0xFF0000FFu);
    CHECK(rs_sample(&tex, 0.75f, 0.25f) == 0xFF00FF00u);
    CHECK(rs_sample(&tex, 0.25f, 0.75f) == 0xFFFF0000u);
    CHECK(rs_sample(&tex, 0.75f, 0.75f) == 0xFFFFFFFFu);
    CHECK(rs_sample(&tex, -3.0f, 9.0f) == 0xFFFF0000u); /* clamped coordinates */
    memset(g_fb, 0, sizeof g_fb);
    rs_draw_triangle(&t, &tex, &st, &a, &b, &c);
    rs_draw_triangle(&t, &tex, &st, &b, &d, &c);
    CHECK(px(1, 1) == 0xFF0000FFu && px(6, 1) == 0xFF00FF00u);
    CHECK(px(1, 6) == 0xFFFF0000u && px(6, 6) == 0xFFFFFFFFu);
    /* Swizzle 2x2 = linear; in 4x4, (2,0) -> 4, (0,2) -> 8, (3,3) -> 15. */
    tex.swizzled = 1;
    CHECK(rs_sample(&tex, 0.75f, 0.25f) == 0xFF00FF00u);
    CHECK(rs_swizzle_offset(2, 0, 4, 4) == 4 && rs_swizzle_offset(0, 2, 4, 4) == 8);
    CHECK(rs_swizzle_offset(3, 3, 4, 4) == 15 && rs_swizzle_offset(1, 0, 4, 2) == 1);
}

static void test_a8_and_palette(void) {
    static const uint8_t alpha[1] = {0x80};
    static const uint8_t index[1] = {1};
    static const uint32_t palette[2] = {0xFF000000u, 0xFF336699u};
    rs_texture_t tex;
    memset(&tex, 0, sizeof tex);
    tex.width = tex.height = 1;
    tex.pitch = 1;
    tex.normalized = 1;
    tex.data = alpha;
    tex.format = RS_TEX_A8;
    CHECK(rs_sample(&tex, 0.5f, 0.5f) == 0x80FFFFFFu);
    tex.data = index;
    tex.palette = (const uint8_t *)palette;
    tex.format = RS_TEX_I8;
    CHECK(rs_sample(&tex, 0.5f, 0.5f) == 0xFF336699u);
}

/* 50 % red SRC_ALPHA / ONE_MINUS_SRC_ALPHA blend over blue, and alpha test. */
static void test_blend_and_alpha_test(void) {
    rs_target_t t = target();
    rs_state_t st;
    rs_vertex_t a = vtx(0, 0, 1, 0, 0, 0.5f, 0, 0), b = vtx(8, 0, 1, 0, 0, 0.5f, 0, 0);
    rs_vertex_t c = vtx(0, 8, 1, 0, 0, 0.5f, 0, 0);
    int i;
    uint32_t p;
    for (i = 0; i < 64; i++)
        g_fb[i] = 0xFF0000FFu;
    memset(&st, 0, sizeof st);
    st.blend = 1;
    st.sfactor = 0x302;
    st.dfactor = 0x303;
    rs_draw_triangle(&t, NULL, &st, &a, &b, &c);
    p = px(0, 0);
    CHECK(((p >> 16) & 0xFF) >= 0x7E && ((p >> 16) & 0xFF) <= 0x81); /* red ~0.5 */
    CHECK((p & 0xFF) >= 0x7E && (p & 0xFF) <= 0x81);                 /* blue ~0.5 */
    st.blend = 0;
    st.alpha_test = 1;
    st.alpha_func = 0x204; /* GREATER */
    st.alpha_ref = 0x90;
    CHECK(rs_draw_triangle(&t, NULL, &st, &a, &b, &c) == 0); /* 0x80 does not pass */
    st.alpha_ref = 0x10;
    CHECK(rs_draw_triangle(&t, NULL, &st, &a, &b, &c) > 0);
}

int main(void) {
    test_triangle_coverage();
    test_flat_color_quad();
    test_texture_2x2();
    test_a8_and_palette();
    test_blend_and_alpha_test();
    if (g_failures) {
        fprintf(stderr, "raster_test: %d of %d checks fail\n", g_failures, g_checks);
        return 1;
    }
    printf("raster_test: OK (%d checks)\n", g_checks);
    return 0;
}
