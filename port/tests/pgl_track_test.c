/*
 * pgl_track_test.c: transform constants and attributes from NV097 methods (gpu/pgl_track.c).
 */
#include <stdio.h>
#include <string.h>

#include "gpu/pgraph_gl.h"

static int g_fail, g_checks;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        g_checks++;                                                                                \
        if (!(cond)) {                                                                             \
            printf("FAIL line %d: ", __LINE__);                                                    \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

static pgl_regs g_r;

static uint32_t bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static void put4(uint32_t method, float x, float y, float z, float w) {
    pgl_regs_track(&g_r, method, bits(x));
    pgl_regs_track(&g_r, method + 4, bits(y));
    pgl_regs_track(&g_r, method + 8, bits(z));
    pgl_regs_track(&g_r, method + 12, bits(w));
}

static int is4(int c, float x, float y, float z, float w) {
    const float *v = &g_r.constants[c * 4];
    return v[0] == x && v[1] == y && v[2] == z && v[3] == w;
}

int main(void) {
    uint64_t stamp;

    /* SET_TRANSFORM_CONSTANT_LOAD then SET_TRANSFORM_CONSTANT: consecutive words. */
    memset(&g_r, 0, sizeof g_r);
    pgl_regs_track(&g_r, 0x1EA4, 96);
    put4(0x0B80, 1.0f, 2.0f, 3.0f, 4.0f);
    put4(0x0B90, 5.0f, 6.0f, 7.0f, 8.0f);
    CHECK(is4(96, 1.0f, 2.0f, 3.0f, 4.0f), "c[96] from the constant load");
    CHECK(is4(97, 5.0f, 6.0f, 7.0f, 8.0f), "c[97] continues after c[96]");
    CHECK(g_r.const_load == 98 * 4, "load pointer at c[98], got %u", g_r.const_load);
    CHECK(g_r.const_stamp[96] && g_r.const_stamp[97], "both constants stamped");

    /* The viewport: SET_VIEWPORT_SCALE is c[58] and SET_VIEWPORT_OFFSET is c[59] (D3D's c[-38]
     * and c[-37]), which the epilogue of every vertex program reads. */
    put4(0x0AF0, 360.0f, -240.0f, 16777215.0f, 0.0f);
    put4(0x0A20, 360.53125f, 240.53125f, 0.0f, 0.0f);
    CHECK(is4(58, 360.0f, -240.0f, 16777215.0f, 0.0f), "viewport scale in c[58]");
    CHECK(is4(59, 360.53125f, 240.53125f, 0.0f, 0.0f), "viewport offset in c[59]");
    CHECK(g_r.const_stamp[58] && g_r.const_stamp[59], "viewport constants stamped for upload");
    CHECK(g_r.const_load == 98 * 4, "the viewport does not move the constant load pointer");
    CHECK(g_r.m[0x0AF0 / 4] == bits(360.0f), "the method itself is still stored for the fixed pipeline");

    /* The same value again is not a change; a new one is. */
    stamp = g_r.const_stamp[59];
    pgl_regs_track(&g_r, 0x0A20, bits(360.53125f));
    CHECK(g_r.const_stamp[59] == stamp, "same viewport offset: no new stamp");
    pgl_regs_track(&g_r, 0x0A20, bits(100.0f));
    CHECK(g_r.const_stamp[59] > stamp && g_r.constants[59 * 4] == 100.0f, "new viewport offset stamped");

    /* A constant load over c[58] writes the same storage. */
    pgl_regs_track(&g_r, 0x1EA4, 58);
    put4(0x0B80, 1.0f, 1.0f, 1.0f, 1.0f);
    CHECK(is4(58, 1.0f, 1.0f, 1.0f, 1.0f), "a constant load to c[58] replaces the viewport scale");

    /* Out of range loads are dropped. */
    pgl_regs_track(&g_r, 0x1EA4, 500);
    pgl_regs_track(&g_r, 0x0B80, bits(9.0f));
    CHECK(g_r.const_load == PGL_CONSTANTS * 4, "load past c[191] is clamped");

    /* Constant attributes: SET_VERTEX_DATA4UB is a D3DCOLOR (ARGB). */
    pgl_regs_track(&g_r, 0x1940 + 3 * 4, 0x80FF0000u);
    CHECK(g_r.vconst[3][0] == 1.0f && g_r.vconst[3][1] == 0.0f && g_r.vconst[3][3] == 128.0f / 255.0f,
          "D3DCOLOR attribute");

    printf("pgl_track_test: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
