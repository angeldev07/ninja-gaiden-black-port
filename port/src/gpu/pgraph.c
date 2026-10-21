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
#include "png.h"
#include "pushbuf.h"

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
    return g_frame < g_ngb_opts.pgraph_frames || g_frame == g_first_draw_frame;
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
    if (!pitch)
        pitch = w * bpp;
    surface = nv2a_phys(phys + g_color_offset, pitch * h);
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
                phys + g_color_offset);
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
    if (x1 >= 4096 || y1 >= 4096 || x0 > x1 || y0 > y1)
        return;
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

static void on_method(void *user, uint32_t sub, uint32_t method, uint32_t param, int non_inc) {
    uint32_t inst = 0;
    (void)user;
    g_words++;
    g_hist[method / 4]++;
    if (!g_frame0_done)
        g_hist_frame0[method / 4]++;
    record(sub, method, param, non_inc);
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
    if (g_class[sub & 7] && g_class[sub & 7] != 0x97)
        return; /* objetos 2D (blit, superficies): solo se registran */
    switch (method) {
    case 0x0200: g_clip_h = param; break;
    case 0x0204: g_clip_v = param; break;
    case 0x0208: g_format = param; break;
    case 0x020C: g_pitch = param; break;
    case 0x0210: g_color_offset = param; break;
    case 0x1D90: g_clear_value = param; break;
    case 0x1D98: g_clear_h = param; break;
    case 0x1D9C: g_clear_v = param; break;
    case 0x1D94: do_clear(param); break;
    case 0x17FC:
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
    case 0x1810: g_draw_arrays++; break;
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
        if (g_flips <= PNG_FIRST_FLIPS || g_flips % 100 == 0) {
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

static void on_event(void *user, int kind, uint32_t at, uint32_t word, uint32_t target) {
    static const char *names[] = {"?", "JMP", "JMP antiguo", "CALL", "RETURN", "ERROR"};
    (void)user;
    flush_run();
    if (logging() || kind == PB_EVENT_ERROR)
        pg_printf("   pb %s en 0x%08X (palabra 0x%08X) -> 0x%08X\n", names[kind <= 5 ? kind : 0],
                  at, word, target);
}

static const pb_callbacks_t k_cb = {on_method, on_event};

uint32_t pgraph_kick(uint32_t dma_instance, uint32_t get, uint32_t put) {
    uint32_t phys = 0, limit = NGB_CONTIG_SIZE - 1, end, span;
    uint8_t *base;
    EnterCriticalSection(&g_pg_lock);
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
    end = base ? pb_run(&g_pb, base, limit + 1, get, put, 1u << 22, &k_cb, NULL) : put;
    flush_run();
    if (end != put)
        pg_printf("   pb: parada en 0x%08X sin llegar a PUT 0x%08X; se fuerza GET = PUT\n", end, put);
    fflush(g_pg);
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
    int tries;
    if (!g_pg)
        return;
    /* Se llama desde ngb_stop: un hilo suspendido podría tener el lock tomado para siempre.
       Se intenta durante ~0.5 s y, si no se consigue, se omite el resumen en vez de colgar. */
    for (tries = 0; !TryEnterCriticalSection(&g_pg_lock); tries++) {
        if (tries >= 50) {
            ngb_log("pgraph: resumen omitido (lock ocupado por un hilo detenido)");
            return;
        }
        Sleep(10);
    }
    flush_run();
    ngb_log("pgraph: %u kicks, %u palabras de datos, %u frames (FLIP_STALL), %u BEGIN_END, "
            "%u DRAW_ARRAYS, %u clears (%u de color), %u semáforos, %u errores del pb",
            g_kicks, g_words, g_flips, g_begin_end, g_draw_arrays, g_clears, g_color_clears,
            g_sem_releases, g_pb.errors);
    ngb_log("pgraph: texturas: %u SET_TEXTURE_OFFSET, %u offsets distintos; primer frame con "
            "primitivas: %d; %d PNG", g_tex_sets, g_ntex,
            g_first_draw_frame == 0xFFFFFFFFu ? -1 : (int)g_first_draw_frame, g_png_count);
    for (i = 1; i < 16; i++)
        if (g_prims[i])
            ngb_log("  primitivas %-14s %u", prim_name(i), g_prims[i]);
    summary_hist(g_hist_frame0, "primer frame (hasta el primer FLIP_STALL), por método:");
    summary_hist(g_hist, "total por método:");
    fflush(g_pg);
    LeaveCriticalSection(&g_pg_lock);
}
