/*
 * glstate.h: NV2A fixed-function state (NV097 methods) -> OpenGL state.
 *
 * `nv2a_state` stores the values as they arrive in the push buffer, one per method; the
 * PGRAPH interpreter only has to copy them. `glstate_translate` is a pure function that
 * validates them and turns them into GL values (`gl_state`); `glstate_apply` does that and
 * calls GL. The alpha test and fog have no fixed equivalent in the core profile: they go in
 * `gl_state` as values for the fragment shader uniforms.
 *
 * Methods and values: xemu, hw/xbox/nv2a/nv2a_regs.h (see glstate.c).
 */
#pragma once

#include <stdint.h>

/* Raw values of the NV097 methods (method address in the comment). */
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
    uint32_t stencil_mask;        /* 0x0360 SET_STENCIL_MASK (write) */
    uint32_t stencil_func;        /* 0x0364 SET_STENCIL_FUNC */
    uint32_t stencil_func_ref;    /* 0x0368 SET_STENCIL_FUNC_REF */
    uint32_t stencil_func_mask;   /* 0x036C SET_STENCIL_FUNC_MASK (read) */
    uint32_t stencil_op_fail;     /* 0x0370 SET_STENCIL_OP_FAIL */
    uint32_t stencil_op_zfail;    /* 0x0374 SET_STENCIL_OP_ZFAIL */
    uint32_t stencil_op_zpass;    /* 0x0378 SET_STENCIL_OP_ZPASS */
    uint32_t alpha_test_enable;   /* 0x0300 SET_ALPHA_TEST_ENABLE */
    uint32_t alpha_func;          /* 0x033C SET_ALPHA_FUNC */
    uint32_t alpha_ref;           /* 0x0340 SET_ALPHA_REF (0-255) */
    uint32_t color_mask;          /* 0x0358 SET_COLOR_MASK (one byte per channel: A R G B) */
    uint32_t surface_clip_h;      /* 0x0200 SET_SURFACE_CLIP_HORIZONTAL: x | width << 16 */
    uint32_t surface_clip_v;      /* 0x0204 SET_SURFACE_CLIP_VERTICAL: y | height << 16 */
    uint32_t fog_enable;          /* 0x02A4 SET_FOG_ENABLE */
    uint32_t fog_mode;            /* 0x029C SET_FOG_MODE */
    uint32_t fog_gen_mode;        /* 0x02A0 SET_FOG_GEN_MODE */
    uint32_t fog_color;           /* 0x02A8 SET_FOG_COLOR (R in the low byte: ABGR) */
    float fog_params[3];          /* 0x09C0 SET_FOG_PARAMS */
    uint32_t poly_offset_fill;    /* 0x0338 SET_POLY_OFFSET_FILL_ENABLE */
    float poly_offset_factor;     /* 0x0384 SET_POLYGON_OFFSET_SCALE_FACTOR */
    float poly_offset_bias;       /* 0x0388 SET_POLYGON_OFFSET_BIAS (Z units) */
    float zclip_min, zclip_max;   /* 0x0394/0x0398 SET_CLIP_MIN/MAX (Z units) */
    uint32_t surface_width;       /* render target size (not a method) */
    uint32_t surface_height;
} nv2a_state;

/* What has to be passed to GL (glcorearb.h enums) and to the shader uniforms. */
typedef struct gl_state {
    int blend;
    unsigned blend_src, blend_dst, blend_equation;
    float blend_color[4];
    int blend_signed;      /* NV2A *_SIGNED equation: approximated with the normal one */
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
    int scissor[4];        /* x, y (bottom), width, height in GL pixels */
    /* uniforms */
    int alpha_test;
    unsigned alpha_func;
    float alpha_ref;       /* 0-1 */
    int fog;
    unsigned fog_mode;     /* GL_LINEAR/GL_EXP/GL_EXP2 (or the NV2A _ABS value) */
    float fog_color[4];
    float fog_params[3];
    /* glPolygonOffset: the NV2A bias is in Z units of the clip range; GL units are
     * the resolution of our 24-bit depth buffer. */
    int poly_offset;
    float poly_factor, poly_units;
} gl_state;

enum glstate_error {
    GLSTATE_ERR_ARGS = -1,
    GLSTATE_ERR_BLEND = -2,    /* unknown blend factor or equation */
    GLSTATE_ERR_COMPARE = -3,  /* unknown comparison function (depth, stencil, alpha) */
    GLSTATE_ERR_CULL = -4,     /* unknown cull face or winding order */
    GLSTATE_ERR_STENCIL = -5,  /* unknown stencil operation */
    GLSTATE_ERR_FOG = -6,      /* unknown fog mode */
    GLSTATE_ERR_GL = -7,       /* glGetError after applying */
};

/* State after the NV2A reset (like the GL default): no blend, depth LESS, CCW... */
void nv2a_state_defaults(nv2a_state *s, uint32_t surface_width, uint32_t surface_height);

/* Validates and translates; pure (no GL). Returns 0 or a `glstate_error`. */
int glstate_translate(const nv2a_state *in, gl_state *out);

/* Translates and applies with the current GL context (glbackend). */
int glstate_apply(const nv2a_state *in, gl_state *applied);

const char *glstate_error_text(int code);
