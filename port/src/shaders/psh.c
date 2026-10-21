/*
 * psh.c: register combiners del NV2A -> fragment shader GLSL 4.00.
 *
 * Implementación propia a partir de la descripción del formato; no reutiliza código de
 * xemu ni de Cxbx-Reloaded (GPL) ni nada del XDK. Fuentes consultadas:
 *   - NVIDIA, extensión GL_NV_register_combiners (y _2): el modelo de etapas generales y
 *     final, los mapeos de entrada, AB/CD/SUM/MUX, escala y sesgo, blue-to-alpha.
 *     https://registry.khronos.org/OpenGL/extensions/NV/NV_register_combiners.txt
 *   - xemu, hw/xbox/nv2a/pgraph/glsl/psh.c y nv2a_regs.h: posición de los campos en las
 *     palabras NV097, inicialización de r0.a, condición del mux y combiner final.
 *     https://github.com/xemu-project/xemu/tree/master/hw/xbox/nv2a
 *   - Cxbx-Reloaded, src/core/hle/D3D8/XbPixelShader.cpp: valores de los PS_* de la
 *     estructura de pixel shader de D3D, que son las mismas palabras.
 *
 * Palabras (métodos NV097 verificados en nv2a_regs.h de xemu):
 *
 *   COMBINER_CONTROL 0x1E60: bits 0-7 etapas (1-8), bit 8 mux por el bit alto de r0.a
 *     (si no, por el bajo), bit 12 C0 propia por etapa, bit 16 C1 propia por etapa.
 *   COLOR_ICW / ALPHA_ICW (0x0AC0 / 0x0260 + 4i): A en el byte alto ... D en el bajo. Cada
 *     byte: bits 0-3 registro, bit 4 canal (RGB: .rgb o .aaa; alfa: .b o .a), bits 5-7
 *     mapeo (psh_map).
 *   COLOR_OCW / ALPHA_OCW (0x1E40 / 0x0AA0 + 4i): bits 0-3 destino de CD, 4-7 de AB, 8-11
 *     de SUM; bit 12 CD producto escalar, 13 AB producto escalar, 14 SUM es mux, 15-17
 *     escala y sesgo (psh_scale), 18 CD blue-to-alpha, 19 AB blue-to-alpha (solo RGB).
 *   SPECULAR_FOG_CW0 0x0288: A, B, C, D del final; CW1 0x028C: E, F, G y en el byte bajo
 *     0x80 clamp de V1R0_SUM, 0x40 complementa v1, 0x20 complementa r0.
 *   SHADER_STAGE_PROGRAM 0x1E70: modo de la etapa i en los bits 5i a 5i+4.
 *   SHADER_OTHER_STAGE_INPUT 0x1E78: etapa fuente de la 2 en los bits 16-19 y de la 3 en
 *     los 20-23 (la 1 siempre lee de la 0).
 *   SHADER_CLIP_PLANE_MODE 0x17F8: bit 4i + k, componente k de la etapa i: 1 = descarta si
 *     la coordenada es >= 0, 0 = si es < 0.
 *
 * Semántica emulada: entradas mapeadas; AB = A·B (o dot), CD = C·D (o dot), SUM = AB + CD
 * o mux(AB, CD) con r0.a; escala y sesgo sobre las tres salidas y acotado a [-1, 1]; las
 * escrituras van después de leer todo (RGB, luego alfa, luego blue-to-alpha). r0.a empieza
 * valiendo t0.a si la etapa 0 tiene textura (1 si no). Final: rgb = A·B + (1 - A)·C + D,
 * alfa = G, con EF_PROD = E·F y V1R0_SUM = v1 + r0 (complementos y clamp opcionales). Las
 * constantes y el fog son uniforms: el juego las cambia sin recompilar el shader.
 */
#include "psh.h"

#include <stdio.h>
#include <string.h>

#include "emit.h"

#define FINAL_CLAMP_SUM 0x80u
#define FINAL_COMPLEMENT_V1 0x40u
#define FINAL_COMPLEMENT_R0 0x20u

static const char *const k_tex_names[PSH_TEX_MODE_COUNT] = {
    "NONE", "PROJECT2D", "PROJECT3D", "CUBEMAP", "PASSTHRU", "CLIPPLANE", "BUMPENVMAP",
    "BUMPENVMAP_LUM", "BRDF", "DOT_ST", "DOT_ZW", "DOT_RFLCT_DIFF", "DOT_RFLCT_SPEC",
    "DOT_STR_3D", "DOT_STR_CUBE", "DPNDNT_AR", "DPNDNT_GB", "DOTPRODUCT", "DOT_RFLCT_SPEC_CONST",
};

const char *nv2a_psh_error(int code) {
    switch (code) {
    case PSH_ERR_ARGS: return "argumentos inválidos";
    case PSH_ERR_STAGES: return "número de etapas fuera de [1, 8]";
    case PSH_ERR_REGISTER: return "registro no válido en esa posición";
    case PSH_ERR_FLAGS: return "escala reservada o flag solo de RGB en la parte de alfa";
    case PSH_ERR_FINAL: return "mapeo no admitido en el combiner final";
    case PSH_ERR_TEXMODE: return "modo de textura no válido";
    case PSH_ERR_SPACE: return "el GLSL no cabe en el buffer";
    default: return code >= 0 ? "ok" : "error desconocido";
    }
}

/* ---- decodificación -------------------------------------------------------------------- */

static psh_input decode_input(uint32_t word, int slot) {
    uint32_t byte = (word >> (24 - 8 * slot)) & 0xFFu;
    psh_input in;
    in.reg = (uint8_t)(byte & 0xFu);
    in.alpha = (uint8_t)((byte >> 4) & 1u);
    in.map = (uint8_t)(byte >> 5);
    return in;
}

static int readable_in_stage(uint8_t reg) {
    return reg <= PSH_REG_V1 || (reg >= PSH_REG_T0 && reg <= PSH_REG_R1);
}

static int writable(uint8_t reg) {
    return reg == PSH_REG_ZERO || reg == PSH_REG_V0 || reg == PSH_REG_V1 ||
           (reg >= PSH_REG_T0 && reg <= PSH_REG_R1);
}

static int decode_portion(uint32_t inputs, uint32_t outputs, int is_alpha, psh_portion *p) {
    int k;
    uint8_t scale;
    memset(p, 0, sizeof *p);
    for (k = 0; k < 4; k++) {
        p->in[k] = decode_input(inputs, k);
        if (!readable_in_stage(p->in[k].reg)) {
            return PSH_ERR_REGISTER;
        }
    }
    p->cd_out = (uint8_t)(outputs & 0xFu);
    p->ab_out = (uint8_t)((outputs >> 4) & 0xFu);
    p->sum_out = (uint8_t)((outputs >> 8) & 0xFu);
    p->cd_dot = (uint8_t)((outputs >> 12) & 1u);
    p->ab_dot = (uint8_t)((outputs >> 13) & 1u);
    p->mux = (uint8_t)((outputs >> 14) & 1u);
    scale = (uint8_t)((outputs >> 15) & 7u);
    p->cd_blue_to_alpha = (uint8_t)((outputs >> 18) & 1u);
    p->ab_blue_to_alpha = (uint8_t)((outputs >> 19) & 1u);
    if (!writable(p->cd_out) || !writable(p->ab_out) || !writable(p->sum_out)) {
        return PSH_ERR_REGISTER;
    }
    if (scale == 5 || scale == 7) {
        return PSH_ERR_FLAGS;
    }
    p->scale = scale;
    if (is_alpha && (p->cd_dot || p->ab_dot || p->cd_blue_to_alpha || p->ab_blue_to_alpha)) {
        return PSH_ERR_FLAGS;
    }
    return 0;
}

static int decode_final(uint32_t abcd, uint32_t efg, psh_final *f) {
    int k;
    memset(f, 0, sizeof *f);
    f->present = abcd != 0 || efg != 0;
    if (!f->present) {
        return 0;
    }
    for (k = 0; k < 4; k++) {
        f->in[k] = decode_input(abcd, k);
    }
    for (k = 0; k < 3; k++) {
        f->in[4 + k] = decode_input(efg, k);
    }
    for (k = 0; k < 7; k++) {
        uint8_t reg = f->in[k].reg;
        if (reg == 6 || reg == 7) {
            return PSH_ERR_REGISTER;
        }
        if (k >= 4 && k <= 5 && reg == PSH_REG_EF_PROD) {
            return PSH_ERR_REGISTER; /* E y F no pueden leer su propio producto */
        }
        if (f->in[k].map > PSH_MAP_UNSIGNED_INVERT) {
            return PSH_ERR_FINAL;
        }
    }
    f->clamp_sum = (uint8_t)((efg & FINAL_CLAMP_SUM) != 0);
    f->complement_v1 = (uint8_t)((efg & FINAL_COMPLEMENT_V1) != 0);
    f->complement_r0 = (uint8_t)((efg & FINAL_COMPLEMENT_R0) != 0);
    return 0;
}

static int is_dependent(uint8_t mode) {
    return mode >= PSH_TEX_BUMPENVMAP;
}

static int is_emulated(uint8_t mode) {
    return mode <= PSH_TEX_BUMPENVMAP_LUM;
}

static int decode_textures(const psh_registers *r, psh_program *p) {
    int i;
    for (i = 0; i < PSH_TEXTURES; i++) {
        uint8_t mode = (uint8_t)((r->texture_modes >> (5 * i)) & 0x1Fu);
        if (mode >= PSH_TEX_MODE_COUNT || (i == 0 && is_dependent(mode))) {
            return PSH_ERR_TEXMODE;
        }
        p->tex_mode[i] = mode;
        p->compare[i] = (uint8_t)((r->compare_mode >> (4 * i)) & 0xFu);
        if (i == 2) {
            p->tex_source[i] = (uint8_t)((r->other_stage_input >> 16) & 0xFu);
        } else if (i == 3) {
            p->tex_source[i] = (uint8_t)((r->other_stage_input >> 20) & 0xFu);
        }
        if (i > 0 && p->tex_source[i] >= i) {
            return PSH_ERR_TEXMODE;
        }
        if (!is_emulated(mode)) {
            p->unsupported_tex |= (uint8_t)(1u << i);
        }
    }
    return 0;
}

int nv2a_psh_decode(const psh_registers *regs, psh_program *out) {
    int s, err;
    uint32_t control;
    if (!regs || !out) {
        return PSH_ERR_ARGS;
    }
    memset(out, 0, sizeof *out);
    control = regs->combiner_control;
    out->n_stages = (int)(control & 0xFFu);
    if (out->n_stages < 1 || out->n_stages > PSH_MAX_STAGES) {
        return PSH_ERR_STAGES;
    }
    out->mux_msb = (uint8_t)((control >> 8) & 1u);
    out->unique_c0 = (uint8_t)((control >> 12) & 1u);
    out->unique_c1 = (uint8_t)((control >> 16) & 1u);
    for (s = 0; s < out->n_stages; s++) {
        err = decode_portion(regs->rgb_inputs[s], regs->rgb_outputs[s], 0, &out->stage[s].rgb);
        if (err) {
            return err;
        }
        err = decode_portion(regs->alpha_inputs[s], regs->alpha_outputs[s], 1,
                             &out->stage[s].alpha);
        if (err) {
            return err;
        }
    }
    err = decode_final(regs->final_inputs_abcd, regs->final_inputs_efg, &out->final);
    if (err) {
        return err;
    }
    err = decode_textures(regs, out);
    if (err) {
        return err;
    }
    return out->n_stages;
}

/* ---- GLSL ------------------------------------------------------------------------------ */

static const char *reg_var(uint8_t reg) {
    switch (reg) {
    case PSH_REG_C0: return "c0";
    case PSH_REG_C1: return "c1";
    case PSH_REG_FOG: return "fog";
    case PSH_REG_V0: return "v0";
    case PSH_REG_V1: return "v1";
    case PSH_REG_T0: return "t0";
    case PSH_REG_T1: return "t1";
    case PSH_REG_T2: return "t2";
    case PSH_REG_T3: return "t3";
    case PSH_REG_R0: return "r0";
    case PSH_REG_R1: return "r1";
    case PSH_REG_V1R0_SUM: return "v1r0";
    case PSH_REG_EF_PROD: return "efprod";
    default: return NULL; /* cero */
    }
}

/* El valor mapeado de una entrada: vec3 (`rgb` = 1) o float. */
static void emit_input(glsl_buf *b, const psh_input *in, int rgb) {
    const char *var = reg_var(in->reg);
    char src[32];
    if (!var) {
        snprintf(src, sizeof src, rgb ? "vec3(0.0)" : "0.0");
    } else if (rgb) {
        snprintf(src, sizeof src, in->alpha ? "vec3(%s.a)" : "%s.rgb", var);
    } else {
        snprintf(src, sizeof src, in->alpha ? "%s.a" : "%s.b", var);
    }
    switch (in->map) {
    case PSH_MAP_UNSIGNED_IDENTITY: glsl_emit(b, "max(%s, 0.0)", src); break;
    case PSH_MAP_UNSIGNED_INVERT: glsl_emit(b, "(1.0 - clamp(%s, 0.0, 1.0))", src); break;
    case PSH_MAP_EXPAND_NORMAL: glsl_emit(b, "(2.0 * max(%s, 0.0) - 1.0)", src); break;
    case PSH_MAP_EXPAND_NEGATE: glsl_emit(b, "(-2.0 * max(%s, 0.0) + 1.0)", src); break;
    case PSH_MAP_HALFBIAS_NORMAL: glsl_emit(b, "(max(%s, 0.0) - 0.5)", src); break;
    case PSH_MAP_HALFBIAS_NEGATE: glsl_emit(b, "(-max(%s, 0.0) + 0.5)", src); break;
    case PSH_MAP_SIGNED_IDENTITY: glsl_emit(b, "%s", src); break;
    default: glsl_emit(b, "(-%s)", src); break;
    }
}

static void emit_scale(glsl_buf *b, const char *name, uint8_t scale, int rgb) {
    const char *lo = rgb ? "vec3(-1.0)" : "-1.0";
    const char *hi = rgb ? "vec3(1.0)" : "1.0";
    switch (scale) {
    case PSH_SCALE_BIAS: glsl_emit(b, "        %s = %s - 0.5;\n", name, name); break;
    case PSH_SCALE_SHIFTLEFT_1: glsl_emit(b, "        %s = %s * 2.0;\n", name, name); break;
    case PSH_SCALE_SHIFTLEFT_1_BIAS:
        glsl_emit(b, "        %s = (%s - 0.5) * 2.0;\n", name, name);
        break;
    case PSH_SCALE_SHIFTLEFT_2: glsl_emit(b, "        %s = %s * 4.0;\n", name, name); break;
    case PSH_SCALE_SHIFTRIGHT_1: glsl_emit(b, "        %s = %s * 0.5;\n", name, name); break;
    default: break;
    }
    glsl_emit(b, "        %s = clamp(%s, %s, %s);\n", name, name, lo, hi);
}

/* Las tres salidas de una mitad: ab, cd y sum (con sufijo `_a` en la de alfa). */
static void emit_portion(glsl_buf *b, const psh_portion *p, int rgb) {
    const char *type = rgb ? "vec3" : "float";
    const char *sfx = rgb ? "" : "_a";
    const char *names = "ABCD";
    int k;
    for (k = 0; k < 4; k++) {
        glsl_emit(b, "        %s %c%s = ", type, names[k], sfx);
        emit_input(b, &p->in[k], rgb);
        glsl_emit(b, ";\n");
    }
    if (rgb && p->ab_dot) {
        glsl_emit(b, "        vec3 ab = vec3(dot(A, B));\n");
    } else {
        glsl_emit(b, "        %s ab%s = A%s * B%s;\n", type, sfx, sfx, sfx);
    }
    if (rgb && p->cd_dot) {
        glsl_emit(b, "        vec3 cd = vec3(dot(C, D));\n");
    } else {
        glsl_emit(b, "        %s cd%s = C%s * D%s;\n", type, sfx, sfx, sfx);
    }
    if (p->mux) {
        glsl_emit(b, "        %s sum%s = mux_cd ? cd%s : ab%s;\n", type, sfx, sfx, sfx);
    } else {
        glsl_emit(b, "        %s sum%s = ab%s + cd%s;\n", type, sfx, sfx, sfx);
    }
    {
        char name[8];
        snprintf(name, sizeof name, "ab%s", sfx);
        emit_scale(b, name, p->scale, rgb);
        snprintf(name, sizeof name, "cd%s", sfx);
        emit_scale(b, name, p->scale, rgb);
        snprintf(name, sizeof name, "sum%s", sfx);
        emit_scale(b, name, p->scale, rgb);
    }
}

static void emit_writes(glsl_buf *b, const psh_portion *p, int rgb) {
    const char *swz = rgb ? "rgb" : "a";
    const char *sfx = rgb ? "" : "_a";
    if (reg_var(p->ab_out)) glsl_emit(b, "        %s.%s = ab%s;\n", reg_var(p->ab_out), swz, sfx);
    if (reg_var(p->cd_out)) glsl_emit(b, "        %s.%s = cd%s;\n", reg_var(p->cd_out), swz, sfx);
    if (reg_var(p->sum_out)) {
        glsl_emit(b, "        %s.%s = sum%s;\n", reg_var(p->sum_out), swz, sfx);
    }
}

static void emit_stage(glsl_buf *b, const psh_program *p, int s) {
    const psh_stage *st = &p->stage[s];
    glsl_emit(b, "    /* etapa %d */\n    {\n", s);
    glsl_emit(b, "        vec4 c0 = psC0[%d];\n        vec4 c1 = psC1[%d];\n",
              p->unique_c0 ? s : 0, p->unique_c1 ? s : 0);
    if (st->rgb.mux || st->alpha.mux) {
        if (p->mux_msb) {
            glsl_emit(b, "        bool mux_cd = r0.a >= 0.5;\n");
        } else {
            glsl_emit(b, "        bool mux_cd = (uint(clamp(r0.a, 0.0, 1.0) * 255.0) & 1u) != 0u;\n");
        }
    }
    emit_portion(b, &st->rgb, 1);
    emit_portion(b, &st->alpha, 0);
    emit_writes(b, &st->rgb, 1);
    emit_writes(b, &st->alpha, 0);
    if (st->rgb.ab_blue_to_alpha && reg_var(st->rgb.ab_out)) {
        glsl_emit(b, "        %s.a = ab.b;\n", reg_var(st->rgb.ab_out));
    }
    if (st->rgb.cd_blue_to_alpha && reg_var(st->rgb.cd_out)) {
        glsl_emit(b, "        %s.a = cd.b;\n", reg_var(st->rgb.cd_out));
    }
    glsl_emit(b, "    }\n");
}

static const char *sampler_type(uint8_t mode) {
    switch (mode) {
    case PSH_TEX_PROJECT2D:
    case PSH_TEX_BUMPENVMAP:
    case PSH_TEX_BUMPENVMAP_LUM: return "sampler2D";
    case PSH_TEX_PROJECT3D: return "sampler3D";
    case PSH_TEX_CUBEMAP: return "samplerCube";
    default: return NULL;
    }
}

static void emit_texture(glsl_buf *b, const psh_program *p, int i) {
    uint8_t mode = p->tex_mode[i];
    int src = p->tex_source[i];
    int k;
    switch (mode) {
    case PSH_TEX_NONE:
        glsl_emit(b, "    vec4 t%d = vec4(0.0);\n", i);
        break;
    case PSH_TEX_PROJECT2D:
        glsl_emit(b, "    vec4 t%d = textureProj(tex%d, vec3(oT%d.xy * texScale[%d], oT%d.w));\n", i,
                  i, i, i, i);
        break;
    case PSH_TEX_PROJECT3D:
        glsl_emit(b, "    vec4 t%d = textureProj(tex%d, oT%d);\n", i, i, i);
        break;
    case PSH_TEX_CUBEMAP:
        glsl_emit(b, "    vec4 t%d = texture(tex%d, oT%d.xyz);\n", i, i, i);
        break;
    case PSH_TEX_PASSTHRU:
        glsl_emit(b, "    vec4 t%d = clamp(oT%d, 0.0, 1.0);\n", i, i);
        break;
    case PSH_TEX_CLIPPLANE:
        for (k = 0; k < 4; k++) {
            glsl_emit(b, "    if (oT%d.%c %s 0.0) discard;\n", i, "xyzw"[k],
                      (p->compare[i] >> k) & 1u ? ">=" : "<");
        }
        glsl_emit(b, "    vec4 t%d = vec4(0.0);\n", i);
        break;
    case PSH_TEX_BUMPENVMAP:
    case PSH_TEX_BUMPENVMAP_LUM:
        glsl_emit(b, "    vec4 t%d = texture(tex%d, (oT%d.xy + bumpMat[%d] * t%d.rg) * texScale[%d]);\n",
                  i, i, i, i, src, i);
        if (mode == PSH_TEX_BUMPENVMAP_LUM) {
            glsl_emit(b,
                      "    t%d.rgb *= clamp(t%d.b * bumpScale[%d] + bumpOffset[%d], 0.0, 1.0);\n",
                      i, src, i, i);
        }
        break;
    default:
        glsl_emit(b, "    vec4 t%d = vec4(0.0); /* modo %s sin emular */\n", i,
                  k_tex_names[mode]);
        break;
    }
}

static void emit_final(glsl_buf *b, const psh_final *f) {
    const char *names = "ABCDEFG";
    int k;
    if (!f->present) {
        glsl_emit(b, "    fragColor = clamp(r0, 0.0, 1.0);\n");
        return;
    }
    glsl_emit(b, "    /* combiner final: rgb = A*B + (1-A)*C + D, alfa = G */\n");
    glsl_emit(b, "    vec4 c0 = psFinalC0;\n    vec4 c1 = psFinalC1;\n");
    glsl_emit(b, "    vec3 sum_v1 = %s;\n", f->complement_v1 ? "1.0 - v1.rgb" : "v1.rgb");
    glsl_emit(b, "    vec3 sum_r0 = %s;\n", f->complement_r0 ? "1.0 - r0.rgb" : "r0.rgb");
    glsl_emit(b, "    vec4 v1r0 = vec4(%s, 0.0);\n",
              f->clamp_sum ? "clamp(sum_v1 + sum_r0, 0.0, 1.0)" : "sum_v1 + sum_r0");
    for (k = 4; k < 6; k++) {
        glsl_emit(b, "    vec3 f%c = ", names[k]);
        emit_input(b, &f->in[k], 1);
        glsl_emit(b, ";\n");
    }
    glsl_emit(b, "    vec4 efprod = vec4(fE * fF, 0.0);\n");
    for (k = 0; k < 4; k++) {
        glsl_emit(b, "    vec3 f%c = ", names[k]);
        emit_input(b, &f->in[k], 1);
        glsl_emit(b, ";\n");
    }
    glsl_emit(b, "    float fG = ");
    emit_input(b, &f->in[6], 0);
    glsl_emit(b, ";\n");
    glsl_emit(b, "    fragColor = clamp(vec4(fD + mix(fC, fB, fA), fG), 0.0, 1.0);\n");
}

int nv2a_psh_to_glsl(const psh_program *p, char *out, size_t outlen) {
    glsl_buf b;
    int i, result;
    if (!p || !out || p->n_stages < 1 || p->n_stages > PSH_MAX_STAGES) {
        return PSH_ERR_ARGS;
    }
    glsl_buf_init(&b, out, outlen);
    glsl_emit(&b,
              "#version 400\n/* Register combiners del NV2A: %d etapas (ngb, psh.c). */\n"
              "in vec4 oD0;\nin vec4 oD1;\nin vec4 oFog;\n"
              "in vec4 oT0;\nin vec4 oT1;\nin vec4 oT2;\nin vec4 oT3;\n",
              p->n_stages);
    for (i = 0; i < PSH_TEXTURES; i++) {
        const char *type = sampler_type(p->tex_mode[i]);
        if (type) {
            glsl_emit(&b, "uniform %s tex%d;\n", type, i);
        }
    }
    glsl_emit(&b,
              "uniform vec4 psC0[%d];\nuniform vec4 psC1[%d];\n"
              "uniform vec4 psFinalC0;\nuniform vec4 psFinalC1;\nuniform vec4 fogColor;\n"
              "uniform mat2 bumpMat[%d];\nuniform float bumpScale[%d];\n"
              "uniform float bumpOffset[%d];\nuniform int alphaTest;\nuniform int alphaFunc;\n"
              "uniform float alphaRef;\n"
              "/* Texturas lineales (LU_IMAGE): el NV2A las direcciona en texels y texScale =\n"
              " * 1/(ancho, alto) las pasa a [0, 1]. Para las demas vale 1. */\n"
              "uniform vec2 texScale[%d];\nout vec4 fragColor;\n\n"
              "/* Alpha test (estado fijo del NV2A, ver port/src/gl/glstate.c): alphaFunc es\n"
              " * GL_NEVER + n y se compara en 8 bits, como el hardware. */\n"
              "bool nv_alpha_pass(float a) {\n"
              "    int ia = int(clamp(a, 0.0, 1.0) * 255.0 + 0.5);\n"
              "    int ir = int(clamp(alphaRef, 0.0, 1.0) * 255.0 + 0.5);\n"
              "    switch (alphaFunc - 0x200) {\n"
              "    case 0: return false;\n"
              "    case 1: return ia < ir;\n"
              "    case 2: return ia == ir;\n"
              "    case 3: return ia <= ir;\n"
              "    case 4: return ia > ir;\n"
              "    case 5: return ia != ir;\n"
              "    case 6: return ia >= ir;\n"
              "    default: return true;\n"
              "    }\n"
              "}\n\nvoid main() {\n",
              PSH_MAX_STAGES, PSH_MAX_STAGES, PSH_TEXTURES, PSH_TEXTURES, PSH_TEXTURES, PSH_TEXTURES);
    for (i = 0; i < PSH_TEXTURES; i++) {
        emit_texture(&b, p, i);
    }
    glsl_emit(&b,
              "    vec4 v0 = oD0;\n    vec4 v1 = oD1;\n"
              "    vec4 fog = vec4(fogColor.rgb, clamp(oFog.x, 0.0, 1.0));\n"
              "    vec4 r0 = vec4(0.0, 0.0, 0.0, %s);\n    vec4 r1 = vec4(0.0);\n",
              p->tex_mode[0] != PSH_TEX_NONE ? "t0.a" : "1.0");
    for (i = 0; i < p->n_stages; i++) {
        emit_stage(&b, p, i);
    }
    emit_final(&b, &p->final);
    glsl_emit(&b, "    if (alphaTest != 0 && !nv_alpha_pass(fragColor.a)) discard;\n}\n");
    result = glsl_buf_result(&b);
    if (result < 0) {
        if (outlen) {
            out[0] = 0;
        }
        return PSH_ERR_SPACE;
    }
    return result;
}
