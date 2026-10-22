/*
 * pgraph.c: lo que hace la GPU con los comandos del push buffer (H7a: casi nada).
 *
 * Cada vez que D3D avanza DMA_PUT, nv2a.c llama a pgraph_kick: el decodificador
 * (pushbuf.c) recorre los comandos de GET a PUT y aquí:
 *   - se escribe cada método en el log de PGRAPH (h7-pgraph-<fecha>.log), agrupando las
 *     rachas del mismo método (matrices, programas, datos de vértices en línea);
 *   - se mantiene el estado mínimo para entender el frame: objeto de cada subcanal
 *     (SET_OBJECT vía RAMHT), objetos DMA, superficie de color, valor de clear;
 *   - se ejecutan los dos efectos que D3D espera de vuelta: los semáforos
 *     (BACK_END_WRITE_SEMAPHORE_RELEASE escribe en memoria) y el CLEAR_SURFACE de color,
 *     que se aplica a la superficie y se vuelca a PNG. Nada más se dibuja.
 * Referencias: xemu hw/xbox/nv2a/pgraph/pgraph.c (métodos), pfifo.c (RAMHT), nv2a.c (DMA).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ngb.h"
#include "nv2a.h"
#include "../gfx/present.h"
#include "png.h"
#include "raster.h"
#include "pushbuf.h"
#include "pgraph_gl.h"

#define PFIFO_RAMHT 0x002210u
#define PFIFO_CACHE1_PUSH1 0x003204u
#define MAX_PNG 24
#define PNG_FIRST_FLIPS 10 /* PNG de cada FLIP de los 10 primeros frames; luego cada 100 */
#define MAX_TEXTURES 4096
#define RUN_PARAMS 4

static FILE *g_pg;
static CRITICAL_SECTION g_pg_lock;
static char g_png_base[MAX_PATH];
static pb_state_t g_pb;
static uint32_t g_frame, g_kicks, g_words;
static uint32_t g_hist[0x2000 / 4], g_hist_frame0[0x2000 / 4];
static uint32_t g_class[8];
static uint32_t g_dma[0x80 / 4]; /* instancias de SET_CONTEXT_DMA_* (métodos 0x180..0x1FC) */
static uint32_t g_clip_h, g_clip_v, g_format, g_pitch, g_color_offset;
static uint32_t g_clear_value, g_clear_h, g_clear_v, g_sem_offset;
static uint32_t g_begin_end, g_prims[16], g_draw_arrays, g_clears, g_color_clears, g_flips;
static uint32_t g_sem_releases;
static int g_png_count;
static int g_frame0_done;
static uint32_t g_first_draw_frame = 0xFFFFFFFFu, g_tex_sets, g_ntex;
static uint32_t g_tex[MAX_TEXTURES];
/* Estado de textura por etapa: offset, formato, control0, control1 (pitch), rectángulo. */
static uint32_t g_tex_offset[4], g_tex_format[4], g_tex_ctl0[4], g_tex_ctl1[4], g_tex_rect[4];
#define MAX_TEX_PNG 12
#define MAX_TEX_SEEN 256
static uint32_t g_tex_seen[MAX_TEX_SEEN][2]; /* (offset, formato) ya vistos al dibujar */
static uint32_t g_tex_nseen;
static int g_tex_png;
static uint32_t g_tex_palette[4];

/* H7b: estado para el rasterizador por software (raster.c). */
static uint32_t g_vfmt[16];      /* SET_VERTEX_DATA_ARRAY_FORMAT por atributo */
static uint32_t g_vconst_diffuse = 0xFFFFFFFFu; /* SET_VERTEX_DATA4UB del difuso (slot 3) */
static uint32_t g_prim;          /* primitiva de BEGIN_END en curso (0 = ninguna) */
static uint32_t *g_inline;       /* palabras de INLINE_ARRAY de la primitiva en curso */
static uint32_t g_inline_n, g_inline_cap;
static uint32_t g_blend_enable, g_sfactor = 1, g_dfactor, g_alpha_test, g_alpha_func = 0x207;
static uint32_t g_alpha_ref, g_exec_mode;
static uint32_t g_drawn_tris, g_drawn_pixels, g_skipped_draws, g_skip_pos, g_skip_target, g_skip_prim;
static uint32_t g_present_offset, g_present_pitch, g_present_w, g_present_h;
static LARGE_INTEGER g_pace_next, g_pace_freq;
static volatile LONG g_pace_pending; /* FLIP_STALL hecho; el pacing va al salir de los locks */

/* Pusher asíncrono (con GL): la cola es un solo tramo [g_q_get, g_q_put) del push buffer en
 * curso. El juego mueve PUT; el hilo de render consume y mueve GET al terminar cada tramo. */
static CRITICAL_SECTION g_q_lock;
static volatile LONG g_patt_color0; /* PGRAPH_PATT_COLOR0, escrito por el pusher */
static uint32_t g_block_wakes;       /* NO_OPERATION(5) de D3D_BlockOnTime atendidos */
static uint32_t g_q_inst, g_q_get, g_q_put;
static int g_q_busy, g_q_started;
static volatile int g_q_running; /* el hilo de render está dentro de pb_run para la cola */

/* H7c: render OpenGL (--renderer gl). g_r es el estado NV097 que necesita pgraph_gl.c; los
 * índices de ARRAY_ELEMENT y DRAW_ARRAYS de la primitiva en curso van en g_aidx. */
static int g_gl;
static pgl_regs g_r;
static uint32_t *g_aidx;
static uint32_t g_aidx_n, g_aidx_cap;
#define SHOT_EVERY 300 /* con --window, captura automática cada 300 frames presentados */

/* H7c: medidas por frame con QueryPerformanceCounter (baratas: ~20 ns cada lectura). Se
 * acumulan ticks por fase y cada PERF_EVERY frames se escribe una línea "perf:" con la
 * media por frame y los fps reales (frames / tiempo de reloj de la ventana de medida). */
#define PERF_EVERY 300
static struct {
    LONGLONG kick, draw, clear, present, pace, t0, log0;
    LONGLONG pb, body; /* dentro de pb_run y de kick_body (en el hilo que lo ejecuta) */
    LONGLONG lockwait, call; /* esperando g_pg_lock y dentro de pgl_call */
    long kcalls0;
    uint32_t mmio0, flips0, kicks0, tris0;
    char last[320];
} g_perf;

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

/* Cierra una ventana de medida: media por frame de cada fase y fps reales. */
static void perf_report(void) {
    LONGLONG now = qpc(), log_now = ngb_log_ticks();
    long kc = ngb_kernel_calls();
    uint32_t mmio = nv2a_mmio_count(), n = g_flips - g_perf.flips0;
    double wall;
    if (!g_perf.t0 || !n) {
        g_perf.t0 = now;
        if (g_gl) {
            char gl[700];
            pgl_perf_take(gl, sizeof gl, 1); /* descarta lo acumulado antes de la primera ventana */
        }
    } else {
        wall = ticks_ms(now - g_perf.t0);
        snprintf(g_perf.last, sizeof g_perf.last,
                 "perf: frames %u-%u: %.1f fps reales, %.2f ms/frame; por frame: pusher %.2f ms "
                 "(dibujo %.2f, clear %.2f), present %.2f, pacing %.2f, log %.2f ms; %.0f llamadas "
                 "al kernel, %.0f MMIO, %.1f kicks, %.0f primitivas; pb_run %.2f ms, kick_body %.2f ms, lock %.2f ms, pgl_call %.2f ms",
                 g_perf.flips0, g_flips, n * 1000.0 / wall, wall / n,
                 ticks_ms(g_perf.kick - g_perf.present) / n,
                 ticks_ms(g_perf.draw) / n, ticks_ms(g_perf.clear) / n, ticks_ms(g_perf.present) / n,
                 ticks_ms(g_perf.pace) / n, ticks_ms(log_now - g_perf.log0) / n,
                 (double)(kc - g_perf.kcalls0) / n, (double)(mmio - g_perf.mmio0) / n,
                 (double)(g_kicks - g_perf.kicks0) / n, (double)(g_begin_end - g_perf.tris0) / n,
                 ticks_ms(g_perf.pb) / n, ticks_ms(g_perf.body) / n, ticks_ms(g_perf.lockwait) / n,
                 ticks_ms(g_perf.call) / n);
        ngb_log("    · %s", g_perf.last);
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
    g_perf.kick = g_perf.draw = g_perf.clear = g_perf.present = g_perf.pace = 0;
    g_perf.pb = g_perf.body = g_perf.lockwait = g_perf.call = 0;
}

/* Racha de palabras del mismo método (para no escribir 10000 líneas de INLINE_ARRAY). */
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

/* Frames con todos los métodos en el log (--pgraph-frames, 3 por defecto) y además el
 * primer frame que dibuja (con BEGIN_END); el resto solo se cuenta. */
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

/* Anotación legible del parámetro de algunos métodos. */
static void annotate(uint32_t method, uint32_t p, char *out, size_t outlen) {
    out[0] = 0;
    switch (method) {
    case 0x17FC: snprintf(out, outlen, "  (%s)", prim_name(p)); break;
    case 0x1810: snprintf(out, outlen, "  (inicio %u, %u vértices)", p & 0xFFFFFF, (p >> 24) + 1); break;
    case 0x1D94:
        snprintf(out, outlen, "  (%s%s%s)", (p & 0xF0) ? "color " : "", (p & 1) ? "Z " : "",
                 (p & 2) ? "stencil" : "");
        break;
    case 0x0200: case 0x0204: snprintf(out, outlen, "  (%u + %u)", p & 0xFFFF, p >> 16); break;
    case 0x0208:
        snprintf(out, outlen, "  (color fmt %u, zeta %u, tipo %u, %ux%u log2)", p & 0xF,
                 (p >> 4) & 0xF, (p >> 8) & 0xF, (p >> 16) & 0xFF, p >> 24);
        break;
    case 0x020C: snprintf(out, outlen, "  (pitch color %u, zeta %u)", p & 0xFFFF, p >> 16); break;
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

/* Nombre sin el índice "+N" (clave de agrupación de la racha). */
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
        /* Fuera de los frames del log no hay nada que agrupar: buscar el nombre del método y
         * compararlo costaba ~1 µs por palabra, decenas de ms por frame en el 3D. */
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

/* RAMHT: handle -> instancia (bytes en RAMIN). Hash de xemu pfifo.c (ramht_hash). */
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
    for (i = 0; i < size / 8; i++) { /* la entrada del hash o, si no cuadra, búsqueda lineal */
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

/* Objeto DMA en RAMIN: dirección física base y límite (xemu nv_dma_load). */
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
    if (g_present_offset) { /* el búfer de 720 que se presenta (H7b) */
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
            if (bpp == 4) { /* X8R8G8B8 / A8R8G8B8 en little endian: B, G, R, A */
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
        ngb_log("    · PGRAPH: %s -> %s (%ux%u, %u B/píxel, física 0x%08X)", why, path, w, h, bpp,
                phys);
        pg_printf("== PNG %s: %s (%ux%u)\n", why, path, w, h);
    }
    free(rgb);
}

/* Coordenada swizzle (Morton) de (x, y) en una textura w x h potencia de 2 (como en xemu,
 * swizzle.c): los bits de x e y se intercalan mientras ambos tengan bits. */
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

/* Vuelca a PNG la textura de una etapa (formatos de 32 y 16 bits, lineales o swizzle). */
static void dump_texture(uint32_t stage) {
    uint32_t fmt = g_tex_format[stage], color = (fmt >> 8) & 0xFF, phys, limit, x, y, bpp;
    uint32_t w = 1u << ((fmt >> 20) & 0xF), h = 1u << ((fmt >> 24) & 0xF), pitch = 0;
    uint32_t dma = g_dma[(((fmt & 3) == 2 ? 0x188u : 0x184u) - 0x180u) / 4];
    int linear, swz;
    uint8_t *src, *rgb;
    const uint8_t *pal = NULL;
    char path[MAX_PATH + 32];
    switch (color) {
    case 0x06: case 0x07: bpp = 4; linear = 0; break; /* A8R8G8B8 / X8R8G8B8 swizzle */
    case 0x12: case 0x1E: bpp = 4; linear = 1; break; /* LU_IMAGE_A8R8G8B8 / X8R8G8B8 */
    case 0x05: bpp = 2; linear = 0; break;             /* R5G6B5 swizzle */
    case 0x11: bpp = 2; linear = 1; break;             /* LU_IMAGE_R5G6B5 */
    case 0x0B: bpp = 1; linear = 0; break;             /* I8_A8R8G8B8: índice a paleta, swizzle */
    case 0x19: bpp = 1; linear = 0; break;             /* A8 swizzle (solo alfa) */
    default:
        pg_printf("== textura etapa %u: formato de color 0x%02X no volcado (offset 0x%08X)\n", stage,
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
    if (color == 0x0B) { /* SET_TEXTURE_PALETTE: dma en bits 0-1, dirección alineada a 64 */
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
        ngb_log("    · PGRAPH: textura etapa %u (formato 0x%02X %s, %ux%u, física 0x%08X) -> %s",
                stage, color, linear ? "lineal" : "swizzle", w, h, phys + g_tex_offset[stage], path);
        pg_printf("== PNG textura etapa %u, formato 0x%02X, %ux%u: %s\n", stage, color, w, h, path);
    }
    free(rgb);
}

/* Al empezar una primitiva: vuelca las texturas activas que aún no se hayan visto. */
static void dump_active_textures(void) {
    uint32_t s, i;
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

/* Superficie de color actual como destino del rasterizador. 0 si no hay. */
static int color_target(rs_target_t *t) {
    uint32_t phys, limit, pitch = g_pitch & 0xFFFF, w = g_clip_h >> 16, h = g_clip_v >> 16;
    uint32_t cf = g_format & 0xF;
    memset(t, 0, sizeof *t);
    if (((g_format >> 8) & 0xF) == 2 || !w || !h || !pitch) /* swizzle: no soportado */
        return 0;
    if (!dma_object(g_dma[(0x194 - 0x180) / 4], &phys, &limit))
        phys = 0;
    t->bpp = (cf == 3 || cf == 1 || cf == 2) ? 2 : 4;
    t->width = w + (g_clip_h & 0xFFFF);
    t->height = h + (g_clip_v & 0xFFFF);
    /* El rasterizador escribe filas [0, height) y columnas [0, width): el búfer entero tiene
     * que caber en la RAM de la GPU y cada fila en el pitch (datos que manda el juego). */
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
    if (w == 720) { /* el búfer que acabará en pantalla */
        g_present_offset = phys + g_color_offset;
        g_present_pitch = pitch;
        g_present_w = w;
        g_present_h = h;
    }
    return 1;
}

/* Textura de la etapa 0 para el rasterizador (formatos de dump_texture). */
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
    case 0x12: case 0x1E: case 0x11: /* LU_IMAGE: lineales, coordenadas en texels */
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
    /* Tamaños que manda el juego: hasta 4096 de lado (como el volcado) y en 64 bits, para que
     * un 32768x32768 no dé 0 por desbordamiento y deje leer fuera de la RAM de la GPU. */
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
    if (ogl) /* UB_OGL: bytes R, G, B, A en memoria */
        c = (c & 0xFF00FF00u) | ((c & 0xFFu) << 16) | ((c >> 16) & 0xFFu);
    v->r = (float)((c >> 16) & 0xFF) / 255.0f;
    v->g = (float)((c >> 8) & 0xFF) / 255.0f;
    v->b = (float)(c & 0xFF) / 255.0f;
    v->a = (float)(c >> 24) / 255.0f;
}

/* Palabras que ocupa un atributo con este formato en INLINE_ARRAY. */
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
        /* El tope se mira antes del realloc: si no, el bloque viejo ya se habría movido y
           g_aidx quedaría colgando (los counts vienen del juego). */
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
        *phys = 0; /* sin objeto DMA: offsets como físicas (lo mismo que el rasterizador) */
    return 1;
}

/* Estado que el render GL lee de métodos con efectos (cargas de programa y constantes,
 * atributos constantes). El resto se guarda tal cual en g_r.m. */
static void gl_track(uint32_t method, uint32_t param) {
    float f;
    memcpy(&f, &param, 4);
    /* D3D reenvía muchos registros con el mismo valor (luces, material): solo un valor nuevo
     * cuenta como cambio para los uniforms. */
    if (g_r.m[method / 4] != param) {
        g_r.m[method / 4] = param;
        g_r.blk_stamp[method / PGL_BLOCK] = ++g_r.stamp;
    }
    if (method >= 0x0B00 && method < 0x0B80) { /* SET_TRANSFORM_PROGRAM */
        if (g_r.prog_load < PGL_PROGRAM_SLOTS * 4)
            g_r.program[g_r.prog_load++] = param;
    } else if (method == 0x1E9C) { /* SET_TRANSFORM_PROGRAM_LOAD */
        g_r.prog_load = param < PGL_PROGRAM_SLOTS ? param * 4 : PGL_PROGRAM_SLOTS * 4;
    } else if (method >= 0x0B80 && method < 0x0C00) { /* SET_TRANSFORM_CONSTANT */
        if (g_r.const_load < PGL_CONSTANTS * 4) {
            uint32_t old;
            memcpy(&old, &g_r.constants[g_r.const_load], 4);
            if (old != param)
                g_r.const_stamp[g_r.const_load / 4] = ++g_r.stamp;
            g_r.constants[g_r.const_load++] = f;
        }
    } else if (method == 0x1EA4) { /* SET_TRANSFORM_CONSTANT_LOAD */
        g_r.const_load = param < PGL_CONSTANTS ? param * 4 : PGL_CONSTANTS * 4;
    } else if (method >= 0x1940 && method < 0x1980) { /* SET_VERTEX_DATA4UB: D3DCOLOR */
        float *v = g_r.vconst[(method - 0x1940) / 4];
        v[0] = (float)((param >> 16) & 0xFF) / 255.0f;
        v[1] = (float)((param >> 8) & 0xFF) / 255.0f;
        v[2] = (float)(param & 0xFF) / 255.0f;
        v[3] = (float)(param >> 24) / 255.0f;
    } else if (method >= 0x1A00 && method < 0x1B00) { /* SET_VERTEX_DATA4F_M */
        g_r.vconst[(method - 0x1A00) / 16][(method / 4) & 3] = f;
    } else if (method >= 0x1880 && method < 0x1900) { /* SET_VERTEX_DATA2F_M */
        float *v = g_r.vconst[(method - 0x1880) / 8];
        v[(method / 4) & 1] = f;
        v[2] = 0.0f;
        v[3] = 1.0f;
    } else if (method >= 0x1900 && method < 0x1940) { /* SET_VERTEX_DATA2S */
        float *v = g_r.vconst[(method - 0x1900) / 4];
        v[0] = (float)(int16_t)(param & 0xFFFF);
        v[1] = (float)(int16_t)(param >> 16);
        v[2] = 0.0f;
        v[3] = 1.0f;
    }
}

/* Cierra la primitiva en curso con OpenGL (en el hilo de render). */
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

/* Vértices de INLINE_ARRAY (posición XYZRHW en pantalla, difuso, texcoord0) -> triángulos. */
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
    /* Posición de 4 floats = XYZRHW ya transformada; la de 3D (3 floats) necesita el
     * intérprete de vertex programs (siguiente paso). */
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
        g_skipped_draws++; /* puntos y líneas: no se dibujan */
        g_skip_prim++;
        break;
    }
#undef TRI
    free(vs);
}

static void inline_push(uint32_t w) {
    if (g_inline_n == g_inline_cap) {
        uint32_t cap = g_inline_cap ? g_inline_cap * 2 : 4096, *p;
        /* El tope se mira antes del realloc: si no, el bloque viejo ya se habría movido y
           g_inline quedaría colgando (los counts vienen del juego). */
        if (cap > (1u << 24) || (p = (uint32_t *)realloc(g_inline, (size_t)cap * 4)) == NULL)
            return;
        g_inline = p;
        g_inline_cap = cap;
    }
    g_inline[g_inline_n++] = w;
}

/* Espera al siguiente "vblank" (1/60 s de reloj) para ir a ~60 fps como la consola. */
static void pace(void) {
    LARGE_INTEGER now;
    LONGLONG t1 = qpc();
    if (g_ngb_opts.no_pacing)
        return;
    if (!g_pace_freq.QuadPart) {
        QueryPerformanceFrequency(&g_pace_freq);
        QueryPerformanceCounter(&g_pace_next);
    }
    g_pace_next.QuadPart += g_pace_freq.QuadPart / 60;
    QueryPerformanceCounter(&now);
    if (now.QuadPart > g_pace_next.QuadPart + g_pace_freq.QuadPart / 10) {
        g_pace_next = now; /* muy atrasados: no intentar recuperar */
        return;
    }
    while (now.QuadPart < g_pace_next.QuadPart) {
        LONGLONG ms = (g_pace_next.QuadPart - now.QuadPart) * 1000 / g_pace_freq.QuadPart;
        Sleep(ms > 1 ? (DWORD)(ms - 1) : 0);
        QueryPerformanceCounter(&now);
    }
    InterlockedExchangeAdd64(&g_perf.pace, qpc() - t1);
}

/* Lo llama nv2a.c al volver del kick, ya sin el lock del NV2A ni g_pg_lock (issue #35): el
 * Sleep del pacing ya no bloquea al hilo del vblank ni a otros accesos MMIO. Con el pusher
 * asíncrono el pacing es del hilo de render (kick_worker): el juego no espera aquí. */
void pgraph_pace(void) {
    if (g_gl)
        return;
    if (InterlockedExchange(&g_pace_pending, 0))
        pace();
}

/* FLIP_STALL: presenta el búfer de 720 de ancho en la ventana y deja el pacing pendiente
 * para cuando se suelten los locks. Si el mismo kick ya traía otro FLIP_STALL sin esperar
 * (push buffers en bucle, como el del vídeo), ese pacing se hace aquí. */
static void present_and_pace(void) {
    uint8_t *fb;
    LONGLONG t0 = qpc();
    if (InterlockedExchange(&g_pace_pending, 0))
        pace();
    if (g_gl) {
        char path[MAX_PATH + 32];
        int shot = g_flips % SHOT_EVERY == 0 && g_png_base[0];
        if (shot)
            snprintf(path, sizeof path, "%s-shot-%05u.png", g_png_base, g_flips);
        if (!pgl_flip(&g_r, shot ? path : NULL))
            ngb_log("    · gfx: captura del frame %u -> %s (%.1f fps, GL)", g_flips, path, gfx_fps());
    } else if (g_present_offset && g_ngb_opts.window) {
        fb = nv2a_phys(g_present_offset, g_present_pitch * g_present_h);
        if (fb) {
            gfx_present(fb, (int)g_present_w, (int)g_present_h, (int)g_present_pitch,
                        GFX_FMT_X8R8G8B8);
            if (g_flips % SHOT_EVERY == 0 && g_png_base[0]) {
                char path[MAX_PATH + 32], used[MAX_PATH + 32];
                snprintf(path, sizeof path, "%s-shot-%05u.png", g_png_base, g_flips);
                if (!gfx_screenshot(path, used, sizeof used))
                    ngb_log("    · gfx: captura del frame %u -> %s (%.1f fps)", g_flips, used, gfx_fps());
            }
        }
    }
    g_perf.present += qpc() - t0;
    InterlockedExchange(&g_pace_pending, 1);
}

/* CLEAR_SURFACE de color: se rellena el rectángulo de clear de la superficie. */
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
    /* Cada fila del rectángulo tiene que caber en el pitch: si no, la escritura pisaría la
     * fila siguiente o saldría del búfer. */
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
        dump_png("primer clear de color");
}

/*
 * HLE del "fixup" de push buffers estáticos de D3D (D3DDevice_RunPushBuffer, 0x3CD680).
 *
 * D3D salta desde su anillo a un push buffer estático que termina en un JUMP de vuelta, y
 * reescribe ese JUMP en cada uso. Si la GPU todavía no ha terminado el uso anterior, no
 * puede reescribirlo aún: mete en el anillo NO_OPERATION (0x100) con un parámetro que en la
 * Xbox detiene la GPU y lanza una interrupción; la rutina de D3D hace entonces el parcheo
 * (el mismo que FUN_003cd5e0 hace directamente cuando la GPU ya pasó) y la GPU sigue con el
 * JUMP al push buffer. Aquí no hay interrupción: el parcheo se hace en el acto.
 *   parámetro (i << 5) | 0xD: registro i de la tabla del dispositivo (dev + 0x264 + i·20);
 *   parámetro (va << 5) | 0xC: registro en el propio anillo, en va (contigua, 0x8xxxxxxx).
 * Registro (5 palabras): [0] retorno en el anillo | flags, [1] siguiente registro, [2] palabra
 * final del push buffer (donde va el JUMP de vuelta), [3] lista de fixups, [4] base.
 * Sin esto, el JUMP de vuelta seguía apuntando al uso anterior: la GPU emulada daba vueltas
 * hasta el tope de 4 M palabras, pisaba el fence de D3D con valores viejos y el juego se
 * colgaba en D3D_BlockOnTime (al empezar la demo del título).
 */
#define D3D_G_PDEVICE_VA 0x003DAA88u /* D3D_g_pDevice de NGB (symbols.csv de H5) */
static uint32_t g_pb_fixups;

/* Memoria del juego accesible: RAM contigua o la imagen del XBE. */
static void *game_mem(uint32_t va, uint32_t len) {
    if (va >= NGB_CONTIG_BASE && va - NGB_CONTIG_BASE <= NGB_CONTIG_SIZE &&
        len <= NGB_CONTIG_SIZE - (va - NGB_CONTIG_BASE))
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

static void d3d_pushbuffer_fixup(uint32_t param) {
    uint32_t rec, n;
    if ((param & 0x1Fu) == 0xDu) {
        uint32_t *dev = (uint32_t *)game_mem(D3D_G_PDEVICE_VA, 4);
        if (!dev)
            return;
        rec = *dev + 0x99u * 4u + (param >> 5) * 20u;
    } else {
        rec = (param >> 5) | 0x80000000u;
    }
    for (n = 0; n < 64 && rec != 0 && rec != 0xFFFFFFFFu; n++) {
        uint32_t *r = (uint32_t *)game_mem(rec, 20), *end, w0, next;
        if (!r || (end = (uint32_t *)game_mem(r[2], 4)) == NULL) {
            ngb_log("    · PGRAPH: fixup de push buffer con registro inválido 0x%08X", rec);
            return;
        }
        w0 = r[0];
        *end = (w0 & 0x0FFFFFFFu) + 1u; /* JUMP de vuelta al anillo */
        if (w0 & 0x20000000u)
            d3d_apply_fixups(r[3], r[4]);
        next = r[1];
        r[1] = 0xFFFFFFFFu;
        if ((int32_t)w0 < 0)
            r[0] = w0 & 0xBFFFFFFFu;
        rec = next;
    }
    g_pb_fixups++;
}

static void on_method(void *user, uint32_t sub, uint32_t method, uint32_t param, int non_inc) {
    uint32_t inst = 0;
    (void)user;
    g_words++;
    g_hist[method / 4]++;
    if (!g_frame0_done)
        g_hist_frame0[method / 4]++;
    record(sub, method, param, non_inc);
    if (method == 0x0100 && ((param & 0x1Fu) == 0xCu || (param & 0x1Fu) == 0xDu)) {
        d3d_pushbuffer_fixup(param); /* NO_OPERATION con interrupción: fixup de D3D */
        return;
    }
    if (method == 0x0100 && (param & 0x1Fu) == 5u) {
        /* D3D_BlockOnTime, con la GPU muy atrás, mete NO_OPERATION(5) y espera al KEVENT del
         * dispositivo (+0x773 palabras) que su ISR señala cuando la GPU llega ahí. */
        uint32_t *dev = (uint32_t *)game_mem(D3D_G_PDEVICE_VA, 4);
        if (dev && *dev)
            ngb_ke_set_event(*dev + 0x773u * 4u);
        g_block_wakes++;
        return;
    }
    if (method == 0) { /* SET_OBJECT: el handle se busca en RAMHT; la clase está en RAMIN */
        uint8_t *obj;
        if (ramht_lookup(param, &inst) && (obj = nv2a_ramin(inst, 4)) != NULL) {
            g_class[sub & 7] = obj[0];
            flush_run();
            if (logging())
                pg_printf("   subcanal %u = objeto 0x%08X, clase 0x%02X (instancia 0x%05X)\n", sub,
                          param, obj[0], inst);
        }
        return;
    }
    if (method >= 0x180 && method < 0x200) { /* SET_CONTEXT_DMA_*: handle -> instancia */
        if (!g_class[sub & 7] || g_class[sub & 7] == 0x97) /* los de la clase 3D */
            g_dma[(method - 0x180) / 4] = ramht_lookup(param, &inst) ? inst : 0;
        return;
    }
    if (g_class[sub & 7] == 0x44 && method == 0x0310) {
        /* Pattern, SET_MONOCHROME_COLOR0: el hardware lo deja en PGRAPH_PATT_COLOR0 (0x400B10).
         * D3D lo usa de marcador: al esperar a la GPU compara sus bits 2-6 con su valla. */
        InterlockedExchange(&g_patt_color0, (LONG)param);
        return;
    }
    if (g_class[sub & 7] && g_class[sub & 7] != 0x97)
        return; /* objetos 2D (blit, superficies): solo se registran */
    if (g_gl)
        gl_track(method, param);
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
                pg_printf("==== primer BEGIN_END con primitivas en el frame %u\n", g_frame);
            }
            g_begin_end++;
            dump_active_textures();
            g_prims[param & 15]++;
        }
        break;
    case 0x1810: /* DRAW_ARRAYS: inicio en los bits 0-23, número de vértices - 1 en 24-31 */
        g_draw_arrays++;
        if (g_gl) {
            uint32_t k, first = param & 0xFFFFFF, count = (param >> 24) + 1;
            for (k = 0; k < count; k++)
                aidx_push(first + k);
        }
        break;
    case 0x1800: /* ARRAY_ELEMENT16: dos índices por palabra, el bajo primero */
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
    case 0x194C: g_vconst_diffuse = param; break; /* SET_VERTEX_DATA4UB del difuso */
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
    case 0x1B00: case 0x1B40: case 0x1B80: case 0x1BC0: { /* SET_TEXTURE_OFFSET[etapa] */
        uint32_t k;
        g_tex_offset[(method - 0x1B00) / 0x40] = param;
        g_tex_sets++;
        for (k = 0; k < g_ntex && g_tex[k] != param; k++)
            ;
        if (k == g_ntex && g_ntex < MAX_TEXTURES)
            g_tex[g_ntex++] = param;
        break;
    }
    case 0x1D6C: g_sem_offset = param; break;
    case 0x1760: case 0x1764: case 0x1768: case 0x176C: case 0x1770: case 0x1774:
    case 0x1778: case 0x177C: case 0x1780: case 0x1784: case 0x1788: case 0x178C:
    case 0x1790: case 0x1794: case 0x1798: case 0x179C:
        g_vfmt[(method - 0x1760) / 4] = param;
        break;
    case 0x1D70: { /* BACK_END_WRITE_SEMAPHORE_RELEASE: D3D espera ver este valor */
        uint32_t phys, limit;
        uint8_t *p;
        if (dma_object(g_dma[(0x1A4 - 0x180) / 4], &phys, &limit) &&
            (p = nv2a_phys(phys + g_sem_offset, 4)) != NULL)
            memcpy(p, &param, 4);
        g_sem_releases++;
        break;
    }
    case 0x0130: /* FLIP_STALL: fin de frame */
        flush_run();
        g_flips++;
        present_and_pace();
        if (g_flips % PERF_EVERY == 0)
            perf_report();
        if (!g_gl && (g_flips <= PNG_FIRST_FLIPS || g_flips % 100 == 0)) {
            char why[48];
            snprintf(why, sizeof why, "FLIP_STALL del frame %u", g_frame);
            dump_png(why);
        }
        if (logging())
            pg_printf("==== fin del frame %u (FLIP_STALL): %u BEGIN_END, %u DRAW_ARRAYS, %u clears\n",
                      g_frame, g_begin_end, g_draw_arrays, g_clears);
        if (!g_frame0_done)
            g_frame0_done = 1;
        g_frame++;
        break;
    default: break;
    }
}

/* Primeros saltos/llamadas del kick en curso, para explicar un kick enorme (kick_body). */
static struct {
    int kind;
    uint32_t at, word, target;
} g_kick_ev[6];
static int g_kick_nev;

static void on_event(void *user, int kind, uint32_t at, uint32_t word, uint32_t target) {
    static const char *names[] = {"?", "JMP", "JMP antiguo", "CALL", "RETURN", "ERROR"};
    (void)user;
    if (g_kick_nev < 6) {
        g_kick_ev[g_kick_nev].kind = kind;
        g_kick_ev[g_kick_nev].at = at;
        g_kick_ev[g_kick_nev].word = word;
        g_kick_ev[g_kick_nev].target = target;
        g_kick_nev++;
    }
    flush_run();
    if (logging() || kind == PB_EVENT_ERROR)
        pg_printf("   pb %s en 0x%08X (palabra 0x%08X) -> 0x%08X\n", names[kind <= 5 ? kind : 0],
                  at, word, target);
}

/* Base en el host del objeto DMA del kick en curso (para on_readable). */
static const uint8_t *g_kick_base;

/* Un salto o una llamada del push buffer puede llevar a una página de la arena que el juego
 * nunca reservó (en la Xbox sería RAM con basura): leerla aquí sería un fallo de página en el
 * loader. Se comprueba al entrar en cada página. */
static int on_readable(void *user, uint32_t offset) {
    MEMORY_BASIC_INFORMATION m;
    (void)user;
    return g_kick_base && VirtualQuery(g_kick_base + offset, &m, sizeof m) && m.State == MEM_COMMIT &&
           !(m.Protect & (PAGE_NOACCESS | PAGE_GUARD));
}

static const pb_callbacks_t k_cb = {on_method, on_event, on_readable};

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
        phys = 0; /* sin objeto DMA válido: GET/PUT como direcciones físicas */
        limit = NGB_CONTIG_SIZE - 1;
    }
    span = nv2a_phys_span(phys);
    if (span && limit >= span)
        limit = span - 1;
    base = span ? nv2a_phys(phys, 1) : NULL;
    g_kicks++;
    if (logging())
        pg_printf("-- kick %u: GET 0x%08X -> PUT 0x%08X (objeto DMA 0x%05X: física 0x%08X)\n",
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
            ngb_log("    · PGRAPH: kick enorme: al entrar count %d, subrutina %d; eventos:", st_count,
                    st_sub);
            for (e = 0; e < g_kick_nev; e++)
                ngb_log("        %d en 0x%08X (palabra 0x%08X) -> 0x%08X", g_kick_ev[e].kind,
                        g_kick_ev[e].at, g_kick_ev[e].word, g_kick_ev[e].target);
        }
        /* Un kick normal son miles de palabras: uno de más de un millón es un replay de un
           push buffer viejo (el síntoma de los cuelgues de D3D_BlockOnTime). */
        if (g_words - words0 > (1u << 20))
            ngb_log("    · PGRAPH: kick %u enorme: %u palabras, GET 0x%08X -> PUT 0x%08X, acaba en 0x%08X",
                    g_kicks, g_words - words0, get, put, end);
    }
    flush_run();
    if (end != put)
        pg_printf("   pb: parada en 0x%08X sin llegar a PUT 0x%08X; se fuerza GET = PUT\n", end, put);
    fflush(g_pg);
    g_perf.body += qpc() - t_body;
}

/* Hilo de render: consume la cola hasta vaciarla. El pacing va fuera de g_pg_lock. */
static void kick_worker(void *unused) {
    (void)unused;
    for (;;) {
        kick_args_t k;
        LONGLONG t0 = qpc();
        EnterCriticalSection(&g_q_lock);
        if (g_q_get == g_q_put) {
            g_q_busy = 0;
            LeaveCriticalSection(&g_q_lock);
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
        g_q_get = k.put; /* tramo hecho: GET llega a PUT */
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
    /* Mientras consume, la posición real (como el GET del hardware): D3D_BlockOnTime decide
     * con ella dónde parchear una espera, y un GET atrasado le haría parchear lo ya pasado. */
    get = g_q_busy && g_q_running ? g_pb.cur : g_q_get;
    LeaveCriticalSection(&g_q_lock);
    return get;
}

void pgraph_fifo_set_get(uint32_t get) {
    if (!g_gl)
        return;
    for (;;) { /* D3D solo mueve GET con el pusher parado: se espera a que acabe lo que haya */
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
    if (g_gl) { /* asíncrono: se apunta el nuevo PUT y el hilo de render lo consume */
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
        kick_body(&k); /* rasterizador por software: síncrono */
        g_perf.call += qpc() - t_call;
    }
    g_perf.kick += qpc() - t_kick;
    LeaveCriticalSection(&g_pg_lock);
    return put; /* D3D espera GET == PUT: todo lo enviado cuenta como consumido */
}

void pgraph_vblank(uint32_t count) {
    if (count <= 3 || count % 60 == 0) {
        EnterCriticalSection(&g_pg_lock);
        pg_printf("~~ vblank %u (frame %u)\n", count, g_frame);
        LeaveCriticalSection(&g_pg_lock);
    }
}

int pgraph_init(const char *log_path) {
    char *dot;
    InitializeCriticalSection(&g_pg_lock);
    InitializeCriticalSection(&g_q_lock);
    if (fopen_s(&g_pg, log_path, "wb") || !g_pg) {
        ngb_log("nv2a: no se puede crear el log de PGRAPH %s", log_path);
        g_pg = NULL;
        return -1;
    }
    snprintf(g_png_base, sizeof g_png_base, "%s", log_path);
    dot = strrchr(g_png_base, '.');
    if (dot)
        *dot = 0;
    pg_printf("ngb-loader H7a: métodos de PGRAPH (f = frame, s = subcanal, método, nombre NV097)\n");
    ngb_log("nv2a: log de PGRAPH en %s", log_path);
    pgl_regs_defaults(&g_r);
    if (g_ngb_opts.window && g_ngb_opts.renderer != NGB_RENDERER_SOFT) {
        char why[400];
        if (!pgl_start((HWND)gfx_hwnd(), why, sizeof why)) {
            g_gl = 1;
            gfx_set_external(1);
            ngb_log("pgraph: render OpenGL (%s)", why);
        } else {
            ngb_log("pgraph: sin OpenGL (%s): rasterizador por software", why);
        }
    } else {
        ngb_log("pgraph: rasterizador por software%s", g_ngb_opts.window ? "" : " (sin --window)");
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
    ngb_log("pgraph: %u métodos distintos", n);
}

void pgraph_summary(void) {
    uint32_t i;
    int locked = 0, tries;
    if (!g_pg)
        return;
    /* Se llama desde ngb_stop con los hilos aún vivos: otro hilo puede estar a mitad de un
       kick con el lock tomado. Se intenta ~0.5 s y, si no se consigue, se omite el resumen
       (escribirlo sin lock correría contra el hilo que sigue ejecutando). */
    for (tries = 0; !(locked = TryEnterCriticalSection(&g_pg_lock)); tries++) {
        if (tries >= 50) {
            ngb_log("pgraph: resumen omitido (lock ocupado)%s%s",
                    g_perf.last[0] ? "; última medida: " : "", g_perf.last);
            pgl_summary(); /* solo contadores: no necesita el lock */
            return;
        }
        Sleep(10);
    }
    flush_run();
    if (g_perf.last[0])
        ngb_log("pgraph: última medida: %s", g_perf.last);
    ngb_log("pgraph: %u kicks, %u palabras de datos, %u frames (FLIP_STALL), %u BEGIN_END, "
            "%u DRAW_ARRAYS, %u clears (%u de color), %u semáforos, %u errores del pb, "
            "%u fixups de push buffer, %u esperas de D3D_BlockOnTime despertadas",
            g_kicks, g_words, g_flips, g_begin_end, g_draw_arrays, g_clears, g_color_clears,
            g_sem_releases, g_pb.errors, g_pb_fixups, g_block_wakes);
    ngb_log("pgraph: rasterizador: %u triángulos, %u píxeles escritos; %u primitivas sin dibujar "
            "(%u con posición no XYZRHW, %u sin superficie, %u puntos/líneas); %u DRAW_ARRAYS y "
            "ARRAY_ELEMENT sin dibujar",
            g_drawn_tris, g_drawn_pixels, g_skipped_draws, g_skip_pos, g_skip_target, g_skip_prim,
            g_draw_arrays + g_hist[0x1800 / 4] + g_hist[0x1808 / 4]);
    ngb_log("pgraph: texturas: %u SET_TEXTURE_OFFSET, %u offsets distintos; primer frame con "
            "primitivas: %d; %d PNG", g_tex_sets, g_ntex,
            g_first_draw_frame == 0xFFFFFFFFu ? -1 : (int)g_first_draw_frame, g_png_count);
    pgl_summary();
    for (i = 1; i < 16; i++)
        if (g_prims[i])
            ngb_log("  primitivas %-14s %u", prim_name(i), g_prims[i]);
    summary_hist(g_hist_frame0, "primer frame (hasta el primer FLIP_STALL), por método:");
    summary_hist(g_hist, "total por método:");
    fflush(g_pg);
    if (locked)
        LeaveCriticalSection(&g_pg_lock);
}
