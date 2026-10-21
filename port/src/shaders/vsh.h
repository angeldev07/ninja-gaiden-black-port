/*
 * vsh.h: vertex programs del NV2A (Xbox) -> GLSL 4.x.
 *
 * Un vertex program del NV2A es una lista de hasta 136 instrucciones de 128 bits (4 u32,
 * el primero sin uso). Cada instrucción lleva en paralelo una operación vectorial (MAC) y
 * una escalar (ILU), lee hasta tres operandos A, B y C (temporal R0-R12, entrada v0-v15
 * o constante c[0-191], con swizzle y negación) y escribe con máscara en un temporal y en
 * un registro de salida (o[]) o de constantes (c[]). El juego lo carga con el método
 * `NV097_SET_TRANSFORM_PROGRAM` (0x0B00), o D3D lo crea con `CreateVertexShader` a partir
 * de un binario cuya cabecera es `0x2078 | (n_instrucciones << 16)` y le siguen las
 * instrucciones tal cual.
 *
 * Disposición de los campos y semántica: ver el docstring de vsh.c (fuentes públicas
 * citadas allí; implementación propia, sin código de xemu ni de Cxbx y sin el XDK).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define VSH_MAX_INSN 136
#define VSH_WORDS_PER_INSN 4
#define VSH_NUM_TEMPS 12    /* R0-R11; R12 es un alias de oPos */
#define VSH_TEMP_OPOS 12
#define VSH_NUM_INPUTS 16   /* v0-v15 */
#define VSH_NUM_CONSTS 192  /* c[0]-c[191] (c[-96..95] en el ensamblador de D3D) */

/* Cabecera del binario de D3D (`CreateVertexShader`): tipo en los 16 bits bajos. */
#define VSH_BINARY_TYPE_PROGRAM 0x2078u /* "xv": programa normal */
#define VSH_BINARY_TYPE_WRITE 0x7778u   /* "xw": puede escribir constantes */
#define VSH_BINARY_TYPE_STATE 0x7378u   /* "xs": programa de estado */

/* Máscaras de escritura: bit 3 = x, 2 = y, 1 = z, 0 = w (como en el formato). */
#define VSH_MASK_X 0x8u
#define VSH_MASK_Y 0x4u
#define VSH_MASK_Z 0x2u
#define VSH_MASK_W 0x1u

typedef enum vsh_mac_op {
    VSH_MAC_NOP = 0,
    VSH_MAC_MOV = 1, /* a */
    VSH_MAC_MUL = 2, /* a * b */
    VSH_MAC_ADD = 3, /* a + c (el NV2A suma A y C, no B) */
    VSH_MAC_MAD = 4, /* a * b + c */
    VSH_MAC_DP3 = 5,
    VSH_MAC_DPH = 6, /* dot(a.xyz, b.xyz) + b.w */
    VSH_MAC_DP4 = 7,
    VSH_MAC_DST = 8, /* (1, a.y * b.y, a.z, b.w) */
    VSH_MAC_MIN = 9,
    VSH_MAC_MAX = 10,
    VSH_MAC_SLT = 11,
    VSH_MAC_SGE = 12,
    VSH_MAC_ARL = 13, /* A0 = floor(a.x) */
    VSH_MAC_COUNT
} vsh_mac_op;

typedef enum vsh_ilu_op {
    VSH_ILU_NOP = 0,
    VSH_ILU_MOV = 1, /* c */
    VSH_ILU_RCP = 2, /* 1 / c.x */
    VSH_ILU_RCC = 3, /* 1 / c.x acotado a [5.42e-20, 1.84e19] en módulo */
    VSH_ILU_RSQ = 4, /* 1 / sqrt(|c.x|) */
    VSH_ILU_EXP = 5, /* (2^floor(x), fract(x), 2^x, 1) */
    VSH_ILU_LOG = 6, /* (floor(log2|x|), |x| / 2^floor(log2|x|), log2|x|, 1) */
    VSH_ILU_LIT = 7, /* coeficientes de iluminación de c */
    VSH_ILU_COUNT
} vsh_ilu_op;

/* De dónde sale un operando (campo MUX de 2 bits). */
typedef enum vsh_mux {
    VSH_MUX_NONE = 0,  /* no válido si la operación usa el operando */
    VSH_MUX_TEMP = 1,  /* R[index] */
    VSH_MUX_INPUT = 2, /* v[index] */
    VSH_MUX_CONST = 3, /* c[index] (+ A0 si `relative`) */
} vsh_mux;

/* Registros de salida (dirección de 8 bits cuando la salida es o[]). */
typedef enum vsh_out_reg {
    VSH_OUT_POS = 0,
    VSH_OUT_D0 = 3,  /* diffuse */
    VSH_OUT_D1 = 4,  /* specular */
    VSH_OUT_FOG = 5,
    VSH_OUT_PTS = 6, /* tamaño de punto */
    VSH_OUT_B0 = 7,  /* diffuse de la cara trasera */
    VSH_OUT_B1 = 8,  /* specular de la cara trasera */
    VSH_OUT_T0 = 9,
    VSH_OUT_T1 = 10,
    VSH_OUT_T2 = 11,
    VSH_OUT_T3 = 12,
    VSH_OUT_COUNT = 13
} vsh_out_reg;

typedef struct vsh_operand {
    uint8_t mux;        /* vsh_mux */
    uint8_t index;      /* registro (R, v o c) */
    uint8_t swizzle[4]; /* componente fuente de x, y, z, w (0 = x ... 3 = w) */
    uint8_t negate;
    uint8_t relative;   /* solo constantes: c[A0 + index] */
} vsh_operand;

typedef struct vsh_insn {
    uint32_t words[VSH_WORDS_PER_INSN];
    uint8_t mac;          /* vsh_mac_op */
    uint8_t ilu;          /* vsh_ilu_op */
    vsh_operand a, b, c;
    uint8_t mac_mask;     /* escritura del MAC en R[temp] */
    uint8_t ilu_mask;     /* escritura del ILU en R[ilu_temp] */
    uint8_t temp;         /* campo OUT_R: temporal destino del MAC */
    uint8_t ilu_temp;     /* temporal destino del ILU: R1 si el MAC no es NOP, si no `temp` */
    uint8_t out_mask;     /* escritura en o[] o c[] */
    uint8_t out_is_output;/* 1 = o[out_address], 0 = c[out_address] */
    uint8_t out_address;
    uint8_t out_from_ilu; /* 1 = la salida es el resultado del ILU, 0 = el del MAC */
    uint8_t final;        /* última instrucción del programa */
} vsh_insn;

typedef struct vsh_program {
    int n_insn;
    vsh_insn insn[VSH_MAX_INSN];
    uint32_t inputs_read;     /* bit i = lee v[i] */
    uint32_t outputs_written; /* bit i = escribe o[i] (vsh_out_reg) */
    int writes_constants;     /* alguna instrucción escribe c[] */
    int uses_a0;              /* ARL o direccionamiento relativo */
} vsh_program;

/* Códigos de error (negativos). */
enum vsh_error {
    VSH_ERR_ARGS = -1,     /* puntero nulo o n_insn fuera de [1, 136] */
    VSH_ERR_OPCODE = -2,   /* MAC 14-15 */
    VSH_ERR_MUX = -3,      /* operando usado con MUX 0 */
    VSH_ERR_REGISTER = -4, /* temporal 13-15 o constante > 191 */
    VSH_ERR_OUTPUT = -5,   /* salida 1, 2 o > 12, o escritura desde un NOP o un ARL */
    VSH_ERR_NO_FINAL = -6, /* ninguna instrucción con el bit FINAL en las n_insn */
    VSH_ERR_SPACE = -7,    /* el GLSL no cabe en el buffer */
};

/*
 * Decodifica `n_insn` instrucciones (4 u32 cada una) hasta la primera con el bit FINAL.
 * Devuelve el número de instrucciones decodificadas (>= 1) o un `vsh_error`.
 */
int nv2a_vsh_decode(const uint32_t *prog, int n_insn, vsh_program *out);

/*
 * Escribe en `out` un vertex shader GLSL 4.00 equivalente (terminado en NUL). Devuelve la
 * longitud escrita o un `vsh_error` (VSH_ERR_SPACE si no cabe; `out` queda vacío).
 *
 * Interfaz del shader generado:
 *   layout(location = i) in vec4 v<i>;      solo las entradas que lee
 *   uniform vec4 c[192];                    constantes del vertex program
 *   uniform vec2 surfaceSize;               ancho y alto del render target, en píxeles
 *   uniform vec2 clipRange;                 z mínima y máxima del depth buffer (0, 2^24-1...)
 *   out vec4 oD0, oD1, oB0, oB1, oFog, oPts, oT0, oT1, oT2, oT3;
 *   gl_Position                             oPos pasado de "screen space" a clip space
 */
int nv2a_vsh_to_glsl(const vsh_program *p, char *out, size_t outlen);

/* Texto de un código de error. */
const char *nv2a_vsh_error(int code);

/* Nombres de las operaciones ("MOV", "DP4"...), para trazas y tests. */
const char *nv2a_vsh_mac_name(int op);
const char *nv2a_vsh_ilu_name(int op);
