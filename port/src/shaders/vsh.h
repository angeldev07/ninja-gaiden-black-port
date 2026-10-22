/*
 * vsh.h: NV2A (Xbox) vertex programs -> GLSL 4.x.
 *
 * An NV2A vertex program is a list of up to 136 128-bit instructions (4 u32, the first
 * one unused). Each instruction carries in parallel a vector operation (MAC) and a scalar
 * one (ILU), reads up to three operands A, B and C (temporary R0-R12, input v0-v15 or
 * constant c[0-191], with swizzle and negation) and writes with a mask to a temporary and to
 * an output register (o[]) or a constant register (c[]). The game loads it with the method
 * `NV097_SET_TRANSFORM_PROGRAM` (0x0B00), or D3D creates it with `CreateVertexShader` from
 * a binary whose header is `0x2078 | (n_instructions << 16)` followed by the
 * instructions as they are.
 *
 * Field layout and semantics: see the docstring of vsh.c (public sources cited there;
 * own implementation, with no xemu or Cxbx code and without the XDK).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define VSH_MAX_INSN 136
#define VSH_WORDS_PER_INSN 4
#define VSH_NUM_TEMPS 12    /* R0-R11; R12 is an alias of oPos */
#define VSH_TEMP_OPOS 12
#define VSH_NUM_INPUTS 16   /* v0-v15 */
#define VSH_NUM_CONSTS 192  /* c[0]-c[191] (c[-96..95] in the D3D assembler) */

/* D3D binary header (`CreateVertexShader`): type in the low 16 bits. */
#define VSH_BINARY_TYPE_PROGRAM 0x2078u /* "xv": normal program */
#define VSH_BINARY_TYPE_WRITE 0x7778u   /* "xw": can write constants */
#define VSH_BINARY_TYPE_STATE 0x7378u   /* "xs": state program */

/* Write masks: bit 3 = x, 2 = y, 1 = z, 0 = w (as in the format). */
#define VSH_MASK_X 0x8u
#define VSH_MASK_Y 0x4u
#define VSH_MASK_Z 0x2u
#define VSH_MASK_W 0x1u

typedef enum vsh_mac_op {
    VSH_MAC_NOP = 0,
    VSH_MAC_MOV = 1, /* a */
    VSH_MAC_MUL = 2, /* a * b */
    VSH_MAC_ADD = 3, /* a + c (the NV2A adds A and C, not B) */
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
    VSH_ILU_RCC = 3, /* 1 / c.x clamped to [5.42e-20, 1.84e19] in magnitude */
    VSH_ILU_RSQ = 4, /* 1 / sqrt(|c.x|) */
    VSH_ILU_EXP = 5, /* (2^floor(x), fract(x), 2^x, 1) */
    VSH_ILU_LOG = 6, /* (floor(log2|x|), |x| / 2^floor(log2|x|), log2|x|, 1) */
    VSH_ILU_LIT = 7, /* lighting coefficients from c */
    VSH_ILU_COUNT
} vsh_ilu_op;

/* Where an operand comes from (2-bit MUX field). */
typedef enum vsh_mux {
    VSH_MUX_NONE = 0,  /* not valid if the operation uses the operand */
    VSH_MUX_TEMP = 1,  /* R[index] */
    VSH_MUX_INPUT = 2, /* v[index] */
    VSH_MUX_CONST = 3, /* c[index] (+ A0 if `relative`) */
} vsh_mux;

/* Output registers (8-bit address when the output is o[]). */
typedef enum vsh_out_reg {
    VSH_OUT_POS = 0,
    VSH_OUT_D0 = 3,  /* diffuse */
    VSH_OUT_D1 = 4,  /* specular */
    VSH_OUT_FOG = 5,
    VSH_OUT_PTS = 6, /* point size */
    VSH_OUT_B0 = 7,  /* back-face diffuse */
    VSH_OUT_B1 = 8,  /* back-face specular */
    VSH_OUT_T0 = 9,
    VSH_OUT_T1 = 10,
    VSH_OUT_T2 = 11,
    VSH_OUT_T3 = 12,
    VSH_OUT_COUNT = 13
} vsh_out_reg;

typedef struct vsh_operand {
    uint8_t mux;        /* vsh_mux */
    uint8_t index;      /* register (R, v or c) */
    uint8_t swizzle[4]; /* source component of x, y, z, w (0 = x ... 3 = w) */
    uint8_t negate;
    uint8_t relative;   /* constants only: c[A0 + index] */
} vsh_operand;

typedef struct vsh_insn {
    uint32_t words[VSH_WORDS_PER_INSN];
    uint8_t mac;          /* vsh_mac_op */
    uint8_t ilu;          /* vsh_ilu_op */
    vsh_operand a, b, c;
    uint8_t mac_mask;     /* MAC write to R[temp] */
    uint8_t ilu_mask;     /* ILU write to R[ilu_temp] */
    uint8_t temp;         /* OUT_R field: destination temporary of the MAC */
    uint8_t ilu_temp;     /* destination temporary of the ILU: R1 if the MAC is not NOP, otherwise `temp` */
    uint8_t out_mask;     /* write to o[] or c[] */
    uint8_t out_is_output;/* 1 = o[out_address], 0 = c[out_address] */
    uint8_t out_address;
    uint8_t out_from_ilu; /* 1 = the output is the ILU result, 0 = the MAC's */
    uint8_t final;        /* last instruction of the program */
} vsh_insn;

typedef struct vsh_program {
    int n_insn;
    vsh_insn insn[VSH_MAX_INSN];
    uint32_t inputs_read;     /* bit i = reads v[i] */
    uint32_t outputs_written; /* bit i = writes o[i] (vsh_out_reg) */
    int writes_constants;     /* some instruction writes c[] */
    int uses_a0;              /* ARL or relative addressing */
} vsh_program;

/* Error codes (negative). */
enum vsh_error {
    VSH_ERR_ARGS = -1,     /* null pointer or n_insn outside [1, 136] */
    VSH_ERR_OPCODE = -2,   /* MAC 14-15 */
    VSH_ERR_MUX = -3,      /* operand used with MUX 0 */
    VSH_ERR_REGISTER = -4, /* temporary 13-15 or constant > 191 */
    VSH_ERR_OUTPUT = -5,   /* output 1, 2 or > 12, or write from a NOP or an ARL */
    VSH_ERR_NO_FINAL = -6, /* no instruction with the FINAL bit among the n_insn */
    VSH_ERR_SPACE = -7,    /* the GLSL does not fit in the buffer */
};

/*
 * Decodes `n_insn` instructions (4 u32 each) up to the first one with the FINAL bit.
 * Returns the number of decoded instructions (>= 1) or a `vsh_error`.
 */
int nv2a_vsh_decode(const uint32_t *prog, int n_insn, vsh_program *out);

/*
 * Writes into `out` an equivalent GLSL 4.00 vertex shader (NUL-terminated). Returns the
 * length written or a `vsh_error` (VSH_ERR_SPACE if it does not fit; `out` is left empty).
 *
 * Interface of the generated shader:
 *   layout(location = i) in vec4 v<i>;      only the inputs it reads
 *   uniform vec4 c[192];                    vertex program constants
 *   uniform vec2 surfaceSize;               width and height of the render target, in pixels
 *   uniform vec2 clipRange;                 min and max z of the depth buffer (0, 2^24-1...)
 *   out vec4 oD0, oD1, oB0, oB1, oFog, oPts, oT0, oT1, oT2, oT3;
 *   gl_Position                             oPos converted from "screen space" to clip space
 */
int nv2a_vsh_to_glsl(const vsh_program *p, char *out, size_t outlen);

/* Text of an error code. */
const char *nv2a_vsh_error(int code);

/* Names of the operations ("MOV", "DP4"...), for traces and tests. */
const char *nv2a_vsh_mac_name(int op);
const char *nv2a_vsh_ilu_name(int op);
