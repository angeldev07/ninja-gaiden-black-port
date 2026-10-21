/*
 * psh.h: register combiners del NV2A (los "pixel shaders" de la Xbox) -> GLSL 4.x.
 *
 * El NV2A no ejecuta un programa por píxel: tiene 4 etapas de textura con un modo fijo
 * cada una y hasta 8 etapas de combinación generales más una final, configuradas con
 * palabras de control. El juego las carga con los métodos NV097 de la tabla de abajo, o
 * D3D con `CreatePixelShader`/`SetPixelShader` a partir de una estructura que trae las
 * mismas palabras. Aquí se reciben tal cual y se genera un fragment shader que las emula.
 * Detalles del formato, en el docstring de psh.c.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define PSH_MAX_STAGES 8
#define PSH_TEXTURES 4

/* Las palabras de estado, en el formato de los métodos NV097 que las cargan. */
typedef struct psh_registers {
    uint32_t combiner_control;     /* 0x1E60 SET_COMBINER_CONTROL: etapas y modos */
    uint32_t rgb_inputs[PSH_MAX_STAGES];     /* 0x0AC0 + 4i SET_COMBINER_COLOR_ICW */
    uint32_t rgb_outputs[PSH_MAX_STAGES];    /* 0x1E40 + 4i SET_COMBINER_COLOR_OCW */
    uint32_t alpha_inputs[PSH_MAX_STAGES];   /* 0x0260 + 4i SET_COMBINER_ALPHA_ICW */
    uint32_t alpha_outputs[PSH_MAX_STAGES];  /* 0x0AA0 + 4i SET_COMBINER_ALPHA_OCW */
    uint32_t final_inputs_abcd;    /* 0x0288 SET_COMBINER_SPECULAR_FOG_CW0 */
    uint32_t final_inputs_efg;     /* 0x028C SET_COMBINER_SPECULAR_FOG_CW1 */
    uint32_t texture_modes;        /* 0x1E70 SET_SHADER_STAGE_PROGRAM: 5 bits por etapa */
    uint32_t other_stage_input;    /* 0x1E78 SET_SHADER_OTHER_STAGE_INPUT */
    uint32_t compare_mode;         /* 0x17F8 SET_SHADER_CLIP_PLANE_MODE: 4 bits por etapa */
    uint32_t dot_mapping;          /* 0x1E74 SET_DOT_RGBMAPPING (sin uso: modos DOT_*) */
    /* Mapas de sombra (no es un registro: lo compone el llamador a partir del formato de la
     * textura de cada etapa y de la función de comparación de sombras):
     *   bits 0-3: la etapa i tiene una textura de profundidad (mapa de sombras);
     *   bits 4-7: esa profundidad es de 24 bits (si no, de 16);
     *   bits 8-10: función de comparación (0 NEVER, 1 LESS, 2 EQUAL, 3 LEQUAL, 4 GREATER,
     *              5 NOTEQUAL, 6 GEQUAL, 7 ALWAYS). */
    uint32_t shadow;
} psh_registers;

/* Registros de los combiners (4 bits). */
typedef enum psh_reg {
    PSH_REG_ZERO = 0,
    PSH_REG_C0 = 1,
    PSH_REG_C1 = 2,
    PSH_REG_FOG = 3,
    PSH_REG_V0 = 4, /* diffuse */
    PSH_REG_V1 = 5, /* specular */
    PSH_REG_T0 = 8,
    PSH_REG_T1 = 9,
    PSH_REG_T2 = 10,
    PSH_REG_T3 = 11,
    PSH_REG_R0 = 12,
    PSH_REG_R1 = 13,
    PSH_REG_V1R0_SUM = 14, /* solo en el combiner final */
    PSH_REG_EF_PROD = 15,  /* solo en el combiner final */
} psh_reg;

/* Mapeo de una entrada (3 bits). */
typedef enum psh_map {
    PSH_MAP_UNSIGNED_IDENTITY = 0, /* max(0, x) */
    PSH_MAP_UNSIGNED_INVERT = 1,   /* 1 - clamp(x, 0, 1) */
    PSH_MAP_EXPAND_NORMAL = 2,     /* 2 max(0, x) - 1 */
    PSH_MAP_EXPAND_NEGATE = 3,     /* -2 max(0, x) + 1 */
    PSH_MAP_HALFBIAS_NORMAL = 4,   /* max(0, x) - 0.5 */
    PSH_MAP_HALFBIAS_NEGATE = 5,   /* -max(0, x) + 0.5 */
    PSH_MAP_SIGNED_IDENTITY = 6,   /* x */
    PSH_MAP_SIGNED_NEGATE = 7,     /* -x */
} psh_map;

/* Escala y sesgo de las salidas de una etapa (3 bits). */
typedef enum psh_scale {
    PSH_SCALE_NONE = 0,
    PSH_SCALE_BIAS = 1,           /* x - 0.5 */
    PSH_SCALE_SHIFTLEFT_1 = 2,    /* x * 2 */
    PSH_SCALE_SHIFTLEFT_1_BIAS = 3, /* (x - 0.5) * 2 */
    PSH_SCALE_SHIFTLEFT_2 = 4,    /* x * 4 */
    PSH_SCALE_SHIFTRIGHT_1 = 6,   /* x / 2 */
} psh_scale;

/* Modo de una etapa de textura (5 bits). Los no admitidos dan t = 0 y un aviso. */
typedef enum psh_tex_mode {
    PSH_TEX_NONE = 0,
    PSH_TEX_PROJECT2D = 1,
    PSH_TEX_PROJECT3D = 2,
    PSH_TEX_CUBEMAP = 3,
    PSH_TEX_PASSTHRU = 4,
    PSH_TEX_CLIPPLANE = 5,
    PSH_TEX_BUMPENVMAP = 6,
    PSH_TEX_BUMPENVMAP_LUM = 7,
    PSH_TEX_BRDF = 8,
    PSH_TEX_DOT_ST = 9,
    PSH_TEX_DOT_ZW = 10,
    PSH_TEX_DOT_RFLCT_DIFF = 11,
    PSH_TEX_DOT_RFLCT_SPEC = 12,
    PSH_TEX_DOT_STR_3D = 13,
    PSH_TEX_DOT_STR_CUBE = 14,
    PSH_TEX_DPNDNT_AR = 15,
    PSH_TEX_DPNDNT_GB = 16,
    PSH_TEX_DOTPRODUCT = 17,
    PSH_TEX_DOT_RFLCT_SPEC_CONST = 18,
    PSH_TEX_MODE_COUNT
} psh_tex_mode;

typedef struct psh_input {
    uint8_t reg;   /* psh_reg */
    uint8_t alpha; /* canal: 1 = alfa; 0 = RGB (en la parte de alfa, el azul) */
    uint8_t map;   /* psh_map */
} psh_input;

/* La mitad RGB o la de alfa de una etapa general. */
typedef struct psh_portion {
    psh_input in[4];   /* A, B, C, D */
    uint8_t ab_out, cd_out, sum_out; /* registro destino (0 = se descarta) */
    uint8_t ab_dot, cd_dot;          /* solo RGB: producto escalar en vez de producto */
    uint8_t mux;                     /* SUM = mux(AB, CD) según r0.a en vez de AB + CD */
    uint8_t scale;                   /* psh_scale */
    uint8_t ab_blue_to_alpha, cd_blue_to_alpha; /* solo RGB */
} psh_portion;

typedef struct psh_stage {
    psh_portion rgb, alpha;
} psh_stage;

typedef struct psh_final {
    int present;      /* 0 si las dos palabras son 0: la salida es r0 */
    psh_input in[7];  /* A-G; G es alfa */
    uint8_t clamp_sum, complement_v1, complement_r0;
} psh_final;

typedef struct psh_program {
    int n_stages;
    psh_stage stage[PSH_MAX_STAGES];
    psh_final final;
    uint8_t mux_msb;    /* el mux mira el bit alto de r0.a (si no, el bajo) */
    uint8_t unique_c0;  /* cada etapa usa su C0 (si no, todas la de la etapa 0) */
    uint8_t unique_c1;
    uint8_t tex_mode[PSH_TEXTURES];   /* psh_tex_mode */
    uint8_t tex_source[PSH_TEXTURES]; /* etapa de la que lee un modo dependiente */
    uint8_t compare[PSH_TEXTURES];    /* CLIPPLANE: bit k = descarta si coord k >= 0 */
    uint8_t shadow[PSH_TEXTURES];     /* 0 normal, 16 o 24: mapa de sombras de esos bits */
    uint8_t shadow_func;              /* función de comparación (ver psh_registers.shadow) */
    uint8_t unsupported_tex;          /* bit i = el modo de la etapa i no se emula */
} psh_program;

enum psh_error {
    PSH_ERR_ARGS = -1,     /* puntero nulo */
    PSH_ERR_STAGES = -2,   /* número de etapas fuera de [1, 8] */
    PSH_ERR_REGISTER = -3, /* registro de entrada o de salida no válido en esa posición */
    PSH_ERR_FLAGS = -4,    /* escala reservada, o dot/blue-to-alpha en la parte de alfa */
    PSH_ERR_FINAL = -5,    /* mapeo del combiner final distinto de identidad/inversión */
    PSH_ERR_TEXMODE = -6,  /* modo de textura reservado, o dependiente en la etapa 0 */
    PSH_ERR_SPACE = -7,    /* el GLSL no cabe */
};

/* Decodifica las palabras de estado. Devuelve el número de etapas o un `psh_error`. */
int nv2a_psh_decode(const psh_registers *regs, psh_program *out);

/*
 * Escribe un fragment shader GLSL 4.00 (terminado en NUL). Devuelve la longitud o un
 * `psh_error` (PSH_ERR_SPACE si no cabe; `out` queda vacío).
 *
 * Interfaz:
 *   in vec4 oD0, oD1, oFog, oT0-oT3;       las salidas del vertex shader (vsh.h)
 *   uniform sampler2D/3D/Cube tex0-tex3;   según el modo de cada etapa
 *   uniform vec4 psC0[8], psC1[8];         constantes por etapa (FACTOR0/1)
 *   uniform vec4 psFinalC0, psFinalC1;     constantes del final (SPECULAR_FOG_FACTOR)
 *   uniform vec4 fogColor;                 color de niebla (fog.rgb)
 *   uniform mat2 bumpMat[4];               BUMPENVMAP: matriz de cada etapa
 *   uniform float bumpScale[4], bumpOffset[4];   BUMPENVMAP_LUM
 *   out vec4 fragColor;
 */
int nv2a_psh_to_glsl(const psh_program *p, char *out, size_t outlen);

const char *nv2a_psh_error(int code);
