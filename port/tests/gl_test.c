/*
 * gl_test.c: prueba visual del backend de OpenGL con shaders traducidos del NV2A.
 *
 * Dibuja en el FBO de 720x480, arriba, un triángulo con color por vértice y un quad con una
 * textura de tablero; abajo, un quad con una textura DXT1 y otro con una P8 swizzled (las
 * dos convertidas por texconv), y encima una banda roja translúcida con blend alfa aplicado
 * por glstate desde valores NV097. Los vertex programs están escritos a mano en microcódigo
 * NV2A y pasan por nv2a_vsh_to_glsl; los fragment shaders son register combiners (r0 = v0 y
 * modulate t0 * v0) traducidos por nv2a_psh_to_glsl. Los vértices usan los formatos del
 * NV2A (float, D3DCOLOR, UB_OGL, short normalizado), con y sin índices y con QUADS. Lee el
 * FBO, comprueba unos píxeles y guarda la captura en %NGB_DATA%\runs\shots\gl_test.png.
 *
 *   gl_test [--hidden] [--out PNG]   la prueba (sin --hidden enseña la ventana 2 s)
 *   gl_test --compile DIR            compila con el driver todos los *.vert y *.frag de DIR
 *   gl_test --stress                 genera y compila variantes: cada operación MAC e ILU,
 *                                    A0, escritura de c[], cada mapeo, escala, mux, modo de
 *                                    textura y opción del combiner final
 *
 * Códigos de salida: 0 bien, 1 fallo, 77 no hay contexto OpenGL 4.x (los tests lo saltan).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gl/glbackend.h"
#include "gl/glpng.h"
#include "gl/glstate.h"
#include "gl/texconv.h"
#include "shaders/psh.h"
#include "shaders/vsh.h"

#define EXIT_NO_GL 77
#define W GLB_DEFAULT_WIDTH
#define H GLB_DEFAULT_HEIGHT

/*
 * Transformación a la screen space de la Xbox con c[96]-c[99] y diffuse (v3) a oD0:
 *   DP4 oPos.x, v0, c[96]   w1 = MAC 7 << 21 | CONST 96 << 13 (0xC0000) | A.swz xyzw (0x1B)
 *                           w2 = A.mux v (0x08000000) | B.swz (0x00360000) | B.mux c (0x1800)
 *                                | C.swz (0x6C);  w3 = máscara x (0x8000) | o[] (0x800) | oPos
 *   DP4 oPos.y/z/w          igual con CONST 97/98/99 y máscara y/z/w (0x4000/0x2000/0x1000)
 *   MOV oD0, v3             w1 = MAC 1 << 21 | V 3 << 9 (0x600) | 0x1B; w2 = A.mux v | swz;
 *                           w3 = máscara xyzw (0xF000) | o[] | dirección 3 << 3 (0x18) | FINAL
 *   MOV oT0, v9             w1 = MAC 1 << 21 | V 9 << 9 (0x1200) | 0x1B; w3 = 0xF000 | 0x800
 *                                | dirección 9 << 3 (0x48) | FINAL
 */
static const uint32_t k_vs_color[] = {
    0, 0x00EC001Bu, 0x0836186Cu, 0x00008800u, /* DP4 oPos.x, v0, c[96] */
    0, 0x00EC201Bu, 0x0836186Cu, 0x00004800u, /* DP4 oPos.y, v0, c[97] */
    0, 0x00EC401Bu, 0x0836186Cu, 0x00002800u, /* DP4 oPos.z, v0, c[98] */
    0, 0x00EC601Bu, 0x0836186Cu, 0x00001800u, /* DP4 oPos.w, v0, c[99] */
    0, 0x0020061Bu, 0x0836006Cu, 0x0000F819u, /* MOV oD0, v3 (FINAL) */
};
static const uint32_t k_vs_textured[] = {
    0, 0x00EC001Bu, 0x0836186Cu, 0x00008800u, 0, 0x00EC201Bu, 0x0836186Cu, 0x00004800u,
    0, 0x00EC401Bu, 0x0836186Cu, 0x00002800u, 0, 0x00EC601Bu, 0x0836186Cu, 0x00001800u,
    0, 0x0020061Bu, 0x0836006Cu, 0x0000F818u, /* MOV oD0, v3 */
    0, 0x0020121Bu, 0x0836006Cu, 0x0000F849u, /* MOV oT0, v9 (FINAL) */
};

static char g_vs_color[32768], g_vs_textured[32768], g_fs_color[32768], g_fs_textured[32768];
static char g_log[8192];
static int g_failures;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

static int translate_vs(const uint32_t *words, int n, char *out, size_t len) {
    static vsh_program prog;
    int r = nv2a_vsh_decode(words, n, &prog);
    if (r != n) {
        fprintf(stderr, "vsh_decode: %s\n", nv2a_vsh_error(r));
        return -1;
    }
    r = nv2a_vsh_to_glsl(&prog, out, len);
    return r < 0 ? -1 : 0;
}

/* Una etapa: AB = A * B -> r0 en RGB y alfa; el final queda a 0 (sale r0). */
static int translate_fs(uint32_t rgb_in, uint32_t alpha_in, uint32_t tex_modes, char *out,
                        size_t len) {
    psh_registers regs;
    static psh_program prog;
    memset(&regs, 0, sizeof regs);
    regs.combiner_control = 1;
    regs.rgb_inputs[0] = rgb_in;
    regs.rgb_outputs[0] = 0xC0; /* AB -> R0 */
    regs.alpha_inputs[0] = alpha_in;
    regs.alpha_outputs[0] = 0xC0;
    regs.texture_modes = tex_modes;
    if (nv2a_psh_decode(&regs, &prog) != 1) {
        return -1;
    }
    return nv2a_psh_to_glsl(&prog, out, len) < 0 ? -1 : 0;
}

static int build_shaders(void) {
    if (translate_vs(k_vs_color, 5, g_vs_color, sizeof g_vs_color) ||
        translate_vs(k_vs_textured, 6, g_vs_textured, sizeof g_vs_textured)) {
        return -1;
    }
    /* r0 = v0 * 1 (A = V0, B = cero invertido = 1) */
    if (translate_fs(0x04200000u, 0x14200000u, PSH_TEX_NONE, g_fs_color, sizeof g_fs_color)) {
        return -1;
    }
    /* modulate: r0 = t0 * v0, etapa 0 PROJECT2D */
    return translate_fs(0x08040000u, 0x18140000u, PSH_TEX_PROJECT2D, g_fs_textured,
                        sizeof g_fs_textured);
}

static char *read_text(const char *path) {
    FILE *f = fopen(path, "rb");
    long size;
    char *text;
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = (char *)malloc((size_t)size + 1);
    if (text && fread(text, 1, (size_t)size, f) != (size_t)size) {
        free(text);
        text = NULL;
    }
    if (text) {
        text[size] = 0;
    }
    fclose(f);
    return text;
}

/* Compila cada *.vert (enlazado con el FS modulate) y cada *.frag (con el VS textured). */
static int compile_dir(const char *dir) {
    static const char *const patterns[2] = {"*.vert", "*.frag"};
    int k, ok = 0, total = 0;
    for (k = 0; k < 2; k++) {
        char pattern[MAX_PATH];
        WIN32_FIND_DATAA found;
        HANDLE h;
        snprintf(pattern, sizeof pattern, "%s\\%s", dir, patterns[k]);
        h = FindFirstFileA(pattern, &found);
        if (h == INVALID_HANDLE_VALUE) {
            continue;
        }
        do {
            char path[MAX_PATH];
            char *text;
            unsigned prog = 0;
            int r;
            snprintf(path, sizeof path, "%s\\%s", dir, found.cFileName);
            text = read_text(path);
            total++;
            if (!text) {
                printf("FALLO %s: no se puede leer\n", found.cFileName);
                continue;
            }
            r = k == 0 ? glb_compile(text, g_fs_textured, &prog, g_log, sizeof g_log)
                       : glb_compile(g_vs_textured, text, &prog, g_log, sizeof g_log);
            if (r == 0) {
                ok++;
                printf("OK    %s\n", found.cFileName);
                glb_delete_program(prog);
            } else {
                printf("FALLO %s\n%s\n", found.cFileName, g_log);
            }
            free(text);
        } while (FindNextFileA(h, &found));
        FindClose(h);
    }
    printf("gl_test: compilan %d de %d shaders\n", ok, total);
    return ok == total && total > 0 ? 0 : 1;
}

/* ---- --stress: variantes generadas con el emisor y compiladas con el driver ----------- */

static void put(uint32_t *w, int word, int shift, unsigned bits, unsigned value) {
    w[word] |= (value & ((1u << bits) - 1u)) << shift;
}

/* Una instrucción con A = R1, B = c[k] y C = v2 (lo que la operación no use se ignora). */
static void encode_op(uint32_t *w, unsigned mac, unsigned ilu, unsigned constant,
                      unsigned out_reg, unsigned final) {
    memset(w, 0, 4 * sizeof *w);
    put(w, 1, 25, 3, ilu);
    put(w, 1, 21, 4, mac);
    put(w, 1, 13, 8, constant);
    put(w, 1, 9, 4, 2);       /* V = v2 */
    put(w, 1, 0, 8, 0x1B);    /* A.swz xyzw */
    put(w, 2, 28, 4, 1);      /* A = R1 */
    put(w, 2, 26, 2, 1);
    put(w, 2, 17, 8, 0x1B);
    put(w, 2, 11, 2, 3);      /* B = c[constant] */
    put(w, 2, 2, 8, 0x1B);
    put(w, 3, 28, 2, 2);      /* C = v2 */
    put(w, 3, 24, 4, mac && mac != 13 ? 0xF : 0);
    put(w, 3, 20, 4, 3);      /* R3 */
    put(w, 3, 16, 4, ilu ? 0xF : 0);
    put(w, 3, 12, 4, 0xF);    /* salida xyzw */
    put(w, 3, 11, 1, 1);
    put(w, 3, 3, 8, out_reg);
    put(w, 3, 2, 1, ilu ? 1u : 0u);
    put(w, 3, 0, 1, final);
}

static int stress_vs(const uint32_t *words, int n, const char *what, int *total) {
    static vsh_program prog;
    static char glsl[32768];
    unsigned handle = 0;
    (*total)++;
    if (nv2a_vsh_decode(words, n, &prog) != n || nv2a_vsh_to_glsl(&prog, glsl, sizeof glsl) < 0) {
        printf("FALLO vsh %s: no se traduce\n", what);
        return 0;
    }
    if (glb_compile(glsl, g_fs_textured, &handle, g_log, sizeof g_log)) {
        printf("FALLO vsh %s\n%s\n", what, g_log);
        return 0;
    }
    glb_delete_program(handle);
    return 1;
}

static int stress_fs(const psh_registers *regs, const char *what, int *total) {
    static psh_program prog;
    static char glsl[65536];
    unsigned handle = 0;
    (*total)++;
    if (nv2a_psh_decode(regs, &prog) < 0 || nv2a_psh_to_glsl(&prog, glsl, sizeof glsl) < 0) {
        printf("FALLO psh %s: no se traduce\n", what);
        return 0;
    }
    if (glb_compile(g_vs_textured, glsl, &handle, g_log, sizeof g_log)) {
        printf("FALLO psh %s\n%s\n", what, g_log);
        return 0;
    }
    glb_delete_program(handle);
    return 1;
}

static int run_stress(void) {
    uint32_t prog[4 * 4];
    psh_registers r;
    char what[64];
    unsigned op, k;
    int ok = 0, total = 0;
    for (op = 1; op <= 13; op++) {
        encode_op(prog, op, 0, 7, op == 13 ? 0 : VSH_OUT_T0, 0);
        encode_op(prog + 4, 1, 0, 8, VSH_OUT_POS, 1);
        if (op == 13) {
            prog[3] &= ~(0xFu << 12); /* ARL no escribe salidas */
        }
        snprintf(what, sizeof what, "MAC %s", nv2a_vsh_mac_name((int)op));
        ok += stress_vs(prog, 2, what, &total);
    }
    for (op = 1; op <= 7; op++) {
        encode_op(prog, 4, op, 9, VSH_OUT_D1, 1); /* MAD emparejado: el ILU va a R1 */
        snprintf(what, sizeof what, "ILU %s", nv2a_vsh_ilu_name((int)op));
        ok += stress_vs(prog, 1, what, &total);
    }
    /* ARL + lectura relativa + escritura de c[] + todas las salidas */
    encode_op(prog, 13, 0, 0, 0, 0);
    prog[3] &= ~(0xFu << 12);
    encode_op(prog + 4, 1, 0, 20, VSH_OUT_FOG, 0);
    put(prog + 4, 3, 1, 1, 1); /* A0X: B = c[A0 + 20] */
    encode_op(prog + 8, 2, 0, 30, 40, 0);
    prog[8 + 3] &= ~(1u << 11); /* ORB = 0: escribe c[40] */
    encode_op(prog + 12, 3, 0, 40, VSH_OUT_PTS, 1);
    ok += stress_vs(prog, 4, "A0 y c[] escribible", &total);

    for (k = 0; k < 8; k++) { /* cada mapeo en cada posición, con canal alfa en B y D */
        memset(&r, 0, sizeof r);
        r.combiner_control = 1;
        r.rgb_inputs[0] = (k << 29) | (0x08u << 24) | (k << 21) | (0x14u << 16) | (k << 13) |
                          (0x0Cu << 8) | (k << 5) | 0x13u;
        r.alpha_inputs[0] = r.rgb_inputs[0];
        r.rgb_outputs[0] = 0xC0 | (0xD << 8);
        r.alpha_outputs[0] = 0xC0;
        r.texture_modes = 1;
        snprintf(what, sizeof what, "mapeo %u", k);
        ok += stress_fs(&r, what, &total);
    }
    for (k = 0; k < 8; k++) { /* escala, dot, mux por bit bajo y alto, blue-to-alpha */
        if (k == 5 || k == 7) {
            continue;
        }
        memset(&r, 0, sizeof r);
        r.combiner_control = 2 | ((k & 1) << 8) | (1u << 12) | (1u << 16);
        r.rgb_inputs[0] = 0x48410000u | 0x0905u;
        r.rgb_outputs[0] = 0xC0 | (0xD << 8) | (1u << 12) | (1u << 13) | (k << 15) | (3u << 18);
        r.rgb_inputs[1] = 0x0C0D0905u;
        r.rgb_outputs[1] = (0xC << 8) | (1u << 14) | (k << 15);
        r.alpha_inputs[1] = 0x1C1D2000u;
        r.alpha_outputs[1] = (0xC << 8) | (1u << 14);
        r.texture_modes = 1 | (1u << 5);
        snprintf(what, sizeof what, "escala %u", k);
        ok += stress_fs(&r, what, &total);
    }
    for (k = 0; k <= 18; k++) { /* cada modo de textura en la etapa 1 (dependientes de la 0) */
        memset(&r, 0, sizeof r);
        r.combiner_control = 1;
        r.rgb_inputs[0] = 0x08090000u;
        r.rgb_outputs[0] = 0xC0;
        r.texture_modes = 1u | (k << 5) | (3u << 10) | (2u << 15);
        r.compare_mode = 0xA5A5u;
        snprintf(what, sizeof what, "textura modo %u", k);
        ok += stress_fs(&r, what, &total);
    }
    for (k = 0; k < 8; k++) { /* combiner final: complementos, clamp, EF_PROD y V1R0_SUM */
        memset(&r, 0, sizeof r);
        r.combiner_control = 1;
        r.rgb_inputs[0] = 0x08040000u;
        r.rgb_outputs[0] = 0xC0;
        r.final_inputs_abcd = 0x0F0E2C1Du;
        r.final_inputs_efg = 0x08040000u | 0x1C00u | (k << 5);
        r.texture_modes = 1;
        snprintf(what, sizeof what, "final %u", k);
        ok += stress_fs(&r, what, &total);
    }
    printf("gl_test: --stress compilan %d de %d variantes\n", ok, total);
    return ok == total ? 0 : 1;
}

/* Vértices: v0 = posición (x, y, z, w), v3 = color, v9 = uv. */
typedef struct vertex {
    float pos[4];
    float color[4];
    float uv[2];
} vertex;

static const glb_format k_format = {
    (int)sizeof(vertex), 3, {{0, 4, 0}, {3, 4, 16}, {9, 2, 32}},
};

static void make_checker(uint8_t *rgba, int size, int squares) {
    int x, y;
    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            int light = ((x * squares / size) + (y * squares / size)) % 2 == 0;
            uint8_t *p = rgba + 4 * (y * size + x);
            p[0] = (uint8_t)(light ? 240 : 30);
            p[1] = (uint8_t)(light ? 200 : 60);
            p[2] = (uint8_t)(light ? 60 : 160);
            p[3] = 255;
        }
    }
}

static const uint8_t *pixel(const uint8_t *img, int x, int y) {
    return img + 4 * (y * W + x);
}

static int in_range(int value, int lo, int hi) {
    return value >= lo && value <= hi;
}

static void output_path(char *path, size_t len, const char *override) {
    char root[MAX_PATH], dir[MAX_PATH];
    if (override) {
        snprintf(path, len, "%s", override);
        return;
    }
    if (!GetEnvironmentVariableA("NGB_DATA", root, sizeof root)) {
        char profile[MAX_PATH];
        if (!GetEnvironmentVariableA("USERPROFILE", profile, sizeof profile)) {
            snprintf(profile, sizeof profile, ".");
        }
        snprintf(root, sizeof root, "%s\\ngb-data", profile);
    }
    snprintf(dir, sizeof dir, "%s\\runs", root);
    CreateDirectoryA(dir, NULL);
    snprintf(dir, sizeof dir, "%s\\runs\\shots", root);
    CreateDirectoryA(dir, NULL);
    snprintf(path, len, "%s\\gl_test.png", dir);
}

/* Vértice con color D3DCOLOR (B G R A en memoria) y uv en shorts normalizados. */
typedef struct vertex_nv {
    float pos[4];
    uint32_t color;
    int16_t uv[2];
} vertex_nv;

/* Con S3TC: DXT1 de 8x8, cuatro bloques de un color (rojo, verde, azul, blanco). */
static void make_dxt1(uint8_t blocks[32]) {
    static const uint16_t colors[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    int k;
    for (k = 0; k < 4; k++) {
        blocks[8 * k + 0] = (uint8_t)(colors[k] & 0xFF);
        blocks[8 * k + 1] = (uint8_t)(colors[k] >> 8);
        blocks[8 * k + 2] = (uint8_t)(colors[k] & 0xFF); /* c1 = c0: índices 0 = c0 */
        blocks[8 * k + 3] = (uint8_t)(colors[k] >> 8);
        memset(blocks + 8 * k + 4, 0, 4);
    }
}

/* P8 de 16x16 en orden swizzled: bandas diagonales de 4 texels con 4 colores de paleta. */
static void make_p8(uint8_t indices[256], uint8_t palette[16]) {
    static const uint8_t bgra[16] = {
        0, 128, 255, 255,   /* naranja (D3DCOLOR: B, G, R, A) */
        200, 200, 0, 255,   /* cian */
        255, 0, 128, 255,   /* morado */
        255, 255, 255, 255, /* blanco */
    };
    int x, y;
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            indices[texconv_morton(x, y, 16, 16)] = (uint8_t)(((x >> 2) + (y >> 2)) & 3);
        }
    }
    memcpy(palette, bgra, sizeof bgra);
}

static int upload(int code, int log2_size, const uint8_t *src, size_t len, const uint8_t *pal,
                  size_t pal_len, int *compressed) {
    static uint8_t converted[64 * 64 * 4];
    texconv_desc d;
    uint32_t format = 0x9u | (2u << 4) | ((uint32_t)code << 8) | (1u << 16) |
                      ((uint32_t)log2_size << 20) | ((uint32_t)log2_size << 24);
    if (texconv_describe(format, 0, 0, glb_has_s3tc(), &d) ||
        texconv_convert(src, len, pal, pal_len, &d, converted, sizeof converted)) {
        return -1;
    }
    if (compressed) {
        *compressed = d.upload_compressed;
    }
    return glb_upload_texture(0, &d, converted, 0);
}

static int check_rgb(const uint8_t *img, int x, int y, int r, int g, int b) {
    const uint8_t *p = img + 4 * (y * W + x);
    return p[0] == r && p[1] == g && p[2] == b;
}

static int run_visual(int hidden, const char *out_override) {
    static float constants[192 * 4];
    static const float surface[2] = {(float)W, (float)H};
    static const float clip[2] = {0.0f, 1.0f};
    static uint8_t checker[64 * 64 * 4];
    static uint8_t image[W * H * 4];
    const vertex triangle[3] = {
        {{-0.85f, 0.15f, 0, 1}, {1, 0, 0, 1}, {0, 0}},
        {{-0.15f, 0.15f, 0, 1}, {0, 1, 0, 1}, {0, 0}},
        {{-0.50f, 0.90f, 0, 1}, {0, 0, 1, 1}, {0, 0}},
    };
    const vertex quad[4] = {
        {{0.15f, 0.90f, 0, 1}, {1, 1, 1, 1}, {0, 0}},
        {{0.15f, 0.15f, 0, 1}, {1, 1, 1, 1}, {0, 1}},
        {{0.85f, 0.90f, 0, 1}, {1, 1, 1, 1}, {1, 0}},
        {{0.85f, 0.15f, 0, 1}, {1, 1, 1, 1}, {1, 1}},
    };
    /* abajo a la izquierda (DXT1) y a la derecha (P8); uv 32767 = 1.0 en short normalizado */
    const vertex_nv dxt_quad[4] = {
        {{-0.85f, -0.30f, 0, 1}, 0xFFFFFFFFu, {0, 0}},
        {{-0.15f, -0.30f, 0, 1}, 0xFFFFFFFFu, {32767, 0}},
        {{-0.85f, -0.90f, 0, 1}, 0xFFFFFFFFu, {0, 32767}},
        {{-0.15f, -0.90f, 0, 1}, 0xFFFFFFFFu, {32767, 32767}},
    };
    const vertex_nv p8_quad[4] = {
        {{0.15f, -0.30f, 0, 1}, 0xFFFFFFFFu, {0, 0}},
        {{0.85f, -0.30f, 0, 1}, 0xFFFFFFFFu, {32767, 0}},
        {{0.15f, -0.90f, 0, 1}, 0xFFFFFFFFu, {0, 32767}},
        {{0.85f, -0.90f, 0, 1}, 0xFFFFFFFFu, {32767, 32767}},
    };
    /* banda roja al 50 %: D3DCOLOR 0x80FF0000 (A = 0x80, R = 0xFF), QUADS en orden de perímetro */
    const vertex_nv bar[4] = {
        {{-0.90f, 0.25f, 0, 1}, 0x80FF0000u, {0, 0}},
        {{0.90f, 0.25f, 0, 1}, 0x80FF0000u, {0, 0}},
        {{0.90f, -0.25f, 0, 1}, 0x80FF0000u, {0, 0}},
        {{-0.90f, -0.25f, 0, 1}, 0x80FF0000u, {0, 0}},
    };
    static const uint16_t idx16[6] = {0, 1, 2, 2, 1, 3};
    static const uint32_t idx32[6] = {0, 1, 2, 2, 1, 3};
    /* formato NV2A: posición F x4, color UB_D3D (o UB_OGL), uv S1 x2 */
    glb_format nv_format;
    glb_uniform uniforms[4];
    glb_draw_call call;
    nv2a_state state;
    uint8_t dxt[32], p8[256], palette[16];
    unsigned prog_color = 0, prog_textured = 0;
    char path[MAX_PATH];
    const uint8_t *p;
    int r, dxt_compressed = 0;
    /* Del cuadrado [-1, 1] a píxeles: x' = 360x + 360, y' = -240y + 240, z = 0.5, w = 1. */
    constants[96 * 4 + 0] = W / 2.0f;
    constants[96 * 4 + 3] = W / 2.0f;
    constants[97 * 4 + 1] = -H / 2.0f;
    constants[97 * 4 + 3] = H / 2.0f;
    constants[98 * 4 + 3] = 0.5f;
    constants[99 * 4 + 3] = 1.0f;
    memset(uniforms, 0, sizeof uniforms);
    uniforms[0].name = "c";
    uniforms[0].kind = GLB_U_VEC4;
    uniforms[0].count = 192;
    uniforms[0].values = constants;
    uniforms[1].name = "surfaceSize";
    uniforms[1].kind = GLB_U_VEC2;
    uniforms[1].count = 1;
    uniforms[1].values = surface;
    uniforms[2].name = "clipRange";
    uniforms[2].kind = GLB_U_VEC2;
    uniforms[2].count = 1;
    uniforms[2].values = clip;
    uniforms[3].name = "tex0";
    uniforms[3].kind = GLB_U_INT;
    uniforms[3].ivalue = 0;
    memset(&nv_format, 0, sizeof nv_format);
    nv_format.stride = (int)sizeof(vertex_nv);
    nv_format.n_attribs = 3;
    CHECK(glb_attrib_from_nv2a(0, 0x2u | (4u << 4) | ((uint32_t)sizeof(vertex_nv) << 8), 0,
                               &nv_format.attribs[0]) == 0);
    CHECK(glb_attrib_from_nv2a(3, 0x0u | (4u << 4) | ((uint32_t)sizeof(vertex_nv) << 8), 16,
                               &nv_format.attribs[1]) == 0);
    CHECK(glb_attrib_from_nv2a(9, 0x1u | (2u << 4) | ((uint32_t)sizeof(vertex_nv) << 8), 20,
                               &nv_format.attribs[2]) == 0);

    r = glb_compile(g_vs_color, g_fs_color, &prog_color, g_log, sizeof g_log);
    if (r) {
        fprintf(stderr, "programa de color:\n%s\n", g_log);
    }
    CHECK(r == 0);
    r = glb_compile(g_vs_textured, g_fs_textured, &prog_textured, g_log, sizeof g_log);
    if (r) {
        fprintf(stderr, "programa con textura:\n%s\n", g_log);
    }
    CHECK(r == 0);
    if (g_failures) {
        return 1;
    }
    nv2a_state_defaults(&state, W, H);
    CHECK(glstate_apply(&state, NULL) == 0);
    CHECK(glb_clear(0.10f, 0.10f, 0.12f, 1.0f) == 0);

    /* arriba: triángulo y tablero (float, sin índices) */
    make_checker(checker, 64, 8);
    CHECK(glb_set_texture(0, 64, 64, checker, 0) == 0);
    CHECK(glb_draw(prog_color, triangle, &k_format, GLB_TRIANGLES, 3, uniforms, 3) == 0);
    CHECK(glb_draw(prog_textured, quad, &k_format, GLB_TRIANGLE_STRIP, 4, uniforms, 4) == 0);

    /* abajo a la izquierda: DXT1 (comprimida si hay S3TC), índices u16 */
    make_dxt1(dxt);
    CHECK(upload(0x0C, 3, dxt, sizeof dxt, NULL, 0, &dxt_compressed) == 0);
    memset(&call, 0, sizeof call);
    call.program = prog_textured;
    call.vertices = dxt_quad;
    call.vertex_bytes = sizeof dxt_quad;
    call.format = &nv_format;
    call.prim = GLB_TRIANGLES;
    call.count = 6;
    call.indices = idx16;
    call.index_size = 2;
    call.uniforms = uniforms;
    call.n_uniforms = 4;
    CHECK(glb_draw_ex(&call) == 0);

    /* abajo a la derecha: P8 swizzled con paleta, índices u32 y color UB_OGL */
    make_p8(p8, palette);
    CHECK(upload(0x0B, 4, p8, sizeof p8, palette, sizeof palette, NULL) == 0);
    nv_format.attribs[1].type = GLB_ATTR_UB_OGL; /* 0xFFFFFFFF: blanco en los dos órdenes */
    call.vertices = p8_quad;
    call.vertex_bytes = sizeof p8_quad;
    call.indices = idx32;
    call.index_size = 4;
    CHECK(glb_draw_ex(&call) == 0);
    nv_format.attribs[1].type = GLB_ATTR_UB_D3D;

    /* banda translúcida: SET_BLEND_ENABLE 1, SRC_ALPHA (0x302), ONE_MINUS_SRC_ALPHA (0x303) */
    state.blend_enable = 1;
    state.blend_sfactor = 0x0302;
    state.blend_dfactor = 0x0303;
    CHECK(glstate_apply(&state, NULL) == 0);
    call.program = prog_color;
    call.vertices = bar;
    call.vertex_bytes = sizeof bar;
    call.prim = GLB_QUADS;
    call.count = 4;
    call.indices = NULL;
    call.index_size = 0;
    call.n_uniforms = 3;
    CHECK(glb_draw_ex(&call) == 0);
    nv2a_state_defaults(&state, W, H);
    CHECK(glstate_apply(&state, NULL) == 0);
    CHECK(glb_read_rgba(image, sizeof image) == 0);

    /* fondo, centro del triángulo, cerca del vértice rojo y del azul */
    p = pixel(image, 20, 20);
    CHECK(in_range(p[0], 20, 32) && in_range(p[1], 20, 32) && in_range(p[2], 25, 37));
    p = pixel(image, 180, 144);
    CHECK(in_range(p[0], 60, 110) && in_range(p[1], 60, 110) && in_range(p[2], 60, 110));
    p = pixel(image, 100, 170);
    CHECK(p[0] > p[1] + 80 && p[0] > p[2] + 80);
    p = pixel(image, 180, 40);
    CHECK(p[2] > p[0] + 80 && p[2] > p[1] + 80);
    /* tablero: casillas vecinas distintas y la diagonal igual */
    CHECK(check_rgb(image, 429, 35, 240, 200, 60));
    CHECK(check_rgb(image, 461, 35, 30, 60, 160));
    CHECK(check_rgb(image, 461, 58, 240, 200, 60));
    /* DXT1: rojo, verde, azul y blanco por cuadrantes */
    CHECK(check_rgb(image, 117, 348, 255, 0, 0));
    CHECK(check_rgb(image, 243, 348, 0, 255, 0));
    CHECK(check_rgb(image, 117, 420, 0, 0, 255));
    CHECK(check_rgb(image, 243, 420, 255, 255, 255));
    /* P8: texel (1,1) naranja, (5,1) cian, (1,9) morado, (9,5) blanco */
    CHECK(check_rgb(image, 437, 325, 255, 128, 0));
    CHECK(check_rgb(image, 500, 325, 0, 200, 200));
    CHECK(check_rgb(image, 437, 397, 128, 0, 255));
    CHECK(check_rgb(image, 563, 361, 255, 255, 255));
    /* banda: rojo al 50 % sobre el fondo y sobre una casilla oscura del tablero */
    p = pixel(image, 360, 240);
    CHECK(in_range(p[0], 132, 148) && p[1] < 25 && p[2] < 25);
    p = pixel(image, 429, 190);
    CHECK(in_range(p[0], 135, 150) && in_range(p[1], 22, 38) && in_range(p[2], 72, 88));

    output_path(path, sizeof path, out_override);
    CHECK(glb_write_png(path, W, H, image) == 0);
    printf("gl_test: DXT1 %s; captura en %s\n",
           dxt_compressed ? "subida comprimida (S3TC)" : "descomprimida en CPU", path);
    if (!hidden) {
        glb_show_window(1);
        glb_present();
        Sleep(2000);
    }
    glb_delete_program(prog_color);
    glb_delete_program(prog_textured);
    return g_failures ? 1 : 0;
}

int main(int argc, char **argv) {
    const glb_info *info;
    const char *compile = NULL, *out = NULL;
    int hidden = 0, stress = 0, k, r;
    for (k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "--hidden")) {
            hidden = 1;
        } else if (!strcmp(argv[k], "--compile") && k + 1 < argc) {
            compile = argv[++k];
        } else if (!strcmp(argv[k], "--stress")) {
            stress = 1;
        } else if (!strcmp(argv[k], "--out") && k + 1 < argc) {
            out = argv[++k];
        } else {
            fprintf(stderr, "uso: gl_test [--hidden] [--out PNG] | --compile DIR | --stress\n");
            return 1;
        }
    }
    if (build_shaders()) {
        fprintf(stderr, "gl_test: no se pudieron traducir los shaders de prueba\n");
        return 1;
    }
    r = glb_init(NULL, W, H);
    if (r) {
        printf("gl_test: SIN GL (%s)\n", glb_error_text(r));
        return EXIT_NO_GL;
    }
    info = glb_get_info();
    printf("gl_test: OpenGL %d.%d (%s) GLSL %s | %s | %s\n", info->major, info->minor,
           info->version, info->glsl_version, info->renderer, info->vendor);
    r = compile ? compile_dir(compile) : stress ? run_stress() : run_visual(hidden, out);
    glb_shutdown();
    if (!compile && !stress && r == 0) {
        printf("gl_test: OK\n");
    }
    return r;
}
