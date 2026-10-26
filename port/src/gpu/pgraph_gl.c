/*
 * pgraph_gl.c: PGRAPH render with OpenGL 4.x (H7c). See pgraph_gl.h.
 *
 * Threads and context. The push buffer kick arrives from the VEH, on the game thread that
 * wrote DMA_PUT, with the NV2A lock held. That thread has a patched TEB (fs:[0x20]
 * and fs:[0x28] belong to the fake KPCR), its own stack, and may be any of the Xbox threads.
 * A GL context can only be current on one thread, and the driver should not run with
 * such a TEB. That is why there is a clean render thread, the only one that calls GL:
 * pgraph_kick hands it the whole kick (decoding included) and waits with a pair of events.
 * It costs two thread switches per kick (about 5 per frame in the menu), in exchange for not
 * having to copy the state into a command queue.
 *
 * Surfaces. Each render target (color or zeta) is a GL texture indexed by its
 * physical address (rcache.c). It is drawn with row 0 at the top, as in Xbox memory:
 * this way a surface can be sampled as a texture with the same coordinates as
 * a texture uploaded from RAM, and the present flips it when copying it to the window.
 *
 * Internal resolution (--scale S, fractional allowed: auto gives 2.25 on a 1080p monitor). Surface
 * textures are S times the size the game programmed and the viewport, scissors and copies follow; everything the game sees stays native: the
 * surfaceSize uniform (the vertex programs output native pixels and the shader turns them into
 * NDC, which does not depend on S), the 1/width of render-to-texture on linear textures (native
 * texel coordinates) and the cache sizes in bytes. Nothing reads a surface back into the
 * guest's RAM, so the game cannot tell.
 *
 * Shaders. Vertex programs (0x0B00 + LOAD/START) are translated with nv2a_vsh_to_glsl and
 * the register combiners with nv2a_psh_to_glsl; in fixed transform mode its own
 * shader with the composite matrix (0x0680) is used. They are cached by the hash of the
 * microcode and of the combiner words.
 */
#include "pgraph_gl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ngb.h"
#include "nv2a.h"
#include "rcache.h"
#include "rdoc.h"
#include "../gfx/present.h"
#include "../gl/glbackend.h"
#include "../gl/glload.h"
#include "../gl/glstate.h"
#include "../gl/texconv.h"
#include "../shaders/psh.h"
#include "../shaders/vsh.h"

#define GL_DEPTH_STENCIL_ 0x84F9
#define GL_STATIC_DRAW_ 0x88E4
#define GL_UNSIGNED_INT_24_8_ 0x84FA
#define GL_MIRRORED_REPEAT_ 0x8370
#define GL_CLAMP_TO_BORDER_ 0x812D
#define GL_TEXTURE_CUBE_MAP_SEAMLESS_ 0x884F
#define GL_TEXTURE_MAX_ANISOTROPY_ 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_ 0x84FF

#define MAX_PROGRAMS 256
#define SHADER_TEXT 65536
#define M(r, method) ((r)->m[(method) / 4])

typedef BOOL(WINAPI *swap_interval_fn)(int);

/* Uniform groups and the methods they come from (ranges [lo, hi) in bytes, up to two).
 * A group is uploaded when any of its method blocks has changed since the last upload
 * to that program: this way a draw that only changes the model-view uploads 16 vec4, not 250. */
enum { UG_COMP, UG_MV, UG_INV, UG_TM, UG_PLANE, UG_LIGHT, UG_MAT, UG_FFI, UG_PSC, UG_BUMP, UG_N };
static const struct {
    uint16_t lo, hi, lo2, hi2;
} k_ug[UG_N] = {
    {0x0680, 0x06C0, 0, 0},           /* composite matrix */
    {0x0480, 0x0580, 0, 0},           /* 4 model-view */
    {0x0580, 0x0680, 0, 0},           /* their inverses */
    {0x06C0, 0x07C0, 0, 0},           /* texture matrices */
    {0x0840, 0x0940, 0, 0},           /* texgen planes */
    {0x1000, 0x1400, 0, 0},           /* 8 lights */
    {0x03A8, 0x03B8, 0x09C0, 0x0A60}, /* material, specular params, scene ambient, eye */
    {0x0280, 0x0440, 0, 0},           /* control, color material, texgen and skinning (ffI) */
    {0x0A60, 0x0AA0, 0, 0},           /* psC0 and psC1 */
    {0x1B00, 0x1C00, 0, 0},           /* bump of the 4 stages */
};

typedef struct gl_prog {
    uint64_t vkey, fkey;
    GLuint prog;
    int failed;
    GLint u_c, u_surface, u_clip, u_ffmat;
    GLint u_ffmv, u_ffinv, u_fftm, u_ffplane, u_ffl, u_ffm, u_ffi; /* fixed pipeline */
    GLint u_psc0, u_psc1, u_fc0, u_fc1, u_fog, u_bump, u_bscale, u_boff;
    GLint u_atest, u_afunc, u_aref, u_texscale, u_tex[4], u_fogcfg, u_fogparam, u_spec;
    uint32_t ug_mask;      /* uniform groups the program uses (UG_* bits) */
    uint64_t ug_up[UG_N];  /* stamp + 1 of each group at its last upload (0 = never) */
    uint64_t c_up;         /* stamp + 1 of the last upload of c[] (0 = never) */
    int c_n;               /* active elements of c[] with consecutive locations (0 = uploaded whole) */
    uint64_t misc_hash;    /* of the rest of the uniforms last uploaded (0 = never) */
} gl_prog;

static struct {
    HANDLE thread, req, done;
    void (*fn)(void *);
    void *arg;
    int init_err;
    volatile LONG active;
    HWND hwnd;
    HDC dc;
    GLuint fbo, read_fbo, blit_fbo, vao, vbo, ebo, copy_tex;
    uint32_t copy_w, copy_h;
    float scale;      /* S: surfaces at S times their native size (--scale; may be fractional) */
    float anisotropy; /* maximum anisotropy for the mipmapped textures (1 = off) */
    GLuint present_prog; /* scaled surface -> window with a box filter (0 = plain blit) */
    GLint u_present_step;
    rc_surfaces surf;
    rc_textures tex;
    uint32_t color_addr, zeta_addr; /* current target (0 = none) */
    rc_surface *color_surf;         /* its cache entry (checked against color_addr before use) */
    uint32_t target_w, target_h;
    int has_color, has_zeta;
    uint32_t present_addr;
    uint32_t frame;
    gl_prog progs[MAX_PROGRAMS];
    int nprogs, last_prog;
    uint8_t *conv, *stage;
    size_t conv_cap, stage_cap;
    uint32_t *idx, *gather;
    size_t idx_cap, gather_cap;
    char vs_text[SHADER_TEXT], fs_text[SHADER_TEXT];
    vsh_program vp;
    swap_interval_fn swap_interval;
    /* statistics */
    uint32_t draws, clears, flips, skipped, prog_errors, tex_errors, state_errors;
    uint32_t surf_created, surf_dropped, tex_uploads, rtt_binds, feedback_copies, ff_draws;
    uint32_t present_other, tex_hash_skipped, trace_n, mip_builds;
    uint32_t epoch; /* increases every frame (scan_written_pages) */
    /* Incremental state: the last thing applied to GL. */
    int state_valid, target_valid;
    uint32_t target_key[8];
    nv2a_state last_state;
    gl_state last_gl;
    GLuint cur_prog;
    char first_error[256];
    /* Pages written by the CPU (arena write watch, ngb_contig_written). */
    int ww_ok;
    uint32_t ww_buf[RC_DIRTY_PAGES];
    rc_dirty dirty;
} R;

static int ring_init(void); /* vertex streaming ring, see upload_vertices */

/* The current color target was drawn into: its mipmaps (bind_textures) must be rebuilt. */
static void color_drawn(void) {
    if (R.color_surf && R.has_color && R.color_surf->used && R.color_surf->addr == R.color_addr)
        R.color_surf->mips_stale = 1;
}

/* A native size or coordinate in the scaled surfaces. A span [a, b) maps to [px(a), px(b)), so
 * neighbouring scissors and viewports still meet exactly at fractional scales. */
static GLint px(uint32_t v) { return (GLint)((float)v * R.scale + 0.5f); }

/* Breakdown of the draw with QPC (pgl_perf_take): where the time of each draw goes. */
enum { PF_TARGET, PF_INDEX, PF_PROG, PF_STATE, PF_TEX, PF_UNIF, PF_GATHER, PF_VERT, PF_DRAW, PF_FLIP, PF_WATCH, PF_N };
static struct {
    LONGLONG t[PF_N];
    uint32_t draws, surf_new, tex_up, tex_hash, verts, state_sets, unif_sets, unif_vec4, vc_hits;
} P;

static LONGLONG pf_now(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

/* Adds to phase `k` the time since `*t0` and sets `*t0` to now. */
static void pf_lap(int k, LONGLONG *t0) {
    LONGLONG now = pf_now();
    P.t[k] += now - *t0;
    *t0 = now;
}

void pgl_perf_take(char *out, size_t outlen, uint32_t frames) {
    static const char *names[PF_N] = {"target", "indices", "program", "state", "textures",
                                      "uniforms", "gather", "vertices", "draw", "flip", "write-watch"};
    LARGE_INTEGER f;
    size_t n;
    int k;
    double per;
    if (!frames)
        frames = 1;
    QueryPerformanceFrequency(&f);
    per = 1000.0 / (double)f.QuadPart / (double)(frames ? frames : 1);
    n = (size_t)snprintf(out, outlen, "perf gl (ms/frame):");
    for (k = 0; k < PF_N && n < outlen; k++)
        n += (size_t)snprintf(out + n, outlen - n, " %s %.2f", names[k], (double)P.t[k] * per);
    if (n < outlen)
        snprintf(out + n, outlen - n,
                 "; per frame: %.0f draws (%.0f with the vertices already on the GPU), %.0f vertices uploaded, %.1f new surfaces, %.1f "
                 "textures uploaded, %.0f texture hashes, %.0f state changes, %.0f uniforms (%.0f vec4)",
                 (double)P.draws / frames, (double)P.vc_hits / frames, (double)P.verts / frames, (double)P.surf_new / frames,
                 (double)P.tex_up / frames, (double)P.tex_hash / frames, (double)P.state_sets / frames,
                 (double)P.unif_sets / frames, (double)P.unif_vec4 / frames);
    memset(&P, 0, sizeof P);
}

static void note_error(const char *fmt, ...) {
    va_list ap;
    if (R.first_error[0])
        return;
    va_start(ap, fmt);
    vsnprintf(R.first_error, sizeof R.first_error, fmt, ap);
    va_end(ap);
    ngb_log("    · GL: %s", R.first_error);
}

static void *grow(void *p, size_t *cap, size_t need) {
    void *q;
    if (need <= *cap)
        return p;
    q = realloc(p, need + need / 2);
    if (!q)
        return NULL;
    *cap = need + need / 2;
    return q;
}

/* ---------------------------------------------------------------- render thread ---- */

/* Present: a triangle that covers the destination rectangle. When the surface is larger than
 * the rectangle (4x on a 1080p screen: 2880x1920 into 1440x1080) a bilinear blit would skip
 * texels and shimmer; four bilinear taps at a quarter of the footprint average it like a box
 * filter (exactly 2x2 texels at a 2:1 ratio). Surface row 0 is the top one, hence the flip. */
static const char k_present_vs[] =
    "#version 400\n"
    "out vec2 uv;\n"
    "void main() {\n"
    "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
    "    uv = vec2(p.x, 1.0 - p.y);\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";
static const char k_present_fs[] =
    "#version 400\n"
    "in vec2 uv;\n"
    "uniform sampler2D src;\n"
    "uniform vec2 tap;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "    color = 0.25 * (texture(src, uv - tap) + texture(src, uv + tap) +\n"
    "                    texture(src, uv + vec2(tap.x, -tap.y)) + texture(src, uv + vec2(-tap.x, tap.y)));\n"
    "    color.a = 1.0;\n"
    "}\n";

static int setup_objects(void) {
    ngl_glGenFramebuffers(1, &R.fbo);
    ngl_glGenFramebuffers(1, &R.read_fbo);
    ngl_glGenFramebuffers(1, &R.blit_fbo);
    ngl_glGenVertexArrays(1, &R.vao);
    ngl_glGenBuffers(1, &R.vbo);
    ngl_glGenBuffers(1, &R.ebo);
    ngb_log("pgraph gl: vertex streaming %s", ring_init() ? "through a persistent mapped ring (GL 4.4)"
                                                          : "with glBufferData (no GL 4.4 buffer storage)");
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS_);
    glPointSize(R.scale); /* points cover the same area of the picture at any scale */
    /* 16x anisotropic filtering (core in GL 4.6; without it the query fails and it stays off). */
    R.anisotropy = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_, &R.anisotropy);
    if (glGetError() != GL_NO_ERROR || R.anisotropy < 1.0f)
        R.anisotropy = 1.0f;
    if (R.anisotropy > 16.0f)
        R.anisotropy = 16.0f;
    {
        char log[512];
        unsigned prog = 0;
        if (!glb_compile(k_present_vs, k_present_fs, &prog, log, sizeof log)) {
            R.present_prog = prog;
            R.u_present_step = ngl_glGetUniformLocation(prog, "tap");
            ngl_glUseProgram(prog);
            ngl_glUniform1i(ngl_glGetUniformLocation(prog, "src"), 0);
            ngl_glUseProgram(0);
        } else
            ngb_log("pgraph gl: present shader failed, using a plain blit: %s", log);
    }
    R.ww_ok = ngb_contig_written(R.ww_buf, RC_DIRTY_PAGES) != (size_t)-1;
    R.swap_interval = (swap_interval_fn)(void *)wglGetProcAddress("wglSwapIntervalEXT");
    if (R.swap_interval)
        R.swap_interval(0); /* the pace is set by the FLIP_STALL pacing, not by vsync */
    return glGetError() == GL_NO_ERROR ? 0 : GLB_ERR_GL;
}

static DWORD WINAPI render_main(LPVOID param) {
    int err;
    (void)param;
    err = glb_init(R.hwnd, 720, 480);
    if (!err) {
        R.dc = glb_get_dc();
        err = setup_objects();
    }
    R.init_err = err;
    SetEvent(R.done);
    if (err)
        return 1;
    for (;;) {
        WaitForSingleObject(R.req, INFINITE);
        R.fn(R.arg);
        SetEvent(R.done);
    }
}

int pgl_start(HWND hwnd, char *why, size_t whylen) {
    const glb_info *info;
    if (!hwnd) {
        snprintf(why, whylen, "no window");
        return GLB_ERR_ARGS;
    }
    R.hwnd = hwnd;
    R.scale = g_ngb_opts.scale >= 1.0f && g_ngb_opts.scale <= 4.0f ? g_ngb_opts.scale : 1.0f;
    if (g_ngb_opts.scale_auto) {
        /* At least as many pixels as the picture covers on the monitor, on both axes: 4:3 on
         * 1080p is 1440x1080, so 1080 / 480 = 2.25 (1620x1080); 16:9 is 1920x1080, so
         * 1920 / 720 = 2.67 (1920x1280). */
        MONITORINFO mi;
        memset(&mi, 0, sizeof mi);
        mi.cbSize = sizeof mi;
        if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            float a = g_ngb_opts.widescreen ? 16.0f / 9.0f : 4.0f / 3.0f;
            float mw = (float)(mi.rcMonitor.right - mi.rcMonitor.left);
            float mh = (float)(mi.rcMonitor.bottom - mi.rcMonitor.top);
            float pw = mh * a < mw ? mh * a : mw, ph = pw / a;
            R.scale = pw / 720.0f > ph / 480.0f ? pw / 720.0f : ph / 480.0f;
        }
        if (R.scale < 1.0f)
            R.scale = 1.0f;
        if (R.scale > 4.0f)
            R.scale = 4.0f;
    }
    ngb_log("pgraph gl: internal resolution %dx%d (scale %.2f%s)", px(720), px(480), (double)R.scale,
            g_ngb_opts.scale_auto ? ", matching the monitor" : "");
    rdoc_load(g_ngb_opts.rdoc_frame, g_ngb_opts.rdoc_key); /* before creating the GL context */
    R.req = CreateEventA(NULL, FALSE, FALSE, NULL);
    R.done = CreateEventA(NULL, FALSE, FALSE, NULL);
    R.thread = R.req && R.done ? CreateThread(NULL, 1 << 20, render_main, NULL, 0, NULL) : NULL;
    if (!R.thread) {
        snprintf(why, whylen, "could not create the render thread");
        return GLB_ERR_ARGS;
    }
    ngb_pin_hardware_thread(R.thread);
    ngb_prof_thread(R.thread, "render");
    WaitForSingleObject(R.done, INFINITE);
    if (R.init_err) {
        snprintf(why, whylen, "%s", glb_error_text(R.init_err));
        return R.init_err;
    }
    info = glb_get_info();
    snprintf(why, whylen, "%s / %s / GL %s", info ? info->vendor : "?",
             info ? info->renderer : "?", info ? info->version : "?");
    InterlockedExchange(&R.active, 1);
    return 0;
}

int pgl_active(void) { return R.active != 0; }

void pgl_call(void (*fn)(void *), void *arg) {
    R.fn = fn;
    R.arg = arg;
    SetEvent(R.req);
    WaitForSingleObject(R.done, INFINITE);
}

void pgl_post(void (*fn)(void *), void *arg) {
    R.fn = fn;
    R.arg = arg;
    SetEvent(R.req); /* R.done is left signaled on return and nobody waits for it */
}

/* ---------------------------------------------------------------- state ---- */

void pgl_regs_defaults(pgl_regs *r) {
    nv2a_state s;
    int i;
    nv2a_state_defaults(&s, 720, 480);
    M(r, 0x0344) = s.blend_sfactor;
    M(r, 0x0348) = s.blend_dfactor;
    M(r, 0x0350) = s.blend_equation;
    M(r, 0x0354) = s.depth_func;
    M(r, 0x035C) = s.depth_mask;
    M(r, 0x039C) = s.cull_face;
    M(r, 0x03A0) = s.front_face;
    M(r, 0x0360) = s.stencil_mask;
    M(r, 0x0364) = s.stencil_func;
    M(r, 0x036C) = s.stencil_func_mask;
    M(r, 0x0370) = s.stencil_op_fail;
    M(r, 0x0374) = s.stencil_op_zfail;
    M(r, 0x0378) = s.stencil_op_zpass;
    M(r, 0x033C) = s.alpha_func;
    M(r, 0x0358) = s.color_mask;
    M(r, 0x029C) = s.fog_mode;
    for (i = 0; i < 16; i++) {
        r->vconst[i][0] = r->vconst[i][1] = r->vconst[i][2] = 0.0f;
        r->vconst[i][3] = 1.0f;
    }
    r->vconst[3][0] = r->vconst[3][1] = r->vconst[3][2] = 1.0f; /* white diffuse */
}

static float as_float(uint32_t w) {
    float f;
    memcpy(&f, &w, 4);
    return f;
}

static void argb_to_vec4(uint32_t c, float *out) {
    out[0] = (float)((c >> 16) & 0xFF) / 255.0f;
    out[1] = (float)((c >> 8) & 0xFF) / 255.0f;
    out[2] = (float)(c & 0xFF) / 255.0f;
    out[3] = (float)(c >> 24) / 255.0f;
}

static void state_from_regs(const pgl_regs *r, nv2a_state *s) {
    nv2a_state_defaults(s, R.target_w, R.target_h);
    s->blend_enable = M(r, 0x0304);
    s->blend_sfactor = M(r, 0x0344);
    s->blend_dfactor = M(r, 0x0348);
    s->blend_color = M(r, 0x034C);
    s->blend_equation = M(r, 0x0350);
    s->depth_test_enable = R.has_zeta ? M(r, 0x030C) : 0;
    s->depth_func = M(r, 0x0354);
    s->depth_mask = R.has_zeta ? M(r, 0x035C) : 0;
    s->cull_face_enable = M(r, 0x0308);
    s->cull_face = M(r, 0x039C);
    s->front_face = M(r, 0x03A0);
    s->stencil_test_enable = R.has_zeta ? M(r, 0x032C) : 0;
    s->stencil_mask = M(r, 0x0360);
    s->stencil_func = M(r, 0x0364);
    s->stencil_func_ref = M(r, 0x0368);
    s->stencil_func_mask = M(r, 0x036C);
    s->stencil_op_fail = M(r, 0x0370);
    s->stencil_op_zfail = M(r, 0x0374);
    s->stencil_op_zpass = M(r, 0x0378);
    s->alpha_test_enable = M(r, 0x0300);
    s->alpha_func = M(r, 0x033C);
    s->alpha_ref = M(r, 0x0340);
    s->color_mask = M(r, 0x0358);
    s->surface_clip_h = M(r, 0x0200);
    s->surface_clip_v = M(r, 0x0204);
    s->fog_enable = M(r, 0x02A4);
    s->fog_mode = M(r, 0x029C);
    s->fog_gen_mode = M(r, 0x02A0);
    s->fog_color = M(r, 0x02A8);
    s->fog_params[0] = as_float(M(r, 0x09C0));
    s->fog_params[1] = as_float(M(r, 0x09C4));
    s->fog_params[2] = as_float(M(r, 0x09C8));
    s->poly_offset_fill = M(r, 0x0338);
    s->poly_offset_factor = as_float(M(r, 0x0384));
    s->poly_offset_bias = as_float(M(r, 0x0388));
    s->zclip_min = as_float(M(r, 0x0394));
    s->zclip_max = as_float(M(r, 0x0398));
}

/* Applies the fixed state. Returns what was applied (for the alpha test uniforms). */
static void apply_state(const pgl_regs *r, gl_state *out) {
    nv2a_state s;
    uint32_t ch = M(r, 0x0200), cv = M(r, 0x0204);
    int err;
    state_from_regs(r, &s);
    if (R.state_valid && !memcmp(&s, &R.last_state, sizeof s)) {
        *out = R.last_gl; /* nothing has changed since the previous draw */
        return;
    }
    P.state_sets++;
    err = glstate_apply(&s, out);
    if (err) {
        R.state_errors++;
        note_error("NV097 state not translatable (%s): blend %X/%X eq %X depth %X", glstate_error_text(err),
                   s.blend_sfactor, s.blend_dfactor, s.blend_equation, s.depth_func);
        nv2a_state_defaults(&s, R.target_w, R.target_h);
        glstate_apply(&s, out);
    }
    /* glstate flips the scissor because it assumes OpenGL's Y; here row 0 is the top one,
     * as on the Xbox, so it goes as is. The front face does stay inverted (see
     * glstate.c). */
    glEnable(GL_SCISSOR_TEST);
    if (ch >> 16 && cv >> 16)
        glScissor(px(ch & 0xFFFF), px(cv & 0xFFFF), px((ch & 0xFFFF) + (ch >> 16)) - px(ch & 0xFFFF),
                  px((cv & 0xFFFF) + (cv >> 16)) - px(cv & 0xFFFF));
    else
        glScissor(0, 0, px(R.target_w), px(R.target_h));
    R.last_state = s;
    R.last_gl = *out;
    R.state_valid = 1;
}

/* ---------------------------------------------------------------- surfaces ---- */

static void delete_textures(const unsigned *dead, int n) {
    int i;
    for (i = 0; i < n && i < 16; i++) {
        GLuint t = dead[i];
        glDeleteTextures(1, &t);
    }
}

static int color_bpp(uint32_t cf) {
    return (cf == 1 || cf == 2 || cf == 3) ? 2 : cf == 9 ? 1 : 4;
}

static uint64_t surface_sig(const rc_surface *s);

/* 1 if the CPU may have written to [phys, phys + len) after frame `since` (without write watch,
 * or outside the physical RAM, always 1: the content must be looked at). */
static int cpu_wrote(uint32_t phys, uint32_t len, uint32_t since) {
    return !R.ww_ok || rc_dirty_last(&R.dirty, phys, len) > since;
}

/* End of frame: marks with a new epoch the pages the CPU wrote since the previous one.
 * The caches store the epoch at which they checked each object. It is done once per frame
 * (about 50 µs: GetWriteWatch walks the 16 k pages of the arena; on every kick it cost
 * 1.7 ms per frame), so a CPU write is seen on the next frame: the
 * textures the CPU rewrites every frame (video) lag one frame behind, and the
 * vertices that get rewritten are marked volatile and always uploaded. */
static void scan_written_pages(void) {
    LONGLONG t0 = pf_now();
    size_t n = ngb_contig_written(R.ww_buf, RC_DIRTY_PAGES);
    if (n == (size_t)-1) {
        R.ww_ok = 0;
        return;
    }
    R.epoch++;
    rc_dirty_mark(&R.dirty, R.ww_buf, n < RC_DIRTY_PAGES ? n : RC_DIRTY_PAGES, R.epoch);
    pf_lap(PF_WATCH, &t0);
}

void pgl_kick_begin(void) {
    /* Reserved: the write watch is collected at the flip (see scan_written_pages). */
}

/* Uploads a native-size picture from RAM into the scaled texture `t` (the upload goes to a
 * temporary texture and a blit stretches it, nearest so that it stays the same picture). */
static void upload_scaled(GLuint t, const rc_surface *s, const uint8_t *src) {
    GLuint tmp;
    GLint sw = (GLint)s->width, sh = (GLint)s->height;
    glGenTextures(1, &tmp);
    glBindTexture(GL_TEXTURE_2D, tmp);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sw, sh, 0, GL_BGRA, GL_UNSIGNED_BYTE, src);
    ngl_glBindFramebuffer(GL_READ_FRAMEBUFFER, R.read_fbo);
    ngl_glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tmp, 0);
    ngl_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, R.blit_fbo);
    ngl_glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
    glDisable(GL_SCISSOR_TEST); /* the blit obeys the scissor */
    R.state_valid = 0;
    ngl_glBlitFramebuffer(0, 0, sw, sh, 0, 0, px(s->width), px(s->height), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    ngl_glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    ngl_glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    ngl_glBindFramebuffer(GL_FRAMEBUFFER, R.fbo);
    glDeleteTextures(1, &tmp);
    glBindTexture(GL_TEXTURE_2D, t);
}

/* Creates a surface's texture, at S times its size. If it is a 32-bit linear color one, it is
 * initialized with whatever is in RAM (the game may have written it with the CPU). */
static void create_surface_texture(rc_surface *s, uint32_t pitch, int swizzled) {
    GLuint t;
    GLsizei w = px(s->width), h = px(s->height);
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    if (s->kind == RC_ZETA) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0, GL_DEPTH_STENCIL_, GL_UNSIGNED_INT_24_8_,
                     NULL);
    } else {
        const uint8_t *src = NULL;
        if (!swizzled && color_bpp(s->format) == 4 && pitch >= s->width * 4)
            src = nv2a_phys(s->addr, pitch * s->height);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(0x0CF2 /* GL_UNPACK_ROW_LENGTH */, src ? (GLint)(pitch / 4) : 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE,
                     R.scale == 1.0f ? src : NULL);
        if (src && R.scale != 1.0f)
            upload_scaled(t, s, src);
        glPixelStorei(0x0CF2, 0);
    }
    s->gl_tex = t;
    s->mem_sig = surface_sig(s);
    s->mips_stale = 1;
    R.surf_created++;
    P.surf_new++;
}

static rc_surface *get_surface(int kind, uint32_t addr, uint32_t pitch, uint32_t w, uint32_t h,
                               uint32_t format, int swizzled) {
    unsigned dead[16];
    int result, n_dead = 0;
    rc_surface *s = rc_surface_get(&R.surf, kind, addr, pitch, w, h, format, &result, dead, 16, &n_dead);
    delete_textures(dead, n_dead);
    if (s && !s->gl_tex)
        create_surface_texture(s, pitch, swizzled);
    return s;
}

/* Signature of a surface's RAM (see rc_surface.mem_sig). */
static uint64_t surface_sig(const rc_surface *s) {
    const uint8_t *p = nv2a_phys(s->addr, s->bytes);
    return p ? rc_sparse_hash(p, s->bytes) : 0;
}

/* Prepares the FBO with the current color and zeta surfaces. 0 if there is no target.
 * Only what will be used is attached (like xemu): D3D leaves a zeta programmed at
 * address 0 in passes without depth and a color at 0 in shadows (color mask
 * at 0); attaching them made them overwrite each other and recreated two textures per frame. */
static int bind_target(const pgl_regs *r, int need_color, int need_zeta) {
    uint32_t ch = M(r, 0x0200), cv = M(r, 0x0204), fmt = M(r, 0x0208), pitch = M(r, 0x020C);
    uint32_t w = (ch >> 16) + (ch & 0xFFFF), h = (cv >> 16) + (cv & 0xFFFF);
    uint32_t cf = need_color ? fmt & 0xF : 0, zf = need_zeta ? (fmt >> 4) & 0xF : 0, base, caddr = 0, zaddr = 0;
    uint32_t key[8];
    int swz = ((fmt >> 8) & 0xF) == 2;
    rc_surface *c = NULL, *z = NULL;
    if (!(ch >> 16) || !(cv >> 16) || w > 4096 || h > 4096)
        return 0;
    /* Same target as the previous draw: the FBO is already ready. */
    key[0] = ch;
    key[1] = cv;
    key[2] = fmt;
    key[3] = pitch;
    key[4] = M(r, 0x0210);
    key[5] = M(r, 0x0214);
    key[6] = M(r, 0x0194);
    key[7] = (M(r, 0x0198) & ~3u) | (uint32_t)(need_color ? 1 : 0) | (uint32_t)(need_zeta ? 2 : 0);
    if (R.target_valid && !memcmp(key, R.target_key, sizeof key))
        return R.has_color || R.has_zeta;
    memcpy(R.target_key, key, sizeof key);
    R.target_valid = 1;
    R.state_valid = 0; /* the scissor and the viewport depend on the target */
    if (cf && pgraph_dma_base(0x194, &base)) {
        caddr = base + M(r, 0x0210);
        c = get_surface(RC_COLOR, caddr, pitch & 0xFFFF, w, h, cf, swz);
        if (c && caddr != R.color_addr)
            c->mem_sig = surface_sig(c);
        if (c)
            c->drawn_frame = R.epoch;
    }
    if (zf && pgraph_dma_base(0x198, &base)) {
        zaddr = base + M(r, 0x0214);
        z = get_surface(RC_ZETA, zaddr, pitch >> 16, w, h, zf, swz);
        /* Requesting the zeta may have recycled (LRU) or invalidated (overlap) the color entry
         * (issue #43): it is revalidated; if the zeta cannot coexist with it, it goes without zeta. */
        if (c && (!c->used || c->kind != RC_COLOR || c->addr != caddr)) {
            c = get_surface(RC_COLOR, caddr, pitch & 0xFFFF, w, h, cf, swz);
            if (c)
                c->mem_sig = surface_sig(c);
            if (z && (!z->used || z->kind != RC_ZETA || z->addr != zaddr))
                z = NULL;
        }
    }
    if (!c && !z) {
        R.has_color = R.has_zeta = 0;
        return 0;
    }
    ngl_glBindFramebuffer(GL_FRAMEBUFFER, R.fbo);
    ngl_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c ? c->gl_tex : 0, 0);
    ngl_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                               z ? z->gl_tex : 0, 0);
    glDrawBuffer(c ? GL_COLOR_ATTACHMENT0 : GL_NONE);
    R.color_addr = c ? caddr : 0;
    R.color_surf = c;
    R.zeta_addr = z ? zaddr : 0;
    R.has_color = c != NULL;
    R.has_zeta = z != NULL;
    R.target_w = w; /* native: they feed surfaceSize and the scissor */
    R.target_h = h;
    glViewport(0, 0, px(w), px(h));
    if (c && (ch >> 16) == 720)
        R.present_addr = caddr; /* the buffer that will end up on screen (like the rasterizer) */
    return 1;
}

/* ---------------------------------------------------------------- textures ---- */

static GLint wrap_mode(uint32_t v) {
    switch (v) {
    case 2: return GL_MIRRORED_REPEAT_;
    case 3: case 5: return GL_CLAMP_TO_EDGE;
    case 4: return GL_CLAMP_TO_BORDER_;
    default: return GL_REPEAT;
    }
}

static GLint min_filter(uint32_t v) {
    switch (v) {
    case 1: return GL_NEAREST;
    case 3: return GL_NEAREST_MIPMAP_NEAREST;
    case 4: return GL_LINEAR_MIPMAP_NEAREST;
    case 5: return GL_NEAREST_MIPMAP_LINEAR;
    case 6: return GL_LINEAR_MIPMAP_LINEAR;
    default: return GL_LINEAR;
    }
}

static void upload_levels(GLuint t, const texconv_desc *d, const uint8_t *data) {
    GLenum target = d->faces == TEXCONV_CUBE_FACES ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    int face, k;
    glBindTexture(target, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (face = 0; face < d->faces; face++) {
        GLenum image = target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face
                                                     : GL_TEXTURE_2D;
        for (k = 0; k < d->levels; k++) {
            const texconv_level *lv = &d->level[k];
            const uint8_t *px = data + d->face_dst_stride * (size_t)face + lv->dst_offset;
            if (d->upload_compressed)
                ngl_glCompressedTexImage2D(image, k, d->gl_internal, lv->width, lv->height, 0,
                                           (GLsizei)lv->dst_size, px);
            else
                glTexImage2D(image, k, (GLint)d->gl_internal, lv->width, lv->height, 0, d->gl_format,
                             d->gl_type, px);
        }
    }
    glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, d->levels - 1);
    /* Only with mipmaps: there it sharpens floors and walls seen at an angle; on a single level
     * (HUD, fonts, video) it would only blur. */
    if (d->levels > 1 && R.anisotropy > 1.0f)
        glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_, R.anisotropy);
}

/* Copies the current color surface to a separate texture: the game is reading it as a
 * texture while drawing into it, and GL does not allow that loop. */
static GLuint feedback_copy(rc_surface *s) {
    GLsizei w = px(s->width), h = px(s->height);
    if (!R.copy_tex || R.copy_w != s->width || R.copy_h != s->height) {
        if (!R.copy_tex)
            glGenTextures(1, &R.copy_tex);
        glBindTexture(GL_TEXTURE_2D, R.copy_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        R.copy_w = s->width;
        R.copy_h = s->height;
    }
    glBindTexture(GL_TEXTURE_2D, R.copy_tex);
    ngl_glBindFramebuffer(GL_READ_FRAMEBUFFER, R.fbo);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
    R.feedback_copies++;
    return R.copy_tex;
}

/* Texture of stage `i`: a surface (render-to-texture) or the texture cache. */
static GLuint stage_texture(const pgl_regs *r, int i, GLenum *target, float scale[2], rc_surface **surf) {
    uint32_t mb = 0x1B00u + 0x40u * (uint32_t)i, fmt = M(r, mb + 4), base, phys, pal = M(r, mb + 0x20);
    uint32_t pal_phys = 0;
    texconv_desc d;
    rc_texkey key;
    rc_texture *t;
    rc_surface *s;
    const uint8_t *src, *palette = NULL;
    size_t pal_bytes = 0;
    unsigned dead;
    int derr, action, linear;
    uint64_t hash;
    *target = GL_TEXTURE_2D;
    scale[0] = scale[1] = 1.0f;
    *surf = NULL;
    if (!pgraph_dma_base((fmt & 3) == 2 ? 0x188 : 0x184, &base))
        return 0;
    phys = base + M(r, mb);
    derr = texconv_describe(fmt, M(r, mb + 0x10), M(r, mb + 0x1C), glb_has_s3tc(), &d);
    {
        uint32_t c = (fmt >> 8) & 0xFFu;
        /* Depth texture (shadow map): the Z surface the game drew earlier
           in this frame is already a GL texture; there is nothing to convert from memory. */
        s = rc_surface_find(&R.surf, c >= 0x2A && c <= 0x31 ? RC_ZETA : RC_COLOR, phys);
        /* texconv does not describe depth formats; 0x2E-0x31 (LIN_D24S8, LIN_F24S8, LIN_D16,
           LIN_F16) are linear, addressed in texels like any other linear texture. */
        linear = derr ? c >= 0x2E && c <= 0x31 : d.linear;
    }
    if (s && s->gl_tex && !(R.has_color && s->addr == R.color_addr) &&
        cpu_wrote(s->addr, s->bytes, s->drawn_frame) && surface_sig(s) != s->mem_sig) {
        /* The CPU wrote something else into the surface's memory (a texture loaded
         * where there used to be a render target): the surface is no longer valid. */
        GLuint g = rc_surface_drop(s);
        glDeleteTextures(1, &g);
        R.target_valid = 0;
        R.surf_dropped++;
        s = NULL;
    }
    if (s && s->gl_tex) {
        R.rtt_binds++;
        if (linear) {
            scale[0] = 1.0f / (float)s->width;
            scale[1] = 1.0f / (float)s->height;
        }
        if (R.has_color && phys == R.color_addr)
            return feedback_copy(s);
        *surf = s;
        return s->gl_tex;
    }
    if (derr) {
        R.tex_errors++;
        note_error("texture of stage %d not convertible: format 0x%08X (%s)", i, fmt,
                   texconv_error_text(derr));
        return 0;
    }
    if (d.linear) {
        scale[0] = 1.0f / (float)d.width;
        scale[1] = 1.0f / (float)d.height;
    }
    if (d.faces == TEXCONV_CUBE_FACES)
        *target = GL_TEXTURE_CUBE_MAP;
    memset(&key, 0, sizeof key);
    key.addr = phys;
    key.format = fmt;
    key.control1 = d.linear ? M(r, mb + 0x10) : 0;
    key.rect = d.linear ? M(r, mb + 0x1C) : 0;
    key.palette = d.kind == TEXCONV_PALETTE ? pal : 0;
    t = rc_texture_get(&R.tex, &key, R.epoch, &action, &dead);
    if (dead) {
        GLuint g = dead;
        glDeleteTextures(1, &g);
    }
    if (!t)
        return 0;
    if (action == RC_TEX_USE)
        return t->gl_tex;
    if (d.kind == TEXCONV_PALETTE && !pgraph_dma_base((pal & 3) == 2 ? 0x188 : 0x184, &pal_phys))
        pal_phys = 0xFFFFFFFFu; /* no base: cpu_wrote will say yes */
    if (action == RC_TEX_CHECK && !cpu_wrote(phys, (uint32_t)d.src_size, t->checked_frame) &&
        (d.kind != TEXCONV_PALETTE || !cpu_wrote(pal_phys + (pal & ~0x3Fu), 1024, t->checked_frame))) {
        /* No page of the texture (nor of the palette) has been written since it was checked:
         * there is no need to hash it. */
        rc_texture_checked(&R.tex, t, t->hash, R.epoch);
        R.tex_hash_skipped++;
        return t->gl_tex;
    }
    src = nv2a_phys(phys, (uint32_t)d.src_size);
    if (!src)
        return t->gl_tex;
    if (d.kind == TEXCONV_PALETTE) {
        uint32_t pbase;
        int n = texconv_palette_entries(pal);
        if (n > 0 && pgraph_dma_base((pal & 3) == 2 ? 0x188 : 0x184, &pbase)) {
            pal_bytes = (size_t)n * 4;
            palette = nv2a_phys(pbase + (pal & ~0x3Fu), (uint32_t)pal_bytes);
            if (!palette)
                pal_bytes = 0;
        }
    }
    hash = rc_hash(src, d.src_size, palette ? rc_hash(palette, pal_bytes, 1) : 0);
    P.tex_hash++;
    if (action == RC_TEX_CHECK && !rc_texture_checked(&R.tex, t, hash, R.epoch))
        return t->gl_tex;
    R.conv = (uint8_t *)grow(R.conv, &R.conv_cap, d.dst_size);
    if (!R.conv || texconv_convert(src, d.src_size, palette, pal_bytes, &d, R.conv, d.dst_size)) {
        R.tex_errors++;
        return t->gl_tex;
    }
    if (!t->gl_tex) {
        GLuint g;
        glGenTextures(1, &g);
        t->gl_tex = g;
    }
    upload_levels(t->gl_tex, &d, R.conv);
    rc_texture_uploaded(t, hash, R.epoch);
    R.tex_uploads++;
    P.tex_up++;
    return t->gl_tex;
}

static void bind_textures(const pgl_regs *r, float tex_scale[8]) {
    uint32_t modes = M(r, 0x1E70);
    int i;
    for (i = 0; i < 4; i++) {
        uint32_t mb = 0x1B00u + 0x40u * (uint32_t)i, mode = (modes >> (5 * i)) & 0x1F;
        uint32_t addr = M(r, mb + 0x08), filter = M(r, mb + 0x14);
        GLenum target;
        GLuint t = 0;
        GLint minf;
        rc_surface *surf;
        tex_scale[2 * i] = tex_scale[2 * i + 1] = 1.0f;
        ngl_glActiveTexture(GL_TEXTURE0 + (GLenum)i);
        if (!(M(r, mb + 0x0C) & 0x40000000u) || mode == 0 || mode == 4 || mode == 5) {
            glBindTexture(GL_TEXTURE_2D, 0);
            continue;
        }
        t = stage_texture(r, i, &target, &tex_scale[2 * i], &surf);
        glBindTexture(target, t);
        if (!t)
            continue;
        glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap_mode(addr & 0xF));
        glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap_mode((addr >> 8) & 0xF));
        if ((addr & 0xF) == 4 || ((addr >> 8) & 0xF) == 4) {
            /* SET_TEXTURE_BORDER_COLOR (ARGB). A shadow map in BORDER reads its red as the
             * depth outside the map: white keeps everything beyond the light's frustum lit. */
            uint32_t bc = M(r, mb + 0x24);
            GLfloat c[4];
            c[0] = (float)((bc >> 16) & 0xFF) / 255.0f;
            c[1] = (float)((bc >> 8) & 0xFF) / 255.0f;
            c[2] = (float)(bc & 0xFF) / 255.0f;
            c[3] = (float)(bc >> 24) / 255.0f;
            glTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, c);
        }
        minf = min_filter((filter >> 16) & 0xFF);
        if (surf && surf->kind == RC_COLOR && minf != GL_NEAREST && minf != GL_NEAREST_MIPMAP_NEAREST) {
            /* A rendered color surface read smaller than it was drawn (the bloom of the game
             * shrinks the 720x480 scene to 256x256 with one bilinear tap per pixel): mipmaps,
             * so the sample averages its whole footprint. Without them, thin bright details
             * fell between the taps on some frames and not on others, and the game's sparse
             * blur turned that into shimmering ghosts around the characters; much worse at an
             * internal resolution of 2-4x, where the tap skips 7 texels instead of 3. Read at
             * its own size the level is 0, the same picture as before. */
            if (surf->mips_stale) {
                glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 1000);
                ngl_glGenerateMipmap(target);
                surf->mips_stale = 0;
                R.mip_builds++;
            }
            minf = GL_LINEAR_MIPMAP_LINEAR;
        }
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, minf);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, ((filter >> 24) & 0xF) == 1 ? GL_NEAREST : GL_LINEAR);
    }
    ngl_glActiveTexture(GL_TEXTURE0);
}

/* ---------------------------------------------------------------- shaders ---- */

/* NV2A fog (like xemu, glsl/vsh.c): the hardware converts the vertex fog distance
 * (oFog.x of a vertex program, or the one the fixed mode generates) into the factor read by the
 * final combiner, according to SET_FOG_MODE and SET_FOG_PARAMS. Without this oFog was 0 and, with
 * fog enabled, everything came out in the fog color (black in the first level). */
#define FOG_GLSL \
    "uniform ivec3 fogCfg;\nuniform vec2 fogParam;\n" \
    "float ngbFog(float d) {\n" \
    "    if (fogCfg.x == 0) return 1.0;\n" \
    "    if (isinf(d) || isnan(d)) d = 0.0;\n" \
    "    float f;\n" \
    "    if (fogCfg.y == 0x800 || fogCfg.y == 0x802)\n" \
    "        f = fogParam.x + exp2(d * fogParam.y * 16.0) - 1.5;\n" \
    "    else if (fogCfg.y == 0x801 || fogCfg.y == 0x803)\n" \
    "        f = fogParam.x + exp2(-d * d * fogParam.y * fogParam.y * 32.0) - 1.5;\n" \
    "    else\n" \
    "        f = fogParam.x + d * fogParam.y - 1.0;\n" \
    "    if (fogCfg.y >= 0x802 && fogCfg.y <= 0x804) f = abs(f);\n" \
    "    return f;\n" \
    "}\n"

/*
 * NV2A fixed pipeline (D3D transform & lighting without a vertex program). Registers
 * (xemu's nv2a_regs.h as reference; own code):
 *   - position: composite matrix 0x0680 (already includes projection and viewport); note: model-view
 *     0x0480, normals with the inverse model-view 0x0580;
 *   - skinning (SET_SKIN_MODE 0x0328: 1/2 = 2 matrices, 3/4 = 3, 5/6 = 4; the odd ones compute
 *     the last weight): model-view 0x0480 + m·0x40 and inverses 0x0580 + m·0x40 weighted with
 *     the weights of attribute 1; the position comes from the sum in eye space through the composite matrix, which
 *     with skinning D3D leaves as projection and viewport only (0x0440 stays at zero);
 *   - lights 0..7 at 0x1000 + i·0x80 (ambient, diffuse, specular, range, half vector, infinite
 *     direction, local position, attenuation) and the mode of each at 0x03BC (2 bits: 1
 *     infinite, 2 local, 3 spot, treated as local);
 *   - material: scene ambient 0x0A10, emission 0x03A8, alpha 0x03B4, color material 0x0298
 *     (2 bits per term: emission, ambient, diffuse, specular; 0 material, 1 vertex
 *     diffuse, 2 vertex specular); specular enabled 0x03B8;
 *   - texgen S/T/R/Q per stage at 0x03C0 + e·0x10 and their planes at 0x0840 + e·0x40; texture
 *     matrices at 0x06C0 + e·0x40, enabled at 0x0420 + e·4.
 * D3D already multiplies the material into the light colors; with color material it is
 * multiplied by the vertex color. Approximations: fixed specular power (16) instead
 * of SET_SPECULAR_PARAMS, spot lights without a cone and sphere map in OpenGL convention.
 */
static const char k_ff_vs[] =
    "#version 400\n"
    "layout(location = 0) in vec4 v0;\nlayout(location = 1) in vec4 v1;\nlayout(location = 2) in vec4 v2;\n"
    "layout(location = 3) in vec4 v3;\nlayout(location = 4) in vec4 v4;\n"
    "layout(location = 9) in vec4 v9;\nlayout(location = 10) in vec4 v10;\n"
    "layout(location = 11) in vec4 v11;\nlayout(location = 12) in vec4 v12;\n"
    "layout(location = 5) in vec4 v5;\n"
    "uniform vec4 ffMat[4];\nuniform vec2 surfaceSize;\nuniform vec2 clipRange;\n"
    "uniform vec4 ffMV[16];\nuniform vec4 ffInv[16];\nuniform vec4 ffTM[16];\n"
    "uniform vec4 ffPlane[16];\n"
    "uniform vec4 ffL[64];\nuniform vec4 ffM[4];\nuniform int ffI[32];\n"
    FOG_GLSL
    "out vec4 oD0;\nout vec4 oD1;\nout vec4 oB0;\nout vec4 oB1;\nout vec4 oFog;\nout vec4 oPts;\n"
    "out vec4 oT0;\nout vec4 oT1;\nout vec4 oT2;\nout vec4 oT3;\n"
    "vec3 cmSource(int sel, vec3 mat) { return sel == 1 ? v3.rgb : sel == 2 ? v4.rgb : mat; }\n"
    "vec4 mvMul(int m, vec4 v) {\n"
    "    return vec4(dot(ffMV[m * 4], v), dot(ffMV[m * 4 + 1], v), dot(ffMV[m * 4 + 2], v), dot(ffMV[m * 4 + 3], v));\n"
    "}\n"
    "vec3 invMul(int m, vec3 n) { return n.x * ffInv[m * 4].xyz + n.y * ffInv[m * 4 + 1].xyz + n.z * ffInv[m * 4 + 2].xyz; }\n"
    "void main() {\n"
    "    /* Skinning (SET_SKIN_MODE): 2-4 model-views weighted by v1; in the \"G\" modes the\n"
    "     * last weight is 1 - the sum of the others. With skinning, D3D leaves in the composite\n"
    "     * matrix only the projection (with the viewport); without it, it is the whole transform. */\n"
    "    int nm = ffI[26] == 0 ? 0 : (ffI[26] + 3) / 2, nw = (ffI[26] & 1) != 0 ? nm - 1 : nm;\n"
    "    vec4 pe = mvMul(0, v0);\n"
    "    vec3 ne = invMul(0, v2.xyz);\n"
    "    vec4 p = vec4(dot(ffMat[0], v0), dot(ffMat[1], v0), dot(ffMat[2], v0), dot(ffMat[3], v0));\n"
    "    if (nm > 0) {\n"
    "        float used = 0.0;\n"
    "        pe = vec4(0.0); ne = vec3(0.0);\n"
    "        for (int i = 0; i < nm; i++) {\n"
    "            float wi = i < nw ? v1[i] : 1.0 - used;\n"
    "            used += wi;\n"
    "            vec4 q = mvMul(i, v0);\n"
    "            pe.xyz += wi * (q.xyz / (q.w != 0.0 ? q.w : 1.0));\n"
    "            ne += wi * invMul(i, v2.xyz);\n"
    "        }\n"
    "        pe.w = 1.0;\n"
    "        p = vec4(dot(ffMat[0], pe), dot(ffMat[1], pe), dot(ffMat[2], pe), dot(ffMat[3], pe));\n"
    "    }\n"
    "    float w = p.w < 0.0 ? clamp(p.w, -1.84467e19, -5.42101e-20) : clamp(p.w, 5.42101e-20, 1.84467e19);\n"
    "    vec3 s = p.xyz / w;\n"
    "    vec3 n = vec3(s.x * (2.0 / surfaceSize.x) - 1.0, s.y * (2.0 / surfaceSize.y) - 1.0, s.z);\n"
    "    if (clipRange.y != clipRange.x) n.z = (s.z - clipRange.x) * (2.0 / (clipRange.y - clipRange.x)) - 1.0;\n"
    "    gl_Position = vec4(n * w, w);\n"
    "    vec3 eyeP = pe.xyz / (pe.w != 0.0 ? pe.w : 1.0);\n"
    "    ne = dot(ne, ne) > 0.0 ? normalize(ne) : vec3(0.0, 0.0, 1.0);\n"
    "    oD0 = v3; oD1 = v4;\n"
    "    if (ffI[0] != 0) {\n"
    "        /* Base color, as xemu's vsh-ff.c: the scene ambient (0x0A10) already carries the\n"
    "         * material emission and the global ambient; MATERIAL_EMISSION (0x03A8) scales the\n"
    "         * vertex color used as emission or ambient source. It is NOT added as a color: D3D\n"
    "         * sets it to (1,1,1) for vertex-colored ambient, which washed those objects to white. */\n"
    "        int cm = ffI[2], es = cm & 3, as = (cm >> 2) & 3, ds = (cm >> 4) & 3, ss = (cm >> 6) & 3;\n"
    "        bool localEye = (ffI[4] & 0x10000) != 0;\n"
    "        vec3 col;\n"
    "        if (es == 0 || as == 0) {\n"
    "            col = ffM[0].rgb;\n"
    "            if (es != 0) col += cmSource(es, vec3(0.0)) * ffM[1].rgb;\n"
    "            else if (as != 0) col += cmSource(as, vec3(0.0)) * ffM[1].rgb;\n"
    "        } else {\n"
    "            col = cmSource(es, vec3(0.0)) + cmSource(as, vec3(0.0)) * ffM[1].rgb;\n"
    "        }\n"
    "        float a = ds == 1 ? v3.a : ds == 2 ? v4.a : ffM[1].w;\n"
    "        vec3 spe = vec3(0.0);\n"
    "        vec3 vpEye = normalize(ffM[3].xyz / (ffM[3].w != 0.0 ? ffM[3].w : 1.0) - eyeP);\n"
    "        for (int i = 0; i < 8; i++) {\n"
    "            int mode = (ffI[1] >> (2 * i)) & 3;\n"
    "            if (mode == 0) continue;\n"
    "            vec3 L;\n"
    "            float att = 1.0, nh;\n"
    "            if (mode == 1) {\n"
    "                L = normalize(ffL[i * 8 + 3].xyz);\n"
    "                nh = localEye ? dot(ne, normalize(L + vpEye)) : dot(ne, ffL[i * 8 + 4].xyz);\n"
    "            } else {\n"
    "                vec3 d = ffL[i * 8 + 5].xyz - eyeP;\n"
    "                float dist = length(d);\n"
    "                if (ffL[i * 8 + 6].w > 0.0 && dist > ffL[i * 8 + 6].w) continue; /* out of range */\n"
    "                L = dist > 0.0 ? d / dist : vec3(0.0, 0.0, 1.0);\n"
    "                vec3 k = ffL[i * 8 + 6].xyz;\n"
    "                att = 1.0 / (k.x + k.y * dist + k.z * dist * dist);\n"
    "                nh = dot(ne, normalize(L + (localEye ? vpEye : vec3(0.0))));\n"
    "                if (mode == 3) {\n"
    "                    /* Spot: s = dot(dir, L) + dir.w through the rational falloff (L, M, N). */\n"
    "                    vec4 sd = ffL[i * 8 + 7];\n"
    "                    vec3 fo = vec3(ffL[i * 8].w, ffL[i * 8 + 1].w, ffL[i * 8 + 2].w);\n"
    "                    float s = dot(sd.xyz, L) + sd.w, sf = 1.0;\n"
    "                    if (s <= 0.0) sf = 0.0;\n"
    "                    else if (s < 1.0) {\n"
    "                        float den = fo.y * s + fo.z;\n"
    "                        sf = s <= -fo.x ? 0.0 : den == 0.0 ? 1.0 : den > 0.0 ? clamp((s + fo.x) / den, 0.0, 1.0) : 0.0;\n"
    "                    }\n"
    "                    att *= sf;\n"
    "                }\n"
    "            }\n"
    "            float nl = max(dot(ne, L), 0.0);\n"
    "            nh = max(nh, 0.0);\n"
    "            /* Specular power through SET_SPECULAR_PARAMS: (x + L) / (M x + N). */\n"
    "            float pf = 0.0;\n"
    "            if (nl > 0.0 && nh > -ffM[2].x) {\n"
    "                float den = ffM[2].y * nh + ffM[2].z;\n"
    "                pf = den == 0.0 ? 10000.0 : den > 0.0 ? clamp((nh + ffM[2].x) / den, 0.0, 10000.0) : 0.0;\n"
    "            }\n"
    "            col += ffL[i * 8].rgb * att * cmSource(as, vec3(1.0));\n"
    "            col += ffL[i * 8 + 1].rgb * att * nl * cmSource(ds, vec3(1.0));\n"
    "            spe += ffL[i * 8 + 2].rgb * att * pf * cmSource(ss, vec3(1.0));\n"
    "        }\n"
    "        oD0 = vec4(col, a);\n"
    "        oD1 = vec4(spe, v4.a);\n"
    "        /* Without SEPARATE_SPECULAR (0x0294 bit 0) the specular folds into the diffuse and\n"
    "         * D1 carries the vertex specular. SPECULAR_ENABLE is applied in the pixel shader. */\n"
    "        if (ffI[3] != 0 && (ffI[4] & 1) == 0) {\n"
    "            oD0.rgb += oD1.rgb;\n"
    "            oD1 = v4;\n"
    "        }\n"
    "    }\n"
    "    vec4 tin[4] = vec4[4](v9, v10, v11, v12);\n"
    "    vec4 tout[4];\n"
    "    for (int e = 0; e < 4; e++) {\n"
    "        vec4 t = tin[e];\n"
    "        for (int c = 0; c < 4; c++) {\n"
    "            int g = ffI[9 + e * 4 + c];\n"
    "            if (g == 0) continue;\n"
    "            if (g == 1) t[c] = dot(ffPlane[e * 4 + c], pe);\n"
    "            else if (g == 2) t[c] = dot(ffPlane[e * 4 + c], v0);\n"
    "            else if (g == 3) {\n"
    "                vec3 r = reflect(normalize(eyeP), ne);\n"
    "                float m = 2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0));\n"
    "                t[c] = c == 0 ? r.x / m + 0.5 : c == 1 ? r.y / m + 0.5 : c == 3 ? 1.0 : 0.0;\n"
    "            } else if (g == 4) t[c] = c < 3 ? ne[c] : 1.0;\n"
    "            else { vec3 r = reflect(normalize(eyeP), ne); t[c] = c < 3 ? r[c] : 1.0; }\n"
    "        }\n"
    "        if (ffI[5 + e] != 0)\n"
    "            t = vec4(dot(ffTM[e * 4], t), dot(ffTM[e * 4 + 1], t), dot(ffTM[e * 4 + 2], t), dot(ffTM[e * 4 + 3], t));\n"
    "        tout[e] = t;\n"
    "    }\n"
    "    /* Fog distance (SET_FOG_GEN_MODE): specular alpha, FOG_X (attribute 5) or, in the\n"
    "     * eye modes, the distance in eye space. */\n"
    "    float fogd = ffI[25] == 0 ? v4.a : ffI[25] == 6 ? v5.x : ffI[25] == 1 ? length(eyeP) : abs(eyeP.z);\n"
    "    oD0 = clamp(oD0, 0.0, 1.0); oD1 = clamp(oD1, 0.0, 1.0);\n"
    "    oB0 = oD0; oB1 = oD1; oFog = vec4(ngbFog(fogd)); oPts = vec4(1.0);\n"
    "    oT0 = tout[0]; oT1 = tout[1]; oT2 = tout[2]; oT3 = tout[3];\n"
    "}\n";

static const char k_fallback_fs[] =
    "#version 400\nin vec4 oD0;\nout vec4 fragColor;\nvoid main() { fragColor = oD0; }\n";

static void psh_regs_from(const pgl_regs *r, psh_registers *p) {
    int i;
    memset(p, 0, sizeof *p);
    p->combiner_control = M(r, 0x1E60);
    for (i = 0; i < PSH_MAX_STAGES; i++) {
        p->rgb_inputs[i] = M(r, 0x0AC0 + 4 * i);
        p->rgb_outputs[i] = M(r, 0x1E40 + 4 * i);
        p->alpha_inputs[i] = M(r, 0x0260 + 4 * i);
        p->alpha_outputs[i] = M(r, 0x0AA0 + 4 * i);
    }
    p->final_inputs_abcd = M(r, 0x0288);
    p->final_inputs_efg = M(r, 0x028C);
    p->texture_modes = M(r, 0x1E70);
    for (i = 0; i < 4; i++) /* stage without an active texture: mode NONE (t = 0) */
        if (!(M(r, 0x1B0C + 0x40 * i) & 0x40000000u))
            p->texture_modes &= ~(0x1Fu << (5 * i));
    p->other_stage_input = M(r, 0x1E78);
    p->compare_mode = M(r, 0x17F8);
    p->dot_mapping = M(r, 0x1E74);
    /* Shadow maps: active stages with a depth texture (color formats
     * 0x2A-0x31; the X8_Y24 ones are 0x2A, 0x2B, 0x2E and 0x2F) and the comparison function
     * (0x1E6C SET_SHADOW_DEPTH_FUNC). */
    for (i = 0; i < 4; i++) {
        uint32_t c = (M(r, 0x1B04 + 0x40 * (uint32_t)i) >> 8) & 0xFFu;
        if (!(M(r, 0x1B0C + 0x40 * (uint32_t)i) & 0x40000000u) || c < 0x2A || c > 0x31)
            continue;
        p->shadow |= 1u << i;
        if (c == 0x2A || c == 0x2B || c == 0x2E || c == 0x2F)
            p->shadow |= 1u << (4 + i);
    }
    p->shadow |= (M(r, 0x1E6C) & 7u) << 8;
}

/* The GLSL from vsh.c converts Y to the OpenGL convention; here row 0 is the top one, so
 * it is undone at the end of main. */
static int unflip_vs(char *text, size_t cap) {
    static const char add[] = "    gl_Position.y = -gl_Position.y;\n    oFog = vec4(ngbFog(oFog.x));\n";
    static const char decl[] = FOG_GLSL;
    char *nl = strchr(text, '\n'), *end;
    size_t len, n = sizeof add - 1;
    if (!nl || strlen(text) + sizeof decl + sizeof add > cap)
        return -1;
    /* The fog declarations go after "#version" and the computation at the end of main. */
    memmove(nl + 1 + (sizeof decl - 1), nl + 1, strlen(nl + 1) + 1);
    memcpy(nl + 1, decl, sizeof decl - 1);
    end = strrchr(text, '}');
    len = strlen(text);
    if (!end || len + n + 1 > cap)
        return -1;
    memmove(end + n, end, strlen(end) + 1);
    memcpy(end, add, n);
    return 0;
}

static void link_uniforms(gl_prog *p) {
    static const char *texn[4] = {"tex0", "tex1", "tex2", "tex3"};
    GLuint g = p->prog;
    int i;
    p->u_c = ngl_glGetUniformLocation(g, "c");
    p->u_surface = ngl_glGetUniformLocation(g, "surfaceSize");
    p->u_clip = ngl_glGetUniformLocation(g, "clipRange");
    p->u_ffmat = ngl_glGetUniformLocation(g, "ffMat");
    p->u_ffmv = ngl_glGetUniformLocation(g, "ffMV");
    p->u_ffinv = ngl_glGetUniformLocation(g, "ffInv");
    p->u_fftm = ngl_glGetUniformLocation(g, "ffTM");
    p->u_ffplane = ngl_glGetUniformLocation(g, "ffPlane");
    p->u_ffl = ngl_glGetUniformLocation(g, "ffL");
    p->u_ffm = ngl_glGetUniformLocation(g, "ffM");
    p->u_ffi = ngl_glGetUniformLocation(g, "ffI");
    p->u_psc0 = ngl_glGetUniformLocation(g, "psC0");
    p->u_psc1 = ngl_glGetUniformLocation(g, "psC1");
    p->u_fc0 = ngl_glGetUniformLocation(g, "psFinalC0");
    p->u_fc1 = ngl_glGetUniformLocation(g, "psFinalC1");
    p->u_fog = ngl_glGetUniformLocation(g, "fogColor");
    p->u_bump = ngl_glGetUniformLocation(g, "bumpMat");
    p->u_bscale = ngl_glGetUniformLocation(g, "bumpScale");
    p->u_boff = ngl_glGetUniformLocation(g, "bumpOffset");
    p->u_atest = ngl_glGetUniformLocation(g, "alphaTest");
    p->u_afunc = ngl_glGetUniformLocation(g, "alphaFunc");
    p->u_aref = ngl_glGetUniformLocation(g, "alphaRef");
    p->u_spec = ngl_glGetUniformLocation(g, "specMode");
    p->u_texscale = ngl_glGetUniformLocation(g, "texScale");
    p->u_fogcfg = ngl_glGetUniformLocation(g, "fogCfg");
    p->u_fogparam = ngl_glGetUniformLocation(g, "fogParam");
    p->ug_mask = (uint32_t)(p->u_ffmat >= 0) << UG_COMP | (uint32_t)(p->u_ffmv >= 0) << UG_MV |
                 (uint32_t)(p->u_ffinv >= 0) << UG_INV | (uint32_t)(p->u_fftm >= 0) << UG_TM |
                 (uint32_t)(p->u_ffplane >= 0) << UG_PLANE | (uint32_t)(p->u_ffl >= 0) << UG_LIGHT |
                 (uint32_t)(p->u_ffm >= 0) << UG_MAT | (uint32_t)(p->u_ffi >= 0) << UG_FFI |
                 (uint32_t)(p->u_psc0 >= 0 || p->u_psc1 >= 0) << UG_PSC |
                 (uint32_t)(p->u_bump >= 0 || p->u_bscale >= 0 || p->u_boff >= 0) << UG_BUMP;
    /* c[]: the compiler trims the array to the last element that is used. If the
     * locations of its elements are consecutive (the normal case), only the spans
     * that change can be uploaded; otherwise it is uploaded whole. */
    p->c_n = 0;
    if (p->u_c >= 0) {
        char name[16];
        int lo = 1, hi = PGL_CONSTANTS, mid;
        while (lo < hi) { /* the largest n with c[n-1] active */
            mid = (lo + hi + 1) / 2;
            snprintf(name, sizeof name, "c[%d]", mid - 1);
            if (ngl_glGetUniformLocation(g, name) >= 0)
                lo = mid;
            else
                hi = mid - 1;
        }
        snprintf(name, sizeof name, "c[%d]", lo - 1);
        if (ngl_glGetUniformLocation(g, name) == p->u_c + lo - 1) {
            snprintf(name, sizeof name, "c[%d]", lo / 2);
            if (ngl_glGetUniformLocation(g, name) == p->u_c + lo / 2)
                p->c_n = lo;
        }
    }
    ngl_glUseProgram(g);
    R.cur_prog = g;
    for (i = 0; i < 4; i++) {
        p->u_tex[i] = ngl_glGetUniformLocation(g, texn[i]);
        if (p->u_tex[i] >= 0)
            ngl_glUniform1i(p->u_tex[i], i);
    }
}

static gl_prog *get_program(const pgl_regs *r) {
    static psh_program pp;
    psh_registers pr;
    uint64_t vk = 0, fk;
    uint32_t start = M(r, 0x1EA0), i, n = 0;
    int program_mode = (M(r, 0x1E94) & 3) == 2, k, err;
    gl_prog *p;
    char log[1024];
    if (program_mode) {
        if (start >= PGL_PROGRAM_SLOTS)
            return NULL;
        for (i = start; i < PGL_PROGRAM_SLOTS; i++)
            if (r->program[i * 4 + 3] & 1u)
                break;
        n = (i < PGL_PROGRAM_SLOTS ? i + 1 : PGL_PROGRAM_SLOTS) - start;
        vk = rc_hash(&r->program[start * 4], (size_t)n * 16, 0x5653u) | 1u;
    } else {
        R.ff_draws++;
    }
    psh_regs_from(r, &pr);
    fk = rc_hash(&pr, sizeof pr, 0x5053u);
    if (R.last_prog >= 0 && R.last_prog < R.nprogs && R.progs[R.last_prog].vkey == vk &&
        R.progs[R.last_prog].fkey == fk)
        return R.progs[R.last_prog].failed ? NULL : &R.progs[R.last_prog];
    for (k = 0; k < R.nprogs; k++)
        if (R.progs[k].vkey == vk && R.progs[k].fkey == fk) {
            R.last_prog = k;
            return R.progs[k].failed ? NULL : &R.progs[k];
        }
    if (R.nprogs < MAX_PROGRAMS) {
        k = R.nprogs++;
    } else { /* table full: an entry is recycled (rare: the game uses few combinations) */
        k = (int)(R.frame % MAX_PROGRAMS);
        if (R.progs[k].prog)
            ngl_glDeleteProgram(R.progs[k].prog);
        if (R.cur_prog == R.progs[k].prog)
            R.cur_prog = 0;
    }
    p = &R.progs[k];
    memset(p, 0, sizeof *p);
    p->vkey = vk;
    p->fkey = fk;
    R.last_prog = k;
    if (program_mode) {
        err = nv2a_vsh_decode(&r->program[start * 4], (int)(PGL_PROGRAM_SLOTS - start), &R.vp);
        if (err < 0 || nv2a_vsh_to_glsl(&R.vp, R.vs_text, sizeof R.vs_text) < 0 ||
            unflip_vs(R.vs_text, sizeof R.vs_text)) {
            note_error("vertex program in slot %u not translatable (%s)", start, nv2a_vsh_error(err));
            p->failed = 1;
            R.prog_errors++;
            return NULL;
        }
    } else {
        snprintf(R.vs_text, sizeof R.vs_text, "%s", k_ff_vs);
    }
    err = nv2a_psh_decode(&pr, &pp);
    if (err < 0 || nv2a_psh_to_glsl(&pp, R.fs_text, sizeof R.fs_text) < 0)
        snprintf(R.fs_text, sizeof R.fs_text, "%s", k_fallback_fs);
    err = glb_compile(R.vs_text, R.fs_text, &p->prog, log, sizeof log);
    if (err) {
        note_error("shader rejected by the driver: %.200s", log);
        p->failed = 1;
        R.prog_errors++;
        return NULL;
    }
    link_uniforms(p);
    return p;
}

/* Texgen: register value -> shader code (0 none, 1 eye, 2 object, 3 sphere,
 * 4 normal, 5 reflection). */
static int texgen_code(uint32_t mode) {
    switch (mode) {
    case 0x2400: return 1;
    case 0x2401: return 2;
    case 0x2402: return 3;
    case 0x8511: return 4;
    case 0x8512: return 5;
    default: return 0;
    }
}

/* Uploads `n` vec4 if the program has that uniform. */
static void u4(GLint loc, int n, const float *v) {
    if (loc < 0)
        return;
    ngl_glUniform4fv(loc, n, v);
    P.unif_vec4 += (uint32_t)n;
}

/* 16 consecutive vec4 of the registers (the matrices are floats as they are). */
static void u4_regs(GLint loc, const pgl_regs *r, uint32_t method) {
    float v[16 * 4];
    memcpy(v, &r->m[method / 4], sizeof v);
    u4(loc, 16, v);
}

/* Last change of a group's methods. */
static uint64_t group_stamp(const pgl_regs *r, int g) {
    uint64_t s = 0;
    uint32_t b;
    for (b = k_ug[g].lo / PGL_BLOCK; b < (k_ug[g].hi + PGL_BLOCK - 1u) / PGL_BLOCK; b++)
        s = r->blk_stamp[b] > s ? r->blk_stamp[b] : s;
    for (b = k_ug[g].lo2 / PGL_BLOCK; b < (k_ug[g].hi2 + PGL_BLOCK - 1u) / PGL_BLOCK; b++)
        s = r->blk_stamp[b] > s ? r->blk_stamp[b] : s;
    return s;
}

/* c[]: only the spans of constants written with another value since the last upload to this
 * program. Two changes less than 4 elements apart go in the same call. */
static void upload_constants(const pgl_regs *r, gl_prog *p) {
    uint64_t since = p->c_up;
    int i, last;
    if (p->u_c < 0)
        return;
    if (!p->c_n) {
        for (i = 0; i < PGL_CONSTANTS; i++)
            if (r->const_stamp[i] >= since)
                break;
        if (i < PGL_CONSTANTS)
            u4(p->u_c, PGL_CONSTANTS, r->constants);
    } else {
        for (i = 0; i < p->c_n; i++) {
            int k;
            if (r->const_stamp[i] < since)
                continue;
            last = i;
            for (k = i + 1; k < p->c_n && k - last <= 4; k++)
                if (r->const_stamp[k] >= since)
                    last = k;
            u4(p->u_c + i, last - i + 1, &r->constants[i * 4]);
            i = last;
        }
    }
    p->c_up = r->stamp + 1;
}

/* Fixed pipeline uniforms (see k_ff_vs); `dirty` are the UG_* groups that have changed. */
static void set_ff_uniforms(const pgl_regs *r, gl_prog *p, uint32_t dirty) {
    static float l[64 * 4];
    GLint ints[32];
    float m[4 * 4];
    int i, k, e, c;
    if (dirty & (1u << UG_MV))
        u4_regs(p->u_ffmv, r, 0x0480);
    if (dirty & (1u << UG_INV))
        u4_regs(p->u_ffinv, r, 0x0580);
    if (dirty & (1u << UG_TM))
        u4_regs(p->u_fftm, r, 0x06C0);
    if (dirty & (1u << UG_PLANE))
        u4_regs(p->u_ffplane, r, 0x0840);
    if (dirty & (1u << UG_LIGHT)) {
        memset(l, 0, sizeof l);
        for (i = 0; i < 8; i++) {
            uint32_t b = 0x1000 + 0x80 * (uint32_t)i;
            static const uint32_t src[8] = {0x00, 0x0C, 0x18, 0x34, 0x28, 0x5C, 0x68, 0x4C};
            for (k = 0; k < 8; k++)
                for (c = 0; c < 3; c++)
                    l[(i * 8 + k) * 4 + c] = as_float(M(r, b + src[k] + 4 * (uint32_t)c));
            l[(i * 8 + 6) * 4 + 3] = as_float(M(r, b + 0x24)); /* range of the local light */
            l[(i * 8 + 7) * 4 + 3] = as_float(M(r, b + 0x58)); /* spot direction w */
            for (c = 0; c < 3; c++) /* spot falloff (L, M, N) in the w of ambient/diffuse/specular */
                l[(i * 8 + c) * 4 + 3] = as_float(M(r, b + 0x40 + 4 * (uint32_t)c));
        }
        u4(p->u_ffl, 64, l);
    }
    if (dirty & (1u << UG_MAT)) {
        memset(m, 0, sizeof m);
        for (c = 0; c < 3; c++) {
            m[c] = as_float(M(r, 0x0A10 + 4 * (uint32_t)c));
            m[4 + c] = as_float(M(r, 0x03A8 + 4 * (uint32_t)c));
        }
        m[7] = as_float(M(r, 0x03B4));
        for (c = 0; c < 3; c++) /* SET_SPECULAR_PARAMS (L, M, N) */
            m[8 + c] = as_float(M(r, 0x09E0 + 4 * (uint32_t)c));
        for (c = 0; c < 4; c++) /* SET_EYE_POSITION */
            m[12 + c] = as_float(M(r, 0x0A50 + 4 * (uint32_t)c));
        u4(p->u_ffm, 4, m);
    }
    if ((dirty & (1u << UG_FFI)) && p->u_ffi >= 0) {
        memset(ints, 0, sizeof ints);
        ints[0] = (GLint)(M(r, 0x0314) & 1u);
        ints[1] = (GLint)(M(r, 0x03BC) & 0xFFFFu);
        ints[2] = (GLint)(M(r, 0x0298) & 0xFFu);
        ints[3] = (GLint)(M(r, 0x03B8) & 1u);
        ints[4] = (GLint)M(r, 0x0294);
        for (e = 0; e < 4; e++) {
            ints[5 + e] = (GLint)(M(r, 0x0420 + 4 * (uint32_t)e) & 1u);
            for (c = 0; c < 4; c++)
                ints[9 + e * 4 + c] = texgen_code(M(r, 0x03C0 + 0x10 * (uint32_t)e + 4 * (uint32_t)c));
        }
        ints[25] = (GLint)M(r, 0x02A0);
        ints[26] = (GLint)(M(r, 0x0328) & 7u); /* SET_SKIN_MODE */
        ngl_glUniform1iv(p->u_ffi, 32, ints);
        P.unif_vec4 += 8;
    }
}

static void set_uniforms(const pgl_regs *r, gl_prog *p, const gl_state *st, const float tex_scale[8]) {
    float v[16 * 4], surf[2], clip[2];
    uint32_t src[26], dirty = 0;
    uint64_t h;
    int i, g;
    if (R.cur_prog != p->prog) {
        ngl_glUseProgram(p->prog);
        R.cur_prog = p->prog;
    }
    upload_constants(r, p);
    /* Register groups: by change stamps, without looking at the values. */
    for (g = 0; g < UG_N; g++) {
        uint64_t now;
        if (!(p->ug_mask & (1u << g)))
            continue;
        now = group_stamp(r, g) + 1;
        if (p->ug_up[g] != now) {
            p->ug_up[g] = now;
            dirty |= 1u << g;
        }
    }
    /* The rest comes from a few registers and the draw state: a hash is enough. */
    src[0] = R.target_w;
    src[1] = R.target_h;
    src[2] = M(r, 0x0394);
    src[3] = M(r, 0x0398);
    src[4] = M(r, 0x1E20);
    src[5] = M(r, 0x1E24);
    src[6] = M(r, 0x02A8);
    src[7] = (uint32_t)st->alpha_test;
    src[8] = (uint32_t)st->alpha_func;
    memcpy(&src[9], &st->alpha_ref, 4);
    src[10] = M(r, 0x02A4);
    src[11] = M(r, 0x029C);
    src[12] = M(r, 0x02A0);
    src[13] = M(r, 0x09C0);
    src[14] = M(r, 0x09C4);
    memcpy(&src[15], tex_scale, 8 * sizeof(float));
    src[23] = 0x554Eu;
    src[24] = M(r, 0x03B8);
    src[25] = M(r, 0x0294);
    h = rc_hash(src, sizeof src, 0) | 1u;
    if (p->misc_hash != h) {
        p->misc_hash = h;
        dirty |= 1u << UG_N;
    }
    if (!dirty)
        return;
    P.unif_sets++;
    if (dirty & (1u << UG_N)) {
        surf[0] = (float)R.target_w;
        surf[1] = (float)R.target_h;
        clip[0] = as_float(M(r, 0x0394));
        clip[1] = as_float(M(r, 0x0398));
        if (p->u_surface >= 0)
            ngl_glUniform2fv(p->u_surface, 1, surf);
        if (p->u_clip >= 0)
            ngl_glUniform2fv(p->u_clip, 1, clip);
        if (p->u_fc0 >= 0) {
            argb_to_vec4(M(r, 0x1E20), v);
            u4(p->u_fc0, 1, v);
        }
        if (p->u_fc1 >= 0) {
            argb_to_vec4(M(r, 0x1E24), v);
            u4(p->u_fc1, 1, v);
        }
        if (p->u_fog >= 0) {
            uint32_t c = M(r, 0x02A8); /* ABGR: R in the low byte */
            v[0] = (float)(c & 0xFF) / 255.0f;
            v[1] = (float)((c >> 8) & 0xFF) / 255.0f;
            v[2] = (float)((c >> 16) & 0xFF) / 255.0f;
            v[3] = (float)(c >> 24) / 255.0f;
            u4(p->u_fog, 1, v);
        }
        if (p->u_atest >= 0)
            ngl_glUniform1i(p->u_atest, st->alpha_test);
        if (p->u_spec >= 0)
            ngl_glUniform1i(p->u_spec, (GLint)((M(r, 0x03B8) & 1u) |
                                               ((M(r, 0x0294) & (1u << 17)) ? 0u : 2u)));
        if (p->u_afunc >= 0)
            ngl_glUniform1i(p->u_afunc, (GLint)st->alpha_func);
        if (p->u_aref >= 0)
            ngl_glUniform1fv(p->u_aref, 1, &st->alpha_ref);
        if (p->u_texscale >= 0)
            ngl_glUniform2fv(p->u_texscale, 4, tex_scale);
        if (p->u_fogcfg >= 0)
            ngl_glUniform3i(p->u_fogcfg, (GLint)(M(r, 0x02A4) & 1), (GLint)M(r, 0x029C), (GLint)M(r, 0x02A0));
        if (p->u_fogparam >= 0) {
            v[0] = as_float(M(r, 0x09C0));
            v[1] = as_float(M(r, 0x09C4));
            ngl_glUniform2fv(p->u_fogparam, 1, v);
        }
    }
    if (dirty & (1u << UG_COMP)) {
        memcpy(v, &r->m[0x0680 / 4], 16 * sizeof(float));
        u4(p->u_ffmat, 4, v);
    }
    if (p->u_ffl >= 0)
        set_ff_uniforms(r, p, dirty);
    if (dirty & (1u << UG_PSC)) {
        if (p->u_psc0 >= 0) {
            for (i = 0; i < 8; i++)
                argb_to_vec4(M(r, 0x0A60 + 4 * i), &v[4 * i]);
            u4(p->u_psc0, 8, v);
        }
        if (p->u_psc1 >= 0) {
            for (i = 0; i < 8; i++)
                argb_to_vec4(M(r, 0x0A80 + 4 * i), &v[4 * i]);
            u4(p->u_psc1, 8, v);
        }
    }
    if (dirty & (1u << UG_BUMP)) {
        if (p->u_bump >= 0) {
            for (i = 0; i < 16; i++)
                v[i] = as_float(M(r, 0x1B28 + 0x40 * (i / 4) + 4 * (i % 4)));
            ngl_glUniformMatrix2fv(p->u_bump, 4, GL_FALSE, v);
        }
        if (p->u_bscale >= 0) {
            for (i = 0; i < 4; i++)
                v[i] = as_float(M(r, 0x1B38 + 0x40 * i));
            ngl_glUniform1fv(p->u_bscale, 4, v);
        }
        if (p->u_boff >= 0) {
            for (i = 0; i < 4; i++)
                v[i] = as_float(M(r, 0x1B3C + 0x40 * i));
            ngl_glUniform1fv(p->u_boff, 4, v);
        }
    }
}

/* ---------------------------------------------------------------- vertices and drawing ---- */

/* Vertex cache (H7c, issue #43): the game's vertex arrays almost never change from one
 * frame to the next (the scenery), so each combination of formats, addresses and vertex
 * range is uploaded once to its own GL buffer and reused as long as the write watch does not
 * see CPU writes to that memory. */
#define VCACHE_SIZE 1024
typedef struct vc_entry {
    uint64_t key;
    uint32_t epoch, last_use, ok_mask;
    int volatile_; /* the CPU has rewritten these vertices at some point: not reused */
    size_t total;
    GLuint buf;
} vc_entry;
static vc_entry g_vc[VCACHE_SIZE];
static uint32_t g_vc_clock;

/* Entry for `key`: its own if present, otherwise a free one or the least used of its two slots. */
static vc_entry *vcache_slot(uint64_t key) {
    vc_entry *e = &g_vc[key % VCACHE_SIZE], *alt = &g_vc[(key >> 32) % VCACHE_SIZE];
    if (e->key == key)
        return e;
    if (alt->key == key)
        return alt;
    if (!e->buf)
        return e;
    if (!alt->buf)
        return alt;
    return e->last_use <= alt->last_use ? e : alt;
}

/* Streaming ring for the vertices and indices of each draw (GL 4.4 buffer storage, mapped once,
 * persistent and coherent): the data is written once, straight where the GPU reads it. Before,
 * every draw went through a staging copy and two glBufferData calls, and the driver allocated
 * and copied again. The ring has RING_SEGS segments. When the writer leaves a segment, a fence
 * is set after the next draw (ring_fence_left: the draw that reads what was just written may
 * still come), and the writer waits on it before it enters that segment again, so nothing the
 * GPU may still read is overwritten. Without GL 4.4 (ring_init leaves map NULL) the
 * glBufferData path stays. */
#define RING_SIZE (64u << 20)
#define RING_SEGS 8u
#define RING_SEG (RING_SIZE / RING_SEGS)
#define RING_NONE ((size_t)-1)
static struct {
    GLuint buf;
    uint8_t *map;
    size_t head;
    unsigned cur;
    GLsync fence[RING_SEGS];
    unsigned left; /* segments left behind whose fence is not set yet (bit mask) */
    uint32_t waits; /* the writer caught up with the GPU and had to wait */
} g_ring;

/* After a draw: fences for the segments the writer has left. */
static void ring_fence_left(void) {
    unsigned s;
    for (s = 0; g_ring.left && s < RING_SEGS; s++)
        if (g_ring.left & (1u << s)) {
            g_ring.fence[s] = ngl_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            g_ring.left &= ~(1u << s);
        }
}

static int ring_init(void) {
    const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    if (!ngl_glBufferStorage || !ngl_glMapBufferRange || !ngl_glFenceSync || !ngl_glClientWaitSync ||
        !ngl_glDeleteSync || !ngl_glCopyBufferSubData)
        return 0;
    ngl_glGenBuffers(1, &g_ring.buf);
    ngl_glBindBuffer(GL_ARRAY_BUFFER, g_ring.buf);
    ngl_glBufferStorage(GL_ARRAY_BUFFER, RING_SIZE, NULL, flags);
    g_ring.map = (uint8_t *)ngl_glMapBufferRange(GL_ARRAY_BUFFER, 0, RING_SIZE, flags);
    ngl_glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (!g_ring.map) {
        ngl_glDeleteBuffers(1, &g_ring.buf);
        g_ring.buf = 0;
        while (glGetError() != GL_NO_ERROR) {
        }
    }
    return g_ring.map != NULL;
}

/* `n` bytes of the ring, aligned to 64: their offset, or RING_NONE (no ring, or more than a
 * segment: the caller uses glBufferData). */
static size_t ring_alloc(size_t n) {
    size_t start;
    unsigned s, last;
    if (!g_ring.map || !n || n > RING_SEG)
        return RING_NONE;
    start = (g_ring.head + 63) & ~(size_t)63;
    if (start + n > RING_SIZE)
        start = 0;
    last = (unsigned)((start + n - 1) / RING_SEG);
    for (s = (unsigned)(start / RING_SEG);; s = (s + 1) % RING_SEGS) {
        if (s != g_ring.cur) {
            g_ring.left |= 1u << g_ring.cur;
            if (g_ring.left & (1u << s)) { /* no draw since it was left: fenced now */
                g_ring.fence[s] = ngl_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                g_ring.left &= ~(1u << s);
            }
            if (g_ring.fence[s]) {
                GLenum w = ngl_glClientWaitSync(g_ring.fence[s], 0, 0);
                if (w == GL_TIMEOUT_EXPIRED)
                    g_ring.waits++;
                while (w == GL_TIMEOUT_EXPIRED) /* until the GPU is done with the segment */
                    w = ngl_glClientWaitSync(g_ring.fence[s], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000u);
                if (w == GL_WAIT_FAILED)
                    glFinish(); /* an invalid fence: wait for everything instead */
                ngl_glDeleteSync(g_ring.fence[s]);
                g_ring.fence[s] = NULL;
            }
            g_ring.cur = s;
        }
        if (s == last)
            break;
    }
    g_ring.head = start + n;
    return start;
}

/* Vertex data goes to the GL buffer in blocks: the attributes of one interleaved game array
 * (same stride, overlapping ranges) are copied as one block of memory with one memcpy, and GL
 * reads them with the game's stride. Before, every vertex of every attribute was a small
 * memcpy of its own, which was most of the drawing time in a busy scene (about 25 ns per
 * vertex). Each block starts at the same address modulo 16 as in the game, so the alignment of
 * every attribute is kept. Only CMP (expanded to 3 floats) and sparse ranges (`gather`) are
 * still packed vertex by vertex, each attribute in its own region. The vertices are
 * [vmin, vmin + nv) or, with `gather`, the nv indices of that list relative to vmin (when the
 * range is much larger than the vertices used: issue #43); `span` is the memory span they
 * cover. The R.idx indices are already rebased. */
static int upload_vertices(const pgl_regs *r, const pgl_draw_t *d, uint32_t vmin, uint32_t nv,
                           const uint32_t *gather, uint32_t span) {
    rc_attr a[16];
    uint32_t word_off[16], per = 0, slot, v, ok_mask = 0, key_words[2 * 16 + 3];
    uint32_t src_phys[16], src_len[16];
    const uint8_t *src[16];
    size_t off[16], sstride[16], gl_stride[16], total = 0, ring_off = RING_NONE, base = 0;
    int blk_of[16], nblk = 0, b;
    struct {
        uint32_t lo, hi; /* physical range of an array block */
        size_t stride, off, size;
        const uint8_t *p;
    } blk[16];
    uint64_t key = 0;
    vc_entry *e = NULL;
    int dirty = 0;
    for (slot = 0; slot < 16; slot++) {
        if (rc_attr_layout(M(r, 0x1760 + 4 * slot), &a[slot]))
            a[slot].enabled = 0;
        word_off[slot] = per;
        per += a[slot].enabled ? (uint32_t)a[slot].inline_words : 0;
        src[slot] = NULL;
        src_phys[slot] = src_len[slot] = 0;
        sstride[slot] = 0;
        blk_of[slot] = -1;
    }
    for (slot = 0; slot < 16; slot++) {
        const rc_attr *at = &a[slot];
        if (!at->enabled)
            continue;
        if (d->inline_words) {
            src[slot] = (const uint8_t *)(d->inline_words + (size_t)vmin * per + word_off[slot]);
            sstride[slot] = (size_t)per * 4;
        } else {
            uint32_t o = M(r, 0x1720 + 4 * slot), dma;
            /* Stride, vmin and the span come from the game: the offset and the length are
             * computed in 64 bits and rejected if they do not fit in the Xbox RAM. */
            uint64_t start, len;
            sstride[slot] = at->stride ? at->stride : (uint32_t)at->src_bytes;
            start = (uint64_t)vmin * sstride[slot];
            len = (uint64_t)sstride[slot] * (span - 1) + (uint64_t)at->src_bytes;
            if (!pgraph_dma_base(o & 0x80000000u ? 0x1A0 : 0x19C, &dma) || start >= NGB_CONTIG_SIZE ||
                len > NGB_CONTIG_SIZE)
                continue;
            src_phys[slot] = dma + (o & 0x7FFFFFFFu) + (uint32_t)start;
            src_len[slot] = (uint32_t)len;
            src[slot] = nv2a_phys(src_phys[slot], src_len[slot]);
        }
        if (src[slot])
            ok_mask |= 1u << slot;
    }
    /* Blocks: every attribute that is read as it is in memory. */
    for (slot = 0; slot < 16; slot++) {
        if (!(ok_mask & (1u << slot)) || a[slot].type == 6 || gather)
            continue;
        if (d->inline_words) {
            if (!nblk) {
                blk[0].p = (const uint8_t *)(d->inline_words + (size_t)vmin * per);
                blk[0].size = (size_t)nv * per * 4;
                blk[0].stride = (size_t)per * 4;
                nblk = 1;
            }
            blk_of[slot] = 0;
            continue;
        }
        for (b = 0; b < nblk; b++)
            if (blk[b].stride == sstride[slot] && src_phys[slot] <= blk[b].hi &&
                src_phys[slot] + src_len[slot] >= blk[b].lo)
                break;
        if (b == nblk) {
            blk[b].lo = src_phys[slot];
            blk[b].hi = src_phys[slot] + src_len[slot];
            blk[b].stride = sstride[slot];
            nblk++;
        } else {
            if (src_phys[slot] < blk[b].lo)
                blk[b].lo = src_phys[slot];
            if (src_phys[slot] + src_len[slot] > blk[b].hi)
                blk[b].hi = src_phys[slot] + src_len[slot];
        }
        blk_of[slot] = b;
    }
    for (b = 0; b < nblk; b++) {
        if (!d->inline_words) { /* the union of ranges checked one by one: checked again whole */
            blk[b].size = blk[b].hi - blk[b].lo;
            blk[b].p = nv2a_phys(blk[b].lo, (uint32_t)blk[b].size);
        }
        if (blk[b].p) {
            blk[b].off = ((total + 15) & ~(size_t)15) + ((uintptr_t)blk[b].p & 15);
            total = blk[b].off + blk[b].size;
        }
    }
    for (slot = 0; slot < 16; slot++) {
        const rc_attr *at = &a[slot];
        if (!(ok_mask & (1u << slot)))
            continue;
        b = blk_of[slot];
        if (b >= 0 && !blk[b].p)
            blk_of[slot] = b = -1; /* not a valid block: packed */
        if (b >= 0) {
            off[slot] = blk[b].off + (size_t)(src[slot] - blk[b].p);
            gl_stride[slot] = blk[b].stride;
        } else {
            off[slot] = (total + 3) & ~(size_t)3;
            gl_stride[slot] = (size_t)at->dst_bytes;
            total = off[slot] + (size_t)at->dst_bytes * nv;
        }
    }
    if (!d->inline_words && R.ww_ok) {
        for (slot = 0; slot < 16; slot++) {
            key_words[2 * slot] = M(r, 0x1760 + 4 * slot);
            key_words[2 * slot + 1] = src[slot] ? src_phys[slot] : 0;
        }
        key_words[32] = vmin;
        key_words[33] = nv;
        key_words[34] = span;
        key = rc_hash(key_words, sizeof key_words,
                      gather ? rc_hash(gather, (size_t)nv * 4, 0x4741u) : 0x5643u) | 1u;
        e = vcache_slot(key);
        for (slot = 0; slot < 16 && !dirty; slot++)
            if (src[slot] && cpu_wrote(src_phys[slot], src_len[slot], e->epoch))
                dirty = 1;
        if (e->key == key && e->buf && dirty)
            e->volatile_ = 1;
    }
    ngl_glBindVertexArray(R.vao);
    if (e && e->buf && e->key == key && !dirty && !e->volatile_ && e->total == total &&
        e->ok_mask == ok_mask) {
        ngl_glBindBuffer(GL_ARRAY_BUFFER, e->buf); /* hit: nothing to copy or upload */
        e->last_use = ++g_vc_clock;
        P.vc_hits++;
    } else {
        LONGLONG tg = pf_now();
        uint8_t *dst_base;
        if (e && e->key == key && e->volatile_)
            e = NULL; /* rewritten by the CPU every frame: streamed, not cached */
        ring_off = ring_alloc(total ? total : 4);
        if (ring_off != RING_NONE) {
            dst_base = g_ring.map + ring_off;
        } else {
            R.stage = (uint8_t *)grow(R.stage, &R.stage_cap, total ? total : 4);
            if (!R.stage)
                return 0;
            dst_base = R.stage;
        }
        for (b = 0; b < nblk; b++)
            if (blk[b].p)
                memcpy(dst_base + blk[b].off, blk[b].p, blk[b].size);
        for (slot = 0; slot < 16; slot++) {
            const rc_attr *at = &a[slot];
            const uint8_t *sp = src[slot];
            size_t st = sstride[slot];
            uint8_t *dst = dst_base + off[slot];
            if (!sp || blk_of[slot] >= 0)
                continue;
            for (v = 0; v < nv; v++) {
                const uint8_t *vp = sp + st * (gather ? gather[v] : v);
                if (at->type == 6) {
                    uint32_t packed;
                    float xyz[3];
                    memcpy(&packed, vp, 4);
                    glb_decode_cmp(packed, xyz);
                    memcpy(dst + (size_t)v * 12, xyz, 12);
                } else {
                    memcpy(dst + (size_t)v * (size_t)at->dst_bytes, vp, (size_t)at->src_bytes);
                }
            }
        }
        tg = pf_now() - tg; /* "gather": the CPU side; "vertices" keeps the GL side */
        P.t[PF_GATHER] += tg;
        P.t[PF_VERT] -= tg;
        if (e) {
            if (!e->buf)
                ngl_glGenBuffers(1, &e->buf);
            if (e->key != key)
                e->volatile_ = 0;
            e->key = key;
            e->epoch = R.epoch;
            e->last_use = ++g_vc_clock;
            e->total = total;
            e->ok_mask = ok_mask;
            ngl_glBindBuffer(GL_ARRAY_BUFFER, e->buf);
            if (ring_off != RING_NONE) { /* a copy on the GPU, from the ring */
                ngl_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(total ? total : 4), NULL, GL_STATIC_DRAW_);
                ngl_glBindBuffer(GL_COPY_READ_BUFFER, g_ring.buf);
                ngl_glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_ARRAY_BUFFER, (GLintptr)ring_off, 0,
                                        (GLsizeiptr)(total ? total : 4));
            } else {
                ngl_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(total ? total : 4), R.stage, GL_STATIC_DRAW_);
            }
        } else if (ring_off != RING_NONE) {
            ngl_glBindBuffer(GL_ARRAY_BUFFER, g_ring.buf);
            base = ring_off;
        } else {
            ngl_glBindBuffer(GL_ARRAY_BUFFER, R.vbo);
            ngl_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(total ? total : 4), R.stage, GL_STREAM_DRAW);
        }
        P.verts += nv;
    }
    for (slot = 0; slot < 16; slot++) {
        const rc_attr *at = &a[slot];
        const void *at_off;
        GLsizei stride;
        if (!(ok_mask & (1u << slot))) {
            ngl_glDisableVertexAttribArray(slot);
            ngl_glVertexAttrib4f(slot, r->vconst[slot][0], r->vconst[slot][1], r->vconst[slot][2],
                                 r->vconst[slot][3]);
            continue;
        }
        at_off = (const void *)(base + off[slot]);
        stride = (GLsizei)gl_stride[slot];
        ngl_glEnableVertexAttribArray(slot);
        switch (at->type) {
        case 0: /* D3DCOLOR: B G R A in memory */
            ngl_glVertexAttribPointer(slot, at->comps == 4 ? GL_BGRA : at->comps, GL_UNSIGNED_BYTE,
                                      GL_TRUE, stride, at_off);
            break;
        case 4:
            ngl_glVertexAttribPointer(slot, at->comps, GL_UNSIGNED_BYTE, GL_TRUE, stride, at_off);
            break;
        case 1: case 5:
            ngl_glVertexAttribPointer(slot, at->comps, GL_SHORT, at->type == 1, stride, at_off);
            break;
        default: /* F and expanded CMP */
            ngl_glVertexAttribPointer(slot, at->comps, GL_FLOAT, GL_FALSE, stride, at_off);
            break;
        }
    }
    return 1;
}

static uint32_t *idx_buffer(size_t n) {
    R.idx = (uint32_t *)grow(R.idx, &R.idx_cap, n * sizeof(uint32_t));
    return R.idx;
}

/* Diagnostics (NGB_GL_TRACE="frame:step:from:prefix"): in that frame it logs each draw with
 * its state and, starting at draw `from`, saves the color target to PNG every `step` draws
 * (prefix-dNNNN.png). It helps
 * find the draw that ruins the image without dumping the whole push buffer. */
static void trace_draw(const pgl_regs *r, const pgl_draw_t *d, uint32_t n_idx, uint32_t nv) {
    static int init;
    static uint32_t frame = 0xFFFFFFFFu, step = 25, from = 0;
    static char prefix[MAX_PATH];
    uint32_t k;
    if (!init) {
        const char *e = getenv("NGB_GL_TRACE");
        init = 1;
        if (e && sscanf(e, "%u:%u:%u:%259s", &frame, &step, &from, prefix) != 4)
            frame = 0xFFFFFFFFu;
        if (!step)
            step = 1;
    }
    if (R.frame != frame)
        return;
    k = R.trace_n++;
    ngb_log("    · trace f%u d%u: prim %u, %u indices, %u vertices, target 0x%08X %ux%u zeta 0x%08X; "
            "mode %X prog %u; blend %u %X/%X eq %X; alpha %u %X ref %X; z %u %X mask %u; stencil %u; "
            "colors %08X; fog %u %X gen %X; tex0 %s0x%08X fmt %08X; tex1 %s0x%08X fmt %08X; comb %08X/%08X",
            R.frame, k, d->prim, n_idx, nv, R.color_addr, R.target_w, R.target_h, R.zeta_addr,
            M(r, 0x1E94), M(r, 0x1EA0), M(r, 0x0304) & 1, M(r, 0x0344), M(r, 0x0348), M(r, 0x0350),
            M(r, 0x0300) & 1, M(r, 0x033C), M(r, 0x0340), M(r, 0x030C) & 1, M(r, 0x0354), M(r, 0x035C) & 1,
            M(r, 0x032C) & 1, M(r, 0x0358), M(r, 0x02A4) & 1, M(r, 0x029C), M(r, 0x02A0),
            (M(r, 0x1B0C) & 0x40000000u) ? "" : "(off)", M(r, 0x1B00), M(r, 0x1B04),
            (M(r, 0x1B4C) & 0x40000000u) ? "" : "(off)", M(r, 0x1B40), M(r, 0x1B44), M(r, 0x0288), M(r, 0x1E60));
    if (nv <= 4) {
        char words[200];
        size_t w, n = 0;
        words[0] = 0;
        for (w = 0; d->inline_words && w < d->n_inline && w < 16 && n < sizeof words; w++)
            n += (size_t)snprintf(words + n, sizeof words - n, " %08X", d->inline_words[w]);
        ngb_log("    · trace f%u d%u: formats %08X %08X %08X %08X; constant diffuse %.2f %.2f %.2f %.2f; inline:%s",
                R.frame, k, M(r, 0x1760), M(r, 0x1764), M(r, 0x176C), M(r, 0x1784), r->vconst[3][0],
                r->vconst[3][1], r->vconst[3][2], r->vconst[3][3], words);
    }
    if (k >= from && (k - from) % step == 0 && R.has_color) {
        rc_surface *c = rc_surface_find(&R.surf, RC_COLOR, R.color_addr);
        uint8_t *buf;
        char path[MAX_PATH + 32];
        int sw = c ? px(c->width) : 0, sh = c ? px(c->height) : 0;
        if (!c || !c->gl_tex || !(buf = (uint8_t *)malloc((size_t)sw * sh * 4)))
            return;
        ngl_glBindFramebuffer(GL_READ_FRAMEBUFFER, R.read_fbo);
        ngl_glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c->gl_tex, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, sw, sh, GL_BGRA, GL_UNSIGNED_BYTE, buf);
        ngl_glBindFramebuffer(GL_READ_FRAMEBUFFER, R.fbo);
        snprintf(path, sizeof path, "%s-d%04u.png", prefix, k);
        gfx_write_png(path, buf, sw, sh);
        free(buf);
    }
}

void pgl_draw(const pgl_regs *r, const pgl_draw_t *d) {
    static const GLenum modes[11] = {0, GL_POINTS, GL_LINES, GL_LINE_LOOP, GL_LINE_STRIP,
                                     GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, GL_TRIANGLES,
                                     GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN};
    gl_prog *p;
    gl_state st;
    float tex_scale[8];
    uint32_t n, i, vmin = 0xFFFFFFFFu, vmax = 0, per = 0, slot, *idx, span = 0, nverts = 0;
    const uint32_t *gather = NULL;
    LONGLONG t0 = pf_now();
    if (d->prim < 1 || d->prim > 10 ||
        !bind_target(r, M(r, 0x0358) != 0, (M(r, 0x030C) & 1) || (M(r, 0x032C) & 1))) {
        R.skipped++;
        return;
    }
    pf_lap(PF_TARGET, &t0);
    if (d->inline_words) {
        for (slot = 0; slot < 16; slot++) {
            rc_attr a;
            if (!rc_attr_layout(M(r, 0x1760 + 4 * slot), &a) && a.enabled)
                per += (uint32_t)a.inline_words;
        }
        n = per ? d->n_inline / per : 0;
        if (!n || !(idx = idx_buffer((size_t)n * 3 + 8)))
            goto skip;
        for (i = 0; i < n; i++)
            idx[i] = i;
        vmin = 0;
        vmax = n - 1;
    } else {
        n = d->n_indices;
        if (!n || !(idx = idx_buffer((size_t)n * 3 + 8)))
            goto skip;
        for (i = 0; i < n; i++) {
            uint32_t k = d->indices[i];
            idx[i] = k;
            vmin = k < vmin ? k : vmin;
            vmax = k > vmax ? k : vmax;
        }
        if (vmax - vmin >= (1u << 22))
            goto skip;
        for (i = 0; i < n; i++)
            idx[i] -= vmin;
        span = vmax - vmin + 1;
        if (span > 2 * n + 64) {
            /* Sparse range (issue #43): only the vertices that are used are uploaded. */
            R.gather = (uint32_t *)grow(R.gather, &R.gather_cap, (size_t)n * sizeof(uint32_t));
            if (!R.gather)
                goto skip;
            nverts = rc_compact_indices(idx, n, R.gather);
            gather = R.gather;
        }
    }
    if (d->prim == 8) { /* QUADS: the core profile does not have them */
        uint32_t *tri = idx + n;
        uint32_t m = rc_quads_to_triangles(idx, n, tri);
        memmove(idx, tri, (size_t)m * sizeof *idx);
        n = m;
        if (!n)
            goto skip;
    }
    pf_lap(PF_INDEX, &t0);
    p = get_program(r);
    if (!p)
        goto skip;
    pf_lap(PF_PROG, &t0);
    apply_state(r, &st);
    pf_lap(PF_STATE, &t0);
    bind_textures(r, tex_scale);
    pf_lap(PF_TEX, &t0);
    set_uniforms(r, p, &st, tex_scale);
    pf_lap(PF_UNIF, &t0);
    if (!span)
        span = vmax - vmin + 1;
    if (!upload_vertices(r, d, vmin, gather ? nverts : span, gather, span))
        goto skip;
    pf_lap(PF_VERT, &t0);
    {
        size_t io = ring_alloc((size_t)n * sizeof(uint32_t));
        if (io != RING_NONE) {
            memcpy(g_ring.map + io, idx, (size_t)n * sizeof(uint32_t));
            ngl_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ring.buf);
            glDrawElements(modes[d->prim], (GLsizei)n, GL_UNSIGNED_INT, (const void *)io);
        } else {
            ngl_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, R.ebo);
            ngl_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(uint32_t)), idx, GL_STREAM_DRAW);
            glDrawElements(modes[d->prim], (GLsizei)n, GL_UNSIGNED_INT, NULL);
        }
        ring_fence_left();
    }
    color_drawn();
    ngl_glBindVertexArray(0);
    pf_lap(PF_DRAW, &t0);
    R.draws++;
    P.draws++;
    trace_draw(r, d, n, vmax - vmin + 1);
    return;
skip:
    R.skipped++;
}

/* ---------------------------------------------------------------- clear and present ---- */

void pgl_clear(const pgl_regs *r, uint32_t flags) {
    uint32_t ch = M(r, 0x1D98), cv = M(r, 0x1D9C), x0 = ch & 0xFFFF, x1 = ch >> 16, y0 = cv & 0xFFFF;
    uint32_t y1 = cv >> 16, zs = M(r, 0x1D8C), zf = (M(r, 0x0208) >> 4) & 0xF;
    GLbitfield bits = 0;
    float c[4];
    if (!bind_target(r, (flags & 0xF0) != 0, (flags & 3) != 0))
        return;
    R.clears++;
    R.state_valid = 0; /* the clear touches scissor and masks */
    if (!x1 && !y1) {
        x1 = R.target_w - 1;
        y1 = R.target_h - 1;
    }
    if (x0 > x1 || y0 > y1)
        return;
    glEnable(GL_SCISSOR_TEST);
    glScissor(px(x0), px(y0), px(x1 + 1) - px(x0), px(y1 + 1) - px(y0));
    if ((flags & 0xF0) && R.has_color) {
        argb_to_vec4(M(r, 0x1D90), c);
        glColorMask((flags & 0x10) != 0, (flags & 0x20) != 0, (flags & 0x40) != 0, (flags & 0x80) != 0);
        glClearColor(c[0], c[1], c[2], c[3]);
        bits |= GL_COLOR_BUFFER_BIT;
    }
    if ((flags & 1) && R.has_zeta) {
        glDepthMask(GL_TRUE);
        glClearDepth(zf == 1 ? (double)(zs & 0xFFFF) / 65535.0 : (double)(zs >> 8) / 16777215.0);
        bits |= GL_DEPTH_BUFFER_BIT;
    }
    if ((flags & 2) && R.has_zeta) {
        glStencilMask(0xFF);
        glClearStencil((GLint)(zs & 0xFF));
        bits |= GL_STENCIL_BUFFER_BIT;
    }
    if (bits)
        glClear(bits);
    if (bits & GL_COLOR_BUFFER_BIT)
        color_drawn();
}

/* Window rectangle with the TV's aspect, as large as the window allows (like present.c): a
 * 1920x1080 screen shows a 1440x1080 picture at 4:3 and fills itself at 16:9 (--widescreen). */
static void fit_rect(int cw, int ch, int out[4]) {
    int an = g_ngb_opts.widescreen ? 16 : 4, ad = g_ngb_opts.widescreen ? 9 : 3;
    int dh = ch, dw = (dh * an + ad / 2) / ad;
    if (dw > cw) {
        dw = cw;
        dh = (dw * ad + an / 2) / an;
    }
    out[0] = (cw - dw) / 2;
    out[1] = (ch - dh) / 2;
    out[2] = out[0] + dw;
    out[3] = out[1] + dh;
}

/* Diagnostics (NGB_GL_DUMP_SURFACES): saves next to the capture a PNG of each color surface of
 * the cache, to see the intermediate steps of the frame. */
static void dump_surfaces(const char *shot_path) {
    int k;
    for (k = 0; k < RC_MAX_SURFACES; k++) {
        const rc_surface *c = &R.surf.s[k];
        int sw = px(c->width), sh = px(c->height);
        char path[MAX_PATH];
        uint8_t *buf;
        if (!c->used || c->kind != RC_COLOR || !c->gl_tex)
            continue;
        buf = (uint8_t *)malloc((size_t)sw * sh * 4);
        if (!buf)
            continue;
        ngl_glBindFramebuffer(GL_READ_FRAMEBUFFER, R.read_fbo);
        ngl_glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c->gl_tex, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, sw, sh, GL_BGRA, GL_UNSIGNED_BYTE, buf);
        snprintf(path, sizeof path, "%.*s-c%08X.png", (int)(strlen(shot_path) - 4), shot_path, c->addr);
        gfx_write_png(path, buf, sw, sh);
        free(buf);
    }
}

/* Diagnostics (NGB_GL_SHOT_WINDOW): the window's back buffer next to the capture, as
 * <shot>-window.png, to check the present (letterbox and filter) without photographing the screen. */
static void shot_window(const char *shot_path, int w, int h) {
    char path[MAX_PATH];
    uint8_t *buf = w > 0 && h > 0 ? (uint8_t *)malloc((size_t)w * (size_t)h * 4) : NULL;
    size_t i;
    if (!buf)
        return;
    ngl_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, buf);
    for (i = 0; i < (size_t)h / 2; i++) { /* GL rows go bottom-up; the PNG top-down */
        uint32_t *a = (uint32_t *)buf + i * (size_t)w, *b = (uint32_t *)buf + ((size_t)h - 1 - i) * (size_t)w;
        int x;
        for (x = 0; x < w; x++) {
            uint32_t t = a[x];
            a[x] = b[x];
            b[x] = t;
        }
    }
    snprintf(path, sizeof path, "%.*s-window.png", (int)(strlen(shot_path) - 4), shot_path);
    gfx_write_png(path, buf, w, h);
    free(buf);
    ngl_glBindFramebuffer(GL_READ_FRAMEBUFFER, R.read_fbo);
}

int pgl_flip(const pgl_regs *r, const char *shot_path) {
    rc_surface *s = NULL;
    RECT client = {0, 0, 0, 0};
    uint32_t base;
    int rect[4], result = -1;
    LONGLONG t0 = pf_now();
    R.flips++;
    if ((M(r, 0x0200) >> 16) == 720 && pgraph_dma_base(0x194, &base))
        s = rc_surface_find(&R.surf, RC_COLOR, base + M(r, 0x0210));
    if (!s && R.present_addr)
        s = rc_surface_find(&R.surf, RC_COLOR, R.present_addr);
    else if (s && s->addr != R.present_addr)
        R.present_other++; /* the rasterizer heuristic would have presented another one */
    if (!GetClientRect(R.hwnd, &client))
        client.right = client.bottom = 0;
    if (shot_path) { /* diagnostics: what is presented and which surfaces exist */
        int k;
        char line[1024];
        size_t n = (size_t)snprintf(line, sizeof line, "GL flip %u: presents 0x%08X (current color 0x%08X, %ux%u);",
                                    R.flips, s ? s->addr : 0, R.color_addr, (unsigned)(M(r, 0x0200) >> 16),
                                    (unsigned)(M(r, 0x0204) >> 16));
        for (k = 0; k < RC_MAX_SURFACES && n < sizeof line; k++)
            if (R.surf.s[k].used)
                n += (size_t)snprintf(line + n, sizeof line - n, " %c%08X:%ux%u/f%u", R.surf.s[k].kind ? 'z' : 'c',
                                      R.surf.s[k].addr, R.surf.s[k].width, R.surf.s[k].height,
                                      R.surf.s[k].drawn_frame);
        ngb_log("    · %s", line);
    }
    ngl_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, client.right, client.bottom);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (s && s->gl_tex) {
        GLint sw = px(s->width), sh = px(s->height);
        ngl_glBindFramebuffer(GL_READ_FRAMEBUFFER, R.read_fbo);
        ngl_glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->gl_tex, 0);
        fit_rect(client.right, client.bottom, rect);
        if (R.present_prog && rect[2] > rect[0] && rect[3] > rect[1]) {
            float rx = (float)sw / (float)(rect[2] - rect[0]), ry = (float)sh / (float)(rect[3] - rect[1]);
            float tap[2];
            /* A quarter of the footprint, in texture coordinates; 0 when magnifying. */
            tap[0] = rx > 1.0f ? 0.25f * rx / (float)sw : 0.0f;
            tap[1] = ry > 1.0f ? 0.25f * ry / (float)sh : 0.0f;
            glDisable(GL_BLEND);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_STENCIL_TEST);
            glDisable(GL_CULL_FACE);
            glViewport(rect[0], client.bottom - rect[3], rect[2] - rect[0], rect[3] - rect[1]);
            ngl_glUseProgram(R.present_prog);
            R.cur_prog = 0; /* the next draw binds its own program again */
            ngl_glUniform2fv(R.u_present_step, 1, tap);
            ngl_glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, s->gl_tex);
            /* bind_textures sets the game's filter again if it samples this surface. */
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            ngl_glBindVertexArray(R.vao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            ngl_glBindVertexArray(0);
        } else {
            /* Surface row 0 is the top one: it is flipped when copying to the window. */
            ngl_glBlitFramebuffer(0, 0, sw, sh, rect[0], client.bottom - rect[1], rect[2], client.bottom - rect[3],
                                  GL_COLOR_BUFFER_BIT, GL_LINEAR);
        }
        if (shot_path) {
            size_t n_px = (size_t)sw * (size_t)sh;
            uint8_t *buf = (uint8_t *)malloc(n_px * 4);
            if (buf) {
                glPixelStorei(GL_PACK_ALIGNMENT, 4);
                glReadPixels(0, 0, sw, sh, GL_BGRA, GL_UNSIGNED_BYTE, buf);
                result = gfx_write_png(shot_path, buf, sw, sh);
                free(buf);
            }
            if (getenv("NGB_GL_DUMP_SURFACES"))
                dump_surfaces(shot_path);
            if (getenv("NGB_GL_SHOT_WINDOW")) /* what the present drew into the window (back buffer) */
                shot_window(shot_path, client.right, client.bottom);
        }
    }
    SwapBuffers(R.dc);
    rdoc_after_present(R.frame);
    ngl_glBindFramebuffer(GL_FRAMEBUFFER, R.fbo);
    R.color_addr = 0; /* the FBO is prepared again on the next draw */
    R.target_valid = R.state_valid = 0;
    if (R.ww_ok)
        scan_written_pages();
    R.frame++;
    R.trace_n = 0;
    pf_lap(PF_FLIP, &t0);
    gfx_note_frame();
    return result;
}

void pgl_summary(void) {
    if (!R.active)
        return;
    ngb_log("pgraph gl: %u draws (%u with fixed transform), %u not drawn, %u clears, %u flips; "
            "%u surfaces created (%u dropped due to CPU writes), %u uses as texture (%u copies "
            "for simultaneous read and write); "
            "textures: %u uploads, %u hash checks, %u hits; %u programs (%u errors), "
            "%u texture errors, %u state errors; %u flips with a buffer other than the last 720-wide one",
            R.draws, R.ff_draws, R.skipped, R.clears, R.flips, R.surf_created, R.surf_dropped, R.rtt_binds,
            R.feedback_copies, R.tex_uploads, R.tex.checks, R.tex.hits, R.nprogs, R.prog_errors,
            R.tex_errors, R.state_errors, R.present_other);
    if (g_ring.map)
        ngb_log("pgraph gl: vertex ring: %u waits for the GPU", g_ring.waits);
    ngb_log("pgraph gl: %u mipmap builds of rendered surfaces read as textures", R.mip_builds);
    if (R.first_error[0])
        ngb_log("pgraph gl: first error: %s", R.first_error);
}
