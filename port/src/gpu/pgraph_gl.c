/*
 * pgraph_gl.c: render de PGRAPH con OpenGL 4.x (H7c). Ver pgraph_gl.h.
 *
 * Hilos y contexto. El kick del push buffer llega desde el VEH, en el hilo del juego que
 * escribió DMA_PUT, con el lock del NV2A tomado. Ese hilo tiene el TEB parcheado (fs:[0x20]
 * y fs:[0x28] son del KPCR falso), una pila propia y puede ser cualquiera de los hilos Xbox.
 * Un contexto GL solo puede estar actual en un hilo, y el driver no debería ejecutarse con
 * un TEB así. Por eso hay un hilo de render limpio, el único que llama a GL: pgraph_kick le
 * pasa el kick entero (decodificación incluida) y espera con un par de eventos. Cuesta dos
 * cambios de hilo por kick (unos 5 por frame en el menú), a cambio de no tener que copiar
 * el estado a una cola de comandos.
 *
 * Superficies. Cada render target (color o zeta) es una textura GL indexada por su
 * dirección física (rcache.c). Se dibuja con la fila 0 arriba, como en la memoria de la
 * Xbox: así una superficie se puede muestrear como textura con las mismas coordenadas que
 * una textura subida desde la RAM, y el present la voltea al copiarla a la ventana.
 *
 * Shaders. Los vertex programs (0x0B00 + LOAD/START) se traducen con nv2a_vsh_to_glsl y
 * los register combiners con nv2a_psh_to_glsl; con el modo de transformación fijo se usa un
 * shader propio con la matriz compuesta (0x0680). Se cachean por el hash del microcódigo y
 * de las palabras de los combiners.
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

#define MAX_PROGRAMS 256
#define SHADER_TEXT 65536
#define M(r, method) ((r)->m[(method) / 4])

typedef BOOL(WINAPI *swap_interval_fn)(int);

typedef struct gl_prog {
    uint64_t vkey, fkey;
    GLuint prog;
    int failed;
    GLint u_c, u_surface, u_clip, u_ffmat;
    GLint u_ffmv, u_ffinv, u_ffproj, u_fftm, u_ffplane, u_ffl, u_ffm, u_ffi; /* pipeline fijo */
    GLint u_psc0, u_psc1, u_fc0, u_fc1, u_fog, u_bump, u_bscale, u_boff;
    GLint u_atest, u_afunc, u_aref, u_texscale, u_tex[4], u_fogcfg, u_fogparam;
    uint32_t const_serial; /* constantes subidas por última vez (+1; 0 = nunca) */
    uint64_t unif_hash;    /* del resto de uniforms subidos por última vez (0 = nunca) */
} gl_prog;

static struct {
    HANDLE thread, req, done;
    void (*fn)(void *);
    void *arg;
    int init_err;
    volatile LONG active;
    HWND hwnd;
    HDC dc;
    GLuint fbo, read_fbo, vao, vbo, ebo, copy_tex;
    uint32_t copy_w, copy_h;
    rc_surfaces surf;
    rc_textures tex;
    uint32_t color_addr, zeta_addr; /* destino actual (0 = ninguno) */
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
    /* estadísticas */
    uint32_t draws, clears, flips, skipped, prog_errors, tex_errors, state_errors;
    uint32_t surf_created, surf_dropped, tex_uploads, rtt_binds, feedback_copies, ff_draws;
    uint32_t present_other, tex_hash_skipped, trace_n;
    uint32_t epoch; /* sube en cada frame (scan_written_pages) */
    /* Estado incremental: lo último que se aplicó a GL. */
    int state_valid, target_valid;
    uint32_t target_key[8];
    nv2a_state last_state;
    gl_state last_gl;
    GLuint cur_prog;
    char first_error[256];
    /* Páginas escritas por la CPU (write watch de la arena, ngb_contig_written). */
    int ww_ok;
    uint32_t ww_buf[RC_DIRTY_PAGES];
    rc_dirty dirty;
} R;

/* Desglose del dibujo con QPC (pgl_perf_take): dónde se va el tiempo de cada draw. */
enum { PF_TARGET, PF_INDEX, PF_PROG, PF_STATE, PF_TEX, PF_UNIF, PF_VERT, PF_DRAW, PF_FLIP, PF_WATCH, PF_N };
static struct {
    LONGLONG t[PF_N];
    uint32_t draws, surf_new, tex_up, tex_hash, verts, state_sets, unif_sets, vc_hits;
} P;

static LONGLONG pf_now(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

/* Suma a la fase `k` el tiempo desde `*t0` y deja `*t0` en ahora. */
static void pf_lap(int k, LONGLONG *t0) {
    LONGLONG now = pf_now();
    P.t[k] += now - *t0;
    *t0 = now;
}

void pgl_perf_take(char *out, size_t outlen, uint32_t frames) {
    static const char *names[PF_N] = {"destino", "índices", "programa", "estado", "texturas",
                                      "uniforms", "vértices", "draw", "flip", "write-watch"};
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
                 "; por frame: %.0f draws (%.0f con los vértices ya en la GPU), %.0f vértices subidos, %.1f superficies nuevas, %.1f "
                 "texturas subidas, %.0f hashes de textura, %.0f cambios de estado, %.0f de uniforms",
                 (double)P.draws / frames, (double)P.vc_hits / frames, (double)P.verts / frames, (double)P.surf_new / frames,
                 (double)P.tex_up / frames, (double)P.tex_hash / frames, (double)P.state_sets / frames,
                 (double)P.unif_sets / frames);
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

/* ---------------------------------------------------------------- hilo de render ---- */

static int setup_objects(void) {
    ngl_glGenFramebuffers(1, &R.fbo);
    ngl_glGenFramebuffers(1, &R.read_fbo);
    ngl_glGenVertexArrays(1, &R.vao);
    ngl_glGenBuffers(1, &R.vbo);
    ngl_glGenBuffers(1, &R.ebo);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS_);
    R.ww_ok = ngb_contig_written(R.ww_buf, RC_DIRTY_PAGES) != (size_t)-1;
    R.swap_interval = (swap_interval_fn)(void *)wglGetProcAddress("wglSwapIntervalEXT");
    if (R.swap_interval)
        R.swap_interval(0); /* el ritmo lo pone el pacing de FLIP_STALL, no el vsync */
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
        snprintf(why, whylen, "sin ventana");
        return GLB_ERR_ARGS;
    }
    R.hwnd = hwnd;
    rdoc_load(g_ngb_opts.rdoc_frame); /* antes de crear el contexto GL */
    R.req = CreateEventA(NULL, FALSE, FALSE, NULL);
    R.done = CreateEventA(NULL, FALSE, FALSE, NULL);
    R.thread = R.req && R.done ? CreateThread(NULL, 1 << 20, render_main, NULL, 0, NULL) : NULL;
    if (!R.thread) {
        snprintf(why, whylen, "no se pudo crear el hilo de render");
        return GLB_ERR_ARGS;
    }
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

/* ---------------------------------------------------------------- estado ---- */

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
    r->vconst[3][0] = r->vconst[3][1] = r->vconst[3][2] = 1.0f; /* difuso blanco */
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
}

/* Aplica el estado fijo. Devuelve lo aplicado (para los uniforms de alpha test). */
static void apply_state(const pgl_regs *r, gl_state *out) {
    nv2a_state s;
    uint32_t ch = M(r, 0x0200), cv = M(r, 0x0204);
    int err;
    state_from_regs(r, &s);
    if (R.state_valid && !memcmp(&s, &R.last_state, sizeof s)) {
        *out = R.last_gl; /* nada ha cambiado desde el draw anterior */
        return;
    }
    P.state_sets++;
    err = glstate_apply(&s, out);
    if (err) {
        R.state_errors++;
        note_error("estado NV097 no traducible (%s): blend %X/%X eq %X depth %X", glstate_error_text(err),
                   s.blend_sfactor, s.blend_dfactor, s.blend_equation, s.depth_func);
        nv2a_state_defaults(&s, R.target_w, R.target_h);
        glstate_apply(&s, out);
    }
    /* glstate invierte el scissor porque supone la Y de OpenGL; aquí la fila 0 es la de
     * arriba, como en la Xbox, así que va tal cual. La cara frontal sí queda invertida (ver
     * glstate.c). */
    glEnable(GL_SCISSOR_TEST);
    if (ch >> 16 && cv >> 16)
        glScissor((GLint)(ch & 0xFFFF), (GLint)(cv & 0xFFFF), (GLsizei)(ch >> 16), (GLsizei)(cv >> 16));
    else
        glScissor(0, 0, (GLsizei)R.target_w, (GLsizei)R.target_h);
    R.last_state = s;
    R.last_gl = *out;
    R.state_valid = 1;
}

/* ---------------------------------------------------------------- superficies ---- */

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

/* 1 si la CPU pudo escribir en [phys, phys + len) después del frame `since` (sin write watch,
 * o fuera de la RAM física, siempre 1: hay que mirar el contenido). */
static int cpu_wrote(uint32_t phys, uint32_t len, uint32_t since) {
    return !R.ww_ok || rc_dirty_last(&R.dirty, phys, len) > since;
}

/* Fin de frame: marca con una época nueva las páginas que la CPU escribió desde el anterior.
 * Las cachés guardan la época en que comprobaron cada objeto. Se hace una vez por frame
 * (unos 50 µs: GetWriteWatch recorre las 16 k páginas de la arena; en cada kick costaba
 * 1,7 ms por frame), así que una escritura de la CPU se ve en el frame siguiente: las
 * texturas que la CPU reescribe cada frame (el vídeo) van un frame por detrás, y los
 * vértices que se reescriben se marcan como volátiles y se suben siempre. */
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
    /* Reservado: el write watch se recoge en el flip (ver scan_written_pages). */
}

/* Crea la textura de una superficie. Si es de color de 32 bits y lineal, se inicializa con
 * lo que haya en la RAM (el juego puede haberla escrito con la CPU). */
static void create_surface_texture(rc_surface *s, uint32_t pitch, int swizzled) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    if (s->kind == RC_ZETA) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, (GLsizei)s->width, (GLsizei)s->height, 0,
                     GL_DEPTH_STENCIL_, GL_UNSIGNED_INT_24_8_, NULL);
    } else {
        const uint8_t *src = NULL;
        if (!swizzled && color_bpp(s->format) == 4 && pitch >= s->width * 4)
            src = nv2a_phys(s->addr, pitch * s->height);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(0x0CF2 /* GL_UNPACK_ROW_LENGTH */, src ? (GLint)(pitch / 4) : 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)s->width, (GLsizei)s->height, 0, GL_BGRA,
                     GL_UNSIGNED_BYTE, src);
        glPixelStorei(0x0CF2, 0);
    }
    s->gl_tex = t;
    s->mem_sig = surface_sig(s);
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

/* Firma de la RAM de una superficie (ver rc_surface.mem_sig). */
static uint64_t surface_sig(const rc_surface *s) {
    const uint8_t *p = nv2a_phys(s->addr, s->bytes);
    return p ? rc_sparse_hash(p, s->bytes) : 0;
}

/* Prepara el FBO con la superficie de color y la de zeta actuales. 0 si no hay destino.
 * Solo se adjunta lo que se va a usar (como xemu): D3D deja programadas una zeta en la
 * dirección 0 en pasadas sin profundidad y un color en la 0 en las sombras (máscara de color
 * a 0); adjuntarlas hacía que se pisaran entre sí y se recrearan dos texturas por frame. */
static int bind_target(const pgl_regs *r, int need_color, int need_zeta) {
    uint32_t ch = M(r, 0x0200), cv = M(r, 0x0204), fmt = M(r, 0x0208), pitch = M(r, 0x020C);
    uint32_t w = (ch >> 16) + (ch & 0xFFFF), h = (cv >> 16) + (cv & 0xFFFF);
    uint32_t cf = need_color ? fmt & 0xF : 0, zf = need_zeta ? (fmt >> 4) & 0xF : 0, base, caddr = 0, zaddr = 0;
    uint32_t key[8];
    int swz = ((fmt >> 8) & 0xF) == 2;
    rc_surface *c = NULL, *z = NULL;
    if (!(ch >> 16) || !(cv >> 16) || w > 4096 || h > 4096)
        return 0;
    /* Mismo destino que el draw anterior: el FBO ya está listo. */
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
    R.state_valid = 0; /* el scissor y el viewport dependen del destino */
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
        /* Pedir la zeta puede haber reciclado (LRU) o invalidado (solape) la entrada del color
         * (issue #43): se revalida; si la zeta no puede convivir con él, se queda sin zeta. */
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
    R.zeta_addr = z ? zaddr : 0;
    R.has_color = c != NULL;
    R.has_zeta = z != NULL;
    R.target_w = w;
    R.target_h = h;
    glViewport(0, 0, (GLsizei)w, (GLsizei)h);
    if (c && (ch >> 16) == 720)
        R.present_addr = caddr; /* el búfer que acabará en pantalla (como el rasterizador) */
    return 1;
}

/* ---------------------------------------------------------------- texturas ---- */

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
}

/* Copia la superficie de color actual a una textura aparte: el juego la está leyendo como
 * textura mientras dibuja en ella, y GL no admite ese bucle. */
static GLuint feedback_copy(rc_surface *s) {
    if (!R.copy_tex || R.copy_w != s->width || R.copy_h != s->height) {
        if (!R.copy_tex)
            glGenTextures(1, &R.copy_tex);
        glBindTexture(GL_TEXTURE_2D, R.copy_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)s->width, (GLsizei)s->height, 0, GL_BGRA,
                     GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        R.copy_w = s->width;
        R.copy_h = s->height;
    }
    glBindTexture(GL_TEXTURE_2D, R.copy_tex);
    ngl_glBindFramebuffer(GL_READ_FRAMEBUFFER, R.fbo);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, (GLsizei)s->width, (GLsizei)s->height);
    R.feedback_copies++;
    return R.copy_tex;
}

/* Textura de la etapa `i`: una superficie (render-to-texture) o la caché de texturas. */
static GLuint stage_texture(const pgl_regs *r, int i, GLenum *target, float scale[2]) {
    uint32_t mb = 0x1B00u + 0x40u * (uint32_t)i, fmt = M(r, mb + 4), base, phys, pal = M(r, mb + 0x20);
    uint32_t pal_phys = 0;
    texconv_desc d;
    rc_texkey key;
    rc_texture *t;
    rc_surface *s;
    const uint8_t *src, *palette = NULL;
    size_t pal_bytes = 0;
    unsigned dead;
    int derr, action;
    uint64_t hash;
    *target = GL_TEXTURE_2D;
    scale[0] = scale[1] = 1.0f;
    if (!pgraph_dma_base((fmt & 3) == 2 ? 0x188 : 0x184, &base))
        return 0;
    phys = base + M(r, mb);
    derr = texconv_describe(fmt, M(r, mb + 0x10), M(r, mb + 0x1C), glb_has_s3tc(), &d);
    {
        uint32_t c = (fmt >> 8) & 0xFFu;
        /* Textura de profundidad (mapa de sombras): la superficie Z que el juego dibujó antes
           en este frame ya es una textura GL; no hay nada que convertir de la memoria. */
        s = rc_surface_find(&R.surf, c >= 0x2A && c <= 0x31 ? RC_ZETA : RC_COLOR, phys);
    }
    if (s && s->gl_tex && !(R.has_color && s->addr == R.color_addr) &&
        cpu_wrote(s->addr, s->bytes, s->drawn_frame) && surface_sig(s) != s->mem_sig) {
        /* La CPU ha escrito otra cosa en la memoria de la superficie (una textura cargada
         * donde antes había un render target): la superficie ya no vale. */
        GLuint g = rc_surface_drop(s);
        glDeleteTextures(1, &g);
        R.target_valid = 0;
        R.surf_dropped++;
        s = NULL;
    }
    if (s && s->gl_tex) {
        R.rtt_binds++;
        if (!derr && d.linear) {
            scale[0] = 1.0f / (float)s->width;
            scale[1] = 1.0f / (float)s->height;
        }
        if (R.has_color && phys == R.color_addr)
            return feedback_copy(s);
        return s->gl_tex;
    }
    if (derr) {
        R.tex_errors++;
        note_error("textura de la etapa %d no convertible: formato 0x%08X (%s)", i, fmt,
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
        pal_phys = 0xFFFFFFFFu; /* sin base: cpu_wrote dirá que sí */
    if (action == RC_TEX_CHECK && !cpu_wrote(phys, (uint32_t)d.src_size, t->checked_frame) &&
        (d.kind != TEXCONV_PALETTE || !cpu_wrote(pal_phys + (pal & ~0x3Fu), 1024, t->checked_frame))) {
        /* Ninguna página de la textura (ni de la paleta) se ha escrito desde que se comprobó:
         * no hace falta hashearla. */
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
        tex_scale[2 * i] = tex_scale[2 * i + 1] = 1.0f;
        ngl_glActiveTexture(GL_TEXTURE0 + (GLenum)i);
        if (!(M(r, mb + 0x0C) & 0x40000000u) || mode == 0 || mode == 4 || mode == 5) {
            glBindTexture(GL_TEXTURE_2D, 0);
            continue;
        }
        t = stage_texture(r, i, &target, &tex_scale[2 * i]);
        glBindTexture(target, t);
        if (!t)
            continue;
        glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap_mode(addr & 0xF));
        glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap_mode((addr >> 8) & 0xF));
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, min_filter((filter >> 16) & 0xFF));
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, ((filter >> 24) & 0xF) == 1 ? GL_NEAREST : GL_LINEAR);
    }
    ngl_glActiveTexture(GL_TEXTURE0);
}

/* ---------------------------------------------------------------- shaders ---- */

/* Niebla del NV2A (como xemu, glsl/vsh.c): el hardware convierte la distancia de niebla del
 * vértice (oFog.x de un vertex program, o la que genera el modo fijo) en el factor que lee el
 * combiner final, según SET_FOG_MODE y SET_FOG_PARAMS. Sin esto oFog valía 0 y, con la niebla
 * activa, todo salía del color de la niebla (negro en el primer nivel). */
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
 * Pipeline fijo del NV2A (transform & lighting de D3D sin vertex program). Registros
 * (nv2a_regs.h de xemu como referencia; código propio):
 *   - posición: matriz compuesta 0x0680 (ya incluye proyección y viewport); ojo: model-view
 *     0x0480, normales con la model-view inversa 0x0580;
 *   - skinning (SET_SKIN_MODE 0x0328: 1/2 = 2 matrices, 3/4 = 3, 5/6 = 4; los impares calculan
 *     el último peso): model-view 0x0480 + m·0x40 e inversas 0x0580 + m·0x40 ponderadas con los
 *     pesos del atributo 1; la posición sale de la suma en el ojo por la proyección 0x0440;
 *   - luces 0..7 en 0x1000 + i·0x80 (ambiente, difusa, especular, rango, half vector, dirección
 *     infinita, posición local, atenuación) y el modo de cada una en 0x03BC (2 bits: 1
 *     infinita, 2 local, 3 foco, tratado como local);
 *   - material: ambiente de escena 0x0A10, emisión 0x03A8, alfa 0x03B4, color material 0x0298
 *     (2 bits por término: emisión, ambiente, difusa, especular; 0 material, 1 difusa del
 *     vértice, 2 especular del vértice); especular activado 0x03B8;
 *   - texgen S/T/R/Q por etapa en 0x03C0 + e·0x10 y sus planos en 0x0840 + e·0x40; matrices de
 *     textura en 0x06C0 + e·0x40, activadas en 0x0420 + e·4.
 * D3D ya multiplica el material en los colores de las luces; con color material se
 * multiplica por el color del vértice. Aproximaciones: potencia especular fija (16) en lugar
 * de SET_SPECULAR_PARAMS, focos sin cono y mapa esférico en convención de OpenGL.
 */
static const char k_ff_vs[] =
    "#version 400\n"
    "layout(location = 0) in vec4 v0;\nlayout(location = 1) in vec4 v1;\nlayout(location = 2) in vec4 v2;\n"
    "layout(location = 3) in vec4 v3;\nlayout(location = 4) in vec4 v4;\n"
    "layout(location = 9) in vec4 v9;\nlayout(location = 10) in vec4 v10;\n"
    "layout(location = 11) in vec4 v11;\nlayout(location = 12) in vec4 v12;\n"
    "layout(location = 5) in vec4 v5;\n"
    "uniform vec4 ffMat[4];\nuniform vec2 surfaceSize;\nuniform vec2 clipRange;\n"
    "uniform vec4 ffMV[16];\nuniform vec4 ffInv[16];\nuniform vec4 ffProj[4];\nuniform vec4 ffTM[16];\n"
    "uniform vec4 ffPlane[16];\n"
    "uniform vec4 ffL[64];\nuniform vec4 ffM[3];\nuniform int ffI[32];\n"
    FOG_GLSL
    "out vec4 oD0;\nout vec4 oD1;\nout vec4 oB0;\nout vec4 oB1;\nout vec4 oFog;\nout vec4 oPts;\n"
    "out vec4 oT0;\nout vec4 oT1;\nout vec4 oT2;\nout vec4 oT3;\n"
    "vec3 cmSource(int sel, vec3 mat) { return sel == 1 ? v3.rgb : sel == 2 ? v4.rgb : mat; }\n"
    "vec4 mvMul(int m, vec4 v) {\n"
    "    return vec4(dot(ffMV[m * 4], v), dot(ffMV[m * 4 + 1], v), dot(ffMV[m * 4 + 2], v), dot(ffMV[m * 4 + 3], v));\n"
    "}\n"
    "vec3 invMul(int m, vec3 n) { return n.x * ffInv[m * 4].xyz + n.y * ffInv[m * 4 + 1].xyz + n.z * ffInv[m * 4 + 2].xyz; }\n"
    "void main() {\n"
    "    /* Skinning (SET_SKIN_MODE): 2-4 model-view ponderadas por v1; en los modos \"G\" el\n"
    "     * último peso es 1 - la suma de los demás. Sin skinning, la matriz compuesta. */\n"
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
    "        p = vec4(dot(ffProj[0], pe), dot(ffProj[1], pe), dot(ffProj[2], pe), dot(ffProj[3], pe));\n"
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
    "        vec3 amb = vec3(0.0), dif = vec3(0.0), spe = vec3(0.0);\n"
    "        for (int i = 0; i < 8; i++) {\n"
    "            int mode = (ffI[1] >> (2 * i)) & 3;\n"
    "            if (mode == 0) continue;\n"
    "            vec3 L, H;\n"
    "            float att = 1.0;\n"
    "            if (mode == 1) {\n"
    "                L = normalize(ffL[i * 8 + 3].xyz);\n"
    "                H = normalize(ffL[i * 8 + 4].xyz);\n"
    "            } else {\n"
    "                vec3 d = ffL[i * 8 + 5].xyz - eyeP;\n"
    "                float dist = length(d);\n"
    "                L = dist > 0.0 ? d / dist : vec3(0.0, 0.0, 1.0);\n"
    "                vec3 a = ffL[i * 8 + 6].xyz;\n"
    "                float den = a.x + a.y * dist + a.z * dist * dist;\n"
    "                att = den > 0.0 ? 1.0 / den : 1.0;\n"
    "                if (ffL[i * 8 + 6].w > 0.0 && dist > ffL[i * 8 + 6].w) att = 0.0;\n"
    "                H = normalize(L + vec3(0.0, 0.0, 1.0));\n"
    "            }\n"
    "            float nl = max(dot(ne, L), 0.0);\n"
    "            amb += ffL[i * 8].rgb * att;\n"
    "            dif += ffL[i * 8 + 1].rgb * nl * att;\n"
    "            if (nl > 0.0) spe += ffL[i * 8 + 2].rgb * pow(max(dot(ne, H), 0.0), 16.0) * att;\n"
    "        }\n"
    "        int cm = ffI[2];\n"
    "        vec3 emi = cmSource(cm & 3, vec3(0.0));\n"
    "        vec3 col = ffM[0].rgb + ffM[1].rgb + emi + amb * cmSource((cm >> 2) & 3, vec3(1.0))\n"
    "                 + dif * cmSource((cm >> 4) & 3, vec3(1.0));\n"
    "        float a = ((cm >> 4) & 3) == 1 ? v3.a : ((cm >> 4) & 3) == 2 ? v4.a : ffM[1].w;\n"
    "        oD0 = vec4(clamp(col, 0.0, 1.0), clamp(a, 0.0, 1.0));\n"
    "        oD1 = ffI[3] != 0 ? vec4(clamp(spe * cmSource((cm >> 6) & 3, vec3(1.0)), 0.0, 1.0), 1.0)\n"
    "                         : vec4(0.0, 0.0, 0.0, 1.0);\n"
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
    "    /* Distancia de niebla (SET_FOG_GEN_MODE): alfa especular, FOG_X (atributo 5) o, en los\n"
    "     * modos de ojo, la distancia en el espacio del ojo. */\n"
    "    float fogd = ffI[25] == 0 ? v4.a : ffI[25] == 6 ? v5.x : ffI[25] == 1 ? length(eyeP) : abs(eyeP.z);\n"
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
    for (i = 0; i < 4; i++) /* etapa sin textura activa: modo NONE (t = 0) */
        if (!(M(r, 0x1B0C + 0x40 * i) & 0x40000000u))
            p->texture_modes &= ~(0x1Fu << (5 * i));
    p->other_stage_input = M(r, 0x1E78);
    p->compare_mode = M(r, 0x17F8);
    p->dot_mapping = M(r, 0x1E74);
    /* Mapas de sombra: etapas activas con una textura de profundidad (formatos de color
     * 0x2A-0x31; los X8_Y24 son 0x2A, 0x2B, 0x2E y 0x2F) y la función de comparación
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

/* El GLSL de vsh.c pasa la Y a la convención de OpenGL; aquí la fila 0 es la de arriba, así
 * que se deshace al final del main. */
static int unflip_vs(char *text, size_t cap) {
    static const char add[] = "    gl_Position.y = -gl_Position.y;\n    oFog = vec4(ngbFog(oFog.x));\n";
    static const char decl[] = FOG_GLSL;
    char *nl = strchr(text, '\n'), *end;
    size_t len, n = sizeof add - 1;
    if (!nl || strlen(text) + sizeof decl + sizeof add > cap)
        return -1;
    /* Las declaraciones de la niebla van tras "#version" y el cálculo al final del main. */
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
    p->u_ffproj = ngl_glGetUniformLocation(g, "ffProj");
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
    p->u_texscale = ngl_glGetUniformLocation(g, "texScale");
    p->u_fogcfg = ngl_glGetUniformLocation(g, "fogCfg");
    p->u_fogparam = ngl_glGetUniformLocation(g, "fogParam");
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
    } else { /* tabla llena: se recicla una entrada (raro: el juego usa pocas combinaciones) */
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
            note_error("vertex program en el slot %u no traducible (%s)", start, nv2a_vsh_error(err));
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
        note_error("shader rechazado por el driver: %.200s", log);
        p->failed = 1;
        R.prog_errors++;
        return NULL;
    }
    link_uniforms(p);
    return p;
}

/* Texgen: valor del registro -> código del shader (0 nada, 1 ojo, 2 objeto, 3 esfera,
 * 4 normal, 5 reflexión). */
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

/* Uniforms del pipeline fijo (ver k_ff_vs). */
static void set_ff_uniforms(const pgl_regs *r, gl_prog *p) {
    static float l[64 * 4], big[16 * 4], proj[16];
    GLint ints[32];
    float m[3 * 4];
    int i, k, e, c;
    for (i = 0; i < 64; i++)
        big[i] = as_float(M(r, 0x0480 + 4 * i));
    if (p->u_ffmv >= 0)
        ngl_glUniform4fv(p->u_ffmv, 16, big);
    for (i = 0; i < 64; i++)
        big[i] = as_float(M(r, 0x0580 + 4 * i));
    if (p->u_ffinv >= 0)
        ngl_glUniform4fv(p->u_ffinv, 16, big);
    for (i = 0; i < 16; i++)
        proj[i] = as_float(M(r, 0x0440 + 4 * i));
    if (p->u_ffproj >= 0)
        ngl_glUniform4fv(p->u_ffproj, 4, proj);
    for (i = 0; i < 64; i++)
        big[i] = as_float(M(r, 0x06C0 + 4 * i));
    if (p->u_fftm >= 0)
        ngl_glUniform4fv(p->u_fftm, 16, big);
    for (i = 0; i < 64; i++)
        big[i] = as_float(M(r, 0x0840 + 4 * i));
    if (p->u_ffplane >= 0)
        ngl_glUniform4fv(p->u_ffplane, 16, big);
    memset(l, 0, sizeof l);
    for (i = 0; i < 8; i++) {
        uint32_t b = 0x1000 + 0x80 * (uint32_t)i;
        static const uint32_t src[7] = {0x00, 0x0C, 0x18, 0x34, 0x28, 0x5C, 0x68};
        for (k = 0; k < 7; k++)
            for (c = 0; c < 3; c++)
                l[(i * 8 + k) * 4 + c] = as_float(M(r, b + src[k] + 4 * (uint32_t)c));
        l[(i * 8 + 6) * 4 + 3] = as_float(M(r, b + 0x24)); /* rango de la luz local */
    }
    ngl_glUniform4fv(p->u_ffl, 64, l);
    memset(m, 0, sizeof m);
    for (c = 0; c < 3; c++) {
        m[c] = as_float(M(r, 0x0A10 + 4 * (uint32_t)c));
        m[4 + c] = as_float(M(r, 0x03A8 + 4 * (uint32_t)c));
    }
    m[7] = as_float(M(r, 0x03B4));
    if (p->u_ffm >= 0)
        ngl_glUniform4fv(p->u_ffm, 3, m);
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
    if (p->u_ffi >= 0)
        ngl_glUniform1iv(p->u_ffi, 32, ints);
}

static void set_uniforms(const pgl_regs *r, gl_prog *p, const gl_state *st, const float tex_scale[8]) {
    float v[16 * 4], surf[2], clip[2];
    uint32_t src[24];
    uint64_t h;
    int i;
    if (R.cur_prog != p->prog) {
        ngl_glUseProgram(p->prog);
        R.cur_prog = p->prog;
    }
    /* c[192] solo cuando el juego ha escrito constantes desde la última subida a este programa. */
    if (p->u_c >= 0 && p->const_serial != r->const_serial + 1) {
        ngl_glUniform4fv(p->u_c, PGL_CONSTANTS, r->constants);
        p->const_serial = r->const_serial + 1;
    }
    /* El resto de uniforms sale de unos pocos registros: si su hash no ha cambiado desde el
     * último draw con este programa, no hay nada que subir. */
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
    h = rc_hash(src, sizeof src, 0);
    h = rc_hash(&r->m[0x0680 / 4], 16 * 4, h);      /* matriz compuesta */
    h = rc_hash(&r->m[0x0A60 / 4], 16 * 4, h);      /* psC0 y psC1 */
    h = rc_hash(&r->m[0x1B00 / 4], 0x100, h);       /* bump de las 4 etapas */
    if (p->u_ffl >= 0) { /* pipeline fijo: luces, material, matrices y texgen */
        h = rc_hash(&r->m[0x0280 / 4], 0x0180, h); /* 0x0280-0x03FF: control, material, texgen */
        h = rc_hash(&r->m[0x0420 / 4], 0x0010, h); /* activación de matrices de textura */
        h = rc_hash(&r->m[0x0440 / 4], 0x0240, h); /* proyección, 4 model-view y 4 inversas */
        h = rc_hash(&r->m[0x06C0 / 4], 0x0100, h); /* matrices de textura */
        h = rc_hash(&r->m[0x0840 / 4], 0x0100, h); /* planos de texgen */
        h = rc_hash(&r->m[0x0A10 / 4], 0x0010, h); /* ambiente de escena */
        h = rc_hash(&r->m[0x1000 / 4], 0x0400, h); /* 8 luces */
    }
    if (p->unif_hash == (h | 1u))
        return;
    p->unif_hash = h | 1u;
    P.unif_sets++;
    surf[0] = (float)R.target_w;
    surf[1] = (float)R.target_h;
    clip[0] = as_float(M(r, 0x0394));
    clip[1] = as_float(M(r, 0x0398));
    if (p->u_surface >= 0)
        ngl_glUniform2fv(p->u_surface, 1, surf);
    if (p->u_clip >= 0)
        ngl_glUniform2fv(p->u_clip, 1, clip);
    if (p->u_ffmat >= 0) {
        for (i = 0; i < 16; i++)
            v[i] = as_float(M(r, 0x0680 + 4 * i));
        ngl_glUniform4fv(p->u_ffmat, 4, v);
    }
    if (p->u_ffl >= 0)
        set_ff_uniforms(r, p);
    if (p->u_psc0 >= 0) {
        for (i = 0; i < 8; i++)
            argb_to_vec4(M(r, 0x0A60 + 4 * i), &v[4 * i]);
        ngl_glUniform4fv(p->u_psc0, 8, v);
    }
    if (p->u_psc1 >= 0) {
        for (i = 0; i < 8; i++)
            argb_to_vec4(M(r, 0x0A80 + 4 * i), &v[4 * i]);
        ngl_glUniform4fv(p->u_psc1, 8, v);
    }
    if (p->u_fc0 >= 0) {
        argb_to_vec4(M(r, 0x1E20), v);
        ngl_glUniform4fv(p->u_fc0, 1, v);
    }
    if (p->u_fc1 >= 0) {
        argb_to_vec4(M(r, 0x1E24), v);
        ngl_glUniform4fv(p->u_fc1, 1, v);
    }
    if (p->u_fog >= 0) {
        uint32_t c = M(r, 0x02A8); /* ABGR: R en el byte bajo */
        v[0] = (float)(c & 0xFF) / 255.0f;
        v[1] = (float)((c >> 8) & 0xFF) / 255.0f;
        v[2] = (float)((c >> 16) & 0xFF) / 255.0f;
        v[3] = (float)(c >> 24) / 255.0f;
        ngl_glUniform4fv(p->u_fog, 1, v);
    }
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
    if (p->u_atest >= 0)
        ngl_glUniform1i(p->u_atest, st->alpha_test);
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

/* ---------------------------------------------------------------- vértices y dibujo ---- */

/* Caché de vértices (H7c, issue #43): los arrays de vértices del juego casi nunca cambian de
 * un frame a otro (el escenario), así que cada combinación de formatos, direcciones y rango
 * de vértices se sube una vez a su propio búfer GL y se reutiliza mientras el write watch no
 * vea escrituras de la CPU en esa memoria. */
#define VCACHE_SIZE 1024
typedef struct vc_entry {
    uint64_t key;
    uint32_t epoch, last_use, ok_mask;
    int volatile_; /* la CPU reescribió estos vértices alguna vez: no se reutilizan */
    size_t total;
    GLuint buf;
} vc_entry;
static vc_entry g_vc[VCACHE_SIZE];
static uint32_t g_vc_clock;

/* Entrada para `key`: la suya si está, si no una libre o la menos usada de sus dos huecos. */
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

/* Cada atributo activo va a su propia región del VBO, empaquetado (CMP ya expandido a
 * floats). Los vértices son [vmin, vmin + nv) o, con `gather`, los nv índices de esa lista
 * relativos a vmin (cuando el rango es mucho mayor que los vértices usados: issue #43);
 * `span` es el tramo de memoria que cubren. Los índices de R.idx ya están rebasados. */
static int upload_vertices(const pgl_regs *r, const pgl_draw_t *d, uint32_t vmin, uint32_t nv,
                           const uint32_t *gather, uint32_t span) {
    rc_attr a[16];
    uint32_t word_off[16], per = 0, slot, v, ok_mask = 0, key_words[2 * 16 + 3];
    uint32_t src_phys[16], src_len[16];
    const uint8_t *src[16];
    size_t off[16], sstride[16], total = 0;
    uint64_t key = 0;
    vc_entry *e = NULL;
    int dirty = 0;
    for (slot = 0; slot < 16; slot++) {
        if (rc_attr_layout(M(r, 0x1760 + 4 * slot), &a[slot]))
            a[slot].enabled = 0;
        word_off[slot] = per;
        per += a[slot].enabled ? (uint32_t)a[slot].inline_words : 0;
        off[slot] = total;
        src[slot] = NULL;
        src_phys[slot] = src_len[slot] = 0;
        sstride[slot] = 0;
        if (a[slot].enabled)
            total += ((size_t)a[slot].dst_bytes * nv + 3) & ~(size_t)3;
    }
    for (slot = 0; slot < 16; slot++) {
        const rc_attr *at = &a[slot];
        if (!at->enabled)
            continue;
        if (d->inline_words) {
            src[slot] = (const uint8_t *)(d->inline_words + (size_t)vmin * per + word_off[slot]);
            sstride[slot] = (size_t)per * 4;
        } else {
            uint32_t o = M(r, 0x1720 + 4 * slot), base;
            /* Stride, vmin y el tramo vienen del juego: el desplazamiento y la longitud se
             * calculan en 64 bits y se rechazan si no caben en la RAM de la Xbox. */
            uint64_t start, len;
            sstride[slot] = at->stride ? at->stride : (uint32_t)at->src_bytes;
            start = (uint64_t)vmin * sstride[slot];
            len = (uint64_t)sstride[slot] * (span - 1) + (uint64_t)at->src_bytes;
            if (!pgraph_dma_base(o & 0x80000000u ? 0x1A0 : 0x19C, &base) || start >= NGB_CONTIG_SIZE ||
                len > NGB_CONTIG_SIZE)
                continue;
            src_phys[slot] = base + (o & 0x7FFFFFFFu) + (uint32_t)start;
            src_len[slot] = (uint32_t)len;
            src[slot] = nv2a_phys(src_phys[slot], src_len[slot]);
        }
        if (src[slot])
            ok_mask |= 1u << slot;
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
        ngl_glBindBuffer(GL_ARRAY_BUFFER, e->buf); /* acierto: nada que copiar ni subir */
        e->last_use = ++g_vc_clock;
        P.vc_hits++;
    } else {
        R.stage = (uint8_t *)grow(R.stage, &R.stage_cap, total ? total : 4);
        if (!R.stage)
            return 0;
        for (slot = 0; slot < 16; slot++) {
            const rc_attr *at = &a[slot];
            const uint8_t *sp = src[slot];
            size_t st = sstride[slot];
            uint8_t *dst = R.stage + off[slot];
            if (!sp)
                continue;
            if (!gather && at->type != 6 && st == (size_t)at->src_bytes) {
                memcpy(dst, sp, (size_t)at->src_bytes * nv);
                continue;
            }
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
            ngl_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(total ? total : 4), R.stage, GL_STATIC_DRAW_);
        } else {
            ngl_glBindBuffer(GL_ARRAY_BUFFER, R.vbo);
            ngl_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(total ? total : 4), R.stage, GL_STREAM_DRAW);
        }
        P.verts += nv;
    }
    for (slot = 0; slot < 16; slot++) {
        const rc_attr *at = &a[slot];
        if (!(ok_mask & (1u << slot))) {
            ngl_glDisableVertexAttribArray(slot);
            ngl_glVertexAttrib4f(slot, r->vconst[slot][0], r->vconst[slot][1], r->vconst[slot][2],
                                 r->vconst[slot][3]);
            continue;
        }
        ngl_glEnableVertexAttribArray(slot);
        switch (at->type) {
        case 0: /* D3DCOLOR: B G R A en memoria */
            ngl_glVertexAttribPointer(slot, at->comps == 4 ? GL_BGRA : at->comps, GL_UNSIGNED_BYTE,
                                      GL_TRUE, 4, (const void *)off[slot]);
            break;
        case 4:
            ngl_glVertexAttribPointer(slot, at->comps, GL_UNSIGNED_BYTE, GL_TRUE, 4,
                                      (const void *)off[slot]);
            break;
        case 1: case 5:
            ngl_glVertexAttribPointer(slot, at->comps, GL_SHORT, at->type == 1, at->dst_bytes,
                                      (const void *)off[slot]);
            break;
        default: /* F y CMP expandido */
            ngl_glVertexAttribPointer(slot, at->comps, GL_FLOAT, GL_FALSE, at->dst_bytes,
                                      (const void *)off[slot]);
            break;
        }
    }
    return 1;
}

static uint32_t *idx_buffer(size_t n) {
    R.idx = (uint32_t *)grow(R.idx, &R.idx_cap, n * sizeof(uint32_t));
    return R.idx;
}

/* Diagnóstico (NGB_GL_TRACE="frame:paso:desde:prefijo"): en ese frame registra cada draw con
 * su estado y, a partir del draw `desde`, guarda el destino de color en PNG cada `paso` draws
 * (prefijo-dNNNN.png). Sirve
 * para encontrar el draw que estropea la imagen sin volcar el push buffer entero. */
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
    ngb_log("    · traza f%u d%u: prim %u, %u índices, %u vértices, destino 0x%08X %ux%u zeta 0x%08X; "
            "modo %X prog %u; blend %u %X/%X eq %X; alfa %u %X ref %X; z %u %X máscara %u; stencil %u; "
            "colores %08X; niebla %u %X gen %X; tex0 %s0x%08X fmt %08X; tex1 %s0x%08X fmt %08X; comb %08X/%08X",
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
        ngb_log("    · traza f%u d%u: formatos %08X %08X %08X %08X; difuso constante %.2f %.2f %.2f %.2f; inline:%s",
                R.frame, k, M(r, 0x1760), M(r, 0x1764), M(r, 0x176C), M(r, 0x1784), r->vconst[3][0],
                r->vconst[3][1], r->vconst[3][2], r->vconst[3][3], words);
    }
    if (k >= from && (k - from) % step == 0 && R.has_color) {
        rc_surface *c = rc_surface_find(&R.surf, RC_COLOR, R.color_addr);
        uint8_t *buf;
        char path[MAX_PATH + 32];
        if (!c || !c->gl_tex || !(buf = (uint8_t *)malloc((size_t)c->width * c->height * 4)))
            return;
        ngl_glBindFramebuffer(GL_READ_FRAMEBUFFER, R.read_fbo);
        ngl_glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c->gl_tex, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, (GLsizei)c->width, (GLsizei)c->height, GL_BGRA, GL_UNSIGNED_BYTE, buf);
        ngl_glBindFramebuffer(GL_READ_FRAMEBUFFER, R.fbo);
        snprintf(path, sizeof path, "%s-d%04u.png", prefix, k);
        gfx_write_png(path, buf, (int)c->width, (int)c->height);
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
            /* Rango disperso (issue #43): se suben solo los vértices que se usan. */
            R.gather = (uint32_t *)grow(R.gather, &R.gather_cap, (size_t)n * sizeof(uint32_t));
            if (!R.gather)
                goto skip;
            nverts = rc_compact_indices(idx, n, R.gather);
            gather = R.gather;
        }
    }
    if (d->prim == 8) { /* QUADS: el core profile no los tiene */
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
    ngl_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, R.ebo);
    ngl_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(uint32_t)), idx, GL_STREAM_DRAW);
    glDrawElements(modes[d->prim], (GLsizei)n, GL_UNSIGNED_INT, NULL);
    ngl_glBindVertexArray(0);
    pf_lap(PF_DRAW, &t0);
    R.draws++;
    P.draws++;
    trace_draw(r, d, n, vmax - vmin + 1);
    return;
skip:
    R.skipped++;
}

/* ---------------------------------------------------------------- clear y present ---- */

void pgl_clear(const pgl_regs *r, uint32_t flags) {
    uint32_t ch = M(r, 0x1D98), cv = M(r, 0x1D9C), x0 = ch & 0xFFFF, x1 = ch >> 16, y0 = cv & 0xFFFF;
    uint32_t y1 = cv >> 16, zs = M(r, 0x1D8C), zf = (M(r, 0x0208) >> 4) & 0xF;
    GLbitfield bits = 0;
    float c[4];
    if (!bind_target(r, (flags & 0xF0) != 0, (flags & 3) != 0))
        return;
    R.clears++;
    R.state_valid = 0; /* el clear toca scissor y máscaras */
    if (!x1 && !y1) {
        x1 = R.target_w - 1;
        y1 = R.target_h - 1;
    }
    if (x0 > x1 || y0 > y1)
        return;
    glEnable(GL_SCISSOR_TEST);
    glScissor((GLint)x0, (GLint)y0, (GLsizei)(x1 - x0 + 1), (GLsizei)(y1 - y0 + 1));
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
}

/* Rectángulo de la ventana con el aspecto 4:3 y escala vertical entera (como present.c). */
static void fit_rect(int cw, int ch, int fh, int out[4]) {
    int scale = fh > 0 ? ch / fh : 1, dw, dh;
    if (scale < 1)
        scale = 1;
    while (scale > 1 && (fh * scale * 4 + 1) / 3 > cw)
        scale--;
    dh = fh * scale;
    dw = (dh * 4 + 1) / 3;
    if (dw > cw || dh > ch) {
        dh = ch;
        dw = (dh * 4 + 1) / 3;
        if (dw > cw) {
            dw = cw;
            dh = (dw * 3 + 2) / 4;
        }
    }
    out[0] = (cw - dw) / 2;
    out[1] = (ch - dh) / 2;
    out[2] = out[0] + dw;
    out[3] = out[1] + dh;
}

/* Diagnóstico (NGB_GL_DUMP_SURFACES): guarda junto a la captura un PNG de cada superficie de
 * color de la caché, para ver los pasos intermedios del frame. */
static void dump_surfaces(const char *shot_path) {
    int k;
    for (k = 0; k < RC_MAX_SURFACES; k++) {
        const rc_surface *c = &R.surf.s[k];
        char path[MAX_PATH];
        uint8_t *buf;
        if (!c->used || c->kind != RC_COLOR || !c->gl_tex)
            continue;
        buf = (uint8_t *)malloc((size_t)c->width * c->height * 4);
        if (!buf)
            continue;
        ngl_glBindFramebuffer(GL_READ_FRAMEBUFFER, R.read_fbo);
        ngl_glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c->gl_tex, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, (GLsizei)c->width, (GLsizei)c->height, GL_BGRA, GL_UNSIGNED_BYTE, buf);
        snprintf(path, sizeof path, "%.*s-c%08X.png", (int)(strlen(shot_path) - 4), shot_path, c->addr);
        gfx_write_png(path, buf, (int)c->width, (int)c->height);
        free(buf);
    }
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
        R.present_other++; /* el heurístico del rasterizador habría presentado otra */
    if (!GetClientRect(R.hwnd, &client))
        client.right = client.bottom = 0;
    if (shot_path) { /* diagnóstico: qué se presenta y qué superficies hay */
        int k;
        char line[1024];
        size_t n = (size_t)snprintf(line, sizeof line, "GL flip %u: presenta 0x%08X (color actual 0x%08X, %ux%u);",
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
        ngl_glBindFramebuffer(GL_READ_FRAMEBUFFER, R.read_fbo);
        ngl_glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->gl_tex, 0);
        fit_rect(client.right, client.bottom, (int)s->height, rect);
        /* La fila 0 de la superficie es la de arriba: se voltea al copiar a la ventana. */
        ngl_glBlitFramebuffer(0, 0, (GLint)s->width, (GLint)s->height, rect[0], client.bottom - rect[1],
                              rect[2], client.bottom - rect[3], GL_COLOR_BUFFER_BIT, GL_LINEAR);
        if (shot_path) {
            size_t px = (size_t)s->width * s->height;
            uint8_t *buf = (uint8_t *)malloc(px * 4);
            if (buf) {
                glPixelStorei(GL_PACK_ALIGNMENT, 4);
                glReadPixels(0, 0, (GLsizei)s->width, (GLsizei)s->height, GL_BGRA, GL_UNSIGNED_BYTE, buf);
                result = gfx_write_png(shot_path, buf, (int)s->width, (int)s->height);
                free(buf);
            }
            if (getenv("NGB_GL_DUMP_SURFACES"))
                dump_surfaces(shot_path);
        }
    }
    SwapBuffers(R.dc);
    rdoc_after_present(R.frame);
    ngl_glBindFramebuffer(GL_FRAMEBUFFER, R.fbo);
    R.color_addr = 0; /* el FBO se vuelve a preparar en el siguiente dibujo */
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
    ngb_log("pgraph gl: %u dibujos (%u con transformación fija), %u sin dibujar, %u clears, %u flips; "
            "%u superficies creadas (%u descartadas por escrituras de la CPU), %u usos como textura (%u copias "
            "por lectura y escritura a la vez); "
            "texturas: %u subidas, %u comprobaciones de hash, %u aciertos; %u programas (%u errores), "
            "%u errores de textura, %u de estado; %u flips con un búfer distinto del último de 720",
            R.draws, R.ff_draws, R.skipped, R.clears, R.flips, R.surf_created, R.surf_dropped, R.rtt_binds,
            R.feedback_copies, R.tex_uploads, R.tex.checks, R.tex.hits, R.nprogs, R.prog_errors,
            R.tex_errors, R.state_errors, R.present_other);
    if (R.first_error[0])
        ngb_log("pgraph gl: primer error: %s", R.first_error);
}
