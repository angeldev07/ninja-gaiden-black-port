/*
 * psh.h: NV2A register combiners (the Xbox "pixel shaders") -> GLSL 4.x.
 *
 * The NV2A does not run a per-pixel program: it has 4 texture stages with a fixed mode
 * each and up to 8 general combiner stages plus a final one, configured with control
 * words. The game loads them with the NV097 methods in the table below, or D3D with
 * `CreatePixelShader`/`SetPixelShader` from a structure that carries the same words.
 * Here they are received as they are and a fragment shader that emulates them is generated.
 * Format details are in the psh.c docstring.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define PSH_MAX_STAGES 8
#define PSH_TEXTURES 4

/* The state words, in the format of the NV097 methods that load them. */
typedef struct psh_registers {
    uint32_t combiner_control;     /* 0x1E60 SET_COMBINER_CONTROL: stages and modes */
    uint32_t rgb_inputs[PSH_MAX_STAGES];     /* 0x0AC0 + 4i SET_COMBINER_COLOR_ICW */
    uint32_t rgb_outputs[PSH_MAX_STAGES];    /* 0x1E40 + 4i SET_COMBINER_COLOR_OCW */
    uint32_t alpha_inputs[PSH_MAX_STAGES];   /* 0x0260 + 4i SET_COMBINER_ALPHA_ICW */
    uint32_t alpha_outputs[PSH_MAX_STAGES];  /* 0x0AA0 + 4i SET_COMBINER_ALPHA_OCW */
    uint32_t final_inputs_abcd;    /* 0x0288 SET_COMBINER_SPECULAR_FOG_CW0 */
    uint32_t final_inputs_efg;     /* 0x028C SET_COMBINER_SPECULAR_FOG_CW1 */
    uint32_t texture_modes;        /* 0x1E70 SET_SHADER_STAGE_PROGRAM: 5 bits per stage */
    uint32_t other_stage_input;    /* 0x1E78 SET_SHADER_OTHER_STAGE_INPUT */
    uint32_t compare_mode;         /* 0x17F8 SET_SHADER_CLIP_PLANE_MODE: 4 bits per stage */
    uint32_t dot_mapping;          /* 0x1E74 SET_DOT_RGBMAPPING (unused: DOT_* modes) */
    /* Shadow maps (not a register: composed by the caller from the texture format of each
     * stage and the shadow comparison function):
     *   bits 0-3: stage i has a depth texture (shadow map);
     *   bits 4-7: that depth is 24-bit (otherwise 16);
     *   bits 8-10: comparison function (0 NEVER, 1 LESS, 2 EQUAL, 3 LEQUAL, 4 GREATER,
     *              5 NOTEQUAL, 6 GEQUAL, 7 ALWAYS). */
    uint32_t shadow;
} psh_registers;

/* Combiner registers (4 bits). */
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
    PSH_REG_V1R0_SUM = 14, /* final combiner only */
    PSH_REG_EF_PROD = 15,  /* final combiner only */
} psh_reg;

/* Mapping of an input (3 bits). */
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

/* Scale and bias of a stage's outputs (3 bits). */
typedef enum psh_scale {
    PSH_SCALE_NONE = 0,
    PSH_SCALE_BIAS = 1,           /* x - 0.5 */
    PSH_SCALE_SHIFTLEFT_1 = 2,    /* x * 2 */
    PSH_SCALE_SHIFTLEFT_1_BIAS = 3, /* (x - 0.5) * 2 */
    PSH_SCALE_SHIFTLEFT_2 = 4,    /* x * 4 */
    PSH_SCALE_SHIFTRIGHT_1 = 6,   /* x / 2 */
} psh_scale;

/* Mode of a texture stage (5 bits). Unsupported ones give t = 0 and a warning. */
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
    uint8_t alpha; /* channel: 1 = alpha; 0 = RGB (in the alpha part, the blue one) */
    uint8_t map;   /* psh_map */
} psh_input;

/* The RGB half or the alpha half of a general stage. */
typedef struct psh_portion {
    psh_input in[4];   /* A, B, C, D */
    uint8_t ab_out, cd_out, sum_out; /* destination register (0 = discarded) */
    uint8_t ab_dot, cd_dot;          /* RGB only: dot product instead of product */
    uint8_t mux;                     /* SUM = mux(AB, CD) according to r0.a instead of AB + CD */
    uint8_t scale;                   /* psh_scale */
    uint8_t ab_blue_to_alpha, cd_blue_to_alpha; /* RGB only */
} psh_portion;

typedef struct psh_stage {
    psh_portion rgb, alpha;
} psh_stage;

typedef struct psh_final {
    int present;      /* 0 if both words are 0: the output is r0 */
    psh_input in[7];  /* A-G; G is alpha */
    uint8_t clamp_sum, complement_v1, complement_r0;
} psh_final;

typedef struct psh_program {
    int n_stages;
    psh_stage stage[PSH_MAX_STAGES];
    psh_final final;
    uint8_t mux_msb;    /* the mux looks at the high bit of r0.a (otherwise the low one) */
    uint8_t unique_c0;  /* each stage uses its own C0 (otherwise all use stage 0's) */
    uint8_t unique_c1;
    uint8_t tex_mode[PSH_TEXTURES];   /* psh_tex_mode */
    uint8_t tex_source[PSH_TEXTURES]; /* stage a dependent mode reads from */
    uint8_t compare[PSH_TEXTURES];    /* CLIPPLANE: bit k = discard if coord k >= 0 */
    uint8_t shadow[PSH_TEXTURES];     /* 0 normal, 16 or 24: shadow map of those bits */
    uint8_t shadow_func;              /* comparison function (see psh_registers.shadow) */
    uint8_t unsupported_tex;          /* bit i = the mode of stage i is not emulated */
} psh_program;

enum psh_error {
    PSH_ERR_ARGS = -1,     /* null pointer */
    PSH_ERR_STAGES = -2,   /* number of stages outside [1, 8] */
    PSH_ERR_REGISTER = -3, /* input or output register not valid at that position */
    PSH_ERR_FLAGS = -4,    /* reserved scale, or dot/blue-to-alpha in the alpha part */
    PSH_ERR_FINAL = -5,    /* final combiner mapping other than identity/inversion */
    PSH_ERR_TEXMODE = -6,  /* reserved texture mode, or dependent on stage 0 */
    PSH_ERR_SPACE = -7,    /* the GLSL does not fit */
};

/* Decodes the state words. Returns the number of stages or a `psh_error`. */
int nv2a_psh_decode(const psh_registers *regs, psh_program *out);

/*
 * Writes a GLSL 4.00 fragment shader (NUL-terminated). Returns the length or a
 * `psh_error` (PSH_ERR_SPACE if it does not fit; `out` is left empty).
 *
 * Interface:
 *   in vec4 oD0, oD1, oFog, oT0-oT3;       the vertex shader outputs (vsh.h)
 *   uniform sampler2D/3D/Cube tex0-tex3;   depending on each stage's mode
 *   uniform vec4 psC0[8], psC1[8];         per-stage constants (FACTOR0/1)
 *   uniform vec4 psFinalC0, psFinalC1;     final constants (SPECULAR_FOG_FACTOR)
 *   uniform vec4 fogColor;                 fog color (fog.rgb)
 *   uniform mat2 bumpMat[4];               BUMPENVMAP: matrix of each stage
 *   uniform float bumpScale[4], bumpOffset[4];   BUMPENVMAP_LUM
 *   out vec4 fragColor;
 */
int nv2a_psh_to_glsl(const psh_program *p, char *out, size_t outlen);

const char *nv2a_psh_error(int code);
