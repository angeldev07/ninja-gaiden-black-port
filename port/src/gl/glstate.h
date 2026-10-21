/*
 * glstate.h: estado fijo del NV2A (métodos NV097) -> estado de OpenGL.
 *
 * `nv2a_state` guarda los valores tal cual llegan en el push buffer, uno por método; el
 * intérprete de PGRAPH solo tiene que copiarlos. `glstate_translate` es una función pura
 * que los valida y los pasa a valores de GL (`gl_state`); `glstate_apply` hace eso y llama
 * a GL. El alpha test y la niebla no tienen equivalente fijo en el core profile: van en
 * `gl_state` como valores para los uniforms del fragment shader.
 *
 * Métodos y valores: xemu, hw/xbox/nv2a/nv2a_regs.h (ver glstate.c).
 */
#pragma once

#include <stdint.h>

/* Valores crudos de los métodos NV097 (dirección del método en el comentario). */
typedef struct nv2a_state {
    uint32_t blend_enable;        /* 0x0304 SET_BLEND_ENABLE */
    uint32_t blend_sfactor;       /* 0x0344 SET_BLEND_FUNC_SFACTOR */
    uint32_t blend_dfactor;       /* 0x0348 SET_BLEND_FUNC_DFACTOR */
    uint32_t blend_color;         /* 0x034C SET_BLEND_COLOR (ARGB) */
    uint32_t blend_equation;      /* 0x0350 SET_BLEND_EQUATION */
    uint32_t depth_test_enable;   /* 0x030C SET_DEPTH_TEST_ENABLE */
    uint32_t depth_func;          /* 0x0354 SET_DEPTH_FUNC */
    uint32_t depth_mask;          /* 0x035C SET_DEPTH_MASK */
    uint32_t cull_face_enable;    /* 0x0308 SET_CULL_FACE_ENABLE */
    uint32_t cull_face;           /* 0x039C SET_CULL_FACE */
    uint32_t front_face;          /* 0x03A0 SET_FRONT_FACE */
    uint32_t stencil_test_enable; /* 0x032C SET_STENCIL_TEST_ENABLE */
    uint32_t stencil_mask;        /* 0x0360 SET_STENCIL_MASK (escritura) */
    uint32_t stencil_func;        /* 0x0364 SET_STENCIL_FUNC */
    uint32_t stencil_func_ref;    /* 0x0368 SET_STENCIL_FUNC_REF */
    uint32_t stencil_func_mask;   /* 0x036C SET_STENCIL_FUNC_MASK (lectura) */
    uint32_t stencil_op_fail;     /* 0x0370 SET_STENCIL_OP_FAIL */
    uint32_t stencil_op_zfail;    /* 0x0374 SET_STENCIL_OP_ZFAIL */
    uint32_t stencil_op_zpass;    /* 0x0378 SET_STENCIL_OP_ZPASS */
    uint32_t alpha_test_enable;   /* 0x0300 SET_ALPHA_TEST_ENABLE */
    uint32_t alpha_func;          /* 0x033C SET_ALPHA_FUNC */
    uint32_t alpha_ref;           /* 0x0340 SET_ALPHA_REF (0-255) */
    uint32_t color_mask;          /* 0x0358 SET_COLOR_MASK (un byte por canal: A R G B) */
    uint32_t surface_clip_h;      /* 0x0200 SET_SURFACE_CLIP_HORIZONTAL: x | ancho << 16 */
    uint32_t surface_clip_v;      /* 0x0204 SET_SURFACE_CLIP_VERTICAL: y | alto << 16 */
    uint32_t fog_enable;          /* 0x02A4 SET_FOG_ENABLE */
    uint32_t fog_mode;            /* 0x029C SET_FOG_MODE */
    uint32_t fog_gen_mode;        /* 0x02A0 SET_FOG_GEN_MODE */
    uint32_t fog_color;           /* 0x02A8 SET_FOG_COLOR (R en el byte bajo: ABGR) */
    float fog_params[3];          /* 0x09C0 SET_FOG_PARAMS */
    uint32_t surface_width;       /* tamaño del render target (no es un método) */
    uint32_t surface_height;
} nv2a_state;

/* Lo que hay que pasarle a GL (enums de glcorearb.h) y a los uniforms del shader. */
typedef struct gl_state {
    int blend;
    unsigned blend_src, blend_dst, blend_equation;
    float blend_color[4];
    int blend_signed;      /* ecuación *_SIGNED del NV2A: se aproxima con la normal */
    int depth_test;
    unsigned depth_func;
    int depth_write;
    int cull;
    unsigned cull_face, front_face;
    int stencil;
    unsigned stencil_func;
    int stencil_ref;
    unsigned stencil_read_mask, stencil_write_mask;
    unsigned stencil_fail, stencil_zfail, stencil_zpass;
    int color_mask[4];     /* R, G, B, A */
    int scissor[4];        /* x, y (abajo), ancho, alto en píxeles de GL */
    /* uniforms */
    int alpha_test;
    unsigned alpha_func;
    float alpha_ref;       /* 0-1 */
    int fog;
    unsigned fog_mode;     /* GL_LINEAR/GL_EXP/GL_EXP2 (o el valor _ABS del NV2A) */
    float fog_color[4];
    float fog_params[3];
} gl_state;

enum glstate_error {
    GLSTATE_ERR_ARGS = -1,
    GLSTATE_ERR_BLEND = -2,    /* factor o ecuación de blend desconocidos */
    GLSTATE_ERR_COMPARE = -3,  /* función de comparación (depth, stencil, alfa) desconocida */
    GLSTATE_ERR_CULL = -4,     /* cara de cull o sentido de giro desconocidos */
    GLSTATE_ERR_STENCIL = -5,  /* operación de stencil desconocida */
    GLSTATE_ERR_FOG = -6,      /* modo de niebla desconocido */
    GLSTATE_ERR_GL = -7,       /* glGetError tras aplicar */
};

/* Estado tras el reset del NV2A (como el GL por defecto): sin blend, depth LESS, CCW... */
void nv2a_state_defaults(nv2a_state *s, uint32_t surface_width, uint32_t surface_height);

/* Valida y traduce; puro (sin GL). Devuelve 0 o un `glstate_error`. */
int glstate_translate(const nv2a_state *in, gl_state *out);

/* Traduce y aplica con el contexto GL actual (glbackend). */
int glstate_apply(const nv2a_state *in, gl_state *applied);

const char *glstate_error_text(int code);
