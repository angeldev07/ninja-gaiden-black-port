/*
 * pgraph.c: what the GPU does with the push buffer commands (H7a: almost nothing).
 *
 * Every time D3D advances DMA_PUT, nv2a.c calls pgraph_kick: the decoder
 * (pushbuf.c) walks the commands from GET to PUT and here:
 *   - each method is written to the PGRAPH log (h7-pgraph-<date>.log), grouping runs of
 *     the same method (matrices, programs, inline vertex data);
 *   - the minimal state needed to understand the frame is kept: object of each subchannel
 *     (SET_OBJECT via RAMHT), DMA objects, color surface, clear value;
 *   - the two effects D3D expects back are executed: semaphores
 *     (BACK_END_WRITE_SEMAPHORE_RELEASE writes to memory) and the color CLEAR_SURFACE,
 *     which is applied to the surface and dumped to PNG. Nothing else is drawn.
 * References: xemu hw/xbox/nv2a/pgraph/pgraph.c (methods), pfifo.c (RAMHT), nv2a.c (DMA).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ngb.h"
#include "../libaddr.h"
#include "nv2a.h"
#include "../gfx/present.h"
#include "png.h"
#include "raster.h"
#include "pushbuf.h"
#include "pgraph_gl.h"

#define PFIFO_RAMHT 0x002210u
#define PFIFO_CACHE1_PUSH1 0x003204u
#define MAX_PNG 24
#define PNG_FIRST_FLIPS 10 /* PNG on each FLIP of the first 10 frames; then every 100 */
#define MAX_TEXTURES 4096
#define RUN_PARAMS 4

static FILE *g_pg;
static CRITICAL_SECTION g_pg_lock;
static char g_png_base[MAX_PATH];
static pb_state_t g_pb;
static uint32_t g_frame, g_kicks, g_words, g_bulk_words; /* g_bulk_words: delivered by on_bulk */
static uint32_t g_hist[0x2000 / 4], g_hist_frame0[0x2000 / 4];
static uint32_t g_class[8];
static uint32_t g_dma[0x80 / 4]; /* instances of SET_CONTEXT_DMA_* (methods 0x180..0x1FC) */
static uint32_t g_clip_h, g_clip_v, g_format, g_pitch, g_color_offset;
static uint32_t g_clear_value, g_clear_h, g_clear_v, g_sem_offset;
static uint32_t g_begin_end, g_prims[16], g_draw_arrays, g_clears, g_color_clears, g_flips;
static uint32_t g_sem_releases;
static int g_png_count;
static int g_frame0_done;
static uint32_t g_first_draw_frame = 0xFFFFFFFFu, g_tex_sets, g_ntex;
/* Distinct texture offsets (for the summary): open addressing, offset + 1 (0 = empty). It was
 * a list searched linearly on every SET_TEXTURE_OFFSET: once it held thousands of offsets,
 * after an hour of play, that cost several milliseconds per frame. */
#define TEX_SET_SLOTS (2 * MAX_TEXTURES)
static uint32_t g_tex_set[TEX_SET_SLOTS];
/* Per-stage texture state: offset, format, control0, control1 (pitch), rectangle. */
static uint32_t g_tex_offset[4], g_tex_format[4], g_tex_ctl0[4], g_tex_ctl1[4], g_tex_rect[4];
#define MAX_TEX_PNG 12
#define MAX_TEX_SEEN 256
static uint32_t g_tex_seen[MAX_TEX_SEEN][2]; /* (offset, format) already seen when drawing */
static uint32_t g_tex_nseen;
static int g_tex_png;
static uint32_t g_tex_palette[4];

/* H7b: state for the software rasterizer (raster.c). */
static uint32_t g_vfmt[16];      /* SET_VERTEX_DATA_ARRAY_FORMAT per attribute */
static uint32_t g_vconst_diffuse = 0xFFFFFFFFu; /* SET_VERTEX_DATA4UB of the diffuse (slot 3) */
static uint32_t g_prim;          /* BEGIN_END primitive in progress (0 = none) */
static uint32_t *g_inline;       /* INLINE_ARRAY words of the primitive in progress */
static uint32_t g_inline_n, g_inline_cap;
static uint32_t g_blend_enable, g_sfactor = 1, g_dfactor, g_alpha_test, g_alpha_func = 0x207;
static uint32_t g_alpha_ref, g_exec_mode;
static uint32_t g_drawn_tris, g_drawn_pixels, g_skipped_draws, g_skip_pos, g_skip_target, g_skip_prim;
static uint32_t g_present_offset, g_present_pitch, g_present_w, g_present_h;
static uint32_t g_pace_vblank;  /* vblank count at the last present */
static HANDLE g_vblank_evt;     /* set on every vblank (pgraph_vblank) */
/* The last NO_OPERATION(5) the pusher ran (the wake-up of D3D_BlockOnTime): where, and the vblank. */
static struct {
    uint32_t at, vblank;
} g_nop5_hist[8];
static volatile LONG g_nop5_n;
static volatile LONG g_pace_pending; /* FLIP_STALL done; pacing runs on leaving the locks */

/* Asynchronous pusher (with GL): the queue is a single span [g_q_get, g_q_put) of the current
 * push buffer. The game moves PUT; the render thread consumes and moves GET when each span ends. */
static CRITICAL_SECTION g_q_lock;
static volatile LONG g_patt_color0; /* PGRAPH_PATT_COLOR0, written by the pusher */
static uint32_t g_block_wakes;       /* D3D_BlockOnTime NO_OPERATION(5) served */
static uint32_t g_idle_wakes;        /* its KEVENT set with the GPU idle (idle_wake) */
static uint32_t g_rescue_kicks, g_rescue_wakes; /* stall_rescue: D3D's put kicked, KEVENT set */
static uint32_t g_q_inst, g_q_get, g_q_put;
static int g_q_busy, g_q_started;
static volatile int g_q_running; /* the render thread is inside pb_run for the queue */

/* H7c: OpenGL render (--renderer gl). g_r is the NV097 state pgraph_gl.c needs; the
 * ARRAY_ELEMENT and DRAW_ARRAYS indices of the primitive in progress go in g_aidx. */
static int g_gl;
static pgl_regs g_r;
static uint32_t *g_aidx;
static uint32_t g_aidx_n, g_aidx_cap;
/* --debug-shots N: PNG of every Nth presented frame (0 = off, the default). */
#define SHOT_DUE() (g_ngb_opts.debug_shots && g_flips % g_ngb_opts.debug_shots == 0 && g_png_base[0])

/* H7c: per-frame measurements with QueryPerformanceCounter (cheap: ~20 ns per read). Ticks
 * are accumulated per phase and every PERF_EVERY frames a "perf:" line is written with the
 * per-frame average and the real fps (frames / wall-clock time of the measurement window). */
#define PERF_EVERY 300
static struct {
    LONGLONG kick, draw, clear, present, pace, t0, log0;
    LONGLONG pb, body; /* inside pb_run and kick_body (on the thread that runs it) */
    LONGLONG lockwait, call; /* waiting for g_pg_lock and inside pgl_call */
    long kcalls0;
    uint32_t mmio0, flips0, kicks0, tris0, words0, bulk0;
    char last[480];
} g_perf;

/* Hitches, which the averages of a window hide: the longest time between two FLIP_STALL, how
 * much of it was spent drawing and clearing (render thread), and the frames over 25 ms. A long
 * frame with little drawing time was waiting on the game (loading, its CPU threads). */
static struct {
    LONGLONG last_flip, work0, worst, worst_work;
    uint32_t slow;
    /* The same for the synthetic vblank (DPC thread): the longest gap and the gaps over 20 ms. */
    LONGLONG last_vb;
    volatile LONGLONG vb_worst; /* 64-bit: interlocked on the 32-bit build */
    volatile LONG vb_late;
} g_fr;

static LONGLONG qpc(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

static double ticks_ms(LONGLONG t) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return (double)t * 1000.0 / (double)f.QuadPart;
}

static uint32_t d3d_g_pdevice_va(void);
static void *game_mem(uint32_t va, uint32_t len);

/* The game's side of a window, for frames that come late with the render thread idle: the CPU
 * time of the Xbox threads, the waits on D3D's vblank event (dev + 0x1DBC: 2 per frame at
 * 60 fps; about 4 when the game holds itself at 30 fps, as it does for 180 frames after a
 * stage loads), the time in MMIO accesses, and where the Xbox threads wait inside kernel
 * calls. n = 0 starts a window. */
static void perf_game_take(uint32_t n) {
    static LONGLONG mmio0;
    static LONG fast0;
    uint32_t *dev = (uint32_t *)game_mem(d3d_g_pdevice_va(), 4);
    char cpu[160], kt[600];
    long waits = ngb_ke_waits_take(dev && *dev ? *dev + 0x1DBCu : 0);
    LONGLONG mmio = nv2a_mmio_ticks();
    LONG fast = nv2a_mmio_fast();
    ngb_thread_cpu_take(cpu, sizeof cpu, n);
    ngb_kernel_time_take(kt, sizeof kt, n);
    if (n) {
        ngb_log("    · perf game: %s; %.2f waits per frame on the vblank event; MMIO %.2f ms per frame "
                "(%.0f of the accesses in one exception)",
                cpu, (double)waits / n, ticks_ms(mmio - mmio0) / n, (double)(fast - fast0) / n);
        ngb_log("    · perf wait: %s", kt);
    }
    mmio0 = mmio;
    fast0 = fast;
}

/* Closes a measurement window: per-frame average of each phase and real fps. */
static void perf_report(void) {
    LONGLONG now = qpc(), log_now = ngb_log_ticks();
    long kc = ngb_kernel_calls();
    uint32_t mmio = nv2a_mmio_count(), n = g_flips - g_perf.flips0;
    double wall;
    if (!g_perf.t0 || !n) {
        g_perf.t0 = now;
        perf_game_take(0); /* the first window starts here */
        if (g_gl) {
            char gl[700];
            pgl_perf_take(gl, sizeof gl, 1); /* discards what was accumulated before the first window */
        }
    } else {
        wall = ticks_ms(now - g_perf.t0);
        snprintf(g_perf.last, sizeof g_perf.last,
                 "perf: frames %u-%u: %.1f real fps, %.2f ms/frame; per frame: pusher %.2f ms "
                 "(draw %.2f, clear %.2f), present %.2f, pacing %.2f, log %.2f ms; %.0f kernel calls, "
                 "%.0f MMIO, %.1f kicks, %.0f primitives, %.0f words (%.0f in bulk); pb_run %.2f ms, "
                 "kick_body %.2f ms, lock %.2f ms, pgl_call %.2f ms; worst frame %.1f ms (draws %.1f ms), "
                 "%u over 25 ms; worst vblank gap %.1f ms, %ld over 20 ms",
                 g_perf.flips0, g_flips, n * 1000.0 / wall, wall / n,
                 ticks_ms(g_perf.kick - g_perf.present) / n,
                 ticks_ms(g_perf.draw) / n, ticks_ms(g_perf.clear) / n, ticks_ms(g_perf.present) / n,
                 ticks_ms(g_perf.pace) / n, ticks_ms(log_now - g_perf.log0) / n,
                 (double)(kc - g_perf.kcalls0) / n, (double)(mmio - g_perf.mmio0) / n,
                 (double)(g_kicks - g_perf.kicks0) / n, (double)(g_begin_end - g_perf.tris0) / n,
                 (double)(g_words - g_perf.words0) / n, (double)(g_bulk_words - g_perf.bulk0) / n,
                 ticks_ms(g_perf.pb) / n, ticks_ms(g_perf.body) / n, ticks_ms(g_perf.lockwait) / n,
                 ticks_ms(g_perf.call) / n, ticks_ms(g_fr.worst), ticks_ms(g_fr.worst_work), g_fr.slow,
                 ticks_ms(InterlockedCompareExchange64(&g_fr.vb_worst, 0, 0)), g_fr.vb_late);
        ngb_log("    · %s", g_perf.last);
        perf_game_take(n);
        if (g_gl) {
            char gl[700];
            pgl_perf_take(gl, sizeof gl, n);
            ngb_log("    · %s", gl);
        }
    }
    g_perf.t0 = now;
    g_perf.log0 = log_now;
    g_perf.kcalls0 = kc;
    g_perf.mmio0 = mmio;
    g_perf.flips0 = g_flips;
    g_perf.kicks0 = g_kicks;
    g_perf.tris0 = g_begin_end;
    g_perf.words0 = g_words;
    g_perf.bulk0 = g_bulk_words;
    g_perf.kick = g_perf.draw = g_perf.clear = g_perf.present = g_perf.pace = 0;
    g_perf.pb = g_perf.body = g_perf.lockwait = g_perf.call = 0;
    g_fr.worst = g_fr.worst_work = g_fr.work0 = 0; /* draw and clear start again from 0 */
    g_fr.slow = 0;
    InterlockedExchange64(&g_fr.vb_worst, 0);
    InterlockedExchange(&g_fr.vb_late, 0);
}

/* Run of words of the same method (to avoid writing 10000 lines of INLINE_ARRAY). */
static struct {
    int active;
    uint32_t sub, method, count, params[RUN_PARAMS];
    char name[64];
} g_run;

static void pg_printf(const char *fmt, ...) {
    va_list ap;
    if (!g_pg)
        return;
    va_start(ap, fmt);
    vfprintf(g_pg, fmt, ap);
    va_end(ap);
}

/* Frames with all methods in the log (--pgraph-frames, 3 by default) plus the first
 * frame that draws (with BEGIN_END); the rest are only counted. */
static int logging(void) {
    return g_frame < g_ngb_opts.pgraph_frames || g_frame == g_first_draw_frame ||
           (g_ngb_opts.pgraph_at && g_frame >= g_ngb_opts.pgraph_at &&
            g_frame < g_ngb_opts.pgraph_at + 3);
}

static const char *prim_name(uint32_t p) {
    static const char *names[] = {"END",      "POINTS",         "LINES",
                                  "LINE_LOOP", "LINE_STRIP",    "TRIANGLES",
                                  "TRIANGLE_STRIP", "TRIANGLE_FAN", "QUADS",
                                  "QUAD_STRIP", "POLYGON"};
    return p < 11 ? names[p] : "?";
}

/* Readable annotation of the parameter of some methods. */
static void annotate(uint32_t method, uint32_t p, char *out, size_t outlen) {
    out[0] = 0;
    switch (method) {
    case 0x17FC: snprintf(out, outlen, "  (%s)", prim_name(p)); break;
    case 0x1810: snprintf(out, outlen, "  (start %u, %u vertices)", p & 0xFFFFFF, (p >> 24) + 1); break;
    case 0x1D94:
        snprintf(out, outlen, "  (%s%s%s)", (p & 0xF0) ? "color " : "", (p & 1) ? "Z " : "",
                 (p & 2) ? "stencil" : "");
        break;
    case 0x0200: case 0x0204: snprintf(out, outlen, "  (%u + %u)", p & 0xFFFF, p >> 16); break;
    case 0x0208:
        snprintf(out, outlen, "  (color fmt %u, zeta %u, type %u, %ux%u log2)", p & 0xF,
                 (p >> 4) & 0xF, (p >> 8) & 0xF, (p >> 16) & 0xFF, p >> 24);
        break;
    case 0x020C: snprintf(out, outlen, "  (color pitch %u, zeta %u)", p & 0xFFFF, p >> 16); break;
    default: break;
    }
}

static void flush_run(void) {
    uint32_t i, shown;
    char note[96];
    if (!g_run.active)
        return;
    g_run.active = 0;
    if (!logging())
        return;
    if (g_run.count == 1) {
        annotate(g_run.method, g_run.params[0], note, sizeof note);
        pg_printf("f%u s%u %04X %-34s = 0x%08X%s\n", g_frame, g_run.sub, g_run.method, g_run.name,
                  g_run.params[0], note);
        return;
    }
    pg_printf("f%u s%u %04X %-34s x%u:", g_frame, g_run.sub, g_run.method, g_run.name,
              g_run.count);
    shown = g_run.count < RUN_PARAMS ? g_run.count : RUN_PARAMS;
    for (i = 0; i < shown; i++)
        pg_printf(" %08X", g_run.params[i]);
    pg_printf("%s\n", g_run.count > RUN_PARAMS ? " ..." : "");
}

/* Name without the "+N" index (grouping key of the run). */
static void group_name(const char *name, char *out, size_t outlen) {
    const char *plus = strchr(name, '+');
    size_t n = plus ? (size_t)(plus - name) : strlen(name);
    if (n >= outlen)
        n = outlen - 1;
    memcpy(out, name, n);
    out[n] = 0;
}

static void record(uint32_t sub, uint32_t method, uint32_t param, int non_inc) {
    char name[64], group[64];
    if (!logging()) {
        /* Outside the logged frames there is nothing to group: looking up the method name and
         * comparing it cost ~1 µs per word, tens of ms per frame in 3D. */
        g_run.active = 0;
        return;
    }
    if (g_class[sub & 7] && g_class[sub & 7] != 0x97)
        snprintf(name, sizeof name, "C%02X_M%04X", g_class[sub & 7], method);
    else
        nv097_method_name(method, name, sizeof name);
    group_name(name, group, sizeof group);
    if (g_run.active && g_run.sub == sub && !strcmp(g_run.name, group) &&
        (non_inc || method != g_run.method)) {
        if (g_run.count < RUN_PARAMS)
            g_run.params[g_run.count] = param;
        g_run.count++;
        return;
    }
    flush_run();
    g_run.active = 1;
    g_run.sub = sub;
    g_run.method = method;
    g_run.count = 1;
    g_run.params[0] = param;
    snprintf(g_run.name, sizeof g_run.name, "%s", group);
}

/* RAMHT: handle -> instance (bytes in RAMIN). Hash from xemu pfifo.c (ramht_hash). */
static int ramht_lookup(uint32_t handle, uint32_t *instance) {
    uint32_t r = nv2a_reg_read(PFIFO_RAMHT);
    uint32_t base = ((r >> 4) & 0x1Fu) << 12, size = 1u << (((r >> 16) & 3u) + 12);
    uint32_t bits = 0, hash = 0, h = handle, chid = nv2a_reg_read(PFIFO_CACHE1_PUSH1) & 0x1Fu;
    uint32_t i;
    uint8_t *table = nv2a_ramin(base, size);
    while ((1u << (bits + 1)) < size)
        bits++; /* ctz(size) - 1 */
    while (h) {
        hash ^= h & ((1u << bits) - 1u);
        h >>= bits;
    }
    hash ^= chid << (bits - 4);
    if (!table)
        return 0;
    for (i = 0; i < size / 8; i++) { /* the hash entry or, if it does not fit, linear search */
        uint32_t slot = i ? i - 1 : hash, e[2];
        if (slot * 8 + 8 > size)
            continue;
        memcpy(e, table + slot * 8, 8);
        if (e[0] == handle && (e[1] & 0x80000000u)) {
            *instance = (e[1] & 0xFFFFu) << 4;
            return 1;
        }
    }
    return 0;
}

/* DMA object in RAMIN: base physical address and limit (xemu nv_dma_load). */
static int dma_object(uint32_t instance, uint32_t *phys, uint32_t *limit) {
    uint32_t o[3];
    uint8_t *p = nv2a_ramin(instance, 12);
    if (!p)
        return 0;
    memcpy(o, p, 12);
    if (!o[0] && !o[1] && !o[2])
        return 0;
    *phys = (o[2] & 0xFFFFF000u) | (o[0] >> 20);
    *limit = o[1];
    return 1;
}

static uint32_t bytes_per_pixel(uint32_t color_format) {
    switch (color_format) {
    case 1: case 2: case 3: case 0xA: return 2;
    case 9: return 1;
    default: return 4;
    }
}

static void dump_png(const char *why) {
    uint32_t phys, limit, w = g_clip_h >> 16, h = g_clip_v >> 16, pitch = g_pitch & 0xFFFF;
    uint32_t bpp = bytes_per_pixel(g_format & 0xF), x, y;
    uint8_t *surface, *rgb;
    char path[MAX_PATH + 32];
    if (g_png_count >= MAX_PNG || !w || !h || !g_png_base[0])
        return;
    if (!dma_object(g_dma[(0x194 - 0x180) / 4], &phys, &limit))
        phys = 0;
    phys += g_color_offset;
    if (g_present_offset) { /* the 720-wide buffer that is presented (H7b) */
        phys = g_present_offset;
        pitch = g_present_pitch;
        w = g_present_w;
        h = g_present_h;
        bpp = 4;
    }
    if (!pitch)
        pitch = w * bpp;
    surface = nv2a_phys(phys, pitch * h);
    rgb = (uint8_t *)malloc((size_t)w * h * 3);
    if (!surface || !rgb) {
        free(rgb);
        return;
    }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            const uint8_t *s = surface + (size_t)y * pitch + (size_t)x * bpp;
            uint8_t *d = rgb + ((size_t)y * w + x) * 3;
            if (bpp == 4) { /* X8R8G8B8 / A8R8G8B8 little endian: B, G, R, A */
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
            } else if (bpp == 2) { /* R5G6B5 */
                uint16_t v = (uint16_t)(s[0] | (s[1] << 8));
                d[0] = (uint8_t)(((v >> 11) & 0x1F) * 255 / 31);
                d[1] = (uint8_t)(((v >> 5) & 0x3F) * 255 / 63);
                d[2] = (uint8_t)((v & 0x1F) * 255 / 31);
            } else {
                d[0] = d[1] = d[2] = s[0];
            }
        }
    snprintf(path, sizeof path, "%s-%03d.png", g_png_base, g_png_count);
    if (!png_write_rgb(path, w, h, rgb)) {
        g_png_count++;
        ngb_log("    · PGRAPH: %s -> %s (%ux%u, %u B/pixel, physical 0x%08X)", why, path, w, h, bpp,
                phys);
        pg_printf("== PNG %s: %s (%ux%u)\n", why, path, w, h);
    }
    free(rgb);
}

/* Swizzle (Morton) coordinate of (x, y) in a power-of-two w x h texture (as in xemu,
 * swizzle.c): the bits of x and y are interleaved as long as both have bits left. */
static uint32_t swizzle_offset(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    uint32_t off = 0, bit = 0, mx = 1, my = 1;
    while (mx < w || my < h) {
        if (mx < w) {
            if (x & mx)
                off |= 1u << bit;
            bit++;
            mx <<= 1;
        }
        if (my < h) {
            if (y & my)
                off |= 1u << bit;
            bit++;
            my <<= 1;
        }
    }
    return off;
}

/* Dumps a stage's texture to PNG (32- and 16-bit formats, linear or swizzled). */
static void dump_texture(uint32_t stage) {
    uint32_t fmt = g_tex_format[stage], color = (fmt >> 8) & 0xFF, phys, limit, x, y, bpp;
    uint32_t w = 1u << ((fmt >> 20) & 0xF), h = 1u << ((fmt >> 24) & 0xF), pitch = 0;
    uint32_t dma = g_dma[(((fmt & 3) == 2 ? 0x188u : 0x184u) - 0x180u) / 4];
    int linear, swz;
    uint8_t *src, *rgb;
    const uint8_t *pal = NULL;
    char path[MAX_PATH + 32];
    switch (color) {
    case 0x06: case 0x07: bpp = 4; linear = 0; break; /* A8R8G8B8 / X8R8G8B8 swizzled */
    case 0x12: case 0x1E: bpp = 4; linear = 1; break; /* LU_IMAGE_A8R8G8B8 / X8R8G8B8 */
    case 0x05: bpp = 2; linear = 0; break;             /* R5G6B5 swizzled */
    case 0x11: bpp = 2; linear = 1; break;             /* LU_IMAGE_R5G6B5 */
    case 0x0B: bpp = 1; linear = 0; break;             /* I8_A8R8G8B8: palette index, swizzled */
    case 0x19: bpp = 1; linear = 0; break;             /* A8 swizzled (alpha only) */
    default:
        pg_printf("== texture stage %u: color format 0x%02X not dumped (offset 0x%08X)\n", stage,
                  color, g_tex_offset[stage]);
        return;
    }
    swz = !linear;
    if (linear) {
        w = g_tex_rect[stage] >> 16;
        h = g_tex_rect[stage] & 0xFFFF;
        pitch = g_tex_ctl1[stage] >> 16;
    }
    if (!pitch)
        pitch = w * bpp;
    if (!w || !h || w > 4096 || h > 4096 || !dma_object(dma, &phys, &limit))
        return;
    src = nv2a_phys(phys + g_tex_offset[stage], swz ? w * h * bpp : pitch * h);
    if (color == 0x0B) { /* SET_TEXTURE_PALETTE: dma in bits 0-1, address aligned to 64 */
        uint32_t pdma = g_dma[(((g_tex_palette[stage] & 3) == 2 ? 0x188u : 0x184u) - 0x180u) / 4];
        uint32_t pphys, plimit;
        if (dma_object(pdma, &pphys, &plimit))
            pal = nv2a_phys(pphys + (g_tex_palette[stage] & ~0x3Fu), 1024);
    }
    rgb = (uint8_t *)malloc((size_t)w * h * 3);
    if (!src || !rgb) {
        free(rgb);
        return;
    }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            const uint8_t *p = src + (swz ? swizzle_offset(x, y, w, h) * bpp
                                          : (size_t)y * pitch + (size_t)x * bpp);
            uint8_t *d = rgb + ((size_t)y * w + x) * 3;
            if (bpp == 4) {
                d[0] = p[2];
                d[1] = p[1];
                d[2] = p[0];
            } else if (bpp == 1 && color == 0x0B && pal) {
                const uint8_t *e = pal + (size_t)p[0] * 4; /* A8R8G8B8: B, G, R, A */
                d[0] = e[2];
                d[1] = e[1];
                d[2] = e[0];
            } else if (bpp == 1) {
                d[0] = d[1] = d[2] = p[0];
            } else {
                uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
                d[0] = (uint8_t)(((v >> 11) & 0x1F) * 255 / 31);
                d[1] = (uint8_t)(((v >> 5) & 0x3F) * 255 / 63);
                d[2] = (uint8_t)((v & 0x1F) * 255 / 31);
            }
        }
    snprintf(path, sizeof path, "%s-tex%02d.png", g_png_base, g_tex_png);
    if (!png_write_rgb(path, w, h, rgb)) {
        g_tex_png++;
        ngb_log("    · PGRAPH: texture stage %u (format 0x%02X %s, %ux%u, physical 0x%08X) -> %s",
                stage, color, linear ? "linear" : "swizzle", w, h, phys + g_tex_offset[stage], path);
        pg_printf("== PNG texture stage %u, format 0x%02X, %ux%u: %s\n", stage, color, w, h, path);
    }
    free(rgb);
}

/* At the start of a primitive: dumps the active textures not seen yet. */
static void dump_active_textures(void) {
    uint32_t s, i;
    if (!g_ngb_opts.debug_shots) /* development aid: only with --debug-shots */
        return;
    for (s = 0; s < 4 && g_tex_png < MAX_TEX_PNG; s++) {
        int seen = 0;
        if (!(g_tex_ctl0[s] & 0x40000000u)) /* SET_TEXTURE_CONTROL0_ENABLE */
            continue;
        for (i = 0; i < g_tex_nseen; i++)
            if (g_tex_seen[i][0] == g_tex_offset[s] && g_tex_seen[i][1] == g_tex_format[s])
                seen = 1;
        if (seen || g_tex_nseen >= MAX_TEX_SEEN)
            continue;
        g_tex_seen[g_tex_nseen][0] = g_tex_offset[s];
        g_tex_seen[g_tex_nseen][1] = g_tex_format[s];
        g_tex_nseen++;
        dump_texture(s);
    }
}

/* Current color surface as the rasterizer's target. 0 if there is none. */
static int color_target(rs_target_t *t) {
    uint32_t phys, limit, pitch = g_pitch & 0xFFFF, w = g_clip_h >> 16, h = g_clip_v >> 16;
    uint32_t cf = g_format & 0xF;
    memset(t, 0, sizeof *t);
    if (((g_format >> 8) & 0xF) == 2 || !w || !h || !pitch) /* swizzled: not supported */
        return 0;
    if (!dma_object(g_dma[(0x194 - 0x180) / 4], &phys, &limit))
        phys = 0;
    t->bpp = (cf == 3 || cf == 1 || cf == 2) ? 2 : 4;
    t->width = w + (g_clip_h & 0xFFFF);
    t->height = h + (g_clip_v & 0xFFFF);
    /* The rasterizer writes rows [0, height) and columns [0, width): the whole buffer has to
     * fit in the GPU RAM and each row in the pitch (data sent by the game). */
    if ((uint64_t)t->width * t->bpp > pitch || (uint64_t)pitch * t->height > 0x04000000u)
        return 0;
    t->pixels = nv2a_phys(phys + g_color_offset, pitch * t->height);
    if (!t->pixels)
        return 0;
    t->pitch = pitch;
    t->clip_x0 = (int)(g_clip_h & 0xFFFF);
    t->clip_y0 = (int)(g_clip_v & 0xFFFF);
    t->clip_x1 = (int)t->width;
    t->clip_y1 = (int)t->height;
    if (w == 720) { /* the buffer that will end up on screen */
        g_present_offset = phys + g_color_offset;
        g_present_pitch = pitch;
        g_present_w = w;
        g_present_h = h;
    }
    return 1;
}

/* Stage 0 texture for the rasterizer (dump_texture formats). */
static int stage0_texture(rs_texture_t *tex) {
    uint32_t fmt = g_tex_format[0], color = (fmt >> 8) & 0xFF, phys, limit, bpp;
    uint64_t size;
    uint32_t dma = g_dma[(((fmt & 3) == 2 ? 0x188u : 0x184u) - 0x180u) / 4];
    memset(tex, 0, sizeof *tex);
    if (!(g_tex_ctl0[0] & 0x40000000u) || !dma_object(dma, &phys, &limit))
        return 0;
    tex->width = 1u << ((fmt >> 20) & 0xF);
    tex->height = 1u << ((fmt >> 24) & 0xF);
    tex->swizzled = 1;
    tex->normalized = 1;
    switch (color) {
    case 0x06: tex->format = RS_TEX_A8R8G8B8; break;
    case 0x07: tex->format = RS_TEX_X8R8G8B8; break;
    case 0x05: tex->format = RS_TEX_R5G6B5; break;
    case 0x19: tex->format = RS_TEX_A8; break;
    case 0x0B: tex->format = RS_TEX_I8; break;
    case 0x12: case 0x1E: case 0x11: /* LU_IMAGE: linear, coordinates in texels */
        tex->format = color == 0x11 ? RS_TEX_R5G6B5 : color == 0x12 ? RS_TEX_A8R8G8B8 : RS_TEX_X8R8G8B8;
        tex->swizzled = 0;
        tex->normalized = 0;
        tex->width = g_tex_rect[0] >> 16;
        tex->height = g_tex_rect[0] & 0xFFFF;
        tex->pitch = g_tex_ctl1[0] >> 16;
        break;
    default:
        return 0;
    }
    /* Sizes sent by the game: up to 4096 per side (as in the dump) and in 64 bits, so that
     * a 32768x32768 does not give 0 through overflow and allow reads outside the GPU RAM. */
    if (!tex->width || !tex->height || tex->width > 4096 || tex->height > 4096)
        return 0;
    bpp = tex->format == RS_TEX_R5G6B5 ? 2u : tex->format >= RS_TEX_A8 ? 1u : 4u;
    if (!tex->pitch)
        tex->pitch = tex->width * bpp;
    if (!tex->swizzled && tex->pitch < tex->width * bpp)
        return 0;
    size = tex->swizzled ? (uint64_t)tex->width * tex->height * bpp : (uint64_t)tex->pitch * tex->height;
    if (size > 0x04000000u)
        return 0;
    tex->data = nv2a_phys(phys + g_tex_offset[0], (uint32_t)size);
    if (tex->format == RS_TEX_I8) {
        uint32_t pdma = g_dma[(((g_tex_palette[0] & 3) == 2 ? 0x188u : 0x184u) - 0x180u) / 4];
        uint32_t pphys, plimit;
        if (dma_object(pdma, &pphys, &plimit))
            tex->palette = nv2a_phys(pphys + (g_tex_palette[0] & ~0x3Fu), 1024);
    }
    return tex->data != NULL;
}

static float word_float(uint32_t w) {
    float f;
    memcpy(&f, &w, 4);
    return f;
}

static void unpack_color(uint32_t c, int ogl, rs_vertex_t *v) {
    if (ogl) /* UB_OGL: bytes R, G, B, A in memory */
        c = (c & 0xFF00FF00u) | ((c & 0xFFu) << 16) | ((c >> 16) & 0xFFu);
    v->r = (float)((c >> 16) & 0xFF) / 255.0f;
    v->g = (float)((c >> 8) & 0xFF) / 255.0f;
    v->b = (float)(c & 0xFF) / 255.0f;
    v->a = (float)(c >> 24) / 255.0f;
}

/* Words an attribute with this format takes in INLINE_ARRAY. */
static uint32_t attr_words(uint32_t f) {
    uint32_t type = f & 0xF, size = (f >> 4) & 0xF;
    if (!size)
        return 0;
    switch (type) {
    case 0: case 4: case 6: return 1;           /* UB_D3D, UB_OGL (4 bytes), CMP */
    case 1: case 5: return (size + 1) / 2;      /* S1, S32K: shorts */
    default: return size;                       /* F: floats */
    }
}

static void aidx_push(uint32_t v) {
    if (g_aidx_n == g_aidx_cap) {
        uint32_t cap = g_aidx_cap ? g_aidx_cap * 2 : 4096, *p;
        /* The cap is checked before the realloc: otherwise the old block would already have
           moved and g_aidx would be left dangling (the counts come from the game). */
        if (cap > (1u << 24) || (p = (uint32_t *)realloc(g_aidx, (size_t)cap * 4)) == NULL)
            return;
        g_aidx = p;
        g_aidx_cap = cap;
    }
    g_aidx[g_aidx_n++] = v;
}

int pgraph_dma_base(uint32_t method, uint32_t *phys) {
    uint32_t limit;
    if (method < 0x180 || method >= 0x200)
        return 0;
    if (!dma_object(g_dma[(method - 0x180) / 4], phys, &limit))
        *phys = 0; /* no DMA object: offsets as physical (same as the rasterizer) */
    return 1;
}

/* Closes the primitive in progress with OpenGL (on the render thread). */
static void gl_draw_end(void) {
    pgl_draw_t d;
    memset(&d, 0, sizeof d);
    d.prim = g_prim;
    if (g_inline_n) {
        d.inline_words = g_inline;
        d.n_inline = g_inline_n;
    } else if (g_aidx_n) {
        d.indices = g_aidx;
        d.n_indices = g_aidx_n;
    } else {
        return;
    }
    pgl_draw(&g_r, &d);
}

/* INLINE_ARRAY vertices (XYZRHW screen position, diffuse, texcoord0) -> triangles. */
static void draw_inline(void) {
    rs_target_t t;
    rs_texture_t tex;
    rs_state_t st;
    rs_vertex_t *vs;
    uint32_t per = 0, n, i, slot;
    if (!g_prim || !g_inline_n)
        return;
    for (slot = 0; slot < 16; slot++)
        per += attr_words(g_vfmt[slot]);
    /* A 4-float position = already transformed XYZRHW; the 3D one (3 floats) needs the
     * vertex program interpreter (next step). */
    if (!per || (g_vfmt[0] & 0xF) != 2 || ((g_vfmt[0] >> 4) & 0xF) != 4) {
        g_skipped_draws++;
        g_skip_pos++;
        return;
    }
    if (!color_target(&t)) {
        g_skipped_draws++;
        g_skip_target++;
        return;
    }
    n = g_inline_n / per;
    vs = (rs_vertex_t *)calloc(n ? n : 1, sizeof *vs);
    if (!vs)
        return;
    for (i = 0; i < n; i++) {
        const uint32_t *w = g_inline + (size_t)i * per;
        rs_vertex_t *v = &vs[i];
        unpack_color(g_vconst_diffuse, 0, v);
        for (slot = 0; slot < 16; slot++) {
            uint32_t f = g_vfmt[slot], words = attr_words(f), type = f & 0xF;
            if (!words)
                continue;
            if (slot == 0) {
                v->x = word_float(w[0]);
                v->y = word_float(w[1]);
                v->z = word_float(w[2]);
                v->rhw = word_float(w[3]);
            } else if (slot == 3) {
                if (type == 2 && words >= 4) {
                    v->r = word_float(w[0]);
                    v->g = word_float(w[1]);
                    v->b = word_float(w[2]);
                    v->a = word_float(w[3]);
                } else {
                    unpack_color(w[0], type == 4, v);
                }
            } else if (slot == 9 && type == 2) {
                v->u = word_float(w[0]);
                v->v = words > 1 ? word_float(w[1]) : 0.0f;
            }
            w += words;
        }
    }
    memset(&st, 0, sizeof st);
    st.blend = (int)g_blend_enable;
    st.sfactor = g_sfactor;
    st.dfactor = g_dfactor;
    st.alpha_test = (int)g_alpha_test;
    st.alpha_func = g_alpha_func;
    st.alpha_ref = g_alpha_ref & 0xFF;
    st.use_texture = stage0_texture(&tex);
#define TRI(a, b, c)                                                                      \
    do {                                                                                  \
        g_drawn_pixels += rs_draw_triangle(&t, &tex, &st, &vs[a], &vs[b], &vs[c]);        \
        g_drawn_tris++;                                                                   \
    } while (0)
    switch (g_prim) {
    case 5: /* TRIANGLES */
        for (i = 0; i + 2 < n; i += 3)
            TRI(i, i + 1, i + 2);
        break;
    case 6: /* TRIANGLE_STRIP */
        for (i = 0; i + 2 < n; i++)
            TRI(i, i + 1, i + 2);
        break;
    case 7: case 10: /* TRIANGLE_FAN, POLYGON */
        for (i = 1; i + 1 < n; i++)
            TRI(0, i, i + 1);
        break;
    case 8: /* QUADS */
        for (i = 0; i + 3 < n; i += 4) {
            TRI(i, i + 1, i + 2);
            TRI(i, i + 2, i + 3);
        }
        break;
    case 9: /* QUAD_STRIP */
        for (i = 0; i + 3 < n; i += 2) {
            TRI(i, i + 1, i + 3);
            TRI(i, i + 3, i + 2);
        }
        break;
    default:
        g_skipped_draws++; /* points and lines: not drawn */
        g_skip_prim++;
        break;
    }
#undef TRI
    free(vs);
}

static void inline_push(uint32_t w) {
    if (g_inline_n == g_inline_cap) {
        uint32_t cap = g_inline_cap ? g_inline_cap * 2 : 4096, *p;
        /* The cap is checked before the realloc: otherwise the old block would already have
           moved and g_inline would be left dangling (the counts come from the game). */
        if (cap > (1u << 24) || (p = (uint32_t *)realloc(g_inline, (size_t)cap * 4)) == NULL)
            return;
        g_inline = p;
        g_inline_cap = cap;
    }
    g_inline[g_inline_n++] = w;
}

/* At most one present per vblank, like FLIP_STALL on the console, which waits for the vblank
 * after the previous flip. The vblank is the port's synthetic one (the DPC thread), the clock the
 * game itself runs on. Before, the pacing kept its own 1/60 s schedule: two 60 Hz clocks with
 * an arbitrary phase, so a frame that came a millisecond late for its slot waited a whole
 * frame. 20 to 70 frames of every 300 took 33 ms followed by an instant one, even in the menus:
 * a steady 60 fps average with a visible stutter. Without vblanks for 100 ms (early boot, or
 * the game masking the interrupt) it falls back to 1/60 s of wall clock. */
static void pace(void) {
    static uint32_t seen;       /* last vblank count seen, and when */
    static LONGLONG seen_t, next; /* next: the timed schedule, used only without vblanks */
    LONGLONG t1 = qpc(), now = t1;
    LARGE_INTEGER f;
    uint32_t vb = nv2a_vblank_count();
    if (g_ngb_opts.no_pacing)
        return;
    QueryPerformanceFrequency(&f);
    if (vb != seen || !seen_t) {
        seen = vb;
        seen_t = t1;
    }
    if (t1 - seen_t < f.QuadPart / 10) {
        /* Vblanks are running: wait for the next one. The 50 ms cap is far above their jitter;
         * a shorter one let a late vblank through twice and the game ran fast. */
        LONGLONG limit = t1 + f.QuadPart / 20;
        while (nv2a_vblank_count() == g_pace_vblank && (now = qpc()) < limit)
            WaitForSingleObject(g_vblank_evt, 2);
        next = 0;
    } else {
        /* No vblank for 100 ms (early boot, or the interrupt masked): 1/60 s of wall clock. */
        if (!next || t1 > next + f.QuadPart / 10)
            next = t1;
        next += f.QuadPart / 60;
        while ((now = qpc()) < next) {
            LONGLONG ms = (next - now) * 1000 / f.QuadPart;
            Sleep(ms > 1 ? (DWORD)(ms - 1) : 0);
        }
    }
    g_pace_vblank = nv2a_vblank_count();
    InterlockedExchangeAdd64(&g_perf.pace, qpc() - t1);
}

/* Called by nv2a.c on returning from the kick, with the NV2A lock and g_pg_lock already
 * released (issue #35): the pacing Sleep no longer blocks the vblank thread or other MMIO
 * accesses. With the asynchronous pusher the pacing belongs to the render thread
 * (kick_worker): the game does not wait here. */
void pgraph_pace(void) {
    if (g_gl)
        return;
    if (InterlockedExchange(&g_pace_pending, 0))
        pace();
}

/* FLIP_STALL: presents the 720-wide buffer in the window and leaves the pacing pending
 * until the locks are released. If the same kick already carried another FLIP_STALL without
 * waiting (looping push buffers, like the video one), that pacing is done here. */
static void present_and_pace(void) {
    uint8_t *fb;
    LONGLONG t0 = qpc();
    if (InterlockedExchange(&g_pace_pending, 0))
        pace();
    if (g_gl) {
        char path[MAX_PATH + 32];
        int shot = SHOT_DUE(), user = 0;
        if (shot)
            snprintf(path, sizeof path, "%s-shot-%05u.png", g_png_base, g_flips);
        else
            user = gfx_take_shot_request(path, sizeof path); /* F12 */
        if (!pgl_flip(&g_r, shot || user ? path : NULL))
            ngb_log("    · gfx: capture of frame %u -> %s (%.1f fps, GL)", g_flips, path, gfx_fps());
        else if (user)
            ngb_log("    · gfx: could not save the screenshot %s", path);
    } else if (g_present_offset && g_ngb_opts.window) {
        fb = nv2a_phys(g_present_offset, g_present_pitch * g_present_h);
        if (fb) {
            gfx_present(fb, (int)g_present_w, (int)g_present_h, (int)g_present_pitch,
                        GFX_FMT_X8R8G8B8);
            if (SHOT_DUE()) {
                char path[MAX_PATH + 32], used[MAX_PATH + 32];
                snprintf(path, sizeof path, "%s-shot-%05u.png", g_png_base, g_flips);
                if (!gfx_screenshot(path, used, sizeof used))
                    ngb_log("    · gfx: capture of frame %u -> %s (%.1f fps)", g_flips, used, gfx_fps());
            }
        }
    }
    g_perf.present += qpc() - t0;
    InterlockedExchange(&g_pace_pending, 1);
}

/* Color CLEAR_SURFACE: fills the surface's clear rectangle. */
static void do_clear(uint32_t flags) {
    uint32_t phys, limit, pitch = g_pitch & 0xFFFF, bpp = bytes_per_pixel(g_format & 0xF);
    uint32_t x0 = g_clear_h & 0xFFFF, x1 = g_clear_h >> 16, y0 = g_clear_v & 0xFFFF;
    uint32_t y1 = g_clear_v >> 16, x, y, v = g_clear_value;
    uint8_t *surface;
    g_clears++;
    if (!(flags & 0xF0))
        return;
    g_color_clears++;
    if (!dma_object(g_dma[(0x194 - 0x180) / 4], &phys, &limit))
        phys = 0;
    if (!x1 && !y1) {
        x1 = (g_clip_h >> 16) - 1;
        y1 = (g_clip_v >> 16) - 1;
    }
    if (!pitch)
        pitch = ((g_clip_h >> 16) ? (g_clip_h >> 16) : 640) * bpp;
    /* Each row of the rectangle has to fit in the pitch: otherwise the write would overwrite the
     * next row or run past the buffer. */
    if (x1 >= 4096 || y1 >= 4096 || x0 > x1 || y0 > y1 || (x1 + 1) * bpp > pitch)
        return;
    if ((g_clip_h >> 16) == 720) {
        g_present_offset = phys + g_color_offset;
        g_present_pitch = pitch;
        g_present_w = 720;
        g_present_h = g_clip_v >> 16;
    }
    surface = nv2a_phys(phys + g_color_offset, pitch * (y1 + 1));
    if (!surface)
        return;
    if (bpp == 2)
        v = ((v >> 8) & 0xF800) | ((v >> 5) & 0x07E0) | ((v >> 3) & 0x001F);
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            uint8_t *p = surface + (size_t)y * pitch + (size_t)x * bpp;
            if (bpp == 4)
                memcpy(p, &v, 4);
            else if (bpp == 2)
                memcpy(p, &v, 2);
            else
                *p = (uint8_t)v;
        }
    if (g_color_clears == 1)
        dump_png("first color clear");
}

/*
 * HLE of the static push buffer "fixup" of D3D (D3DDevice_RunPushBuffer, 0x3CD680).
 *
 * D3D jumps from its ring to a static push buffer that ends in a JUMP back, and rewrites
 * that JUMP on every use. If the GPU has not finished the previous use yet, it cannot
 * rewrite it yet: it puts into the ring a NO_OPERATION (0x100) with a parameter that on the
 * Xbox halts the GPU and raises an interrupt; the D3D routine then does the patching
 * (the same that FUN_003cd5e0 does directly when the GPU is already past) and the GPU
 * continues with the JUMP to the push buffer. There is no interrupt here: the patching is
 * done on the spot.
 *   parameter (i << 5) | 0xD: record i of the device table (dev + 0x264 + i·20);
 *   parameter (va << 5) | 0xC: record in the ring itself, at va (contiguous, 0x8xxxxxxx).
 * Record (5 words): [0] return in the ring | flags, [1] next record, [2] final word of the
 * push buffer (where the JUMP back goes), [3] fixup list, [4] base.
 * Without this, the JUMP back kept pointing to the previous use: the emulated GPU looped
 * up to the 4 M word cap, overwrote the D3D fence with old values and the game hung in
 * D3D_BlockOnTime (at the start of the title demo).
 */
#define D3D_G_PDEVICE_REF 0x003DAA88u /* D3D_g_pDevice of the reference XBE (H5 symbols.csv) */
/* The same global in the loaded XBE (D3D is linked into its own section: libaddr.c). */
static uint32_t d3d_g_pdevice_va(void) {
    static uint32_t va;
    static int resolved;
    if (!resolved) {
        resolved = 1;
        va = libaddr_va(&g_ngb_xbe, D3D_G_PDEVICE_REF);
        if (!va)
            ngb_log("pgraph: D3D_g_pDevice not found in this XBE: no push buffer fixups");
    }
    return va;
}
static uint32_t g_pb_fixups;

/* Accessible game memory: the committed part of the contiguous RAM, or the XBE image. The
 * pointers come from the game and can be stale (a record in the ring that the game already
 * rewrote): one into an arena page the game never allocated crashed the render thread. */
static void *game_mem(uint32_t va, uint32_t len) {
    uint32_t top = ngb_contig_committed();
    if (va >= NGB_CONTIG_BASE && va - NGB_CONTIG_BASE < top && len <= top - (va - NGB_CONTIG_BASE))
        return (void *)(uintptr_t)va;
    if (va >= NGB_RESERVE_BASE && va < NGB_RESERVE_END && len <= NGB_RESERVE_END - va)
        return (void *)(uintptr_t)va;
    return NULL;
}

static void d3d_apply_fixups(uint32_t list, uint32_t base) {
    int n;
    for (n = 0; n < 4096; n++) {
        uint32_t *hdr = (uint32_t *)game_mem(list, 8), size;
        void *dst, *src;
        if (!hdr || hdr[0] == 0xFFFFFFFFu)
            return;
        size = hdr[0];
        src = game_mem(list + 8, size);
        dst = game_mem(base + hdr[1], size);
        if (!src || !dst || size > 0x100000u)
            return;
        memmove(dst, src, size);
        list += 8 + size;
    }
}

/* Last record closed by the fixup (its next was left at 0xFFFFFFFF), or 0. */
static uint32_t g_fix_tail;
static uint32_t g_pb_fixups_late;

/* Walks the record chain from rec, like FUN_003cd5e0. */
static void d3d_fixup_chain(uint32_t rec) {
    uint32_t n;
    for (n = 0; n < 64 && rec != 0 && rec != 0xFFFFFFFFu; n++) {
        uint32_t *r = (uint32_t *)game_mem(rec, 20), *end, w0, next;
        if (!r || (end = (uint32_t *)game_mem(r[2], 4)) == NULL) {
            ngb_log("    · PGRAPH: push buffer fixup with invalid record 0x%08X", rec);
            g_fix_tail = 0;
            return;
        }
        w0 = r[0];
        *end = (w0 & 0x0FFFFFFFu) + 1u; /* JUMP back to the ring */
        if (w0 & 0x20000000u)
            d3d_apply_fixups(r[3], r[4]);
        if ((int32_t)w0 < 0)
            r[0] = w0 & 0xBFFFFFFFu;
        /* On the Xbox the ISR does this; here it runs in parallel with the game, which in
           D3DDevice_RunPushBuffer reads the next one of the last record and, if it is 0, chains a
           new one to it. Reading and closing in two steps lost that record (its JUMP back stayed
           stale and the GPU looped up to the cap: the render hang). */
        next = (uint32_t)InterlockedExchange((volatile LONG *)&r[1], (LONG)0xFFFFFFFFu);
        g_fix_tail = rec;
        rec = next;
    }
}

/* The game can still chain a record to the one we just closed (it read the 0 before the
 * close and writes after). That record reaches the GPU without an interrupt of its own: it is
 * served before following a jump, and the ring's JUMP to the static push buffer goes after the link. */
static void d3d_fixup_late(void) {
    uint32_t *r, *dev, *last, next;
    if (!g_fix_tail)
        return;
    dev = (uint32_t *)game_mem(d3d_g_pdevice_va(), 4);
    r = (uint32_t *)game_mem(g_fix_tail, 20);
    last = dev && *dev ? (uint32_t *)game_mem(*dev + 0x58u, 4) : NULL; /* last D3D record */
    if (!r || !last) {
        g_fix_tail = 0;
        return;
    }
    if (*last == g_fix_tail) /* D3D has not created another record: it can still chain it */
        return;
    next = r[1];
    g_fix_tail = 0; /* D3D moved on to another record: this one is no longer chained */
    if (next == 0xFFFFFFFFu || next == 0)
        return;
    {
        /* A real link leads, following the nexts, to D3D's last record. If
           not, the record was in the ring and the game has already rewritten it: it is garbage. */
        uint32_t k, at = next, *q;
        for (k = 0; k < 64 && at != *last; k++) {
            if ((q = (uint32_t *)game_mem(at, 20)) == NULL || q[1] == 0 || q[1] == 0xFFFFFFFFu)
                return;
            at = q[1];
        }
        if (at != *last)
            return;
    }
    next = (uint32_t)InterlockedExchange((volatile LONG *)&r[1], (LONG)0xFFFFFFFFu);
    if (next == 0xFFFFFFFFu || next == 0)
        return;
    g_pb_fixups_late++;
    d3d_fixup_chain(next);
}

static void d3d_pushbuffer_fixup(uint32_t param) {
    uint32_t *dev = (uint32_t *)game_mem(d3d_g_pdevice_va(), 4);
    if ((param & 0x1Fu) == 0xDu) {
        if (!dev)
            return;
        d3d_fixup_chain(*dev + 0x99u * 4u + (param >> 5) * 20u);
    } else {
        d3d_fixup_chain((param >> 5) | 0x80000000u);
    }
    /* The D3D ISR discounts the pending fixup interrupts (dev + 0x50) at 0xC and 0xD,
       not at 0xE. D3D only chains records while some is pending. */
    if ((param & 0x1Fu) != 0xEu && dev && *dev) {
        LONG *pending = (LONG *)game_mem(*dev + 0x50u, 4);
        if (pending && *pending > 0)
            InterlockedDecrement(pending);
    }
    g_pb_fixups++;
}

/* --dump-ram FRAMES and F10: guest RAM snapshot next to the log, to diff game state (finding
 * flags and variables for mods). */
static void maybe_dump_ram(void) {
    static int was_down;
    char path[MAX_PATH], tag[32], *dot;
    const char *list = g_ngb_opts.dump_ram;
    int down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0, hit = down && !was_down;
    was_down = down;
    while (!hit && list && *list) {
        char *next;
        unsigned long f = strtoul(list, &next, 0);
        if (next == list)
            break;
        hit = f == g_flips;
        list = *next == ',' ? next + 1 : next;
    }
    if (!hit)
        return;
    snprintf(path, sizeof path, "%s", g_ngb_opts.log_path);
    if ((dot = strrchr(path, '.')) != NULL && !strchr(dot, '\\'))
        *dot = 0;
    snprintf(tag, sizeof tag, "-ram-%06u.bin", g_flips);
    if (strlen(path) + strlen(tag) < sizeof path)
        strcat(path, tag);
    ngb_dump_guest_ram(path);
}

static void on_method(void *user, uint32_t sub, uint32_t method, uint32_t param, int non_inc) {
    uint32_t inst = 0;
    (void)user;
    g_words++;
    g_hist[method / 4]++;
    if (!g_frame0_done)
        g_hist_frame0[method / 4]++;
    record(sub, method, param, non_inc);
    if (method == 0x0100 && (param & 0x1Fu) >= 0xCu && (param & 0x1Fu) <= 0xEu) {
        d3d_pushbuffer_fixup(param); /* NO_OPERATION with interrupt: D3D fixup */
        return;
    }
    if (method == 0x0100 && (param & 0x1Fu) == 5u) {
        /* D3D_BlockOnTime, with the GPU far behind, puts in NO_OPERATION(5) and waits for the device
         * KEVENT (+0x773 words) that its ISR signals when the GPU gets there. */
        uint32_t *dev = (uint32_t *)game_mem(d3d_g_pdevice_va(), 4);
        LONG h = InterlockedIncrement(&g_nop5_n) - 1;
        g_nop5_hist[h & 7].at = g_pb.cur;
        g_nop5_hist[h & 7].vblank = nv2a_vblank_count();
        if (dev && *dev)
            ngb_ke_set_event(*dev + 0x773u * 4u);
        g_block_wakes++;
        return;
    }
    if (method == 0) { /* SET_OBJECT: the handle is looked up in RAMHT; the class is in RAMIN */
        uint8_t *obj;
        if (ramht_lookup(param, &inst) && (obj = nv2a_ramin(inst, 4)) != NULL) {
            g_class[sub & 7] = obj[0];
            flush_run();
            if (logging())
                pg_printf("   subchannel %u = object 0x%08X, class 0x%02X (instance 0x%05X)\n", sub,
                          param, obj[0], inst);
        }
        return;
    }
    if (method >= 0x180 && method < 0x200) { /* SET_CONTEXT_DMA_*: handle -> instance */
        if (!g_class[sub & 7] || g_class[sub & 7] == 0x97) /* those of the 3D class */
            g_dma[(method - 0x180) / 4] = ramht_lookup(param, &inst) ? inst : 0;
        return;
    }
    if (g_class[sub & 7] == 0x44 && method == 0x0310) {
        /* Pattern, SET_MONOCHROME_COLOR0: the hardware leaves it in PGRAPH_PATT_COLOR0 (0x400B10).
         * D3D uses it as a marker: when waiting for the GPU it compares its bits 2-6 with its fence. */
        InterlockedExchange(&g_patt_color0, (LONG)param);
        return;
    }
    if (g_class[sub & 7] && g_class[sub & 7] != 0x97)
        return; /* 2D objects (blit, surfaces): only logged */
    if (g_gl)
        pgl_regs_track(&g_r, method, param);
    switch (method) {
    case 0x0200: g_clip_h = param; break;
    case 0x0204: g_clip_v = param; break;
    case 0x0208: g_format = param; break;
    case 0x020C: g_pitch = param; break;
    case 0x0210: g_color_offset = param; break;
    case 0x1D90: g_clear_value = param; break;
    case 0x1D98: g_clear_h = param; break;
    case 0x1D9C: g_clear_v = param; break;
    case 0x1D94: {
        LONGLONG t0 = qpc();
        if (g_gl)
            pgl_clear(&g_r, param);
        else
            do_clear(param);
        g_perf.clear += qpc() - t0;
        break;
    }
    case 0x17FC:
        if (!param) {
            LONGLONG t0 = qpc();
            if (g_gl)
                gl_draw_end();
            else
                draw_inline();
            g_perf.draw += qpc() - t0;
            g_prim = 0;
            g_inline_n = 0;
            g_aidx_n = 0;
        } else {
            g_prim = param;
            g_inline_n = 0;
            g_aidx_n = 0;
        }
        if (param) {
            if (g_first_draw_frame == 0xFFFFFFFFu) {
                g_first_draw_frame = g_frame;
                pg_printf("==== first BEGIN_END with primitives in frame %u\n", g_frame);
            }
            g_begin_end++;
            dump_active_textures();
            g_prims[param & 15]++;
        }
        break;
    case 0x1810: /* DRAW_ARRAYS: start in bits 0-23, vertex count - 1 in 24-31 */
        g_draw_arrays++;
        if (g_gl) {
            uint32_t k, first = param & 0xFFFFFF, count = (param >> 24) + 1;
            for (k = 0; k < count; k++)
                aidx_push(first + k);
        }
        break;
    case 0x1800: /* ARRAY_ELEMENT16: two indices per word, the low one first */
        if (g_gl) {
            aidx_push(param & 0xFFFF);
            aidx_push(param >> 16);
        }
        break;
    case 0x1808: /* ARRAY_ELEMENT32 */
        if (g_gl)
            aidx_push(param);
        break;
    case 0x1818: inline_push(param); break; /* INLINE_ARRAY */
    case 0x194C: g_vconst_diffuse = param; break; /* SET_VERTEX_DATA4UB of the diffuse */
    case 0x0300: g_alpha_test = param & 1; break;
    case 0x0304: g_blend_enable = param & 1; break;
    case 0x033C: g_alpha_func = param; break;
    case 0x0340: g_alpha_ref = param; break;
    case 0x0344: g_sfactor = param; break;
    case 0x0348: g_dfactor = param; break;
    case 0x1E94: g_exec_mode = param; break;
    case 0x1B04: case 0x1B44: case 0x1B84: case 0x1BC4:
        g_tex_format[(method - 0x1B00) / 0x40] = param;
        break;
    case 0x1B0C: case 0x1B4C: case 0x1B8C: case 0x1BCC:
        g_tex_ctl0[(method - 0x1B00) / 0x40] = param;
        break;
    case 0x1B10: case 0x1B50: case 0x1B90: case 0x1BD0:
        g_tex_ctl1[(method - 0x1B00) / 0x40] = param;
        break;
    case 0x1B20: case 0x1B60: case 0x1BA0: case 0x1BE0:
        g_tex_palette[(method - 0x1B00) / 0x40] = param;
        break;
    case 0x1B1C: case 0x1B5C: case 0x1B9C: case 0x1BDC:
        g_tex_rect[(method - 0x1B00) / 0x40] = param;
        break;
    case 0x1B00: case 0x1B40: case 0x1B80: case 0x1BC0: { /* SET_TEXTURE_OFFSET[stage] */
        uint32_t k = (param * 0x9E3779B1u) >> 19; /* 13 bits: TEX_SET_SLOTS */
        g_tex_offset[(method - 0x1B00) / 0x40] = param;
        g_tex_sets++;
        while (g_tex_set[k] && g_tex_set[k] != param + 1)
            k = (k + 1) & (TEX_SET_SLOTS - 1);
        if (!g_tex_set[k] && g_ntex < MAX_TEXTURES) { /* half full at most: the probe ends */
            g_tex_set[k] = param + 1;
            g_ntex++;
        }
        break;
    }
    case 0x1D6C: g_sem_offset = param; break;
    case 0x1760: case 0x1764: case 0x1768: case 0x176C: case 0x1770: case 0x1774:
    case 0x1778: case 0x177C: case 0x1780: case 0x1784: case 0x1788: case 0x178C:
    case 0x1790: case 0x1794: case 0x1798: case 0x179C:
        g_vfmt[(method - 0x1760) / 4] = param;
        break;
    case 0x1D70: { /* BACK_END_WRITE_SEMAPHORE_RELEASE: D3D waits to see this value */
        uint32_t phys, limit;
        uint8_t *p;
        if (dma_object(g_dma[(0x1A4 - 0x180) / 4], &phys, &limit) &&
            (p = nv2a_phys(phys + g_sem_offset, 4)) != NULL)
            memcpy(p, &param, 4);
        g_sem_releases++;
        break;
    }
    case 0x0130: /* FLIP_STALL: end of frame */
        flush_run();
        g_flips++;
        maybe_dump_ram();
        present_and_pace();
        {
            static LONGLONG over; /* 25 ms in QPC ticks */
            LONGLONG now = qpc(), work = g_perf.draw + g_perf.clear;
            if (!over) {
                LARGE_INTEGER f;
                QueryPerformanceFrequency(&f);
                over = f.QuadPart / 40;
            }
            if (g_fr.last_flip) {
                LONGLONG dt = now - g_fr.last_flip;
                if (dt > g_fr.worst) {
                    g_fr.worst = dt;
                    g_fr.worst_work = work - g_fr.work0;
                }
                if (dt > over)
                    g_fr.slow++;
            }
            g_fr.last_flip = now;
            g_fr.work0 = work;
        }
        if (g_flips % PERF_EVERY == 0)
            perf_report();
        if (!g_gl && (g_flips <= PNG_FIRST_FLIPS || g_flips % 100 == 0)) {
            char why[48];
            snprintf(why, sizeof why, "FLIP_STALL of frame %u", g_frame);
            dump_png(why);
        }
        if (logging())
            pg_printf("==== end of frame %u (FLIP_STALL): %u BEGIN_END, %u DRAW_ARRAYS, %u clears\n",
                      g_frame, g_begin_end, g_draw_arrays, g_clears);
        if (!g_frame0_done)
            g_frame0_done = 1;
        g_frame++;
        break;
    default: break;
    }
}

/* First jumps/calls of the kick in progress, to explain a huge kick (kick_body). */
static struct {
    int kind;
    uint32_t at, word, target;
} g_kick_ev[6];
static int g_kick_nev;
/* The last kicks run by the pusher (watchdog_dump): GET, PUT and where pb_run ended. */
static struct {
    uint32_t get, put, end, words;
} g_kick_hist[16];
static volatile LONG g_kick_hist_n;

static void on_event(void *user, int kind, uint32_t at, uint32_t word, uint32_t target) {
    static const char *names[] = {"?", "JMP", "old JMP", "CALL", "RETURN", "ERROR"};
    (void)user;
    if (kind == PB_EVENT_JUMP || kind == PB_EVENT_OLD_JUMP || kind == PB_EVENT_CALL)
        d3d_fixup_late();
    if (g_kick_nev < 6) {
        g_kick_ev[g_kick_nev].kind = kind;
        g_kick_ev[g_kick_nev].at = at;
        g_kick_ev[g_kick_nev].word = word;
        g_kick_ev[g_kick_nev].target = target;
        g_kick_nev++;
    }
    flush_run();
    if (logging() || kind == PB_EVENT_ERROR)
        pg_printf("   pb %s at 0x%08X (word 0x%08X) -> 0x%08X\n", names[kind <= 5 ? kind : 0],
                  at, word, target);
}

/* Bulk vertex data (INLINE_ARRAY and ARRAY_ELEMENT16/32): they are two thirds of the push
 * buffer words, and through on_method each one paid for the whole dispatch. With the frame
 * log active they go word by word, so that record() sees them. */
static int on_bulk(void *user, uint32_t sub, uint32_t method, const uint32_t *words, uint32_t n) {
    uint32_t i, *dst;
    (void)user;
    if ((method != 0x1818 && method != 0x1800 && method != 0x1808) || logging() ||
        (g_class[sub & 7] && g_class[sub & 7] != 0x97))
        return 0;
    g_words += n;
    g_bulk_words += n;
    g_hist[method / 4] += n;
    if (!g_frame0_done)
        g_hist_frame0[method / 4] += n;
    g_run.active = 0;
    if (g_gl && g_r.m[method / 4] != words[n - 1]) { /* same as pgl_regs_track with the last one */
        g_r.m[method / 4] = words[n - 1];
        g_r.blk_stamp[method / PGL_BLOCK] = ++g_r.stamp;
    }
    if (method == 0x1818) {
        while (n) {
            uint32_t k = g_inline_cap - g_inline_n;
            if (!k) { /* full: inline_push makes it grow, or drops the word at the cap */
                inline_push(*words++);
                n--;
                if (g_inline_n == g_inline_cap)
                    return 1;
                continue;
            }
            if (k > n)
                k = n;
            memcpy(g_inline + g_inline_n, words, (size_t)k * 4);
            g_inline_n += k;
            words += k;
            n -= k;
        }
        return 1;
    }
    if (!g_gl)
        return 1;
    if (method == 0x1808) {
        for (i = 0; i < n; i++)
            aidx_push(words[i]);
        return 1;
    }
    /* ARRAY_ELEMENT16: two indices per word, the low one first */
    if (g_aidx_cap - g_aidx_n >= 2 * n) {
        dst = g_aidx + g_aidx_n;
        for (i = 0; i < n; i++) {
            dst[2 * i] = words[i] & 0xFFFF;
            dst[2 * i + 1] = words[i] >> 16;
        }
        g_aidx_n += 2 * n;
    } else {
        for (i = 0; i < n; i++) {
            aidx_push(words[i] & 0xFFFF);
            aidx_push(words[i] >> 16);
        }
    }
    return 1;
}

/* Host base of the DMA object of the kick in progress (for on_readable). */
static const uint8_t *g_kick_base;

/* A push buffer jump or call can lead to an arena page the game
 * never reserved (on the Xbox it would be RAM with garbage): reading it here would be a page
 * fault in the loader. It is checked on entering each page.
 * Pages seen readable once are remembered: the contiguous arena is never decommitted
 * (ngb_contig_free only marks the block free), the XBE image (static push buffers) is always
 * committed, and the calls that can free memory or take access away bump ngb_mem_access_gen.
 * Before, every 4 KB of push buffer cost a VirtualQuery: a WOW64 system call of about 3 us in
 * the loader, slower as a long session fragments the address space. Vertices sent inside the
 * push buffer take hundreds of pages per frame, so a busy scene spent up to 10 ms per frame
 * here after a couple of hours. Only the render thread runs the pusher. */
static uint32_t g_readable[1u << 15]; /* one bit per 4 KB page of the 32-bit address space */
static long g_readable_gen = -1;

static int on_readable(void *user, uint32_t offset) {
    MEMORY_BASIC_INFORMATION m;
    uint32_t a = (uint32_t)(uintptr_t)(g_kick_base + offset), page = a >> 12;
    long gen = ngb_mem_access_gen();
    int ok;
    (void)user;
    if (!g_kick_base)
        return 0;
    if (gen != g_readable_gen) {
        memset(g_readable, 0, sizeof g_readable);
        g_readable_gen = gen;
    }
    if (g_readable[page >> 5] >> (page & 31) & 1)
        return 1;
    ok = VirtualQuery((const void *)(uintptr_t)a, &m, sizeof m) && m.State == MEM_COMMIT &&
         !(m.Protect & (PAGE_NOACCESS | PAGE_GUARD));
    if (ok)
        g_readable[page >> 5] |= 1u << (page & 31);
    return ok;
}

static const pb_callbacks_t k_cb = {on_method, on_event, on_readable, on_bulk};

typedef struct kick_args {
    uint32_t dma_instance, get, put;
} kick_args_t;

static void kick_body(void *arg) {
    kick_args_t *k = (kick_args_t *)arg;
    LONGLONG t_body = qpc();
    uint32_t dma_instance = k->dma_instance, get = k->get, put = k->put;
    uint32_t phys = 0, limit = NGB_CONTIG_SIZE - 1, end, span;
    uint8_t *base;
    if (!dma_object(dma_instance << 4, &phys, &limit)) {
        phys = 0; /* no valid DMA object: GET/PUT as physical addresses */
        limit = NGB_CONTIG_SIZE - 1;
    }
    span = nv2a_phys_span(phys);
    if (span && limit >= span)
        limit = span - 1;
    base = span ? nv2a_phys(phys, 1) : NULL;
    g_kicks++;
    if (logging())
        pg_printf("-- kick %u: GET 0x%08X -> PUT 0x%08X (DMA object 0x%05X: physical 0x%08X)\n",
                  g_kicks, get, put, dma_instance << 4, phys);
    g_kick_base = base;
    if (g_gl)
        pgl_kick_begin();
    {
        uint32_t words0 = g_words;
        LONGLONG t_pb = qpc();
        int st_count = (int)g_pb.count, st_sub = g_pb.in_subroutine, e;
        g_kick_nev = 0;
        end = base ? pb_run(&g_pb, base, limit + 1, get, put, 1u << 22, &k_cb, NULL) : put;
        g_perf.pb += qpc() - t_pb;
        if (g_words - words0 > (1u << 20)) {
            ngb_log("    · PGRAPH: huge kick: on entry count %d, subroutine %d; events:", st_count,
                    st_sub);
            for (e = 0; e < g_kick_nev; e++)
                ngb_log("        %d at 0x%08X (word 0x%08X) -> 0x%08X", g_kick_ev[e].kind,
                        g_kick_ev[e].at, g_kick_ev[e].word, g_kick_ev[e].target);
        }
        /* A normal kick is thousands of words: one of over a million is a replay of an old
           push buffer (the symptom of the D3D_BlockOnTime hangs). */
        if (g_words - words0 > (1u << 20))
            ngb_log("    · PGRAPH: huge kick %u: %u words, GET 0x%08X -> PUT 0x%08X, ends at 0x%08X",
                    g_kicks, g_words - words0, get, put, end);
    }
    {
        LONG h = InterlockedIncrement(&g_kick_hist_n) - 1;
        g_kick_hist[h & 15].get = get;
        g_kick_hist[h & 15].put = put;
        g_kick_hist[h & 15].end = end;
        g_kick_hist[h & 15].words = g_pb.words;
    }
    flush_run();
    if (end != put) {
        int e;
        pg_printf("   pb: stopped at 0x%08X without reaching PUT 0x%08X; GET = PUT forced\n", end, put);
        ngb_log("    · PGRAPH: kick %u stopped at 0x%08X (GET 0x%08X, PUT 0x%08X); first jumps:", g_kicks,
                end, get, put);
        for (e = 0; e < g_kick_nev; e++)
            ngb_log("        %d at 0x%08X (word 0x%08X) -> 0x%08X", g_kick_ev[e].kind, g_kick_ev[e].at,
                    g_kick_ev[e].word, g_kick_ev[e].target);
    }
    fflush(g_pg);
    g_perf.body += qpc() - t_body;
}

/* Render thread: consumes the queue until it is empty. The pacing runs outside g_pg_lock. */
/* The queue is empty: the GPU is idle at `put`. D3D_BlockOnTime, when the GPU is far behind,
 * clears its KEVENT, patches WAIT_FOR_IDLE + NO_OPERATION(5) into the ring ahead of GET, reads
 * GET again and sleeps until that NO_OPERATION wakes it. If the pusher passed that spot before
 * the patch landed, nothing woke it: about one time in three the game froze on a black screen at
 * the end of the "Eternal Legend" opening cutscene, with GET == PUT and the patch, never run,
 * behind it. When everything D3D has written was run (its put is our PUT), any spot it patched is
 * behind the GPU, as on the console, so the event is set. */
static void idle_wake(uint32_t put) {
    uint32_t *dev = (uint32_t *)game_mem(d3d_g_pdevice_va(), 4), *dv;
    if (!dev || !*dev || (dv = (uint32_t *)game_mem(*dev, 0x775u * 4u)) == NULL)
        return;
    if (dv[0x774] == 0 && dv[0] - NGB_CONTIG_BASE == put) {
        ngb_ke_set_event(*dev + 0x773u * 4u);
        g_idle_wakes++;
    }
}

static void kick_worker(void *unused) {
    (void)unused;
    for (;;) {
        kick_args_t k;
        LONGLONG t0 = qpc();
        EnterCriticalSection(&g_q_lock);
        if (g_q_get == g_q_put) {
            uint32_t put = g_q_put;
            g_q_busy = 0;
            LeaveCriticalSection(&g_q_lock);
            idle_wake(put);
            return;
        }
        k.dma_instance = g_q_inst;
        k.get = g_q_get;
        k.put = g_q_put;
        LeaveCriticalSection(&g_q_lock);
        EnterCriticalSection(&g_pg_lock);
        g_pb.cur = k.get;
        g_q_running = 1;
        kick_body(&k);
        g_perf.call += qpc() - t0;
        LeaveCriticalSection(&g_pg_lock);
        EnterCriticalSection(&g_q_lock);
        g_q_running = 0;
        g_q_get = k.put; /* span done: GET reaches PUT */
        LeaveCriticalSection(&g_q_lock);
        if (InterlockedExchange(&g_pace_pending, 0))
            pace();
    }
}

uint32_t pgraph_patt_color0(void) { return (uint32_t)g_patt_color0; }

uint32_t pgraph_fifo_get(uint32_t fallback) {
    uint32_t get;
    if (!g_gl || !g_q_started)
        return fallback;
    EnterCriticalSection(&g_q_lock);
    /* While consuming, the real position (like the hardware GET): D3D_BlockOnTime decides
     * with it where to patch a wait, and a stale GET would make it patch what is already past. */
    get = g_q_busy && g_q_running ? g_pb.cur : g_q_get;
    LeaveCriticalSection(&g_q_lock);
    return get;
}

void pgraph_fifo_set_get(uint32_t get) {
    if (!g_gl)
        return;
    for (;;) { /* D3D only moves GET with the pusher stopped: wait for whatever there is to finish */
        EnterCriticalSection(&g_q_lock);
        if (!g_q_busy) {
            g_q_get = get;
            LeaveCriticalSection(&g_q_lock);
            return;
        }
        LeaveCriticalSection(&g_q_lock);
        Sleep(0);
    }
}

uint32_t pgraph_kick(uint32_t dma_instance, uint32_t get, uint32_t put) {
    kick_args_t k;
    LONGLONG t_kick = qpc();
    if (g_gl) { /* asynchronous: the new PUT is noted and the render thread consumes it */
        int post = 0;
        EnterCriticalSection(&g_q_lock);
        if (!g_q_started) {
            g_q_get = get;
            g_q_started = 1;
        }
        g_q_inst = dma_instance;
        g_q_put = put;
        if (!g_q_busy && g_q_get != g_q_put) {
            g_q_busy = 1;
            post = 1;
        }
        get = g_q_get;
        LeaveCriticalSection(&g_q_lock);
        if (post)
            pgl_post(kick_worker, NULL);
        g_perf.kick += qpc() - t_kick;
        return get;
    }
    k.dma_instance = dma_instance;
    k.get = get;
    k.put = put;
    EnterCriticalSection(&g_pg_lock);
    {
        LONGLONG t_call = qpc();
        g_perf.lockwait += t_call - t_kick;
        kick_body(&k); /* software rasterizer: synchronous */
        g_perf.call += qpc() - t_call;
    }
    g_perf.kick += qpc() - t_kick;
    LeaveCriticalSection(&g_pg_lock);
    return put; /* D3D waits for GET == PUT: everything sent counts as consumed */
}

/* Render watchdog: no FLIP_STALL for 3 s while the vblanks run. The state of the pusher and of
 * D3D goes to the log once (it also survives the short log), to find out who waits for whom
 * in a hang. Read without locks from the DPC thread: a racy snapshot is good enough. */
static void watchdog_dump(uint32_t vblanks) { /* vblanks: the current count */
    uint32_t *dev = (uint32_t *)game_mem(d3d_g_pdevice_va(), 4), d = dev ? *dev : 0, *dv, i;
    LONG n = g_kick_hist_n;
    ngb_log("pgraph: watchdog: no FLIP_STALL for 3 s (frame %u, %u kicks)", g_flips, g_kicks);
    ngb_log("pgraph: watchdog: queue GET 0x%08X PUT 0x%08X busy %d running %d; pusher at 0x%08X, method 0x%04X "
            "count %u, subroutine %d (return 0x%08X), %u words",
            g_q_get, g_q_put, g_q_busy, g_q_running, g_pb.cur, g_pb.method, g_pb.count, g_pb.in_subroutine,
            g_pb.return_get, g_pb.words);
    for (i = n > 16 ? (uint32_t)n - 16 : 0; i < (uint32_t)n; i++)
        ngb_log("pgraph: watchdog: kick -%u: GET 0x%08X -> PUT 0x%08X, ended at 0x%08X (words %u)",
                (uint32_t)n - i, g_kick_hist[i & 15].get, g_kick_hist[i & 15].put, g_kick_hist[i & 15].end,
                g_kick_hist[i & 15].words);
    for (i = 0; i < (uint32_t)g_kick_nev; i++)
        ngb_log("pgraph: watchdog: last kick event %d at 0x%08X (word 0x%08X) -> 0x%08X", g_kick_ev[i].kind,
                g_kick_ev[i].at, g_kick_ev[i].word, g_kick_ev[i].target);
    if (d && (dv = (uint32_t *)game_mem(d, 0x774 * 4 + 8)) != NULL) {
        uint32_t *fence = (uint32_t *)game_mem(dv[0xC], 4), *rec = (uint32_t *)game_mem(dv[0x58 / 4], 20);
        ngb_log("pgraph: watchdog: D3D device 0x%08X: put 0x%08X, time 0x%08X, fence 0x%08X, ring size 0x%X, "
                "pending fixups %d, last record 0x%08X, KEVENT signal %d, BlockOnTime target 0x%08X",
                d, dv[0], dv[0xB], fence ? *fence : 0xFFFFFFFFu, dv[0x11], (int)dv[0x50 / 4], dv[0x58 / 4],
                (int)dv[0x773 + 1], dv[0x680]);
        if (rec)
            ngb_log("pgraph: watchdog: last record: return 0x%08X next 0x%08X end 0x%08X fixups 0x%08X base 0x%08X",
                    rec[0], rec[1], rec[2], rec[3], rec[4]);
        {
            /* The wait D3D_BlockOnTime patches into the ring: WAIT_FOR_IDLE, then NO_OPERATION(5). */
            uint32_t lo = dv[9], hi = dv[10], a, found = 0;
            uint32_t *ring = lo < hi ? (uint32_t *)game_mem(lo, hi - lo) : NULL;
            ngb_log("pgraph: watchdog: ring [0x%08X, 0x%08X); vblank now %u", lo, hi, vblanks);
            for (a = 0; ring && a + 4 <= (hi - lo) / 4 && found < 8; a++)
                if (ring[a + 2] == 0x00040100u && ring[a + 3] == 5u &&
                    (ring[a] == 0x00040110u || ring[a] == 0x00040100u)) {
                    ngb_log("pgraph: watchdog: NO_OPERATION(5) patch at 0x%08X", lo + a * 4 + 8);
                    found++;
                }
        }
    }
    {
        LONG n5 = g_nop5_n, k;
        for (k = n5 > 8 ? n5 - 8 : 0; k < n5; k++)
            ngb_log("pgraph: watchdog: NO_OPERATION(5) run at 0x%08X, vblank %u", g_nop5_hist[k & 7].at,
                    g_nop5_hist[k & 7].vblank);
    }
    {
        uint32_t *w = (uint32_t *)game_mem(NGB_CONTIG_BASE + (g_pb.cur & ~3u), 32);
        if (w)
            ngb_log("pgraph: watchdog: words at the pusher: %08X %08X %08X %08X %08X %08X %08X %08X", w[0], w[1],
                    w[2], w[3], w[4], w[5], w[6], w[7]);
        w = g_q_put >= 32 ? (uint32_t *)game_mem(NGB_CONTIG_BASE + ((g_q_put - 32) & ~3u), 32) : NULL;
        if (w)
            ngb_log("pgraph: watchdog: words before PUT: %08X %08X %08X %08X %08X %08X %08X %08X", w[0], w[1],
                    w[2], w[3], w[4], w[5], w[6], w[7]);
    }
    ngb_log_flush();
}

/* Stall rescue, on every vblank (DPC thread). A thread asleep in D3D_BlockOnTime for 100 ms
 * while the GPU is idle will not be woken by the GPU any more: the NO_OPERATION(5) it waits for
 * is behind GET or was never kicked. Seen after 45 minutes of story mode (2026-10-26): GET ==
 * PUT, D3D's put 30 KB ahead of our PUT and no patch in the ring, so idle_wake (which needs
 * D3D's put == PUT) did not fire, and the main thread slept forever. The rescue first kicks
 * what D3D has written, as its own KickOff would; once that has run and D3D's put is our PUT,
 * every spot it could have patched is behind the GPU, and the KEVENT is set (idle_wake's rule). */
static void stall_rescue(uint32_t vblanks) {
    static uint32_t since; /* vblank when a sleeper with the GPU idle was first seen; 0 = none */
    uint32_t *dev = (uint32_t *)game_mem(d3d_g_pdevice_va(), 4), *dv, put, event, d3d_put;
    int idle;
    if (!g_gl || !dev || !*dev || (dv = (uint32_t *)game_mem(*dev, 0x775u * 4u)) == NULL)
        return;
    event = *dev + 0x773u * 4u;
    ngb_ke_watch_waiters(event);
    EnterCriticalSection(&g_q_lock);
    idle = g_q_started && !g_q_busy && g_q_get == g_q_put;
    put = g_q_put;
    LeaveCriticalSection(&g_q_lock);
    if (!idle || ngb_ke_waiters() == 0 || dv[0x774] != 0) {
        since = 0;
        return;
    }
    if (!since) {
        since = vblanks ? vblanks : 1;
        return;
    }
    if (vblanks - since < 6)
        return;
    since = 0;
    d3d_put = dv[0] - NGB_CONTIG_BASE;
    if (d3d_put != put) {
        if (dv[0] < dv[9] || dv[0] >= dv[10]) /* not in D3D's ring: leave it to the watchdog */
            return;
        if (++g_rescue_kicks <= 20)
            ngb_log("pgraph: stall rescue: D3D_BlockOnTime asleep with the GPU idle at 0x%08X; kicking "
                    "D3D's put 0x%08X", put, d3d_put);
        nv2a_rescue_put(d3d_put);
    } else {
        if (++g_rescue_wakes <= 20)
            ngb_log("pgraph: stall rescue: D3D_BlockOnTime asleep with the GPU idle at D3D's put 0x%08X; "
                    "waking it", put);
        ngb_ke_set_event(event);
    }
}

void pgraph_vblank(uint32_t count) {
    LONGLONG now = qpc();
    stall_rescue(count);
    {
        static uint32_t flips_seen, since, dumped;
        if (g_flips != flips_seen || !since) {
            flips_seen = g_flips;
            since = count;
            dumped = 0;
        } else if (!dumped && g_flips && count - since >= 180) {
            dumped = 1;
            watchdog_dump(count);
        }
    }
    if (g_fr.last_vb) {
        LONGLONG dt = now - g_fr.last_vb;
        static LONGLONG over; /* 20 ms in QPC ticks */
        if (!over) {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            over = f.QuadPart / 50;
        }
        LONGLONG worst = InterlockedCompareExchange64(&g_fr.vb_worst, 0, 0);
        while (dt > worst) {
            LONGLONG seen = InterlockedCompareExchange64(&g_fr.vb_worst, dt, worst);
            if (seen == worst)
                break;
            worst = seen;
        }
        if (dt > over)
            InterlockedIncrement(&g_fr.vb_late);
    }
    g_fr.last_vb = now;
    if (g_vblank_evt)
        SetEvent(g_vblank_evt);
    /* Never wait for the lock here: the pusher can hold it while it paces, and a vblank that
     * waited would come late. A busy lock only drops this log line. */
    if ((count <= 3 || count % 60 == 0) && TryEnterCriticalSection(&g_pg_lock)) {
        pg_printf("~~ vblank %u (frame %u)\n", count, g_frame);
        LeaveCriticalSection(&g_pg_lock);
    }
}

int pgraph_init(const char *log_path) {
    char *dot;
    InitializeCriticalSection(&g_pg_lock);
    InitializeCriticalSection(&g_q_lock);
    g_vblank_evt = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (fopen_s(&g_pg, log_path, "wb") || !g_pg) {
        ngb_log("nv2a: cannot create the PGRAPH log %s", log_path);
        g_pg = NULL;
        return -1;
    }
    snprintf(g_png_base, sizeof g_png_base, "%s", log_path);
    dot = strrchr(g_png_base, '.');
    if (dot)
        *dot = 0;
    pg_printf("ngb-loader H7a: PGRAPH methods (f = frame, s = subchannel, method, NV097 name)\n");
    ngb_log("nv2a: PGRAPH log at %s", log_path);
    pgl_regs_defaults(&g_r);
    if (g_ngb_opts.window && g_ngb_opts.renderer != NGB_RENDERER_SOFT) {
        char why[400];
        if (!pgl_start((HWND)gfx_hwnd(), why, sizeof why)) {
            g_gl = 1;
            gfx_set_external(1);
            ngb_log("pgraph: OpenGL render (%s)", why);
        } else {
            ngb_log("pgraph: no OpenGL (%s): software rasterizer", why);
        }
    } else {
        ngb_log("pgraph: software rasterizer%s", g_ngb_opts.window ? "" : " (no --window)");
    }
    return 0;
}

static void summary_hist(const uint32_t *hist, const char *title) {
    uint32_t i, n = 0;
    char name[64];
    ngb_log("pgraph: %s", title);
    for (i = 0; i < 0x2000 / 4; i++) {
        if (!hist[i])
            continue;
        n++;
        nv097_method_name(i * 4, name, sizeof name);
        ngb_log("  %8u  %04X %s", hist[i], i * 4, name);
    }
    ngb_log("pgraph: %u distinct methods", n);
}

void pgraph_summary(void) {
    uint32_t i;
    int locked = 0, tries;
    if (!g_pg)
        return;
    /* Called from ngb_stop with the threads still alive: another thread may be in the middle
       of a kick holding the lock. It tries for ~0.5 s and, if it fails, skips the summary
       (writing it without the lock would race with the thread that keeps running). */
    for (tries = 0; !(locked = TryEnterCriticalSection(&g_pg_lock)); tries++) {
        if (tries >= 50) {
            ngb_log("pgraph: summary skipped (lock busy)%s%s",
                    g_perf.last[0] ? "; last measurement: " : "", g_perf.last);
            pgl_summary(); /* counters only: does not need the lock */
            return;
        }
        Sleep(10);
    }
    flush_run();
    if (g_perf.last[0])
        ngb_log("pgraph: last measurement: %s", g_perf.last);
    ngb_log("pgraph: %u kicks, %u data words, %u frames (FLIP_STALL), %u BEGIN_END, "
            "%u DRAW_ARRAYS, %u clears (%u color), %u semaphores, %u pb errors, "
            "%u push buffer fixups (%u chained late), %u D3D_BlockOnTime waits woken up",
            g_kicks, g_words, g_flips, g_begin_end, g_draw_arrays, g_clears, g_color_clears,
            g_sem_releases, g_pb.errors, g_pb_fixups, g_pb_fixups_late, g_block_wakes);
    ngb_log("pgraph: %u D3D_BlockOnTime waits woken with the GPU idle (patch passed before it landed); "
            "stall rescue: %u kicks of D3D's put, %u waits woken",
            g_idle_wakes, g_rescue_kicks, g_rescue_wakes);
    ngb_log("pgraph: rasterizer: %u triangles, %u pixels written; %u primitives not drawn "
            "(%u with non-XYZRHW position, %u without surface, %u points/lines); %u DRAW_ARRAYS and "
            "ARRAY_ELEMENT not drawn",
            g_drawn_tris, g_drawn_pixels, g_skipped_draws, g_skip_pos, g_skip_target, g_skip_prim,
            g_draw_arrays + g_hist[0x1800 / 4] + g_hist[0x1808 / 4]);
    ngb_log("pgraph: textures: %u SET_TEXTURE_OFFSET, %u distinct offsets; first frame with "
            "primitives: %d; %d PNG", g_tex_sets, g_ntex,
            g_first_draw_frame == 0xFFFFFFFFu ? -1 : (int)g_first_draw_frame, g_png_count);
    pgl_summary();
    for (i = 1; i < 16; i++)
        if (g_prims[i])
            ngb_log("  primitives %-14s %u", prim_name(i), g_prims[i]);
    summary_hist(g_hist_frame0, "first frame (up to the first FLIP_STALL), by method:");
    summary_hist(g_hist, "total by method:");
    fflush(g_pg);
    if (locked)
        LeaveCriticalSection(&g_pg_lock);
}
