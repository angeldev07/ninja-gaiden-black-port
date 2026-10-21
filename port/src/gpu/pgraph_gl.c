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
#include "../gfx/present.h"
#include "../gl/glbackend.h"
#include "../gl/glload.h"
#include "../gl/glstate.h"
#include "../gl/texconv.h"
#include "../shaders/psh.h"
#include "../shaders/vsh.h"

#define GL_DEPTH_STENCIL_ 0x84F9
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
    GLint u_psc0, u_psc1, u_fc0, u_fc1, u_fog, u_bump, u_bscale, u_boff;
    GLint u_atest, u_afunc, u_aref, u_texscale, u_tex[4];
    uint32_t const_serial; /* constantes subidas por última vez (+1; 0 = nunca) */
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
    uint32_t *idx;
    size_t idx_cap;
    char vs_text[SHADER_TEXT], fs_text[SHADER_TEXT];
    vsh_program vp;
    swap_interval_fn swap_interval;
    /* estadísticas */
    uint32_t draws, clears, flips, skipped, prog_errors, tex_errors, state_errors;
    uint32_t surf_created, surf_dropped, tex_uploads, rtt_binds, feedback_copies, ff_draws;
    uint32_t present_other;
    char first_error[256];
} R;

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
    err = glstate_apply(&s, out);
    if (err) {
        R.state_errors++;
        note_error("estado NV097 no traducible (%s): blend %X/%X eq %X depth %X", glstate_error_text(err),
                   s.blend_sfactor, s.blend_dfactor, s.blend_equation, s.depth_func);
        nv2a_state_defaults(&s, R.target_w, R.target_h);
        glstate_apply(&s, out);
    }
    /* glstate invierte la cara frontal y el scissor porque supone la Y de OpenGL; aquí la
     * fila 0 es la de arriba, como en la Xbox, así que van tal cual. */
    glFrontFace(M(r, 0x03A0) == 0x0900 ? GL_CW : GL_CCW);
    glEnable(GL_SCISSOR_TEST);
    if (ch >> 16 && cv >> 16)
        glScissor((GLint)(ch & 0xFFFF), (GLint)(cv & 0xFFFF), (GLsizei)(ch >> 16), (GLsizei)(cv >> 16));
    else
        glScissor(0, 0, (GLsizei)R.target_w, (GLsizei)R.target_h);
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

/* Prepara el FBO con la superficie de color y la de zeta actuales. 0 si no hay destino. */
static int bind_target(const pgl_regs *r) {
    uint32_t ch = M(r, 0x0200), cv = M(r, 0x0204), fmt = M(r, 0x0208), pitch = M(r, 0x020C);
    uint32_t w = (ch >> 16) + (ch & 0xFFFF), h = (cv >> 16) + (cv & 0xFFFF);
    uint32_t cf = fmt & 0xF, zf = (fmt >> 4) & 0xF, base, caddr = 0, zaddr = 0;
    int swz = ((fmt >> 8) & 0xF) == 2;
    rc_surface *c = NULL, *z = NULL;
    if (!(ch >> 16) || !(cv >> 16) || w > 4096 || h > 4096)
        return 0;
    if (cf && pgraph_dma_base(0x194, &base)) {
        caddr = base + M(r, 0x0210);
        c = get_surface(RC_COLOR, caddr, pitch & 0xFFFF, w, h, cf, swz);
        if (c && caddr != R.color_addr)
            c->mem_sig = surface_sig(c);
    }
    if (zf && pgraph_dma_base(0x198, &base)) {
        zaddr = base + M(r, 0x0214);
        z = get_surface(RC_ZETA, zaddr, pitch >> 16, w, h, zf, swz);
    }
    if (!c && !z)
        return 0;
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
    s = rc_surface_find(&R.surf, RC_COLOR, phys);
    if (s && s->gl_tex && surface_sig(s) != s->mem_sig) {
        /* La CPU ha escrito otra cosa en la memoria de la superficie (una textura cargada
         * donde antes había un render target): la superficie ya no vale. */
        GLuint g = rc_surface_drop(s);
        glDeleteTextures(1, &g);
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
    t = rc_texture_get(&R.tex, &key, R.frame, &action, &dead);
    if (dead) {
        GLuint g = dead;
        glDeleteTextures(1, &g);
    }
    if (!t)
        return 0;
    if (action == RC_TEX_USE)
        return t->gl_tex;
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
    if (action == RC_TEX_CHECK && !rc_texture_checked(&R.tex, t, hash, R.frame))
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
    rc_texture_uploaded(t, hash, R.frame);
    R.tex_uploads++;
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

static const char k_ff_vs[] =
    "#version 400\n"
    "/* Transformación fija del NV2A (ngb, pgraph_gl.c): matriz compuesta 0x0680, que ya\n"
    " * incluye el viewport, sin luces ni generación de coordenadas. */\n"
    "layout(location = 0) in vec4 v0;\nlayout(location = 3) in vec4 v3;\n"
    "layout(location = 4) in vec4 v4;\nlayout(location = 9) in vec4 v9;\n"
    "layout(location = 10) in vec4 v10;\nlayout(location = 11) in vec4 v11;\n"
    "layout(location = 12) in vec4 v12;\n"
    "uniform vec4 ffMat[4];\nuniform vec2 surfaceSize;\nuniform vec2 clipRange;\n"
    "out vec4 oD0;\nout vec4 oD1;\nout vec4 oB0;\nout vec4 oB1;\nout vec4 oFog;\nout vec4 oPts;\n"
    "out vec4 oT0;\nout vec4 oT1;\nout vec4 oT2;\nout vec4 oT3;\n"
    "void main() {\n"
    "    vec4 p = vec4(dot(ffMat[0], v0), dot(ffMat[1], v0), dot(ffMat[2], v0), dot(ffMat[3], v0));\n"
    "    float w = p.w < 0.0 ? clamp(p.w, -1.84467e19, -5.42101e-20) : clamp(p.w, 5.42101e-20, 1.84467e19);\n"
    "    vec3 s = p.xyz / w;\n"
    "    vec3 n = vec3(s.x * (2.0 / surfaceSize.x) - 1.0, s.y * (2.0 / surfaceSize.y) - 1.0, s.z);\n"
    "    if (clipRange.y != clipRange.x) n.z = (s.z - clipRange.x) * (2.0 / (clipRange.y - clipRange.x)) - 1.0;\n"
    "    gl_Position = vec4(n * w, w);\n"
    "    oD0 = v3; oD1 = v4; oB0 = v3; oB1 = v4; oFog = vec4(0.0); oPts = vec4(1.0);\n"
    "    oT0 = v9; oT1 = v10; oT2 = v11; oT3 = v12;\n"
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
}

/* El GLSL de vsh.c pasa la Y a la convención de OpenGL; aquí la fila 0 es la de arriba, así
 * que se deshace al final del main. */
static int unflip_vs(char *text, size_t cap) {
    static const char add[] = "    gl_Position.y = -gl_Position.y;\n";
    char *end = strrchr(text, '}');
    size_t len = strlen(text), n = sizeof add - 1;
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
    ngl_glUseProgram(g);
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

static void set_uniforms(const pgl_regs *r, gl_prog *p, const gl_state *st, const float tex_scale[8]) {
    float v[16 * 4], surf[2], clip[2];
    int i;
    ngl_glUseProgram(p->prog);
    if (p->u_c >= 0 && p->const_serial != r->const_serial + 1) {
        ngl_glUniform4fv(p->u_c, PGL_CONSTANTS, r->constants);
        p->const_serial = r->const_serial + 1;
    }
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
}

/* ---------------------------------------------------------------- vértices y dibujo ---- */

/* Cada atributo activo va a su propia región del VBO, empaquetado (CMP ya expandido a
 * floats), con los vértices [vmin, vmax]. Los índices de R.idx ya están rebasados a vmin. */
static int upload_vertices(const pgl_regs *r, const pgl_draw_t *d, uint32_t vmin, uint32_t nv) {
    rc_attr a[16];
    uint32_t word_off[16], per = 0, slot, v;
    size_t off[16], total = 0;
    int ok[16];
    for (slot = 0; slot < 16; slot++) {
        if (rc_attr_layout(M(r, 0x1760 + 4 * slot), &a[slot]))
            a[slot].enabled = 0;
        word_off[slot] = per;
        per += a[slot].enabled ? (uint32_t)a[slot].inline_words : 0;
        off[slot] = total;
        ok[slot] = 0;
        if (a[slot].enabled)
            total += ((size_t)a[slot].dst_bytes * nv + 3) & ~(size_t)3;
    }
    R.stage = (uint8_t *)grow(R.stage, &R.stage_cap, total ? total : 4);
    if (!R.stage)
        return 0;
    for (slot = 0; slot < 16; slot++) {
        const rc_attr *at = &a[slot];
        const uint8_t *src;
        size_t sstride;
        uint8_t *dst = R.stage + off[slot];
        if (!at->enabled)
            continue;
        if (d->inline_words) {
            src = (const uint8_t *)(d->inline_words + (size_t)vmin * per + word_off[slot]);
            sstride = (size_t)per * 4;
        } else {
            uint32_t o = M(r, 0x1720 + 4 * slot), base;
            sstride = at->stride ? at->stride : (uint32_t)at->src_bytes;
            if (!pgraph_dma_base(o & 0x80000000u ? 0x1A0 : 0x19C, &base))
                continue;
            {
                /* Stride, vmin y nv vienen del juego: el desplazamiento y la longitud se
                   calculan en 64 bits y se rechazan si no caben en la RAM de la Xbox. */
                uint64_t start = (uint64_t)vmin * sstride;
                uint64_t len = (uint64_t)sstride * (nv - 1) + at->src_bytes;
                if (start >= NGB_CONTIG_SIZE || len > NGB_CONTIG_SIZE)
                    continue;
                src = nv2a_phys(base + (o & 0x7FFFFFFFu) + (uint32_t)start, (uint32_t)len);
            }
            if (!src)
                continue;
        }
        if (at->type == 6) {
            for (v = 0; v < nv; v++) {
                uint32_t packed;
                float xyz[3];
                memcpy(&packed, src + sstride * v, 4);
                glb_decode_cmp(packed, xyz);
                memcpy(dst + (size_t)v * 12, xyz, 12);
            }
        } else if (sstride == (size_t)at->src_bytes) {
            memcpy(dst, src, (size_t)at->src_bytes * nv);
        } else {
            for (v = 0; v < nv; v++)
                memcpy(dst + (size_t)v * (size_t)at->dst_bytes, src + sstride * v, (size_t)at->src_bytes);
        }
        ok[slot] = 1;
    }
    ngl_glBindVertexArray(R.vao);
    ngl_glBindBuffer(GL_ARRAY_BUFFER, R.vbo);
    ngl_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(total ? total : 4), R.stage, GL_STREAM_DRAW);
    for (slot = 0; slot < 16; slot++) {
        const rc_attr *at = &a[slot];
        if (!ok[slot]) {
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

void pgl_draw(const pgl_regs *r, const pgl_draw_t *d) {
    static const GLenum modes[11] = {0, GL_POINTS, GL_LINES, GL_LINE_LOOP, GL_LINE_STRIP,
                                     GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, GL_TRIANGLES,
                                     GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN};
    gl_prog *p;
    gl_state st;
    float tex_scale[8];
    uint32_t n, i, vmin = 0xFFFFFFFFu, vmax = 0, per = 0, slot, *idx;
    if (d->prim < 1 || d->prim > 10 || !bind_target(r)) {
        R.skipped++;
        return;
    }
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
    }
    if (d->prim == 8) { /* QUADS: el core profile no los tiene */
        uint32_t *tri = idx + n;
        uint32_t m = rc_quads_to_triangles(idx, n, tri);
        memmove(idx, tri, (size_t)m * sizeof *idx);
        n = m;
        if (!n)
            goto skip;
    }
    p = get_program(r);
    if (!p)
        goto skip;
    apply_state(r, &st);
    bind_textures(r, tex_scale);
    set_uniforms(r, p, &st, tex_scale);
    if (!upload_vertices(r, d, vmin, vmax - vmin + 1))
        goto skip;
    ngl_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, R.ebo);
    ngl_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(uint32_t)), idx, GL_STREAM_DRAW);
    glDrawElements(modes[d->prim], (GLsizei)n, GL_UNSIGNED_INT, NULL);
    ngl_glBindVertexArray(0);
    R.draws++;
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
    if (!bind_target(r))
        return;
    R.clears++;
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

int pgl_flip(const pgl_regs *r, const char *shot_path) {
    rc_surface *s = NULL;
    RECT client;
    uint32_t base;
    int rect[4], result = -1;
    R.flips++;
    if ((M(r, 0x0200) >> 16) == 720 && pgraph_dma_base(0x194, &base))
        s = rc_surface_find(&R.surf, RC_COLOR, base + M(r, 0x0210));
    if (!s && R.present_addr)
        s = rc_surface_find(&R.surf, RC_COLOR, R.present_addr);
    else if (s && s->addr != R.present_addr)
        R.present_other++; /* el heurístico del rasterizador habría presentado otra */
    GetClientRect(R.hwnd, &client);
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
        }
    }
    SwapBuffers(R.dc);
    ngl_glBindFramebuffer(GL_FRAMEBUFFER, R.fbo);
    R.color_addr = 0; /* el FBO se vuelve a preparar en el siguiente dibujo */
    R.frame++;
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
