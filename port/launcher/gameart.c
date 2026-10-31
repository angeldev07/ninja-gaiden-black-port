/*
 * gameart.c: the launcher's game art mode. See gameart.h.
 *
 * Assets (docs/target/launcher-laca-design.md, "Game art: assets"), all in demo.afs:
 *
 *   asset          entry         texture  format  size        crop
 *   hero (Ryu)     demo.spr      0        P8      1024x512    its sprite
 *   hero (Ayane)   demo.spr      1        P8      1024x512    its sprite
 *   logo           demo.spr      2        P8      512x256     whole texture, as a mask
 *   logo "BLACK"   demo.spr      3        P8      512x128     whole texture, as a mask
 *   title bg       titlebg.spr   1        P8      512x512     its sprite
 *   ring           titlebg.spr   3        A8      256x256     whole texture (alpha)
 *
 * A texture whose format or size differs (another region's disc, a mod) is skipped on its own.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "gameart.h"
#include "gamedata.h"
#include "gl/texconv.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FMT_P8 0x0B
#define FMT_A8 0x19

/* ------------------------------------------------------------------- grading ---- */

static float smoothstepf(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

static uint8_t to8(float v) {
    v = v * 255.0f + 0.5f;
    return (uint8_t)(v < 0.0f ? 0.0f : v > 255.0f ? 255.0f : v);
}

uint32_t ga_grade(uint8_t r8, uint8_t g8, uint8_t b8) {
    static const float BG[3] = {19 / 255.0f, 17 / 255.0f, 15 / 255.0f};
    static const float CREAM[3] = {239 / 255.0f, 231 / 255.0f, 214 / 255.0f};
    static const float VERM[3] = {210 / 255.0f, 63 / 255.0f, 47 / 255.0f};
    float in[3] = {r8 / 255.0f, g8 / 255.0f, b8 / 255.0f}, out[3];
    float lum = 0.299f * in[0] + 0.587f * in[1] + 0.114f * in[2];
    float t = powf(smoothstepf(0.04f, 0.92f, lum), 1.15f);
    float red = (in[0] - fmaxf(in[1], in[2]) - 0.14f) * 3.0f;
    int i;
    red = red < 0.0f ? 0.0f : red > 1.0f ? 1.0f : red;
    for (i = 0; i < 3; i++) {
        out[i] = BG[i] + (CREAM[i] * 0.86f - BG[i]) * t; /* duotone */
        out[i] += 0.14f * (in[i] - lum);                    /* a little of the original chroma */
        out[i] += (VERM[i] * (0.4f + 0.6f * t) - out[i]) * red * 0.8f;
    }
    return ((uint32_t)to8(out[0]) << 16) | ((uint32_t)to8(out[1]) << 8) | to8(out[2]);
}

uint8_t ga_logo_mask(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    float lum = (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f;
    float glow = r > g ? (float)(r - g) / 255.0f : 0.0f;
    float m = smoothstepf(0.55f, 0.9f, a / 255.0f) * (1.0f - smoothstepf(0.25f, 0.6f, glow) * (1.0f - lum));
    return to8(m);
}

/* -------------------------------------------------------------------- loader ---- */

static CRITICAL_SECTION g_lock;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static HANDLE g_thread;
static volatile LONG g_cancel, g_ready = 1;
static ga_image_t g_img[GA_COUNT];
static int g_hero_count;
static HWND g_notify;
static UINT g_notify_msg;

typedef struct job {
    wchar_t dir[MAX_PATH];
    int hero_index;
} job_t;

static BOOL CALLBACK init_lock(PINIT_ONCE o, PVOID p, PVOID *c) {
    (void)o;
    (void)p;
    (void)c;
    InitializeCriticalSection(&g_lock);
    return TRUE;
}

static void free_image(ga_image_t *im) {
    free(im->px);
    free(im->alpha);
    memset(im, 0, sizeof *im);
}

/* RGBA8 of texture `ordinal` if it has the expected format and size; NULL otherwise. */
static uint8_t *decode(const uint8_t *xpr, size_t len, int ordinal, int fmt, int w, int h) {
    xpr_tex_t t;
    texconv_desc d;
    uint8_t *rgba;
    if (!xpr_texture(xpr, len, ordinal, &t) || (int)((t.format >> 8) & 0xFF) != fmt ||
        texconv_describe(t.format, 0, 0, 0, &d) != 0 || d.width != w || d.height != h ||
        d.faces != 1 || d.src_size > t.data_len)
        return NULL;
    rgba = (uint8_t *)malloc(d.dst_size);
    if (rgba && texconv_convert(t.data, d.src_size, t.palette, t.palette_len, &d, rgba, d.dst_size) != 0) {
        free(rgba);
        rgba = NULL;
    }
    return rgba;
}

/* The sprite rectangle drawn from texture `ordinal`, or the whole texture. */
static void crop_for(const uint8_t *xpr, size_t len, int ordinal, int w, int h, spr_rect_t *r) {
    int i;
    for (i = 0; i < 64; i++) {
        if (!spr_sprite(xpr, len, i, r))
            break;
        if (r->tex == ordinal && r->x1 <= w && r->y1 <= h)
            return;
    }
    r->tex = ordinal;
    r->x0 = r->y0 = 0;
    r->x1 = w;
    r->y1 = h;
}

static int make_rgb(ga_image_t *im, const uint8_t *rgba, int w, const spr_rect_t *r) {
    int x, y;
    im->w = r->x1 - r->x0;
    im->h = r->y1 - r->y0;
    im->px = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)im->w * im->h);
    if (!im->px)
        return 0;
    for (y = 0; y < im->h; y++)
        for (x = 0; x < im->w; x++) {
            const uint8_t *p = rgba + ((size_t)(r->y0 + y) * w + (r->x0 + x)) * 4;
            im->px[(size_t)y * im->w + x] = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        }
    return 1;
}

static int make_mask(ga_image_t *im, const uint8_t *rgba, int w, int h, int logo) {
    size_t i, n = (size_t)w * h;
    im->w = w;
    im->h = h;
    im->alpha = (uint8_t *)malloc(n);
    if (!im->alpha)
        return 0;
    for (i = 0; i < n; i++) {
        const uint8_t *p = rgba + i * 4;
        im->alpha[i] = logo ? ga_logo_mask(p[0], p[1], p[2], p[3]) : p[3];
    }
    return 1;
}

/* The launcher keeps no log file of its own: a disabled asset goes to the debugger output. */
static void log_line(const wchar_t *dir, const char *what) {
    char buf[160];
    (void)dir;
    _snprintf(buf, sizeof buf, "ngb-launcher: game art: %s not available\n", what);
    buf[sizeof buf - 1] = 0;
    OutputDebugStringA(buf);
}

static DWORD WINAPI load_thread(LPVOID p) {
    job_t *job = (job_t *)p;
    wchar_t afs[MAX_PATH];
    ga_image_t img[GA_COUNT];
    uint8_t *demo = NULL, *title = NULL, *rgba;
    size_t demo_len = 0, title_len = 0;
    int heroes[2], nheroes = 0, i;
    spr_rect_t r;
    memset(img, 0, sizeof img);
    _snwprintf(afs, MAX_PATH, L"%ls\\demo.afs", job->dir);
    afs[MAX_PATH - 1] = 0;
    demo = gamedata_load(afs, "demo.spr", &demo_len);
    if (demo && !g_cancel) {
        for (i = 0; i < 2; i++) {
            if ((rgba = decode(demo, demo_len, i, FMT_P8, 1024, 512)) != NULL) {
                heroes[nheroes++] = i;
                free(rgba);
            }
        }
        if (nheroes > 0) {
            int ord = heroes[(job->hero_index % nheroes + nheroes) % nheroes];
            rgba = decode(demo, demo_len, ord, FMT_P8, 1024, 512);
            crop_for(demo, demo_len, ord, 1024, 512, &r);
            if (rgba && !make_rgb(&img[GA_HERO], rgba, 1024, &r))
                free_image(&img[GA_HERO]);
            free(rgba);
        } else {
            log_line(job->dir, "key art");
        }
        if ((rgba = decode(demo, demo_len, 2, FMT_P8, 512, 256)) != NULL) {
            make_mask(&img[GA_LOGO], rgba, 512, 256, 1);
            free(rgba);
        } else {
            log_line(job->dir, "logo");
        }
        if ((rgba = decode(demo, demo_len, 3, FMT_P8, 512, 128)) != NULL) {
            make_mask(&img[GA_LOGO_BLACK], rgba, 512, 128, 1);
            free(rgba);
        } else {
            log_line(job->dir, "logo BLACK");
        }
    }
    title = g_cancel ? NULL : gamedata_load(afs, "titlebg.spr", &title_len);
    if (title && !g_cancel) {
        if ((rgba = decode(title, title_len, 1, FMT_P8, 512, 512)) != NULL) {
            crop_for(title, title_len, 1, 512, 512, &r);
            make_rgb(&img[GA_TITLEBG], rgba, 512, &r);
            free(rgba);
        } else {
            log_line(job->dir, "title background");
        }
        if ((rgba = decode(title, title_len, 3, FMT_A8, 256, 256)) != NULL) {
            make_mask(&img[GA_RING], rgba, 256, 256, 0);
            free(rgba);
        } else {
            log_line(job->dir, "ornament ring");
        }
    }
    free(demo);
    free(title);
    EnterCriticalSection(&g_lock);
    if (!g_cancel) {
        memcpy(g_img, img, sizeof img);
        g_hero_count = nheroes;
    } else {
        for (i = 0; i < GA_COUNT; i++)
            free_image(&img[i]);
    }
    LeaveCriticalSection(&g_lock);
    InterlockedExchange(&g_ready, 1);
    if (!g_cancel && g_notify)
        PostMessageW(g_notify, g_notify_msg, 0, 0);
    free(job);
    return 0;
}

void gameart_notify(void *hwnd, unsigned msg) {
    g_notify = (HWND)hwnd;
    g_notify_msg = msg;
}

void gameart_stop(void) {
    int i;
    InitOnceExecuteOnce(&g_once, init_lock, NULL, NULL);
    if (g_thread) {
        InterlockedExchange(&g_cancel, 1);
        WaitForSingleObject(g_thread, INFINITE);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    EnterCriticalSection(&g_lock);
    for (i = 0; i < GA_COUNT; i++)
        free_image(&g_img[i]);
    g_hero_count = 0;
    LeaveCriticalSection(&g_lock);
    InterlockedExchange(&g_cancel, 0);
    InterlockedExchange(&g_ready, 1);
}

void gameart_start(const wchar_t *game_dir, int hero_index) {
    job_t *job;
    gameart_stop();
    job = (job_t *)calloc(1, sizeof *job);
    if (!job)
        return;
    _snwprintf(job->dir, MAX_PATH, L"%ls", game_dir);
    job->dir[MAX_PATH - 1] = 0;
    job->hero_index = hero_index;
    InterlockedExchange(&g_ready, 0);
    g_thread = CreateThread(NULL, 0, load_thread, job, 0, NULL);
    if (!g_thread) {
        free(job);
        InterlockedExchange(&g_ready, 1);
    }
}

int gameart_ready(void) { return g_ready != 0; }

const ga_image_t *gameart_get(int which) {
    const ga_image_t *im;
    if (which < 0 || which >= GA_COUNT || !g_ready)
        return NULL;
    im = &g_img[which];
    return im->px || im->alpha ? im : NULL;
}

int gameart_hero_count(void) { return g_ready ? g_hero_count : 0; }
