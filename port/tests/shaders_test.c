/*
 * shaders_test.c: vertex programs y register combiners del NV2A -> GLSL (port/src/shaders).
 *
 * Lo compila y ejecuta port\build.cmd; termina con código 1 si algo falla e imprime
 * "shaders_test: OK" si no. Las instrucciones de prueba están codificadas a mano (ver los
 * comentarios con el desglose de cada palabra); no hay datos del juego.
 *
 * Modos extra (los usa tests/port y el spike sobre datos reales):
 *   shaders_test --dump DIR              escribe los GLSL de las pruebas en DIR (*.vert, *.frag)
 *   shaders_test --vsh-file IN OUT       IN: binario de D3D (cabecera 0x2078 | n << 16) o
 *                                        instrucciones sueltas; OUT: el vertex shader GLSL
 *   shaders_test --psh-file IN OUT       IN: los 240 bytes de la definición de pixel shader
 *                                        de D3D (ver psh_file); OUT: el fragment shader GLSL
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shaders/psh.h"
#include "shaders/vsh.h"

static int g_failures, g_checks;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        g_checks++;                                                                 \
        if (!(cond)) {                                                              \
            fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

#define CONTAINS(text, needle)                                                      \
    do {                                                                            \
        g_checks++;                                                                 \
        if (!strstr((text), (needle))) {                                            \
            fprintf(stderr, "FALLO %s:%d: falta \"%s\"\n", __FILE__, __LINE__, needle);\
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

static char g_glsl[65536];

/* El GLSL #version 400 solo admite ASCII (también en los comentarios). */
static int is_ascii(const char *text) {
    for (; *text; text++) {
        if ((unsigned char)*text > 0x7F) {
            return 0;
        }
    }
    return 1;
}
static const char *g_dump_dir;

static void dump(const char *name, const char *text) {
    char path[1024];
    FILE *f;
    if (!g_dump_dir) {
        return;
    }
    snprintf(path, sizeof path, "%s\\%s", g_dump_dir, name);
    f = fopen(path, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

/* ---- vertex programs ------------------------------------------------------------------- */

/*
 * DP4 oPos.x, v0, c[0]
 *   w1 = MAC 7 << 21 (0x00E00000) | A.swz xyzw (0x1B)                     = 0x00E0001B
 *   w2 = A.mux 2 << 26 (0x08000000) | B.swz xyzw (0x00360000) | B.mux 3 << 11 (0x1800)
 *        | C.swz xyzw (0x6C)                                               = 0x0836186C
 *   w3 = máscara de salida x 0x8 << 12 (0x8000) | ORB o[] (0x800), dirección 0 (oPos)
 *                                                                          = 0x00008800
 * MAD R2.xyz, v3, c[5], -R0.wwww  +  RCP R1.w, -R0.w  ->  oT0.xy = ILU, FINAL
 *   w1 = ILU 2 << 25 (0x04000000) | MAC 4 << 21 (0x00800000) | CONST 5 << 13 (0xA000)
 *        | V 3 << 9 (0x600) | A.swz 0x1B                                  = 0x0480A61B
 *   w2 = A.mux 2 (0x08000000) | B.swz 0x00360000 | B.mux 3 (0x1800) | C.neg (0x400)
 *        | C.swz wwww (0x3FC) | C.R alto 0                                 = 0x08361FFC
 *   w3 = C.R bajo 0 | C.mux 1 << 28 | máscara MAC xyz 0xE << 24 | OUT_R 2 << 20
 *        | máscara ILU w 1 << 16 | salida xy 0xC << 12 | ORB 0x800 | dirección 9 << 3
 *        | OUT_MUX ILU 0x4 | FINAL 1                                      = 0x1E21C84D
 */
static const uint32_t k_prog_basic[] = {
    0x00000000u, 0x00E0001Bu, 0x0836186Cu, 0x00008800u,
    0x00000000u, 0x0480A61Bu, 0x08361FFCu, 0x1E21C84Du,
};

/* Codificador de apoyo para los casos que no necesitan el desglose a mano. */
typedef struct enc_operand {
    unsigned mux, index, neg, swz; /* swz: 8 bits, x en los dos altos (0x1B = xyzw) */
} enc_operand;

static void put(uint32_t *w, int word, int shift, unsigned bits, unsigned value) {
    w[word] |= (value & ((1u << bits) - 1u)) << shift;
}

static void encode(uint32_t *w, unsigned mac, unsigned ilu, enc_operand a, enc_operand b,
                   enc_operand c, unsigned mac_mask, unsigned temp, unsigned ilu_mask,
                   unsigned out_mask, unsigned orb, unsigned address, unsigned out_ilu,
                   unsigned a0x, unsigned final) {
    unsigned constant = 0, input = 0;
    const enc_operand *ops[3];
    int k;
    memset(w, 0, 4 * sizeof *w);
    ops[0] = &a;
    ops[1] = &b;
    ops[2] = &c;
    for (k = 0; k < 3; k++) {
        if (ops[k]->mux == VSH_MUX_CONST) constant = ops[k]->index;
        if (ops[k]->mux == VSH_MUX_INPUT) input = ops[k]->index;
    }
    put(w, 1, 25, 3, ilu);
    put(w, 1, 21, 4, mac);
    put(w, 1, 13, 8, constant);
    put(w, 1, 9, 4, input);
    put(w, 1, 8, 1, a.neg);
    put(w, 1, 0, 8, a.swz);
    put(w, 2, 28, 4, a.mux == VSH_MUX_TEMP ? a.index : 0);
    put(w, 2, 26, 2, a.mux);
    put(w, 2, 25, 1, b.neg);
    put(w, 2, 17, 8, b.swz);
    put(w, 2, 13, 4, b.mux == VSH_MUX_TEMP ? b.index : 0);
    put(w, 2, 11, 2, b.mux);
    put(w, 2, 10, 1, c.neg);
    put(w, 2, 2, 8, c.swz);
    put(w, 2, 0, 2, c.mux == VSH_MUX_TEMP ? c.index >> 2 : 0);
    put(w, 3, 30, 2, c.mux == VSH_MUX_TEMP ? c.index & 3 : 0);
    put(w, 3, 28, 2, c.mux);
    put(w, 3, 24, 4, mac_mask);
    put(w, 3, 20, 4, temp);
    put(w, 3, 16, 4, ilu_mask);
    put(w, 3, 12, 4, out_mask);
    put(w, 3, 11, 1, orb);
    put(w, 3, 3, 8, address);
    put(w, 3, 2, 1, out_ilu);
    put(w, 3, 1, 1, a0x);
    put(w, 3, 0, 1, final);
}

static const enc_operand NONE = {0, 0, 0, 0x1B};

static enc_operand op(unsigned mux, unsigned index, unsigned swz) {
    enc_operand o;
    o.mux = mux;
    o.index = index;
    o.neg = 0;
    o.swz = swz;
    return o;
}

static void test_vsh_hand_encoded(void) {
    vsh_program p;
    const vsh_insn *a, *b;
    int n = nv2a_vsh_decode(k_prog_basic, 2, &p);
    CHECK(n == 2);
    a = &p.insn[0];
    b = &p.insn[1];
    CHECK(a->mac == VSH_MAC_DP4 && a->ilu == VSH_ILU_NOP);
    CHECK(a->a.mux == VSH_MUX_INPUT && a->a.index == 0);
    CHECK(a->a.swizzle[0] == 0 && a->a.swizzle[1] == 1 && a->a.swizzle[2] == 2 &&
          a->a.swizzle[3] == 3);
    CHECK(a->b.mux == VSH_MUX_CONST && a->b.index == 0 && !a->b.relative);
    CHECK(a->out_is_output && a->out_address == VSH_OUT_POS && a->out_mask == VSH_MASK_X);
    CHECK(!a->out_from_ilu && !a->final && a->mac_mask == 0);

    CHECK(b->mac == VSH_MAC_MAD && b->ilu == VSH_ILU_RCP);
    CHECK(b->a.mux == VSH_MUX_INPUT && b->a.index == 3);
    CHECK(b->b.mux == VSH_MUX_CONST && b->b.index == 5);
    CHECK(b->c.mux == VSH_MUX_TEMP && b->c.index == 0 && b->c.negate);
    CHECK(b->c.swizzle[0] == 3 && b->c.swizzle[1] == 3 && b->c.swizzle[2] == 3 &&
          b->c.swizzle[3] == 3);
    CHECK(b->mac_mask == (VSH_MASK_X | VSH_MASK_Y | VSH_MASK_Z) && b->temp == 2);
    CHECK(b->ilu_mask == VSH_MASK_W && b->ilu_temp == 1); /* emparejado: el ILU va a R1 */
    CHECK(b->out_is_output && b->out_address == VSH_OUT_T0 &&
          b->out_mask == (VSH_MASK_X | VSH_MASK_Y) && b->out_from_ilu && b->final);
    CHECK(p.inputs_read == ((1u << 0) | (1u << 3)));
    CHECK(p.outputs_written == ((1u << VSH_OUT_POS) | (1u << VSH_OUT_T0)));
    CHECK(!p.writes_constants && !p.uses_a0);

    n = nv2a_vsh_to_glsl(&p, g_glsl, sizeof g_glsl);
    CHECK(n > 0 && (size_t)n == strlen(g_glsl) && is_ascii(g_glsl));
    CONTAINS(g_glsl, "#version 400");
    CONTAINS(g_glsl, "layout(location = 0) in vec4 v0;");
    CONTAINS(g_glsl, "layout(location = 3) in vec4 v3;");
    CHECK(!strstr(g_glsl, "in vec4 v1;"));
    CONTAINS(g_glsl, "uniform vec4 c[192];");
    CONTAINS(g_glsl, "/* 0: DP4 + NOP */");
    CONTAINS(g_glsl, "vec4 mac = vec4(dot(a, b));");
    CONTAINS(g_glsl, "oPos.x = mac.x;");
    CONTAINS(g_glsl, "vec4 b = c[5].xyzw;");
    CONTAINS(g_glsl, "vec4 cc = -R[0].wwww;");
    CONTAINS(g_glsl, "vec4 mac = a * b + cc;");
    CONTAINS(g_glsl, "vec4 ilu = vec4(1.0 / cc.x);");
    CONTAINS(g_glsl, "R[2].xyz = mac.xyz;");
    CONTAINS(g_glsl, "R[1].w = ilu.w;");
    CONTAINS(g_glsl, "oT0.xy = ilu.xy;");
    CONTAINS(g_glsl, "pos.x = pos.x * (2.0 / surfaceSize.x) - 1.0;");
    CONTAINS(g_glsl, "pos.y = 1.0 - pos.y * (2.0 / surfaceSize.y);");
    CONTAINS(g_glsl, "gl_Position = vec4(pos.xyz * w, w);");
    dump("basic.vert", g_glsl);
}

static void test_vsh_a0_ilu_and_constant_writes(void) {
    uint32_t prog[5 * 4];
    vsh_program p;
    const enc_operand v1x = op(VSH_MUX_INPUT, 1, 0x00); /* v1.xxxx */
    int n;
    /* ARL A0.x, v1.x */
    encode(prog, VSH_MAC_ARL, 0, v1x, NONE, NONE, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    /* MOV oD0, c[A0 + 10] */
    encode(prog + 4, VSH_MAC_MOV, 0, op(VSH_MUX_CONST, 10, 0x1B), NONE, NONE, 0, 0, 0, 0xF, 1,
           VSH_OUT_D0, 0, 1, 0);
    /* RSQ R4.w, c[7].x (MAC NOP: el ILU escribe en OUT_R) */
    encode(prog + 8, 0, VSH_ILU_RSQ, NONE, NONE, op(VSH_MUX_CONST, 7, 0x00), 0, 4, VSH_MASK_W, 0,
           0, 0, 0, 0, 0);
    /* MOV c[12], R4  (ORB = 0: escribe una constante) */
    encode(prog + 12, VSH_MAC_MOV, 0, op(VSH_MUX_TEMP, 4, 0x1B), NONE, NONE, 0, 0, 0, 0xF, 0, 12,
           0, 0, 0);
    /* MOV sin destino leyendo R12 (alias de oPos) + LIT c = R4 -> oT1, FINAL */
    encode(prog + 16, VSH_MAC_MOV, VSH_ILU_LIT, op(VSH_MUX_TEMP, 12, 0x1B), NONE,
           op(VSH_MUX_TEMP, 4, 0x1B), 0, 0, 0, 0xF, 1, VSH_OUT_T1, 1, 0, 1);
    n = nv2a_vsh_decode(prog, 5, &p);
    CHECK(n == 5);
    CHECK(p.uses_a0 && p.writes_constants);
    CHECK(p.insn[0].mac == VSH_MAC_ARL && p.insn[0].a.index == 1);
    CHECK(p.insn[1].b.mux == VSH_MUX_NONE && p.insn[1].a.relative && p.insn[1].a.index == 10);
    CHECK(p.insn[2].mac == VSH_MAC_NOP && p.insn[2].ilu_temp == 4 && p.insn[2].c.index == 7);
    CHECK(!p.insn[3].out_is_output && p.insn[3].out_address == 12);
    CHECK(p.insn[4].a.index == VSH_TEMP_OPOS && p.insn[4].out_from_ilu);
    n = nv2a_vsh_to_glsl(&p, g_glsl, sizeof g_glsl);
    CHECK(n > 0 && is_ascii(g_glsl));
    CONTAINS(g_glsl, "A0 = int(floor(a.x));");
    CONTAINS(g_glsl, "vec4 a = C[clamp(A0 + 10, 0, 191)].xyzw;"); /* lee la copia local */
    CONTAINS(g_glsl, "vec4 C[192];");
    CONTAINS(g_glsl, "oD0 = mac;");
    CONTAINS(g_glsl, "vec4 ilu = vec4(inversesqrt(abs(cc.x)));");
    CONTAINS(g_glsl, "R[4].w = ilu.w;");
    CONTAINS(g_glsl, "C[12] = mac;");
    CONTAINS(g_glsl, "vec4 a = oPos.xyzw;");
    CONTAINS(g_glsl, "vec4 ilu = nv_lit(cc);");
    CONTAINS(g_glsl, "oT1 = ilu;");
    dump("a0.vert", g_glsl);
}

static void test_vsh_every_operation_has_glsl(void) {
    static const char *const mac_glsl[VSH_MAC_COUNT] = {
        NULL, "vec4 mac = a;", "a * b;", "a + cc;", "a * b + cc;", "dot(a.xyz, b.xyz))",
        "dot(a.xyz, b.xyz) + b.w", "dot(a, b)", "vec4(1.0, a.y * b.y, a.z, b.w)", "min(a, b)",
        "max(a, b)", "lessThan(a, b)", "greaterThanEqual(a, b)", "A0 = int(floor(a.x));",
    };
    static const char *const ilu_glsl[VSH_ILU_COUNT] = {
        NULL, "vec4 ilu = cc;", "1.0 / cc.x", "nv_rcc(cc.x)", "inversesqrt", "nv_exp(cc.x)",
        "nv_log(cc.x)", "nv_lit(cc)",
    };
    uint32_t prog[4];
    vsh_program p;
    unsigned op_;
    const enc_operand r1 = op(VSH_MUX_TEMP, 1, 0x1B);
    for (op_ = 1; op_ < VSH_MAC_COUNT; op_++) {
        unsigned mask = op_ == VSH_MAC_ARL ? 0 : 0xF;
        encode(prog, op_, 0, r1, r1, r1, mask, 3, 0, 0, 0, 0, 0, 0, 1);
        CHECK(nv2a_vsh_decode(prog, 1, &p) == 1);
        CHECK(nv2a_vsh_to_glsl(&p, g_glsl, sizeof g_glsl) > 0);
        CONTAINS(g_glsl, mac_glsl[op_]);
        CHECK(strcmp(nv2a_vsh_mac_name((int)op_), "?") != 0);
    }
    for (op_ = 1; op_ < VSH_ILU_COUNT; op_++) {
        encode(prog, 0, op_, NONE, NONE, r1, 0, 5, 0xF, 0, 0, 0, 0, 0, 1);
        CHECK(nv2a_vsh_decode(prog, 1, &p) == 1);
        CHECK(nv2a_vsh_to_glsl(&p, g_glsl, sizeof g_glsl) > 0);
        CONTAINS(g_glsl, ilu_glsl[op_]);
        CONTAINS(g_glsl, "R[5] = ilu;");
    }
}

static void test_vsh_errors(void) {
    uint32_t prog[4];
    vsh_program p;
    char small[16];
    const enc_operand r1 = op(VSH_MUX_TEMP, 1, 0x1B);
    CHECK(nv2a_vsh_decode(NULL, 1, &p) == VSH_ERR_ARGS);
    CHECK(nv2a_vsh_decode(k_prog_basic, 0, &p) == VSH_ERR_ARGS);
    CHECK(nv2a_vsh_decode(k_prog_basic, VSH_MAX_INSN + 1, &p) == VSH_ERR_ARGS);
    CHECK(nv2a_vsh_decode(k_prog_basic, 1, &p) == VSH_ERR_NO_FINAL);
    encode(prog, 14, 0, r1, r1, r1, 0xF, 0, 0, 0, 0, 0, 0, 0, 1);
    CHECK(nv2a_vsh_decode(prog, 1, &p) == VSH_ERR_OPCODE);
    encode(prog, VSH_MAC_MUL, 0, r1, NONE, NONE, 0xF, 0, 0, 0, 0, 0, 0, 0, 1);
    CHECK(nv2a_vsh_decode(prog, 1, &p) == VSH_ERR_MUX); /* MUL sin operando B */
    encode(prog, VSH_MAC_MOV, 0, op(VSH_MUX_TEMP, 13, 0x1B), NONE, NONE, 0xF, 0, 0, 0, 0, 0, 0, 0, 1);
    CHECK(nv2a_vsh_decode(prog, 1, &p) == VSH_ERR_REGISTER);
    encode(prog, VSH_MAC_MOV, 0, op(VSH_MUX_CONST, 200, 0x1B), NONE, NONE, 0xF, 0, 0, 0, 0, 0, 0, 0, 1);
    CHECK(nv2a_vsh_decode(prog, 1, &p) == VSH_ERR_REGISTER);
    encode(prog, VSH_MAC_MOV, 0, r1, NONE, NONE, 0, 0, 0, 0xF, 1, 1, 0, 0, 1);
    CHECK(nv2a_vsh_decode(prog, 1, &p) == VSH_ERR_OUTPUT); /* o[1] no existe */
    encode(prog, VSH_MAC_MOV, 0, r1, NONE, NONE, 0, 0, 0, 0xF, 1, VSH_OUT_D0, 1, 0, 1);
    CHECK(nv2a_vsh_decode(prog, 1, &p) == VSH_ERR_OUTPUT); /* salida desde un ILU NOP */
    CHECK(nv2a_vsh_decode(k_prog_basic, 2, &p) == 2);
    CHECK(nv2a_vsh_to_glsl(&p, small, sizeof small) == VSH_ERR_SPACE && small[0] == 0);
    CHECK(strcmp(nv2a_vsh_error(VSH_ERR_SPACE), "ok") != 0);
}

/* ---- register combiners ---------------------------------------------------------------- */

/*
 * tex t0; mul r0, t0, v0 (una etapa):
 *   RGB ICW:   A = T0 (0x08), B = V0 (0x04), C = D = 0           = 0x08040000
 *   RGB OCW:   AB -> R0 (0xC << 4)                               = 0x000000C0
 *   alfa ICW:  A = T0.a (0x18), B = V0.a (0x14)                  = 0x18140000
 *   alfa OCW:  AB -> R0                                          = 0x000000C0
 *   final = 0 (sale r0); etapa 0 de textura PROJECT2D (1).
 */
static void test_psh_modulate(void) {
    psh_registers r;
    psh_program p;
    int n;
    memset(&r, 0, sizeof r);
    r.combiner_control = 1;
    r.rgb_inputs[0] = 0x08040000u;
    r.rgb_outputs[0] = 0x000000C0u;
    r.alpha_inputs[0] = 0x18140000u;
    r.alpha_outputs[0] = 0x000000C0u;
    r.texture_modes = PSH_TEX_PROJECT2D;
    n = nv2a_psh_decode(&r, &p);
    CHECK(n == 1);
    CHECK(p.stage[0].rgb.in[0].reg == PSH_REG_T0 && p.stage[0].rgb.in[1].reg == PSH_REG_V0);
    CHECK(p.stage[0].rgb.in[0].map == PSH_MAP_UNSIGNED_IDENTITY && !p.stage[0].rgb.in[0].alpha);
    CHECK(p.stage[0].alpha.in[0].alpha && p.stage[0].alpha.in[0].reg == PSH_REG_T0);
    CHECK(p.stage[0].rgb.ab_out == PSH_REG_R0 && p.stage[0].rgb.sum_out == PSH_REG_ZERO);
    CHECK(!p.final.present && p.tex_mode[0] == PSH_TEX_PROJECT2D && !p.unsupported_tex);
    n = nv2a_psh_to_glsl(&p, g_glsl, sizeof g_glsl);
    CHECK(n > 0 && (size_t)n == strlen(g_glsl) && is_ascii(g_glsl));
    CONTAINS(g_glsl, "uniform sampler2D tex0;");
    CONTAINS(g_glsl, "vec4 t0 = textureProj(tex0, oT0.xyw);");
    CONTAINS(g_glsl, "vec4 r0 = vec4(0.0, 0.0, 0.0, t0.a);");
    CONTAINS(g_glsl, "vec3 A = max(t0.rgb, 0.0);");
    CONTAINS(g_glsl, "vec3 B = max(v0.rgb, 0.0);");
    CONTAINS(g_glsl, "vec3 ab = A * B;");
    CONTAINS(g_glsl, "float A_a = max(t0.a, 0.0);");
    CONTAINS(g_glsl, "r0.rgb = ab;");
    CONTAINS(g_glsl, "r0.a = ab_a;");
    CONTAINS(g_glsl, "ab = clamp(ab, vec3(-1.0), vec3(1.0));");
    CONTAINS(g_glsl, "fragColor = clamp(r0, 0.0, 1.0);");
    dump("modulate.frag", g_glsl);
}

/*
 * Dos etapas, mux, dot, escala, blue-to-alpha, C0 por etapa, BUMPENVMAP y combiner final:
 *   control = 2 etapas | mux por el bit alto (0x100) | C0 por etapa (0x1000)  = 0x00001102
 *   etapa 0 RGB: A = T0 expand (0x48), B = C0 expand (0x41)                     = 0x48410000
 *                AB -> R0 (0xC0) | AB dot (0x2000) | x2 (2 << 15) | AB b->a (0x80000)
 *                                                                               = 0x000920C0
 *   etapa 1 RGB: A = R0 (0x0C), B = V0, C = T1 (0x09), D = V1 (0x05)           = 0x0C040905
 *                SUM -> R1 (0xD << 8) | mux (0x4000) | bias (1 << 15)          = 0x0000CD00
 *   etapa 1 alfa: A = 0 invertido (0x20), B = V0.a (0x14); AB -> R1            = 0x20140000 / 0xD0
 *   final ABCD: A = fog.a (0x13), B = R1 (0x0D), C = fog (0x03), D = 0         = 0x130D0300
 *   final EFG:  G = R1.a (0x1D), clamp de V1R0_SUM (0x80)                      = 0x00001D80
 *   texturas: etapa 0 PROJECT2D (1), etapa 1 BUMPENVMAP (6 << 5)               = 0x000000C1
 */
static void test_psh_two_stages_and_final(void) {
    psh_registers r;
    psh_program p;
    int n;
    memset(&r, 0, sizeof r);
    r.combiner_control = 0x00001102u;
    r.rgb_inputs[0] = 0x48410000u;
    r.rgb_outputs[0] = 0x000920C0u;
    r.rgb_inputs[1] = 0x0C040905u;
    r.rgb_outputs[1] = 0x0000CD00u;
    r.alpha_inputs[1] = 0x20140000u;
    r.alpha_outputs[1] = 0x000000D0u;
    r.final_inputs_abcd = 0x130D0300u;
    r.final_inputs_efg = 0x00001D80u;
    r.texture_modes = 0x000000C1u;
    n = nv2a_psh_decode(&r, &p);
    CHECK(n == 2);
    CHECK(p.mux_msb && p.unique_c0 && !p.unique_c1);
    CHECK(p.stage[0].rgb.in[0].map == PSH_MAP_EXPAND_NORMAL && p.stage[0].rgb.in[1].reg == PSH_REG_C0);
    CHECK(p.stage[0].rgb.ab_dot && !p.stage[0].rgb.cd_dot && p.stage[0].rgb.ab_blue_to_alpha);
    CHECK(p.stage[0].rgb.scale == PSH_SCALE_SHIFTLEFT_1);
    CHECK(p.stage[1].rgb.mux && p.stage[1].rgb.sum_out == PSH_REG_R1 &&
          p.stage[1].rgb.scale == PSH_SCALE_BIAS);
    CHECK(p.stage[1].alpha.in[0].map == PSH_MAP_UNSIGNED_INVERT);
    CHECK(p.final.present && p.final.clamp_sum && !p.final.complement_v1);
    CHECK(p.final.in[0].reg == PSH_REG_FOG && p.final.in[0].alpha);
    CHECK(p.final.in[6].reg == PSH_REG_R1 && p.final.in[6].alpha);
    CHECK(p.tex_mode[1] == PSH_TEX_BUMPENVMAP && p.tex_source[1] == 0);
    n = nv2a_psh_to_glsl(&p, g_glsl, sizeof g_glsl);
    CHECK(n > 0 && is_ascii(g_glsl));
    CONTAINS(g_glsl, "vec4 t1 = texture(tex1, oT1.xy + bumpMat[1] * t0.rg);");
    CONTAINS(g_glsl, "vec3 A = (2.0 * max(t0.rgb, 0.0) - 1.0);");
    CONTAINS(g_glsl, "vec3 ab = vec3(dot(A, B));");
    CONTAINS(g_glsl, "ab = ab * 2.0;");
    CONTAINS(g_glsl, "r0.a = ab.b;");
    CONTAINS(g_glsl, "vec4 c0 = psC0[1];");
    CONTAINS(g_glsl, "vec4 c1 = psC1[0];");
    CONTAINS(g_glsl, "bool mux_cd = r0.a >= 0.5;");
    CONTAINS(g_glsl, "vec3 sum = mux_cd ? cd : ab;");
    CONTAINS(g_glsl, "sum = sum - 0.5;");
    CONTAINS(g_glsl, "r1.rgb = sum;");
    CONTAINS(g_glsl, "float A_a = (1.0 - clamp(0.0, 0.0, 1.0));");
    CONTAINS(g_glsl, "vec4 v1r0 = vec4(clamp(sum_v1 + sum_r0, 0.0, 1.0), 0.0);");
    CONTAINS(g_glsl, "vec3 fA = max(vec3(fog.a), 0.0);");
    CONTAINS(g_glsl, "float fG = max(r1.a, 0.0);");
    CONTAINS(g_glsl, "fragColor = clamp(vec4(fD + mix(fC, fB, fA), fG), 0.0, 1.0);");
    dump("two_stages.frag", g_glsl);
}

static void test_psh_errors_and_unsupported(void) {
    psh_registers r;
    psh_program p;
    memset(&r, 0, sizeof r);
    CHECK(nv2a_psh_decode(NULL, &p) == PSH_ERR_ARGS);
    CHECK(nv2a_psh_decode(&r, &p) == PSH_ERR_STAGES);
    r.combiner_control = 9;
    CHECK(nv2a_psh_decode(&r, &p) == PSH_ERR_STAGES);
    r.combiner_control = 1;
    r.rgb_outputs[0] = 0x10; /* AB -> C0: no se puede escribir */
    CHECK(nv2a_psh_decode(&r, &p) == PSH_ERR_REGISTER);
    r.rgb_outputs[0] = 0;
    r.rgb_inputs[0] = 0x0E000000u; /* V1R0_SUM fuera del final */
    CHECK(nv2a_psh_decode(&r, &p) == PSH_ERR_REGISTER);
    r.rgb_inputs[0] = 0;
    r.alpha_outputs[0] = 0x2000; /* dot en la parte de alfa */
    CHECK(nv2a_psh_decode(&r, &p) == PSH_ERR_FLAGS);
    r.alpha_outputs[0] = 5u << 15; /* escala reservada */
    CHECK(nv2a_psh_decode(&r, &p) == PSH_ERR_FLAGS);
    r.alpha_outputs[0] = 0;
    r.final_inputs_abcd = 0x4C000000u; /* final: R0 con EXPAND */
    CHECK(nv2a_psh_decode(&r, &p) == PSH_ERR_FINAL);
    r.final_inputs_abcd = 0;
    r.texture_modes = PSH_TEX_BUMPENVMAP; /* dependiente en la etapa 0 */
    CHECK(nv2a_psh_decode(&r, &p) == PSH_ERR_TEXMODE);
    r.texture_modes = 25;
    CHECK(nv2a_psh_decode(&r, &p) == PSH_ERR_TEXMODE);
    r.texture_modes = (uint32_t)PSH_TEX_BUMPENVMAP << 10;
    r.other_stage_input = 2u << 16; /* la etapa 2 no puede leer de sí misma */
    CHECK(nv2a_psh_decode(&r, &p) == PSH_ERR_TEXMODE);
    r.other_stage_input = 1u << 16;
    r.texture_modes = (uint32_t)PSH_TEX_PROJECT2D | ((uint32_t)PSH_TEX_PROJECT2D << 5) |
                      ((uint32_t)PSH_TEX_BUMPENVMAP_LUM << 10) | ((uint32_t)PSH_TEX_DOT_ST << 15);
    r.compare_mode = 0x5u << 12; /* etapa 3: s y r con >= */
    CHECK(nv2a_psh_decode(&r, &p) == 1);
    CHECK(p.tex_source[2] == 1 && p.unsupported_tex == (1u << 3) && p.compare[3] == 5);
    CHECK(nv2a_psh_to_glsl(&p, g_glsl, sizeof g_glsl) > 0);
    CONTAINS(g_glsl, "vec4 t2 = texture(tex2, oT2.xy + bumpMat[2] * t1.rg);");
    CONTAINS(g_glsl, "t2.rgb *= clamp(t1.b * bumpScale[2] + bumpOffset[2], 0.0, 1.0);");
    CONTAINS(g_glsl, "vec4 t3 = vec4(0.0); /* modo DOT_ST sin emular */");
    CHECK(!strstr(g_glsl, "uniform sampler2D tex3;"));
    r.texture_modes = (uint32_t)PSH_TEX_CLIPPLANE;
    CHECK(nv2a_psh_decode(&r, &p) == 1);
    r.compare_mode = 0x1u; /* s >= 0 descarta; t, r, q < 0 descartan */
    CHECK(nv2a_psh_decode(&r, &p) == 1);
    CHECK(nv2a_psh_to_glsl(&p, g_glsl, sizeof g_glsl) > 0);
    CONTAINS(g_glsl, "if (oT0.x >= 0.0) discard;");
    CONTAINS(g_glsl, "if (oT0.y < 0.0) discard;");
    CHECK(nv2a_psh_to_glsl(&p, g_glsl, 32) == PSH_ERR_SPACE);
    CHECK(strcmp(nv2a_psh_error(PSH_ERR_TEXMODE), "ok") != 0);
}

/* ---- modo --vsh-file ------------------------------------------------------------------- */

static int vsh_file(const char *in_path, const char *out_path) {
    static uint32_t words[1 + VSH_MAX_INSN * VSH_WORDS_PER_INSN];
    static vsh_program prog;
    const uint32_t *insn = words;
    size_t n_words;
    int n_insn, result;
    FILE *f = fopen(in_path, "rb");
    if (!f) {
        fprintf(stderr, "no se puede abrir %s\n", in_path);
        return 2;
    }
    n_words = fread(words, sizeof words[0], sizeof words / sizeof words[0], f);
    fclose(f);
    if (n_words && (words[0] & 0xFFFFu) == VSH_BINARY_TYPE_PROGRAM) {
        n_insn = (int)(words[0] >> 16);
        insn = words + 1;
        n_words -= 1;
    } else {
        n_insn = (int)(n_words / VSH_WORDS_PER_INSN);
    }
    if (n_insn > (int)(n_words / VSH_WORDS_PER_INSN)) {
        n_insn = (int)(n_words / VSH_WORDS_PER_INSN);
    }
    result = nv2a_vsh_decode(insn, n_insn, &prog);
    if (result < 0) {
        fprintf(stderr, "%s: %s\n", in_path, nv2a_vsh_error(result));
        return 1;
    }
    result = nv2a_vsh_to_glsl(&prog, g_glsl, sizeof g_glsl);
    if (result < 0) {
        fprintf(stderr, "%s: %s\n", in_path, nv2a_vsh_error(result));
        return 1;
    }
    f = fopen(out_path, "wb");
    if (!f) {
        fprintf(stderr, "no se puede escribir %s\n", out_path);
        return 2;
    }
    fputs(g_glsl, f);
    fclose(f);
    printf("%s: %d instrucciones -> %s\n", in_path, prog.n_insn, out_path);
    return 0;
}

/*
 * Definición de pixel shader de D3D de la Xbox: 60 u32 con las mismas palabras que cargan
 * los métodos NV097 (offsets en u32): alfa ICW 0-7, final ABCD 8, final EFG 9, C0 10-17,
 * C1 18-25, alfa OCW 26-33, RGB ICW 34-41, compare 42, constantes del final 43-44, RGB OCW
 * 45-52, combiner control 53, modos de textura 54, dot mapping 55, entrada de las etapas
 * 56 y tres palabras de mapeo de constantes (57-59) que no hacen falta aquí.
 */
#define PSH_DEF_WORDS 60

static int psh_file(const char *in_path, const char *out_path) {
    uint32_t w[PSH_DEF_WORDS];
    psh_registers r;
    static psh_program prog;
    int result, k;
    FILE *f = fopen(in_path, "rb");
    if (!f) {
        fprintf(stderr, "no se puede abrir %s\n", in_path);
        return 2;
    }
    result = (int)fread(w, sizeof w[0], PSH_DEF_WORDS, f);
    fclose(f);
    if (result != PSH_DEF_WORDS) {
        fprintf(stderr, "%s: se esperaban %d u32\n", in_path, PSH_DEF_WORDS);
        return 1;
    }
    memset(&r, 0, sizeof r);
    for (k = 0; k < PSH_MAX_STAGES; k++) {
        r.alpha_inputs[k] = w[0 + k];
        r.alpha_outputs[k] = w[26 + k];
        r.rgb_inputs[k] = w[34 + k];
        r.rgb_outputs[k] = w[45 + k];
    }
    r.final_inputs_abcd = w[8];
    r.final_inputs_efg = w[9];
    r.compare_mode = w[42];
    r.combiner_control = w[53];
    r.texture_modes = w[54];
    r.dot_mapping = w[55];
    r.other_stage_input = w[56];
    result = nv2a_psh_decode(&r, &prog);
    if (result < 0) {
        fprintf(stderr, "%s: %s\n", in_path, nv2a_psh_error(result));
        return 1;
    }
    result = nv2a_psh_to_glsl(&prog, g_glsl, sizeof g_glsl);
    if (result < 0) {
        fprintf(stderr, "%s: %s\n", in_path, nv2a_psh_error(result));
        return 1;
    }
    f = fopen(out_path, "wb");
    if (!f) {
        fprintf(stderr, "no se puede escribir %s\n", out_path);
        return 2;
    }
    fputs(g_glsl, f);
    fclose(f);
    printf("%s: %d etapas, texturas 0x%05X -> %s\n", in_path, prog.n_stages,
           (unsigned)r.texture_modes, out_path);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 4 && !strcmp(argv[1], "--vsh-file")) {
        return vsh_file(argv[2], argv[3]);
    }
    if (argc == 4 && !strcmp(argv[1], "--psh-file")) {
        return psh_file(argv[2], argv[3]);
    }
    if (argc == 3 && !strcmp(argv[1], "--dump")) {
        g_dump_dir = argv[2];
    }
    test_vsh_hand_encoded();
    test_vsh_a0_ilu_and_constant_writes();
    test_vsh_every_operation_has_glsl();
    test_vsh_errors();
    test_psh_modulate();
    test_psh_two_stages_and_final();
    test_psh_errors_and_unsupported();
    if (g_failures) {
        fprintf(stderr, "shaders_test: %d de %d comprobaciones fallan\n", g_failures, g_checks);
        return 1;
    }
    printf("shaders_test: OK (%d comprobaciones)\n", g_checks);
    return 0;
}
