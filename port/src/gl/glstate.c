/*
 * glstate.c: traducción del estado fijo del NV2A a OpenGL (ver glstate.h).
 *
 * Los valores de los métodos del NV2A son, casi siempre, los mismos enums de OpenGL: la
 * clase Kelvin (NV097) se diseñó para el driver de GL de NVIDIA. Fuente de todos los
 * valores: xemu, hw/xbox/nv2a/nv2a_regs.h (NV097_SET_BLEND_FUNC_SFACTOR_V_*,
 * _BLEND_EQUATION_V_*, _CULL_FACE_V_*, _FRONT_FACE_V_*, _STENCIL_OP_V_*, _FOG_MODE_V_*,
 * máscaras de SET_COLOR_MASK, SET_FOG_COLOR y SET_SURFACE_CLIP_*). Lo que no es idéntico:
 *
 *   - Comparaciones (depth, stencil, alfa): el NV2A guarda los 4 bits bajos; se aceptan
 *     0-7 o 0x200-0x207 y salen como GL_NEVER + n.
 *   - Ecuaciones FUNC_ADD_SIGNED (0xF006) y FUNC_REVERSE_SUBTRACT_SIGNED (0xF005): GL no
 *     tiene blend con signo; se aproximan con la ecuación normal y se marca `blend_signed`.
 *   - Sentido de giro: el vertex shader traducido (vsh.c) invierte la Y al pasar de la
 *     screen space de la Xbox (Y hacia abajo) a NDC, así que lo que en la Xbox es CW en
 *     pantalla es CCW para GL: SET_FRONT_FACE se invierte.
 *   - Scissor: SET_SURFACE_CLIP da x, y desde arriba; GL cuenta y desde abajo.
 *   - Colores: SET_BLEND_COLOR es ARGB (A en el byte alto); SET_FOG_COLOR es ABGR (R en
 *     el byte bajo).
 */
#include "glstate.h"

#include <string.h>

#include "glload.h"

#define NV_FUNC_ADD_SIGNED 0xF006u
#define NV_FUNC_REVERSE_SUBTRACT_SIGNED 0xF005u
#define GL_FUNC_ADD_ 0x8006u
#define GL_MIN_ 0x8007u
#define GL_MAX_ 0x8008u
#define GL_FUNC_SUBTRACT_ 0x800Au
#define GL_FUNC_REVERSE_SUBTRACT_ 0x800Bu
#define GL_INCR_WRAP_ 0x8507u
#define GL_DECR_WRAP_ 0x8508u
#define GL_NEVER_ 0x0200u
#define GL_LESS_ 0x0201u
#define GL_ALWAYS_ 0x0207u
#define GL_CW_ 0x0900u
#define GL_CCW_ 0x0901u
#define GL_LINEAR_FOG 0x2601u

const char *glstate_error_text(int code) {
    switch (code) {
    case GLSTATE_ERR_ARGS: return "argumentos inválidos";
    case GLSTATE_ERR_BLEND: return "factor o ecuación de blend desconocidos";
    case GLSTATE_ERR_COMPARE: return "función de comparación desconocida";
    case GLSTATE_ERR_CULL: return "cara de cull o sentido de giro desconocidos";
    case GLSTATE_ERR_STENCIL: return "operación de stencil desconocida";
    case GLSTATE_ERR_FOG: return "modo de niebla desconocido";
    case GLSTATE_ERR_GL: return "error de OpenGL al aplicar";
    default: return code >= 0 ? "ok" : "error desconocido";
    }
}

void nv2a_state_defaults(nv2a_state *s, uint32_t surface_width, uint32_t surface_height) {
    memset(s, 0, sizeof *s);
    s->blend_sfactor = 0x0001;  /* ONE */
    s->blend_dfactor = 0x0000;  /* ZERO */
    s->blend_equation = GL_FUNC_ADD_;
    s->depth_func = GL_LESS_;
    s->depth_mask = 1;
    s->cull_face = 0x0405;      /* BACK */
    s->front_face = GL_CCW_;
    s->stencil_mask = 0xFF;
    s->stencil_func = GL_ALWAYS_;
    s->stencil_func_mask = 0xFF;
    s->stencil_op_fail = s->stencil_op_zfail = s->stencil_op_zpass = 0x1E00; /* KEEP */
    s->alpha_func = GL_ALWAYS_;
    s->color_mask = 0x01010101u;
    s->fog_mode = GL_LINEAR_FOG;
    s->surface_width = surface_width;
    s->surface_height = surface_height;
}

static int blend_factor(uint32_t v, unsigned *out) {
    if (v <= 1 || (v >= 0x0300 && v <= 0x0308) || (v >= 0x8001 && v <= 0x8004)) {
        *out = v;
        return 0;
    }
    return GLSTATE_ERR_BLEND;
}

static int blend_equation(uint32_t v, unsigned *out, int *is_signed) {
    *is_signed = 0;
    switch (v) {
    case GL_FUNC_ADD_:
    case GL_MIN_:
    case GL_MAX_:
    case GL_FUNC_SUBTRACT_:
    case GL_FUNC_REVERSE_SUBTRACT_: *out = v; return 0;
    case NV_FUNC_ADD_SIGNED: *out = GL_FUNC_ADD_; *is_signed = 1; return 0;
    case NV_FUNC_REVERSE_SUBTRACT_SIGNED:
        *out = GL_FUNC_REVERSE_SUBTRACT_;
        *is_signed = 1;
        return 0;
    default: return GLSTATE_ERR_BLEND;
    }
}

static int compare_func(uint32_t v, unsigned *out) {
    if ((v & ~0xFu) != 0 && (v & ~0xFu) != GL_NEVER_) {
        return GLSTATE_ERR_COMPARE;
    }
    if ((v & 0xFu) > 7) {
        return GLSTATE_ERR_COMPARE;
    }
    *out = GL_NEVER_ + (v & 0xFu);
    return 0;
}

static int stencil_op(uint32_t v, unsigned *out) {
    switch (v) {
    case 0x0000: /* ZERO */
    case 0x1E00: /* KEEP */
    case 0x1E01: /* REPLACE */
    case 0x1E02: /* INCRSAT = GL_INCR */
    case 0x1E03: /* DECRSAT = GL_DECR */
    case 0x150A: /* INVERT */
    case GL_INCR_WRAP_:
    case GL_DECR_WRAP_: *out = v; return 0;
    default: return GLSTATE_ERR_STENCIL;
    }
}

static float byte_at(uint32_t word, int shift) {
    return (float)((word >> shift) & 0xFFu) / 255.0f;
}

int glstate_translate(const nv2a_state *in, gl_state *out) {
    int err;
    uint32_t w, h;
    if (!in || !out) {
        return GLSTATE_ERR_ARGS;
    }
    memset(out, 0, sizeof *out);
    out->blend = in->blend_enable != 0;
    if ((err = blend_factor(in->blend_sfactor, &out->blend_src)) != 0 ||
        (err = blend_factor(in->blend_dfactor, &out->blend_dst)) != 0 ||
        (err = blend_equation(in->blend_equation, &out->blend_equation, &out->blend_signed)) != 0) {
        return err;
    }
    out->blend_color[0] = byte_at(in->blend_color, 16);
    out->blend_color[1] = byte_at(in->blend_color, 8);
    out->blend_color[2] = byte_at(in->blend_color, 0);
    out->blend_color[3] = byte_at(in->blend_color, 24);

    out->depth_test = in->depth_test_enable != 0;
    out->depth_write = in->depth_mask != 0;
    if ((err = compare_func(in->depth_func, &out->depth_func)) != 0) {
        return err;
    }

    out->cull = in->cull_face_enable != 0;
    if (in->cull_face != 0x0404 && in->cull_face != 0x0405 && in->cull_face != 0x0408) {
        return GLSTATE_ERR_CULL;
    }
    out->cull_face = in->cull_face;
    if (in->front_face != GL_CW_ && in->front_face != GL_CCW_) {
        return GLSTATE_ERR_CULL;
    }
    out->front_face = in->front_face == GL_CW_ ? GL_CCW_ : GL_CW_; /* Y invertida */

    out->stencil = in->stencil_test_enable != 0;
    out->stencil_ref = (int)(in->stencil_func_ref & 0xFFu);
    out->stencil_read_mask = in->stencil_func_mask & 0xFFu;
    out->stencil_write_mask = in->stencil_mask & 0xFFu;
    if ((err = compare_func(in->stencil_func, &out->stencil_func)) != 0 ||
        (err = stencil_op(in->stencil_op_fail, &out->stencil_fail)) != 0 ||
        (err = stencil_op(in->stencil_op_zfail, &out->stencil_zfail)) != 0 ||
        (err = stencil_op(in->stencil_op_zpass, &out->stencil_zpass)) != 0) {
        return err;
    }

    out->alpha_test = in->alpha_test_enable != 0;
    out->alpha_ref = (float)(in->alpha_ref & 0xFFu) / 255.0f;
    if ((err = compare_func(in->alpha_func, &out->alpha_func)) != 0) {
        return err;
    }

    out->color_mask[0] = (in->color_mask >> 16) & 1u;
    out->color_mask[1] = (in->color_mask >> 8) & 1u;
    out->color_mask[2] = in->color_mask & 1u;
    out->color_mask[3] = (in->color_mask >> 24) & 1u;

    w = in->surface_clip_h >> 16;
    h = in->surface_clip_v >> 16;
    if (w && h) {
        uint32_t y = in->surface_clip_v & 0xFFFFu;
        out->scissor[0] = (int)(in->surface_clip_h & 0xFFFFu);
        out->scissor[1] = (int)in->surface_height - (int)(y + h);
        out->scissor[2] = (int)w;
        out->scissor[3] = (int)h;
    } else {
        out->scissor[2] = (int)in->surface_width;
        out->scissor[3] = (int)in->surface_height;
    }

    out->fog = in->fog_enable != 0;
    if (in->fog_mode != GL_LINEAR_FOG && (in->fog_mode < 0x0800 || in->fog_mode > 0x0804)) {
        return GLSTATE_ERR_FOG;
    }
    out->fog_mode = in->fog_mode;
    out->fog_color[0] = byte_at(in->fog_color, 0);
    out->fog_color[1] = byte_at(in->fog_color, 8);
    out->fog_color[2] = byte_at(in->fog_color, 16);
    out->fog_color[3] = byte_at(in->fog_color, 24);
    memcpy(out->fog_params, in->fog_params, sizeof out->fog_params);
    return 0;
}

static void enable(GLenum cap, int on) {
    if (on) {
        glEnable(cap);
    } else {
        glDisable(cap);
    }
}

int glstate_apply(const nv2a_state *in, gl_state *applied) {
    gl_state s;
    int err = glstate_translate(in, &s);
    if (err) {
        return err;
    }
    enable(GL_BLEND, s.blend);
    glBlendFunc(s.blend_src, s.blend_dst);
    ngl_glBlendEquation(s.blend_equation);
    ngl_glBlendColor(s.blend_color[0], s.blend_color[1], s.blend_color[2], s.blend_color[3]);
    enable(GL_DEPTH_TEST, s.depth_test);
    glDepthFunc(s.depth_func);
    glDepthMask((GLboolean)s.depth_write);
    enable(GL_CULL_FACE, s.cull);
    glCullFace(s.cull_face);
    glFrontFace(s.front_face);
    enable(GL_STENCIL_TEST, s.stencil);
    glStencilFunc(s.stencil_func, s.stencil_ref, s.stencil_read_mask);
    glStencilMask(s.stencil_write_mask);
    glStencilOp(s.stencil_fail, s.stencil_zfail, s.stencil_zpass);
    glColorMask((GLboolean)s.color_mask[0], (GLboolean)s.color_mask[1], (GLboolean)s.color_mask[2],
                (GLboolean)s.color_mask[3]);
    glEnable(GL_SCISSOR_TEST);
    glScissor(s.scissor[0], s.scissor[1], s.scissor[2], s.scissor[3]);
    if (applied) {
        *applied = s;
    }
    return glGetError() == GL_NO_ERROR ? 0 : GLSTATE_ERR_GL;
}
