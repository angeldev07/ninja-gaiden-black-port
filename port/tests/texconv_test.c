/*
 * texconv_test.c: pruebas sin GPU de texconv.c (contra vectores de ngb_tools.xpr), de la
 * traducción de estado de glstate.c y de los formatos de vértice de glbackend.c.
 *
 * Lo compila y ejecuta port\build.cmd; imprime "texconv_test: OK" o termina con código 1.
 * Los vectores (texconv_vectors.h) son sintéticos: los genera tests/port/texconv_vectors.py.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gl/glbackend.h"
#include "gl/glstate.h"
#include "gl/texconv.h"
#include "texconv_vectors.h"

static int g_failures, g_checks;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        g_checks++;                                                                 \
        if (!(cond)) {                                                              \
            fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

static void test_vectors(void) {
    size_t i;
    for (i = 0; i < sizeof k_vectors / sizeof k_vectors[0]; i++) {
        const texconv_vector *v = &k_vectors[i];
        texconv_desc d;
        uint8_t *dst;
        int r = texconv_describe(v->format, v->control1, v->image_rect, v->s3tc, &d);
        g_checks++;
        if (r != 0 || d.src_size != v->src_len || d.dst_size != v->expected_len) {
            fprintf(stderr, "FALLO %s: describe %d, src %zu/%zu, dst %zu/%zu\n", v->name, r,
                    d.src_size, v->src_len, d.dst_size, v->expected_len);
            g_failures++;
            continue;
        }
        dst = (uint8_t *)malloc(d.dst_size);
        r = texconv_convert(v->src, v->src_len, v->palette_len ? v->palette : NULL, v->palette_len,
                            &d, dst, d.dst_size);
        g_checks++;
        if (r != 0) {
            fprintf(stderr, "FALLO %s: convert %d\n", v->name, r);
            g_failures++;
        } else if (memcmp(dst, v->expected, d.dst_size) != 0) {
            size_t k = 0;
            while (dst[k] == v->expected[k]) {
                k++;
            }
            fprintf(stderr, "FALLO %s: byte %zu vale %u, Python da %u\n", v->name, k, dst[k],
                    v->expected[k]);
            g_failures++;
        }
        free(dst);
    }
}

static uint32_t fmt_word(int code, int log2u, int log2v, int levels, int cube) {
    return 0x9u | (2u << 4) | ((uint32_t)code << 8) | ((uint32_t)levels << 16) |
           ((uint32_t)log2u << 20) | ((uint32_t)log2v << 24) | (cube ? 4u : 0u);
}

static void test_describe(void) {
    texconv_desc d;
    CHECK(texconv_describe(fmt_word(0x06, 3, 2, 1, 0), 0, 0, 0, &d) == 0);
    CHECK(d.width == 8 && d.height == 4 && d.swizzled && !d.compressed);
    CHECK(d.gl_internal == TEXCONV_GL_RGBA8 && d.gl_format == TEXCONV_GL_RGBA &&
          d.gl_type == TEXCONV_GL_UNSIGNED_BYTE && !strcmp(d.name, "A8R8G8B8"));
    /* mipmaps recortados a 1x1: 8x8 con 15 niveles pedidos -> 4 */
    CHECK(texconv_describe(fmt_word(0x05, 3, 3, 15, 0), 0, 0, 0, &d) == 0 && d.levels == 4);
    CHECK(d.level[3].width == 1 && d.level[3].src_offset == 128 + 32 + 8);
    /* DXT5 con S3TC: bloques tal cual */
    CHECK(texconv_describe(fmt_word(0x0F, 4, 4, 1, 0), 0, 0, 1, &d) == 0);
    CHECK(d.upload_compressed && d.gl_internal == TEXCONV_GL_COMPRESSED_DXT5 &&
          d.gl_format == 0 && d.dst_size == 16 * 16);
    CHECK(texconv_describe(fmt_word(0x0F, 4, 4, 1, 0), 0, 0, 0, &d) == 0);
    CHECK(!d.upload_compressed && d.gl_internal == TEXCONV_GL_RGBA8 && d.dst_size == 16 * 16 * 4);
    /* relieve con signo */
    CHECK(texconv_describe(fmt_word(0x28, 2, 2, 1, 0), 0, 0, 0, &d) == 0);
    CHECK(d.gl_internal == TEXCONV_GL_RG8_SNORM && d.gl_type == TEXCONV_GL_BYTE && d.dst_size == 32);
    CHECK(texconv_describe(fmt_word(0x27, 2, 2, 1, 0), 0, 0, 0, &d) == 0);
    CHECK(d.gl_internal == TEXCONV_GL_RGBA8_SNORM && d.dst_size == 64);
    /* cube map: 6 caras alineadas a 128 bytes */
    CHECK(texconv_describe(fmt_word(0x06, 2, 2, 1, 1), 0, 0, 0, &d) == 0);
    CHECK(d.faces == 6 && d.face_src_stride == 128 && d.src_size == 768);
    /* lineal: tamaño de IMAGE_RECT y pitch de CONTROL1 */
    CHECK(texconv_describe(fmt_word(0x12, 0, 0, 1, 0), 64u << 16, (5u << 16) | 3u, 0, &d) == 0);
    CHECK(d.linear && d.width == 5 && d.height == 3 && d.pitch == 64 && d.src_size == 192);
    /* errores */
    CHECK(texconv_describe(fmt_word(0x2A, 2, 2, 1, 0), 0, 0, 0, &d) == TEXCONV_ERR_FORMAT);
    CHECK(texconv_describe(fmt_word(0x24, 2, 2, 1, 0), 0, 0, 0, &d) == TEXCONV_ERR_FORMAT);
    CHECK(texconv_describe((fmt_word(0x06, 2, 2, 1, 0) & ~0xF0u) | 0x30u, 0, 0, 0, &d) ==
          TEXCONV_ERR_DIMENSIONS);
    CHECK(texconv_describe(fmt_word(0x06, 13, 2, 1, 0), 0, 0, 0, &d) == TEXCONV_ERR_DIMENSIONS);
    CHECK(texconv_describe(fmt_word(0x06, 3, 2, 1, 1), 0, 0, 0, &d) == TEXCONV_ERR_DIMENSIONS);
    CHECK(texconv_describe(fmt_word(0x12, 0, 0, 1, 0), 16u << 16, (5u << 16) | 3u, 0, &d) ==
          TEXCONV_ERR_DIMENSIONS); /* pitch menor que una fila */
    CHECK(texconv_describe(fmt_word(0x0B, 2, 2, 1, 0), 0, 0, 0, &d) == 0);
    {
        uint8_t src[16] = {0}, dst[64];
        CHECK(texconv_convert(src, 16, NULL, 0, &d, dst, sizeof dst) == TEXCONV_ERR_PALETTE);
        CHECK(texconv_convert(src, 15, (const uint8_t *)"", 0, &d, dst, sizeof dst) ==
              TEXCONV_ERR_SHORT);
    }
}

static void test_morton_and_palette(void) {
    /* 4x4: offset = y1 x1 y0 x0; 8x4: x2 y1 x1 y0 x0 (swizzle.py) */
    CHECK(texconv_morton(1, 0, 4, 4) == 1 && texconv_morton(0, 1, 4, 4) == 2);
    CHECK(texconv_morton(2, 0, 4, 4) == 4 && texconv_morton(3, 3, 4, 4) == 15);
    CHECK(texconv_morton(4, 0, 8, 4) == 16 && texconv_morton(7, 3, 8, 4) == 31);
    CHECK(texconv_palette_entries(0) == 256 && texconv_palette_entries(3u << 2) == 32);
}

static void test_glstate(void) {
    nv2a_state s;
    gl_state g;
    nv2a_state_defaults(&s, 720, 480);
    CHECK(glstate_translate(&s, &g) == 0);
    CHECK(!g.blend && g.blend_src == 1 && g.blend_dst == 0 && g.blend_equation == 0x8006);
    CHECK(g.depth_func == 0x0201 && g.depth_write && g.front_face == 0x0901); /* CCW tal cual */
    CHECK(g.scissor[0] == 0 && g.scissor[1] == 0 && g.scissor[2] == 720 && g.scissor[3] == 480);
    CHECK(g.color_mask[0] && g.color_mask[1] && g.color_mask[2] && g.color_mask[3]);

    s.blend_enable = 1;
    s.blend_sfactor = 0x0302;  /* SRC_ALPHA */
    s.blend_dfactor = 0x0303;  /* ONE_MINUS_SRC_ALPHA */
    s.blend_equation = 0xF006; /* FUNC_ADD_SIGNED */
    s.blend_color = 0x80FF4000u;
    s.depth_func = 0x0003;     /* LEQUAL, solo los 4 bits bajos */
    s.front_face = 0x0900;     /* CW en la Xbox */
    s.cull_face_enable = 1;
    s.cull_face = 0x0404;
    s.stencil_test_enable = 1;
    s.stencil_func = 0x0202;
    s.stencil_func_ref = 0x1A5;
    s.stencil_op_zpass = 0x8507;
    s.alpha_test_enable = 1;
    s.alpha_func = 0x0206;
    s.alpha_ref = 0x80;
    s.color_mask = 0x00010001u; /* solo rojo y azul */
    s.surface_clip_h = (100u << 16) | 10u;
    s.surface_clip_v = (50u << 16) | 20u;
    s.fog_enable = 1;
    s.fog_mode = 0x0801;
    s.fog_color = 0x40302010u;
    CHECK(glstate_translate(&s, &g) == 0);
    CHECK(g.blend && g.blend_src == 0x0302 && g.blend_dst == 0x0303);
    CHECK(g.blend_equation == 0x8006 && g.blend_signed);
    CHECK(fabsf(g.blend_color[0] - 1.0f) < 1e-6f && fabsf(g.blend_color[1] - 64 / 255.0f) < 1e-6f &&
          g.blend_color[2] == 0.0f && fabsf(g.blend_color[3] - 128 / 255.0f) < 1e-6f);
    CHECK(g.depth_func == 0x0203 && g.front_face == 0x0900 && g.cull && g.cull_face == 0x0404);
    CHECK(g.stencil && g.stencil_func == 0x0202 && g.stencil_ref == 0xA5 &&
          g.stencil_zpass == 0x8507);
    CHECK(g.alpha_test && g.alpha_func == 0x0206 && fabsf(g.alpha_ref - 128 / 255.0f) < 1e-6f);
    CHECK(g.color_mask[0] && !g.color_mask[1] && g.color_mask[2] && !g.color_mask[3]);
    CHECK(g.scissor[0] == 10 && g.scissor[1] == 480 - 70 && g.scissor[2] == 100 && g.scissor[3] == 50);
    CHECK(g.fog && g.fog_mode == 0x0801 && fabsf(g.fog_color[0] - 16 / 255.0f) < 1e-6f &&
          fabsf(g.fog_color[3] - 64 / 255.0f) < 1e-6f);

    s.blend_sfactor = 0x0309;
    CHECK(glstate_translate(&s, &g) == GLSTATE_ERR_BLEND);
    s.blend_sfactor = 0x0302;
    s.depth_func = 0x0208;
    CHECK(glstate_translate(&s, &g) == GLSTATE_ERR_COMPARE);
    s.depth_func = 0x0201;
    s.front_face = 0x0902;
    CHECK(glstate_translate(&s, &g) == GLSTATE_ERR_CULL);
    s.front_face = 0x0901;
    s.stencil_op_fail = 0x1E04;
    CHECK(glstate_translate(&s, &g) == GLSTATE_ERR_STENCIL);
    s.stencil_op_fail = 0x1E00;
    s.fog_mode = 0x0805;
    CHECK(glstate_translate(&s, &g) == GLSTATE_ERR_FOG);
    CHECK(glstate_translate(NULL, &g) == GLSTATE_ERR_ARGS);
}

static void test_vertex_formats(void) {
    glb_attrib a;
    glb_prim prim;
    float xyz[3];
    /* SET_VERTEX_DATA_ARRAY_FORMAT: tipo | tamaño << 4 | stride << 8 */
    CHECK(glb_attrib_from_nv2a(3, 0x0 | (4u << 4) | (24u << 8), 12, &a) == 0);
    CHECK(a.type == GLB_ATTR_UB_D3D && a.components == 4 && a.stride == 24 && a.offset == 12);
    CHECK(glb_attrib_from_nv2a(0, 0x2 | (3u << 4) | (24u << 8), 0, &a) == 0 &&
          a.type == GLB_ATTR_FLOAT && a.components == 3);
    CHECK(glb_attrib_from_nv2a(9, 0x1 | (2u << 4) | (8u << 8), 4, &a) == 0 && a.type == GLB_ATTR_S1);
    CHECK(glb_attrib_from_nv2a(9, 0x5 | (2u << 4) | (8u << 8), 4, &a) == 0 && a.type == GLB_ATTR_S32K);
    CHECK(glb_attrib_from_nv2a(9, 0x4 | (4u << 4) | (8u << 8), 4, &a) == 0 && a.type == GLB_ATTR_UB_OGL);
    CHECK(glb_attrib_from_nv2a(2, 0x6 | (1u << 4) | (16u << 8), 8, &a) == 0 &&
          a.type == GLB_ATTR_CMP && a.components == 3);
    CHECK(glb_attrib_from_nv2a(1, 0x2 | (16u << 8), 0, &a) == 1);            /* desactivado */
    CHECK(glb_attrib_from_nv2a(1, 0x3 | (2u << 4), 0, &a) == GLB_ERR_ARGS);   /* tipo 3 */
    CHECK(glb_attrib_from_nv2a(1, 0x0 | (3u << 4), 0, &a) == GLB_ERR_ARGS);   /* D3DCOLOR de 3 */
    /* CMP: x = 1023 (bits 0-10), y = -1023 (bits 11-21), z = -512 (bits 22-31) */
    glb_decode_cmp(0x3FFu | (0x401u << 11) | (0x200u << 22), xyz);
    CHECK(xyz[0] == 1.0f && xyz[1] == -1.0f && fabsf(xyz[2] - (-512.0f / 511.0f)) < 1e-6f);
    glb_decode_cmp(0, xyz);
    CHECK(xyz[0] == 0.0f && xyz[1] == 0.0f && xyz[2] == 0.0f);
    CHECK(glb_prim_from_nv2a(5, &prim) == 0 && prim == GLB_TRIANGLES);
    CHECK(glb_prim_from_nv2a(8, &prim) == 0 && prim == GLB_QUADS);
    CHECK(glb_prim_from_nv2a(10, &prim) == 0 && prim == GLB_POLYGON);
    CHECK(glb_prim_from_nv2a(0, &prim) == GLB_ERR_ARGS && glb_prim_from_nv2a(11, &prim) == GLB_ERR_ARGS);
}

int main(void) {
    test_vectors();
    test_describe();
    test_morton_and_palette();
    test_glstate();
    test_vertex_formats();
    if (g_failures) {
        fprintf(stderr, "texconv_test: %d de %d comprobaciones fallan\n", g_failures, g_checks);
        return 1;
    }
    printf("texconv_test: OK (%d comprobaciones, %d vectores de ngb_tools.xpr)\n", g_checks,
           (int)(sizeof k_vectors / sizeof k_vectors[0]));
    return 0;
}
