/*
 * pgl_track.c: the NV097 state the GL render reads from methods with side effects (program and
 * constant loads, constant attributes). The rest is stored as is in r->m. No GL here, so the
 * tests can drive it with plain method writes.
 */
#include <string.h>

#include "pgraph_gl.h"

/* Transform constants that alias fixed-function context registers. On the NV2A the vertex
 * program constant memory and the transform context are the same storage: SET_VIEWPORT_SCALE
 * lands in c[58] and SET_VIEWPORT_OFFSET in c[59]. D3D reserves them as c[-38] and c[-37], and
 * the epilogue it appends to every vertex program reads them to go to screen space
 * (oPos.xyz = oPos.xyz * c[-38] / oPos.w + c[-37]). */
#define XFCTX_VIEWPORT_SCALE 58
#define XFCTX_VIEWPORT_OFFSET 59

static void set_constant(pgl_regs *r, uint32_t word, uint32_t param) {
    uint32_t old;
    memcpy(&old, &r->constants[word], 4);
    if (old != param)
        r->const_stamp[word / 4] = ++r->stamp;
    memcpy(&r->constants[word], &param, 4);
}

void pgl_regs_track(pgl_regs *r, uint32_t method, uint32_t param) {
    float f;
    memcpy(&f, &param, 4);
    /* D3D resends many registers with the same value (lights, material): only a new value
     * counts as a change for the uniforms. */
    if (r->m[method / 4] != param) {
        r->m[method / 4] = param;
        r->blk_stamp[method / PGL_BLOCK] = ++r->stamp;
    }
    if (method >= 0x0B00 && method < 0x0B80) { /* SET_TRANSFORM_PROGRAM */
        if (r->prog_load < PGL_PROGRAM_SLOTS * 4)
            r->program[r->prog_load++] = param;
    } else if (method == 0x1E9C) { /* SET_TRANSFORM_PROGRAM_LOAD */
        r->prog_load = param < PGL_PROGRAM_SLOTS ? param * 4 : PGL_PROGRAM_SLOTS * 4;
    } else if (method >= 0x0B80 && method < 0x0C00) { /* SET_TRANSFORM_CONSTANT */
        if (r->const_load < PGL_CONSTANTS * 4)
            set_constant(r, r->const_load++, param);
    } else if (method == 0x1EA4) { /* SET_TRANSFORM_CONSTANT_LOAD */
        r->const_load = param < PGL_CONSTANTS ? param * 4 : PGL_CONSTANTS * 4;
    } else if (method >= 0x0A20 && method < 0x0A30) { /* SET_VIEWPORT_OFFSET */
        set_constant(r, XFCTX_VIEWPORT_OFFSET * 4 + (method - 0x0A20) / 4, param);
    } else if (method >= 0x0AF0 && method < 0x0B00) { /* SET_VIEWPORT_SCALE */
        set_constant(r, XFCTX_VIEWPORT_SCALE * 4 + (method - 0x0AF0) / 4, param);
    } else if (method >= 0x1940 && method < 0x1980) { /* SET_VERTEX_DATA4UB: D3DCOLOR */
        float *v = r->vconst[(method - 0x1940) / 4];
        v[0] = (float)((param >> 16) & 0xFF) / 255.0f;
        v[1] = (float)((param >> 8) & 0xFF) / 255.0f;
        v[2] = (float)(param & 0xFF) / 255.0f;
        v[3] = (float)(param >> 24) / 255.0f;
    } else if (method >= 0x1A00 && method < 0x1B00) { /* SET_VERTEX_DATA4F_M */
        r->vconst[(method - 0x1A00) / 16][(method / 4) & 3] = f;
    } else if (method >= 0x1880 && method < 0x1900) { /* SET_VERTEX_DATA2F_M */
        float *v = r->vconst[(method - 0x1880) / 8];
        v[(method / 4) & 1] = f;
        v[2] = 0.0f;
        v[3] = 1.0f;
    } else if (method >= 0x1900 && method < 0x1940) { /* SET_VERTEX_DATA2S */
        float *v = r->vconst[(method - 0x1900) / 4];
        v[0] = (float)(int16_t)(param & 0xFFFF);
        v[1] = (float)(int16_t)(param >> 16);
        v[2] = 0.0f;
        v[3] = 1.0f;
    }
}
