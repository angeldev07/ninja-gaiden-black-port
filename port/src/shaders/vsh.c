/*
 * vsh.c: decodificador de vertex programs del NV2A y traductor a GLSL 4.00.
 *
 * Implementación propia escrita a partir de la descripción del formato; no reutiliza
 * código de xemu ni de Cxbx-Reloaded (GPL) ni nada del XDK. Fuentes consultadas para el
 * formato y la semántica:
 *   - xemu, hw/xbox/nv2a/pgraph/vsh.c y hw/xbox/nv2a/pgraph/glsl/vsh-prog.c: tabla de
 *     campos de la instrucción, códigos MAC/ILU, alias R12 = oPos, el ILU escribe R1
 *     cuando va emparejado con un MAC, y el paso de "screen space" a clip space.
 *     https://github.com/xemu-project/xemu/tree/master/hw/xbox/nv2a/pgraph
 *   - Cxbx-Reloaded, src/core/hle/D3D8/XbVertexShader.cpp: los mismos campos, la cabecera
 *     0x2078 del binario de D3D y las constantes c[-96..95] = c[0..191] del NV2A.
 *     https://github.com/Cxbx-Reloaded/Cxbx-Reloaded
 *   - envytools (docs de la clase Kelvin, NV097): método SET_TRANSFORM_PROGRAM 0x0B00 y
 *     los registros de carga (LOAD 0x1E9C, START 0x1EA0).
 *     https://github.com/envytools/envytools
 *   - Microsoft, especificación de vs_1_1 de D3D8 (semántica de DST, LIT, EXPP, LOGP).
 *
 * Instrucción: 4 u32 (w0 sin uso). Campos (palabra, bit inicial, bits):
 *
 *   w1: ILU 25,3 | MAC 21,4 | CONST 13,8 | V 9,4 | A.neg 8,1 | A.swz x 6 y 4 z 2 w 0 (2 c/u)
 *   w2: A.R 28,4 | A.mux 26,2 | B.neg 25,1 | B.swz x 23 y 21 z 19 w 17 | B.R 13,4 |
 *       B.mux 11,2 | C.neg 10,1 | C.swz x 8 y 6 z 4 w 2 | C.R alto 0,2
 *   w3: C.R bajo 30,2 | C.mux 28,2 | máscara MAC 24,4 | OUT_R 20,4 | máscara ILU 16,4 |
 *       máscara de salida 12,4 | ORB 11,1 (1 = o[], 0 = c[]) | dirección 3,8 |
 *       OUT_MUX 2,1 (1 = ILU, 0 = MAC) | A0X 1,1 (c[A0 + CONST]) | FINAL 0,1
 *
 * MUX de un operando: 1 = R, 2 = v, 3 = c. Swizzle: 0 = x ... 3 = w. Máscaras: bit 3 = x.
 * Los tres operandos se leen antes de cualquier escritura, así que el MAC y el ILU de una
 * misma instrucción ven los mismos valores. El ILU usa el operando C (su componente x
 * tras el swizzle, salvo MOV y LIT, que usan el vector).
 *
 * **oPos y la "screen space" de la Xbox.** El runtime de D3D de la Xbox termina cada
 * vertex program con un epílogo que divide por w y aplica la escala y el desplazamiento
 * del viewport (c[58] y c[59], c[-38] y c[-37] en el ensamblador): oPos.xyz sale en
 * píxeles y en unidades del depth buffer, ya dividido por w, y oPos.w conserva w para la
 * corrección de perspectiva. OpenGL espera clip space, así que el GLSL generado lo
 * deshace al final: x_ndc = 2x/ancho - 1, y_ndc = 1 - 2y/alto (la Xbox tiene el origen
 * arriba), z_ndc = 2(z - zmin)/(zmax - zmin) - 1 y multiplica xyz por w (con w acotado
 * como hace el NV2A para no dividir por 0). `surfaceSize` y `clipRange` son uniforms.
 */
#include "vsh.h"

#include <stdio.h>
#include <string.h>

#include "emit.h"

#define MUX_FIELD_BITS 2
#define CONST_LIMIT VSH_NUM_CONSTS

static const char *const k_mac_names[VSH_MAC_COUNT] = {
    "NOP", "MOV", "MUL", "ADD", "MAD", "DP3", "DPH", "DP4", "DST", "MIN", "MAX", "SLT", "SGE",
    "ARL",
};
static const char *const k_ilu_names[VSH_ILU_COUNT] = {
    "NOP", "MOV", "RCP", "RCC", "RSQ", "EXP", "LOG", "LIT",
};
static const char *const k_out_names[VSH_OUT_COUNT] = {
    "oPos", NULL, NULL, "oD0", "oD1", "oFog", "oPts", "oB0", "oB1", "oT0", "oT1", "oT2", "oT3",
};
static const char k_comp[4] = {'x', 'y', 'z', 'w'};

static uint32_t field(const uint32_t *w, int word, int shift, int bits) {
    return (w[word] >> shift) & ((1u << bits) - 1u);
}

const char *nv2a_vsh_mac_name(int op) {
    return op >= 0 && op < VSH_MAC_COUNT ? k_mac_names[op] : "?";
}

const char *nv2a_vsh_ilu_name(int op) {
    return op >= 0 && op < VSH_ILU_COUNT ? k_ilu_names[op] : "?";
}

const char *nv2a_vsh_error(int code) {
    switch (code) {
    case VSH_ERR_ARGS: return "argumentos inválidos";
    case VSH_ERR_OPCODE: return "operación MAC desconocida";
    case VSH_ERR_MUX: return "operando usado sin origen (MUX 0)";
    case VSH_ERR_REGISTER: return "registro temporal o constante fuera de rango";
    case VSH_ERR_OUTPUT: return "salida inválida";
    case VSH_ERR_NO_FINAL: return "ninguna instrucción lleva el bit FINAL";
    case VSH_ERR_SPACE: return "el GLSL no cabe en el buffer";
    default: return code >= 0 ? "ok" : "error desconocido";
    }
}

/* Qué operandos lee cada operación: bit 0 = A, 1 = B, 2 = C. */
static unsigned mac_operands(int op) {
    switch (op) {
    case VSH_MAC_NOP: return 0;
    case VSH_MAC_MOV:
    case VSH_MAC_ARL: return 1;
    case VSH_MAC_ADD: return 1 | 4;
    case VSH_MAC_MAD: return 1 | 2 | 4;
    default: return 1 | 2; /* MUL, DP3, DPH, DP4, DST, MIN, MAX, SLT, SGE */
    }
}

static void decode_swizzle(const uint32_t *w, int word, int shift_x, uint8_t swizzle[4]) {
    int k;
    for (k = 0; k < 4; k++) {
        swizzle[k] = (uint8_t)field(w, word, shift_x - 2 * k, 2);
    }
}

/* Comprueba un operando que la instrucción usa; acumula entradas leídas y uso de A0. */
static int check_operand(const vsh_operand *op, vsh_program *p) {
    switch (op->mux) {
    case VSH_MUX_TEMP:
        return op->index <= VSH_TEMP_OPOS ? 0 : VSH_ERR_REGISTER;
    case VSH_MUX_INPUT:
        p->inputs_read |= 1u << op->index;
        return 0;
    case VSH_MUX_CONST:
        if (op->relative) {
            p->uses_a0 = 1;
        }
        return op->index < CONST_LIMIT ? 0 : VSH_ERR_REGISTER;
    default:
        return VSH_ERR_MUX;
    }
}

static int decode_insn(const uint32_t *w, vsh_insn *in, vsh_program *p) {
    unsigned used;
    int err;
    uint8_t c_index;
    memset(in, 0, sizeof *in);
    memcpy(in->words, w, sizeof in->words);
    in->ilu = (uint8_t)field(w, 1, 25, 3);
    in->mac = (uint8_t)field(w, 1, 21, 4);
    if (in->mac >= VSH_MAC_COUNT) {
        return VSH_ERR_OPCODE;
    }
    c_index = (uint8_t)((field(w, 2, 0, 2) << 2) | field(w, 3, 30, 2));

    in->a.negate = (uint8_t)field(w, 1, 8, 1);
    decode_swizzle(w, 1, 6, in->a.swizzle);
    in->a.index = (uint8_t)field(w, 2, 28, 4);
    in->a.mux = (uint8_t)field(w, 2, 26, MUX_FIELD_BITS);

    in->b.negate = (uint8_t)field(w, 2, 25, 1);
    decode_swizzle(w, 2, 23, in->b.swizzle);
    in->b.index = (uint8_t)field(w, 2, 13, 4);
    in->b.mux = (uint8_t)field(w, 2, 11, MUX_FIELD_BITS);

    in->c.negate = (uint8_t)field(w, 2, 10, 1);
    decode_swizzle(w, 2, 8, in->c.swizzle);
    in->c.index = c_index;
    in->c.mux = (uint8_t)field(w, 3, 28, MUX_FIELD_BITS);

    /* Las entradas y las constantes no van en el campo R sino en V y CONST, comunes. */
    {
        vsh_operand *ops[3];
        int k;
        ops[0] = &in->a;
        ops[1] = &in->b;
        ops[2] = &in->c;
        for (k = 0; k < 3; k++) {
            if (ops[k]->mux == VSH_MUX_INPUT) {
                ops[k]->index = (uint8_t)field(w, 1, 9, 4);
            } else if (ops[k]->mux == VSH_MUX_CONST) {
                ops[k]->index = (uint8_t)field(w, 1, 13, 8);
                ops[k]->relative = (uint8_t)field(w, 3, 1, 1);
            }
        }
    }

    in->mac_mask = (uint8_t)field(w, 3, 24, 4);
    in->temp = (uint8_t)field(w, 3, 20, 4);
    in->ilu_mask = (uint8_t)field(w, 3, 16, 4);
    in->out_mask = (uint8_t)field(w, 3, 12, 4);
    in->out_is_output = (uint8_t)field(w, 3, 11, 1);
    in->out_address = (uint8_t)field(w, 3, 3, 8);
    in->out_from_ilu = (uint8_t)field(w, 3, 2, 1);
    in->final = (uint8_t)field(w, 3, 0, 1);
    in->ilu_temp = in->mac != VSH_MAC_NOP ? 1 : in->temp;

    used = mac_operands(in->mac) | (in->ilu != VSH_ILU_NOP ? 4u : 0u);
    {
        const vsh_operand *ops[3];
        int k;
        ops[0] = &in->a;
        ops[1] = &in->b;
        ops[2] = &in->c;
        for (k = 0; k < 3; k++) {
            if (used & (1u << k)) {
                err = check_operand(ops[k], p);
                if (err) {
                    return err;
                }
            }
        }
    }

    if (in->mac == VSH_MAC_ARL) {
        p->uses_a0 = 1;
        if (in->mac_mask || (in->out_mask && !in->out_from_ilu)) {
            return VSH_ERR_OUTPUT; /* ARL solo escribe A0 */
        }
    }
    if (in->mac == VSH_MAC_NOP && in->mac_mask) {
        return VSH_ERR_OUTPUT;
    }
    if (in->ilu == VSH_ILU_NOP && in->ilu_mask) {
        return VSH_ERR_OUTPUT;
    }
    if ((in->mac_mask && in->temp > VSH_TEMP_OPOS) ||
        (in->ilu_mask && in->ilu_temp > VSH_TEMP_OPOS)) {
        return VSH_ERR_REGISTER;
    }
    if (in->out_mask) {
        int unit = in->out_from_ilu ? in->ilu : in->mac;
        if (unit == 0) {
            return VSH_ERR_OUTPUT; /* la salida tomaría el resultado de un NOP */
        }
        if (in->out_is_output) {
            if (in->out_address >= VSH_OUT_COUNT || !k_out_names[in->out_address]) {
                return VSH_ERR_OUTPUT;
            }
            p->outputs_written |= 1u << in->out_address;
        } else {
            if (in->out_address >= CONST_LIMIT) {
                return VSH_ERR_REGISTER;
            }
            p->writes_constants = 1;
        }
    }
    if (in->mac_mask && in->temp == VSH_TEMP_OPOS) {
        p->outputs_written |= 1u << VSH_OUT_POS;
    }
    if (in->ilu_mask && in->ilu_temp == VSH_TEMP_OPOS) {
        p->outputs_written |= 1u << VSH_OUT_POS;
    }
    return 0;
}

int nv2a_vsh_decode(const uint32_t *prog, int n_insn, vsh_program *out) {
    int i;
    if (!prog || !out || n_insn < 1 || n_insn > VSH_MAX_INSN) {
        return VSH_ERR_ARGS;
    }
    memset(out, 0, sizeof *out);
    for (i = 0; i < n_insn; i++) {
        int err = decode_insn(prog + VSH_WORDS_PER_INSN * i, &out->insn[i], out);
        if (err) {
            return err;
        }
        if (out->insn[i].final) {
            out->n_insn = i + 1;
            return out->n_insn;
        }
    }
    return VSH_ERR_NO_FINAL;
}

/* ---- GLSL ----------------------------------------------------------------------------- */

static void mask_text(uint8_t mask, char text[5]) {
    int k, n = 0;
    for (k = 0; k < 4; k++) {
        if (mask & (0x8u >> k)) {
            text[n++] = k_comp[k];
        }
    }
    text[n] = 0;
}

static void emit_operand(glsl_buf *b, const vsh_operand *op, const vsh_program *p) {
    const char *neg = op->negate ? "-" : "";
    char swz[5];
    int k;
    for (k = 0; k < 4; k++) {
        swz[k] = k_comp[op->swizzle[k] & 3];
    }
    swz[4] = 0;
    switch (op->mux) {
    case VSH_MUX_TEMP:
        if (op->index == VSH_TEMP_OPOS) {
            glsl_emit(b, "%soPos.%s", neg, swz);
        } else {
            glsl_emit(b, "%sR[%u].%s", neg, (unsigned)op->index, swz);
        }
        break;
    case VSH_MUX_INPUT:
        glsl_emit(b, "%sv%u.%s", neg, (unsigned)op->index, swz);
        break;
    default: {
        const char *bank = p->writes_constants ? "C" : "c";
        if (op->relative) {
            glsl_emit(b, "%s%s[clamp(A0 + %u, 0, %d)].%s", neg, bank, (unsigned)op->index,
                      VSH_NUM_CONSTS - 1, swz);
        } else {
            glsl_emit(b, "%s%s[%u].%s", neg, bank, (unsigned)op->index, swz);
        }
        break;
    }
    }
}

static const char *mac_expr(int op) {
    switch (op) {
    case VSH_MAC_MOV: return "a";
    case VSH_MAC_MUL: return "a * b";
    case VSH_MAC_ADD: return "a + cc";
    case VSH_MAC_MAD: return "a * b + cc";
    case VSH_MAC_DP3: return "vec4(dot(a.xyz, b.xyz))";
    case VSH_MAC_DPH: return "vec4(dot(a.xyz, b.xyz) + b.w)";
    case VSH_MAC_DP4: return "vec4(dot(a, b))";
    case VSH_MAC_DST: return "vec4(1.0, a.y * b.y, a.z, b.w)";
    case VSH_MAC_MIN: return "min(a, b)";
    case VSH_MAC_MAX: return "max(a, b)";
    case VSH_MAC_SLT: return "vec4(lessThan(a, b))";
    case VSH_MAC_SGE: return "vec4(greaterThanEqual(a, b))";
    default: return "vec4(0.0)";
    }
}

static const char *ilu_expr(int op) {
    switch (op) {
    case VSH_ILU_MOV: return "cc";
    case VSH_ILU_RCP: return "vec4(1.0 / cc.x)";
    case VSH_ILU_RCC: return "nv_rcc(cc.x)";
    case VSH_ILU_RSQ: return "vec4(inversesqrt(abs(cc.x)))";
    case VSH_ILU_EXP: return "nv_exp(cc.x)";
    case VSH_ILU_LOG: return "nv_log(cc.x)";
    case VSH_ILU_LIT: return "nv_lit(cc)";
    default: return "vec4(0.0)";
    }
}

static void emit_write(glsl_buf *b, const char *dst, uint8_t mask, const char *src) {
    char m[5];
    if (!mask) {
        return;
    }
    if (mask == 0xF) {
        glsl_emit(b, "        %s = %s;\n", dst, src);
        return;
    }
    mask_text(mask, m);
    glsl_emit(b, "        %s.%s = %s.%s;\n", dst, m, src, m);
}

static void temp_name(uint8_t index, char *name, size_t len) {
    if (index == VSH_TEMP_OPOS) {
        snprintf(name, len, "oPos");
    } else {
        snprintf(name, len, "R[%u]", (unsigned)index);
    }
}

static void emit_insn(glsl_buf *b, int i, const vsh_insn *in, const vsh_program *p) {
    unsigned used = mac_operands(in->mac) | (in->ilu != VSH_ILU_NOP ? 4u : 0u);
    char name[8];
    glsl_emit(b, "    /* %d: %s + %s */\n    {\n", i, nv2a_vsh_mac_name(in->mac),
              nv2a_vsh_ilu_name(in->ilu));
    if (used & 1) {
        glsl_emit(b, "        vec4 a = ");
        emit_operand(b, &in->a, p);
        glsl_emit(b, ";\n");
    }
    if (used & 2) {
        glsl_emit(b, "        vec4 b = ");
        emit_operand(b, &in->b, p);
        glsl_emit(b, ";\n");
    }
    if (used & 4) {
        glsl_emit(b, "        vec4 cc = ");
        emit_operand(b, &in->c, p);
        glsl_emit(b, ";\n");
    }
    if (in->mac == VSH_MAC_ARL) {
        glsl_emit(b, "        A0 = int(floor(a.x));\n");
    } else if (in->mac != VSH_MAC_NOP) {
        glsl_emit(b, "        vec4 mac = %s;\n", mac_expr(in->mac));
    }
    if (in->ilu != VSH_ILU_NOP) {
        glsl_emit(b, "        vec4 ilu = %s;\n", ilu_expr(in->ilu));
    }
    temp_name(in->temp, name, sizeof name);
    emit_write(b, name, in->mac_mask, "mac");
    temp_name(in->ilu_temp, name, sizeof name);
    emit_write(b, name, in->ilu_mask, "ilu");
    if (in->out_mask) {
        const char *src = in->out_from_ilu ? "ilu" : "mac";
        if (in->out_is_output) {
            emit_write(b, k_out_names[in->out_address], in->out_mask, src);
        } else {
            char dst[16];
            snprintf(dst, sizeof dst, "C[%u]", (unsigned)in->out_address);
            emit_write(b, dst, in->out_mask, src);
        }
    }
    glsl_emit(b, "    }\n");
}

static const char k_helpers[] =
    "/* ILU: reciproco acotado, EXP y LOG parciales de D3D y LIT. */\n"
    "vec4 nv_rcc(float x) {\n"
    "    float r = 1.0 / x;\n"
    "    float m = clamp(abs(r), 5.42101e-20, 1.84467e19);\n"
    "    return vec4(r < 0.0 ? -m : m);\n"
    "}\n"
    "vec4 nv_exp(float x) {\n"
    "    float f = floor(x);\n"
    "    return vec4(exp2(f), x - f, exp2(x), 1.0);\n"
    "}\n"
    "vec4 nv_log(float x) {\n"
    "    float ax = abs(x);\n"
    "    if (ax == 0.0) return vec4(-3.402823e38, 1.0, -3.402823e38, 1.0);\n"
    "    float e = floor(log2(ax));\n"
    "    return vec4(e, ax / exp2(e), log2(ax), 1.0);\n"
    "}\n"
    "vec4 nv_lit(vec4 s) {\n"
    "    float power = clamp(s.w, -127.9961, 127.9961);\n"
    "    float spec = (s.x > 0.0 && s.y > 0.0) ? pow(s.y, power) : 0.0;\n"
    "    return vec4(1.0, max(s.x, 0.0), spec, 1.0);\n"
    "}\n\n";

static const char k_epilogue[] =
    "    /* oPos sale en la screen space de la Xbox (pixeles, unidades del depth buffer y\n"
    "     * ya dividido por w): se pasa a NDC y se multiplica por w para OpenGL. */\n"
    "    vec4 pos = oPos;\n"
    "    pos.x = pos.x * (2.0 / surfaceSize.x) - 1.0;\n"
    "    pos.y = 1.0 - pos.y * (2.0 / surfaceSize.y);\n"
    "    if (clipRange.y != clipRange.x) {\n"
    "        pos.z = (pos.z - clipRange.x) * (2.0 / (clipRange.y - clipRange.x)) - 1.0;\n"
    "    }\n"
    "    float w = pos.w;\n"
    "    w = w < 0.0 ? clamp(w, -1.84467e19, -5.42101e-20) : clamp(w, 5.42101e-20, 1.84467e19);\n"
    "    gl_Position = vec4(pos.xyz * w, w);\n";

int nv2a_vsh_to_glsl(const vsh_program *p, char *out, size_t outlen) {
    glsl_buf b;
    int i, result;
    if (!p || !out || p->n_insn < 1 || p->n_insn > VSH_MAX_INSN) {
        return VSH_ERR_ARGS;
    }
    glsl_buf_init(&b, out, outlen);
    glsl_emit(&b, "#version 400\n/* Vertex program del NV2A: %d instrucciones (ngb, vsh.c). */\n",
              p->n_insn);
    for (i = 0; i < VSH_NUM_INPUTS; i++) {
        if (p->inputs_read & (1u << i)) {
            glsl_emit(&b, "layout(location = %d) in vec4 v%d;\n", i, i);
        }
    }
    glsl_emit(&b,
              "uniform vec4 c[%d];\n"
              "uniform vec2 surfaceSize;\n"
              "uniform vec2 clipRange;\n",
              VSH_NUM_CONSTS);
    for (i = 1; i < VSH_OUT_COUNT; i++) {
        if (k_out_names[i]) {
            glsl_emit(&b, "out vec4 %s;\n", k_out_names[i]);
        }
    }
    glsl_emit(&b, "\n%s", k_helpers);
    glsl_emit(&b, "void main() {\n    vec4 R[%d];\n    for (int i = 0; i < %d; i++) R[i] = vec4(0.0);\n",
              VSH_NUM_TEMPS, VSH_NUM_TEMPS);
    glsl_emit(&b, "    vec4 oPos = vec4(0.0, 0.0, 0.0, 1.0);\n");
    for (i = 1; i < VSH_OUT_COUNT; i++) {
        if (k_out_names[i]) {
            glsl_emit(&b, "    %s = vec4(0.0, 0.0, 0.0, 1.0);\n", k_out_names[i]);
        }
    }
    if (p->uses_a0) {
        glsl_emit(&b, "    int A0 = 0;\n");
    }
    if (p->writes_constants) {
        glsl_emit(&b, "    vec4 C[%d];\n    for (int i = 0; i < %d; i++) C[i] = c[i];\n",
                  VSH_NUM_CONSTS, VSH_NUM_CONSTS);
    }
    for (i = 0; i < p->n_insn; i++) {
        emit_insn(&b, i, &p->insn[i], p);
    }
    glsl_emit(&b, "%s}\n", k_epilogue);
    result = glsl_buf_result(&b);
    if (result < 0) {
        if (outlen) {
            out[0] = 0;
        }
        return VSH_ERR_SPACE;
    }
    return result;
}
