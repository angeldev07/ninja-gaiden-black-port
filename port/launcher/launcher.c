/*
 * launcher.c: ngb-launcher.exe, el launcher gráfico del port (Win32 + GDI, sin dependencias).
 *
 * Una sola ventana de tamaño fijo (1240x700 lógicos, escalados al DPI del sistema) que se
 * dibuja entera en un back buffer DIB y se vuelca con BitBlt. No hay controles de Windows:
 * cada botón, casilla y desplegable se dibuja a mano (rectángulos redondeados con borde
 * suavizado por distancia con signo, texto con GDI) y se registra como una zona de clic en
 * cada pasada de pintura ("modo inmediato": el dibujo y la zona nacen en la misma función,
 * así no pueden desincronizarse). El desplegable abierto se pinta el último y encima.
 *
 *   - Los ajustes viven en ngb.ini junto al exe (GetPrivateProfileString, UTF-16 con BOM
 *     para que las rutas con tildes sobrevivan) y se guardan en cada cambio.
 *   - PLAY lanza ngb-loader.exe <xbe> --window --timeout 0 --max-calls 0 --idle 0 más las
 *     opciones elegidas, sin consola (CREATE_NO_WINDOW), y vigila el proceso con un temporizador.
 *   - Solo se ofrecen las opciones que el parser de port\src\main.c ya entiende.
 *   - El arte es original (un shuriken dibujado por código); no hay nada del juego.
 *
 *   ngb-launcher.exe [--page N]     N = 0 Jugar .. 5 Log (para capturas de la interfaz)
 */
#define UNICODE /* IDC_ARROW, MAKEINTRESOURCE...: las macros de Windows en su versión W */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <commdlg.h>
#include <shellapi.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wchar.h>

#include "version.h"

/* ------------------------------------------------------------------ constantes ---- */

#define LOGICAL_W 1240
#define LOGICAL_H 700
#define SIDEBAR_W 264
#define FOOT_H 88
#define PANEL_X (SIDEBAR_W + 48) /* x de la columna de contenido */
#define PANEL_W (LOGICAL_W - SIDEBAR_W - 96)
#define PANEL_BOTTOM (LOGICAL_H - FOOT_H - 16)
#define ROW_H 72
#define TIMER_TICK 1
#define TICK_MS 500
#define PI_F 3.14159265f

/* Paleta 0xRRGGBB. */
#define C_SIDE 0x0b0b0e
#define C_MAIN 0x121217
#define C_CARD 0x1a1a21
#define C_CARD_HOT 0x22222b
#define C_FIELD 0x0e0e12
#define C_LINE 0x2b2b35
#define C_TEXT 0xe9e5dc
#define C_DIM 0x8b8b96
#define C_GOLD 0xd6b46e
#define C_RED 0xb32020
#define C_PLAY 0x7a1414
#define C_PLAY_HOT 0x951a1a
#define C_OK 0x5fbf78
#define C_WARN 0xe0a840
#define C_BAD 0xd8574c

typedef uint32_t rgb_t;

enum font_id {
    F_BODY, F_SMALL, F_BOLD, F_TINY, F_H1, F_H2, F_BRAND, F_PLAY, F_MONO, F_COUNT
};

enum page_id { PG_PLAY, PG_GRAPHICS, PG_AUDIO, PG_CONTROLS, PG_ADVANCED, PG_LOG, PG_COUNT };

/* Identificadores de las zonas de clic. */
enum ui_id {
    ID_NONE = 0,
    ID_NAV0 = 1, /* ..ID_NAV0 + PG_COUNT - 1 */
    ID_PLAY = 20, ID_STOP, ID_BROWSE, ID_VIEW_LOG, ID_OPEN_RUNS, ID_CB_RENDERER, ID_CB_LOGCAP,
    ID_CHK_PACING, ID_CHK_MUTE, ID_CHK_WAV, ID_CHK_RDOC, ID_CHK_LENIENT,
    ID_ITEM0 = 100 /* elementos del desplegable abierto */
};

typedef struct hit {
    int x, y, w, h, id;
} hit_t;

typedef struct settings {
    wchar_t xbe[MAX_PATH];
    int renderer;   /* 0 automático, 1 gl, 2 soft */
    int pacing;     /* 1: 60 fps; 0: --no-pacing */
    int mute;       /* --mute */
    int audio_wav;  /* --audio-wav */
    int rdoc_key;   /* --rdoc-key */
    int lenient;    /* --lenient */
    int log_cap;    /* --log-cap (2000 por defecto del loader) */
} settings_t;

typedef struct session {
    wchar_t start[48];  /* "22/10/2026 14:03" */
    long duration_s;
    int code;           /* código de salida; SESSION_STOPPED / SESSION_NONE */
    wchar_t log[MAX_PATH];
} session_t;
#define SESSION_NONE (-2)
#define SESSION_STOPPED (-1)
#define DEFAULT_LOG_CAP 2000

/* ------------------------------------------------------------------------ estado ---- */

static HINSTANCE g_inst;
static HWND g_hwnd;
static int g_dpi = 96;
static HDC g_mem;
static HBITMAP g_bmp, g_bmp_old;
static uint32_t *g_px;
static int g_pw, g_ph;
static HFONT g_fonts[F_COUNT];

static wchar_t g_exe_dir[MAX_PATH]; /* con barra final */
static wchar_t g_ini[MAX_PATH];
static wchar_t g_loader[MAX_PATH];
static wchar_t g_runs[MAX_PATH];
static settings_t g_set;
static session_t g_last;

static int g_page = PG_PLAY;
static hit_t g_hits[160];
static int g_nhits;
static int g_hover = ID_NONE, g_down = ID_NONE;
static int g_open_combo = ID_NONE; /* desplegable abierto */
static int g_ovl_x, g_ovl_y, g_ovl_w; /* su caja, para pintar la lista al final */
static int g_mouse_tracked;

static HANDLE g_proc;
static DWORD g_pid;
static ULONGLONG g_t0;
static int g_stopped_by_user;

/* Cola del log. */
static wchar_t *g_log_w;
static int *g_log_ls;
static int g_log_n;
static int g_log_scroll;
static wchar_t g_log_path[MAX_PATH];
static ULONGLONG g_log_sig; /* tamaño + fecha del archivo ya leído */
static wchar_t g_log_stamp[16];

static const wchar_t *k_nav[PG_COUNT] = {L"Jugar", L"Gráficos", L"Audio", L"Controles",
                                         L"Avanzado", L"Log"};
static const wchar_t *k_renderer_items[] = {L"Automático", L"OpenGL 4.x",
                                            L"Software (rasterizador)"};
static const wchar_t *k_logcap_items[] = {L"500", L"2000 (por defecto)", L"10 000", L"50 000"};
static const int k_logcap_vals[] = {500, DEFAULT_LOG_CAP, 10000, 50000};

/* ----------------------------------------------------------------------- utilidades ---- */

static int S(int v) { return MulDiv(v, g_dpi, 96); }

static COLORREF cref(rgb_t c) { return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255); }

static rgb_t lerp_rgb(rgb_t a, rgb_t b, int t256) {
    int r = (int)(((a >> 16) & 255) * (256 - t256) + ((b >> 16) & 255) * t256) >> 8;
    int g = (int)(((a >> 8) & 255) * (256 - t256) + ((b >> 8) & 255) * t256) >> 8;
    int bl = (int)((a & 255) * (256 - t256) + (b & 255) * t256) >> 8;
    return ((rgb_t)r << 16) | ((rgb_t)g << 8) | (rgb_t)bl;
}

static void wfmt(wchar_t *buf, size_t n, const wchar_t *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, n, fmt, ap);
    va_end(ap);
    buf[n - 1] = 0;
}

static void wcat(wchar_t *dst, size_t n, const wchar_t *src) {
    size_t len = wcslen(dst);
    if (len + 1 < n)
        _snwprintf(dst + len, n - len, L"%ls", src);
    dst[n - 1] = 0;
}

static int file_exists(const wchar_t *path) {
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int dir_exists(const wchar_t *path) {
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/* Carpeta de datos del port: %NGB_DATA% o %USERPROFILE%\ngb-data (igual que el loader). */
static void data_dir(wchar_t *out, size_t n) {
    wchar_t v[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"NGB_DATA", v, MAX_PATH);
    out[0] = 0;
    if (len && len < MAX_PATH) {
        wfmt(out, n, L"%ls", v);
        return;
    }
    len = GetEnvironmentVariableW(L"USERPROFILE", v, MAX_PATH);
    if (len && len < MAX_PATH)
        wfmt(out, n, L"%ls\\ngb-data", v);
}

static const wchar_t *base_name(const wchar_t *path) {
    const wchar_t *s = wcsrchr(path, L'\\');
    return s ? s + 1 : path;
}

static void fmt_duration(long s, wchar_t *out, size_t n) {
    if (s < 0)
        s = 0;
    if (s >= 3600)
        wfmt(out, n, L"%ld h %02ld min", s / 3600, (s / 60) % 60);
    else if (s >= 60)
        wfmt(out, n, L"%ld min %02ld s", s / 60, s % 60);
    else
        wfmt(out, n, L"%ld s", s);
}

/* ---------------------------------------------------------------------------- ajustes ---- */

static void ini_text(const wchar_t *sec, const wchar_t *key, const wchar_t *def, wchar_t *out,
                     DWORD n) {
    GetPrivateProfileStringW(sec, key, def, out, n, g_ini);
}
static int ini_int(const wchar_t *sec, const wchar_t *key, int def) {
    return (int)GetPrivateProfileIntW(sec, key, def, g_ini);
}
static void ini_put(const wchar_t *sec, const wchar_t *key, const wchar_t *val) {
    WritePrivateProfileStringW(sec, key, val, g_ini);
}
static void ini_put_int(const wchar_t *sec, const wchar_t *key, int val) {
    wchar_t t[24];
    wfmt(t, 24, L"%d", val);
    ini_put(sec, key, t);
}

/* ngb.ini nuevo: UTF-16 con BOM, para que las rutas con tildes se guarden bien. */
static void ini_ensure(void) {
    static const unsigned char bom[2] = {0xFF, 0xFE};
    DWORD n;
    HANDLE h;
    if (file_exists(g_ini))
        return;
    h = CreateFileW(g_ini, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, bom, 2, &n, NULL);
        CloseHandle(h);
    }
}

static void save_settings(void) {
    ini_put(L"game", L"xbe", g_set.xbe);
    ini_put(L"graphics", L"renderer",
            g_set.renderer == 1 ? L"gl" : g_set.renderer == 2 ? L"soft" : L"auto");
    ini_put_int(L"graphics", L"limit_fps", g_set.pacing);
    ini_put_int(L"audio", L"mute", g_set.mute);
    ini_put_int(L"audio", L"wav", g_set.audio_wav);
    ini_put_int(L"advanced", L"rdoc_key", g_set.rdoc_key);
    ini_put_int(L"advanced", L"lenient", g_set.lenient);
    ini_put_int(L"advanced", L"log_cap", g_set.log_cap);
}

static void load_settings(void) {
    wchar_t t[32];
    wchar_t data[MAX_PATH], def[MAX_PATH];
    int i, ok = 0;
    ini_text(L"game", L"xbe", L"?", g_set.xbe, MAX_PATH);
    if (!wcscmp(g_set.xbe, L"?")) { /* primera vez: la copia de siempre, si existe */
        g_set.xbe[0] = 0;
        data_dir(data, MAX_PATH);
        wfmt(def, MAX_PATH, L"%ls\\game\\default.xbe", data);
        if (data[0] && file_exists(def))
            wfmt(g_set.xbe, MAX_PATH, L"%ls", def);
    }
    ini_text(L"graphics", L"renderer", L"auto", t, 32);
    g_set.renderer = !wcscmp(t, L"gl") ? 1 : !wcscmp(t, L"soft") ? 2 : 0;
    g_set.pacing = ini_int(L"graphics", L"limit_fps", 1) != 0;
    g_set.mute = ini_int(L"audio", L"mute", 0) != 0;
    g_set.audio_wav = ini_int(L"audio", L"wav", 0) != 0;
    g_set.rdoc_key = ini_int(L"advanced", L"rdoc_key", 0) != 0;
    g_set.lenient = ini_int(L"advanced", L"lenient", 0) != 0;
    g_set.log_cap = ini_int(L"advanced", L"log_cap", DEFAULT_LOG_CAP);
    for (i = 0; i < (int)(sizeof k_logcap_vals / sizeof k_logcap_vals[0]); i++)
        ok |= k_logcap_vals[i] == g_set.log_cap;
    if (!ok)
        g_set.log_cap = DEFAULT_LOG_CAP;

    ini_text(L"session", L"start", L"", g_last.start, 48);
    g_last.duration_s = ini_int(L"session", L"duration_s", 0);
    g_last.code = ini_int(L"session", L"code", SESSION_NONE);
    ini_text(L"session", L"log", L"", g_last.log, MAX_PATH);
    if (!g_last.start[0])
        g_last.code = SESSION_NONE;
    save_settings();
}

static int can_play(void);

/* Línea de comandos de PLAY (también la que enseña la página Avanzado). */
static void build_cmdline(wchar_t *out, size_t n) {
    wfmt(out, n, L"\"%ls\" \"%ls\" --window --timeout 0 --max-calls 0 --idle 0", g_loader,
         g_set.xbe);
    if (g_set.renderer == 1)
        wcat(out, n, L" --renderer gl");
    else if (g_set.renderer == 2)
        wcat(out, n, L" --renderer soft");
    if (!g_set.pacing)
        wcat(out, n, L" --no-pacing");
    if (g_set.mute)
        wcat(out, n, L" --mute");
    if (g_set.audio_wav)
        wcat(out, n, L" --audio-wav");
    if (g_set.rdoc_key)
        wcat(out, n, L" --rdoc-key");
    if (g_set.lenient)
        wcat(out, n, L" --lenient");
    if (g_set.log_cap != DEFAULT_LOG_CAP) {
        wchar_t t[40];
        wfmt(t, 40, L" --log-cap %d", g_set.log_cap);
        wcat(out, n, t);
    }
}

/* Resumen de una línea de la configuración actual (barra de estado). */
static void config_summary(wchar_t *out, size_t n) {
    const wchar_t *rend = g_set.renderer == 1   ? L"OpenGL 4.x"
                          : g_set.renderer == 2 ? L"Software"
                                                : L"Render automático";
    wfmt(out, n, L"%ls  ·  %ls  ·  %ls", rend, g_set.pacing ? L"60 fps" : L"Sin límite de fps",
         g_set.mute ? L"Silenciado" : L"Con sonido");
    if (g_set.rdoc_key)
        wcat(out, n, L"  ·  RenderDoc");
    if (g_set.lenient)
        wcat(out, n, L"  ·  Lenient");
    if (g_set.xbe[0]) {
        wcat(out, n, L"  ·  ");
        wcat(out, n, base_name(g_set.xbe));
    }
}

/* -------------------------------------------------------------------------- dibujo ---- */

static void gdi_sync(void) { GdiFlush(); }

static void blend_px(int x, int y, rgb_t c, int a) {
    uint32_t *p, d;
    int ia, r, g, b;
    if ((unsigned)x >= (unsigned)g_pw || (unsigned)y >= (unsigned)g_ph || a <= 0)
        return;
    p = &g_px[y * g_pw + x];
    if (a >= 255) {
        *p = c;
        return;
    }
    d = *p;
    ia = 255 - a;
    r = (int)((((c >> 16) & 255) * a + ((d >> 16) & 255) * ia + 127) / 255);
    g = (int)((((c >> 8) & 255) * a + ((d >> 8) & 255) * ia + 127) / 255);
    b = (int)(((c & 255) * a + (d & 255) * ia + 127) / 255);
    *p = ((rgb_t)r << 16) | ((rgb_t)g << 8) | (rgb_t)b;
}

static void fill_rect(int x, int y, int w, int h, rgb_t c) {
    int x0 = S(x), y0 = S(y), x1 = S(x + w), y1 = S(y + h), i, j;
    gdi_sync();
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > g_pw)
        x1 = g_pw;
    if (y1 > g_ph)
        y1 = g_ph;
    for (j = y0; j < y1; j++)
        for (i = x0; i < x1; i++)
            g_px[j * g_pw + i] = c;
}

/* Rectángulo redondeado en píxeles de dispositivo. stroke = 0: relleno (degradado vertical
 * c1 -> c2); stroke > 0: solo el borde interior de ese grosor. El borde del dibujo se suaviza
 * con la distancia con signo al contorno. */
static void rrect_dev(int X, int Y, int W, int H, int R, rgb_t c1, rgb_t c2, int stroke,
                      int alpha) {
    int i, j;
    float hw = W * 0.5f, hh = H * 0.5f, cx = X + hw, cy = Y + hh, r = (float)R;
    gdi_sync();
    if (r > hw)
        r = hw;
    if (r > hh)
        r = hh;
    for (j = Y - 1; j <= Y + H; j++) {
        int t = H > 1 ? (j - Y) * 256 / H : 0;
        rgb_t col = c1 == c2 ? c1 : lerp_rgb(c1, c2, t < 0 ? 0 : t > 256 ? 256 : t);
        for (i = X - 1; i <= X + W; i++) {
            float qx = fabsf(i + 0.5f - cx) - (hw - r), qy = fabsf(j + 0.5f - cy) - (hh - r);
            float ox = fmaxf(qx, 0.0f), oy = fmaxf(qy, 0.0f);
            float d = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.0f) - r;
            float cov = 0.5f - d;
            if (cov > 1)
                cov = 1;
            if (stroke > 0) {
                float inner = 0.5f + d + (float)stroke;
                if (inner < cov)
                    cov = inner;
            }
            if (cov > 0)
                blend_px(i, j, col, (int)(cov * alpha));
        }
    }
}

static void fill_rrect(int x, int y, int w, int h, int r, rgb_t c) {
    rrect_dev(S(x), S(y), S(x + w) - S(x), S(y + h) - S(y), S(r), c, c, 0, 255);
}
static void fill_rrect_grad(int x, int y, int w, int h, int r, rgb_t c1, rgb_t c2) {
    rrect_dev(S(x), S(y), S(x + w) - S(x), S(y + h) - S(y), S(r), c1, c2, 0, 255);
}
static void stroke_rrect(int x, int y, int w, int h, int r, rgb_t c) {
    int t = S(1) < 1 ? 1 : S(1);
    rrect_dev(S(x), S(y), S(x + w) - S(x), S(y + h) - S(y), S(r), c, c, t, 255);
}
static void fill_circle(int cx, int cy, int r, rgb_t c) {
    fill_rrect(cx - r, cy - r, 2 * r, 2 * r, r, c);
}

/* Segmento con extremos redondos (coordenadas lógicas en coma flotante). */
static void capsule(float ax, float ay, float bx, float by, float thick, rgb_t col) {
    float k = (float)g_dpi / 96.0f, dx, dy, len2, rad = thick * k * 0.5f;
    int i, j, x0, x1, y0, y1;
    gdi_sync();
    ax *= k; ay *= k; bx *= k; by *= k;
    dx = bx - ax;
    dy = by - ay;
    len2 = dx * dx + dy * dy;
    x0 = (int)floorf((ax < bx ? ax : bx) - rad - 1);
    x1 = (int)ceilf((ax > bx ? ax : bx) + rad + 1);
    y0 = (int)floorf((ay < by ? ay : by) - rad - 1);
    y1 = (int)ceilf((ay > by ? ay : by) + rad + 1);
    for (j = y0; j <= y1; j++)
        for (i = x0; i <= x1; i++) {
            float px = i + 0.5f - ax, py = j + 0.5f - ay, t = len2 > 0 ? (px * dx + py * dy) / len2 : 0;
            float qx, qy, d, cov;
            if (t < 0)
                t = 0;
            if (t > 1)
                t = 1;
            qx = px - dx * t;
            qy = py - dy * t;
            d = sqrtf(qx * qx + qy * qy) - rad;
            cov = 0.5f - d;
            if (cov > 1)
                cov = 1;
            if (cov > 0)
                blend_px(i, j, col, (int)(cov * 255));
        }
}

/* Triángulo con borde suavizado (supersampling 4x4). */
static void tri_aa(float ax, float ay, float bx, float by, float cx, float cy, rgb_t col) {
    float k = (float)g_dpi / 96.0f, mnx, mxx, mny, mxy;
    int i, j, sx, sy;
    gdi_sync();
    ax *= k; ay *= k; bx *= k; by *= k; cx *= k; cy *= k;
    mnx = fminf(ax, fminf(bx, cx));
    mxx = fmaxf(ax, fmaxf(bx, cx));
    mny = fminf(ay, fminf(by, cy));
    mxy = fmaxf(ay, fmaxf(by, cy));
    for (j = (int)floorf(mny); j <= (int)ceilf(mxy); j++)
        for (i = (int)floorf(mnx); i <= (int)ceilf(mxx); i++) {
            int hits = 0;
            for (sy = 0; sy < 4; sy++)
                for (sx = 0; sx < 4; sx++) {
                    float px = i + (sx + 0.5f) / 4, py = j + (sy + 0.5f) / 4;
                    float d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by);
                    float d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy);
                    float d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay);
                    int neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
                    hits += !(neg && pos);
                }
            if (hits)
                blend_px(i, j, col, hits * 255 / 16);
        }
}

/* El mismo shuriken del icono (res\make_icon.py), en un cuadrado carmesí. */
static void draw_emblem(int x, int y, int size) {
    float cx = x + size * 0.5f, cy = y + size * 0.5f, R = size * 0.405f, r = size * 0.125f;
    int k, side;
    fill_rrect_grad(x, y, size, size, size * 22 / 100, 0x961c1c, 0x3a0808);
    stroke_rrect(x, y, size, size, size * 22 / 100, 0xa88246);
    for (k = 0; k < 4; k++) {
        float tip = (45.0f + 90.0f * k) * PI_F / 180.0f;
        float tx = cx + R * cosf(tip), ty = cy - R * sinf(tip);
        for (side = -1; side <= 1; side += 2) {
            float in = tip + side * PI_F / 4.0f;
            tri_aa(cx, cy, tx, ty, cx + r * cosf(in), cy - r * sinf(in),
                   side > 0 ? 0xf6e4aa : 0xb28436);
        }
    }
    fill_circle((int)cx, (int)cy, size * 9 / 100 < 2 ? 2 : size * 9 / 100, 0xd0a85c);
    fill_circle((int)cx, (int)cy, size * 6 / 100 < 1 ? 1 : size * 6 / 100, 0x220606);
}

/* ---- texto (GDI) ---- */

static void make_fonts(void) {
    static const struct {
        const wchar_t *face;
        int px, weight;
    } spec[F_COUNT] = {
        {L"Segoe UI", 15, FW_NORMAL}, {L"Segoe UI", 12, FW_NORMAL}, {L"Segoe UI", 15, FW_SEMIBOLD},
        {L"Segoe UI", 11, FW_SEMIBOLD}, {L"Georgia", 34, FW_NORMAL}, {L"Georgia", 22, FW_NORMAL},
        {L"Georgia", 18, FW_BOLD}, {L"Georgia", 26, FW_BOLD}, {L"Consolas", 12, FW_NORMAL},
    };
    int i;
    for (i = 0; i < F_COUNT; i++) {
        if (g_fonts[i])
            DeleteObject(g_fonts[i]);
        g_fonts[i] = CreateFontW(-S(spec[i].px), 0, 0, 0, spec[i].weight, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, spec[i].face);
    }
}

static void txt(int font, rgb_t col, int x, int y, int w, int h, UINT fmt, const wchar_t *s) {
    RECT r;
    HGDIOBJ old = SelectObject(g_mem, g_fonts[font]);
    SetBkMode(g_mem, TRANSPARENT);
    SetTextColor(g_mem, cref(col));
    r.left = S(x);
    r.top = S(y);
    r.right = S(x + w);
    r.bottom = S(y + h);
    DrawTextW(g_mem, s, -1, &r, fmt | DT_NOPREFIX);
    SelectObject(g_mem, old);
}

static int text_w(int font, const wchar_t *s) {
    SIZE sz;
    HGDIOBJ old = SelectObject(g_mem, g_fonts[font]);
    GetTextExtentPoint32W(g_mem, s, (int)wcslen(s), &sz);
    SelectObject(g_mem, old);
    return MulDiv(sz.cx, 96, g_dpi);
}

#define T_LINE (DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS)

/* ---------------------------------------------------------------- widgets (inmediato) ---- */

static void add_hit(int x, int y, int w, int h, int id) {
    if (g_nhits < (int)(sizeof g_hits / sizeof g_hits[0])) {
        hit_t *t = &g_hits[g_nhits++];
        t->x = x;
        t->y = y;
        t->w = w;
        t->h = h;
        t->id = id;
    }
}

static int hot(int id) { return g_hover == id; }
static int pressed(int id) { return g_down == id && g_hover == id; }

static int hit_at(int lx, int ly) {
    int i;
    for (i = g_nhits - 1; i >= 0; i--) {
        const hit_t *t = &g_hits[i];
        if (lx >= t->x && lx < t->x + t->w && ly >= t->y && ly < t->y + t->h)
            return t->id;
    }
    return ID_NONE;
}

/* Botón. style: 0 normal, 1 principal (rojo), 2 discreto. Deshabilitado: sin zona de clic. */
static void ui_button(int id, int x, int y, int w, int h, const wchar_t *label, int style,
                      int enabled) {
    rgb_t fill, edge, fg;
    int over = enabled && hot(id);
    if (style == 1) {
        fill = !enabled ? 0x3a1616 : pressed(id) ? 0x621010 : over ? C_PLAY_HOT : C_PLAY;
        edge = !enabled ? 0x4a2020 : 0xb64040;
        fg = enabled ? 0xfff3e8 : 0x8c6a66;
        fill_rrect_grad(x, y, w, h, 8, lerp_rgb(fill, 0xffffff, enabled ? 24 : 0), fill);
        stroke_rrect(x, y, w, h, 8, edge);
        SetTextCharacterExtra(g_mem, S(3));
        txt(F_PLAY, fg, x, y, w, h, DT_SINGLELINE | DT_VCENTER | DT_CENTER, label);
        SetTextCharacterExtra(g_mem, 0);
    } else {
        fill = !enabled ? C_CARD : over ? C_CARD_HOT : C_CARD;
        edge = !enabled ? 0x26262e : over ? 0x4a4a58 : C_LINE;
        fg = enabled ? C_TEXT : 0x585862;
        fill_rrect(x, y, w, h, 8, fill);
        stroke_rrect(x, y, w, h, 8, edge);
        txt(F_BOLD, fg, x, y, w, h, DT_SINGLELINE | DT_VCENTER | DT_CENTER, label);
    }
    if (enabled)
        add_hit(x, y, w, h, id);
}

static void row_text(int x, int y, int w, const wchar_t *label, const wchar_t *desc) {
    txt(F_BODY, C_TEXT, x, y, w, 22, T_LINE, label);
    txt(F_SMALL, C_DIM, x, y + 24, w, 36, DT_WORDBREAK, desc);
}

static void row_frame(int id, int x, int y, int w) {
    if (hot(id))
        fill_rrect(x - 14, y - 6, w + 28, ROW_H - 4, 10, 0x17171c);
    fill_rect(x, y + ROW_H - 8, w, 1, 0x1f1f27);
}

/* Casilla con título y descripción. */
static void ui_check(int id, int x, int y, int w, const wchar_t *label, const wchar_t *desc,
                     int on) {
    row_frame(id, x, y, w);
    if (on) {
        fill_rrect(x, y + 3, 22, 22, 6, C_RED);
        capsule((float)x + 5.5f, (float)y + 14.5f, (float)x + 9.5f, (float)y + 18.5f, 2.4f, 0xffffff);
        capsule((float)x + 9.5f, (float)y + 18.5f, (float)x + 17, (float)y + 9.5f, 2.4f, 0xffffff);
    } else {
        fill_rrect(x, y + 3, 22, 22, 6, C_FIELD);
        stroke_rrect(x, y + 3, 22, 22, 6, hot(id) ? 0x5a5a68 : 0x3a3a46);
    }
    row_text(x + 38, y, w - 38, label, desc);
    add_hit(x - 14, y - 6, w + 28, ROW_H - 4, id);
}

static int combo_count(int id) {
    return id == ID_CB_RENDERER ? (int)(sizeof k_renderer_items / sizeof k_renderer_items[0])
                                : (int)(sizeof k_logcap_items / sizeof k_logcap_items[0]);
}
static const wchar_t *combo_text(int id, int i) {
    return id == ID_CB_RENDERER ? k_renderer_items[i] : k_logcap_items[i];
}
static int combo_sel(int id) {
    int i;
    if (id == ID_CB_RENDERER)
        return g_set.renderer;
    for (i = 0; i < combo_count(id); i++)
        if (k_logcap_vals[i] == g_set.log_cap)
            return i;
    return 1;
}

/* Fila con título, descripción y un desplegable a la derecha. */
static void ui_combo(int id, int x, int y, int w, const wchar_t *label, const wchar_t *desc) {
    int bw = 300, bx = x + w - bw, by = y + 2, open = g_open_combo == id, over = hot(id);
    row_frame(id, x, y, w);
    row_text(x, y, w - bw - 24, label, desc);
    fill_rrect(bx, by, bw, 40, 8, open || over ? C_CARD_HOT : C_CARD);
    stroke_rrect(bx, by, bw, 40, 8, open ? C_RED : over ? 0x4a4a58 : C_LINE);
    txt(F_BODY, C_TEXT, bx + 14, by, bw - 48, 40, T_LINE, combo_text(id, combo_sel(id)));
    capsule((float)bx + bw - 28, (float)by + 17, (float)bx + bw - 22, (float)by + 23, 2.0f, C_DIM);
    capsule((float)bx + bw - 22, (float)by + 23, (float)bx + bw - 16, (float)by + 17, 2.0f, C_DIM);
    add_hit(bx, by, bw, 40, id);
    if (open) {
        g_ovl_x = bx;
        g_ovl_y = by + 44;
        g_ovl_w = bw;
    }
}

/* Lista del desplegable abierto: la última en pintarse y en registrar zonas. */
static void paint_overlay(void) {
    int n, i, h = 36;
    if (g_open_combo == ID_NONE)
        return;
    n = combo_count(g_open_combo);
    fill_rrect(g_ovl_x + 2, g_ovl_y + 4, g_ovl_w, n * h + 8, 10, 0x050507); /* sombra */
    fill_rrect(g_ovl_x, g_ovl_y, g_ovl_w, n * h + 8, 10, 0x1d1d25);
    stroke_rrect(g_ovl_x, g_ovl_y, g_ovl_w, n * h + 8, 10, 0x3a3a46);
    for (i = 0; i < n; i++) {
        int iy = g_ovl_y + 4 + i * h, id = ID_ITEM0 + i;
        if (hot(id))
            fill_rrect(g_ovl_x + 4, iy, g_ovl_w - 8, h, 7, 0x2c2c38);
        txt(F_BODY, i == combo_sel(g_open_combo) ? C_GOLD : C_TEXT, g_ovl_x + 16, iy, g_ovl_w - 32,
            h, T_LINE, combo_text(g_open_combo, i));
        add_hit(g_ovl_x + 4, iy, g_ovl_w - 8, h, id);
    }
}

static void page_header(const wchar_t *title, const wchar_t *sub) {
    txt(F_H1, C_TEXT, PANEL_X, 26, PANEL_W, 50, DT_SINGLELINE | DT_VCENTER, title);
    txt(F_SMALL, C_DIM, PANEL_X, 80, PANEL_W, 20, DT_SINGLELINE, sub);
}

static void section_title(int y, const wchar_t *s) {
    txt(F_H2, C_GOLD, PANEL_X, y, PANEL_W, 32, DT_SINGLELINE | DT_VCENTER, s);
    fill_rect(PANEL_X, y + 36, PANEL_W, 1, C_LINE);
}

static void caption(int x, int y, int w, const wchar_t *s) {
    SetTextCharacterExtra(g_mem, S(2));
    txt(F_TINY, C_DIM, x, y, w, 16, DT_SINGLELINE, s);
    SetTextCharacterExtra(g_mem, 0);
}

static void info_card(int x, int y, int w, int h, const wchar_t *s) {
    fill_rrect(x, y, w, h, 8, C_CARD);
    fill_rrect(x, y + 8, 3, h - 16, 1, C_GOLD);
    txt(F_SMALL, C_DIM, x + 18, y + 8, w - 34, h - 16, DT_WORDBREAK | DT_VCENTER, s);
}

static void status_dot(int x, int y, rgb_t c) {
    fill_circle(x, y, 8, lerp_rgb(c, 0x000000, 200));
    fill_circle(x, y, 4, c);
}

/* ------------------------------------------------------------------------- juego ---- */

static int loader_ok(void) { return file_exists(g_loader); }
static int xbe_ok(void) { return g_set.xbe[0] && file_exists(g_set.xbe); }
static int running(void) { return g_proc != NULL; }
static int can_play(void) { return !running() && loader_ok() && xbe_ok(); }

static void exit_text(int code, wchar_t *out, size_t n) {
    switch (code) {
    case SESSION_NONE: wfmt(out, n, L"—"); break;
    case SESSION_STOPPED: wfmt(out, n, L"Detenido desde el launcher"); break;
    case 0: wfmt(out, n, L"Salida normal"); break;
    case 1: wfmt(out, n, L"Argumentos o XBE inválidos (código 1)"); break;
    case 2: wfmt(out, n, L"Llamada del kernel sin implementar (código 2)"); break;
    case 3: wfmt(out, n, L"El juego se cerró por una excepción (código 3)"); break;
    case 4: wfmt(out, n, L"Límite de llamadas alcanzado (código 4)"); break;
    case 5: wfmt(out, n, L"Tiempo o inactividad agotados (código 5)"); break;
    case 6: wfmt(out, n, L"El juego pidió apagar (código 6)"); break;
    default: wfmt(out, n, L"Código de salida %d", code); break;
    }
}

/* El .log más reciente de la carpeta de ejecuciones (sin los -pgraph). */
static int latest_log(wchar_t *out, size_t n, ULONGLONG *sig) {
    WIN32_FIND_DATAW fd;
    wchar_t pat[MAX_PATH];
    HANDLE h;
    ULONGLONG best = 0;
    int found = 0;
    wfmt(pat, MAX_PATH, L"%ls\\*.log", g_runs);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        ULONGLONG t = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) |
                      fd.ftLastWriteTime.dwLowDateTime;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || wcsstr(fd.cFileName, L"-pgraph"))
            continue;
        if (!found || t > best) {
            best = t;
            wfmt(out, n, L"%ls\\%ls", g_runs, fd.cFileName);
            *sig = t ^ ((ULONGLONG)fd.nFileSizeLow << 1);
            found = 1;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

#define LOG_TAIL_BYTES (160 * 1024)
#define LOG_MAX_LINES 3000

/* Relee la cola del último log si cambió. 1 si hay contenido nuevo. */
static int log_reload(void) {
    wchar_t path[MAX_PATH];
    ULONGLONG sig = 0;
    HANDLE h;
    LARGE_INTEGER size, pos;
    DWORD want, got = 0;
    char *raw;
    int wn, i, start, nl = 0, first;
    SYSTEMTIME st;
    if (!latest_log(path, MAX_PATH, &sig)) {
        if (g_log_n) {
            free(g_log_w);
            free(g_log_ls);
            g_log_w = NULL;
            g_log_ls = NULL;
            g_log_n = 0;
            g_log_path[0] = 0;
            return 1;
        }
        return 0;
    }
    if (!wcscmp(path, g_log_path) && sig == g_log_sig)
        return 0;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    GetFileSizeEx(h, &size);
    want = size.QuadPart > LOG_TAIL_BYTES ? LOG_TAIL_BYTES : (DWORD)size.QuadPart;
    pos.QuadPart = size.QuadPart - want;
    SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
    raw = (char *)malloc(want + 1);
    if (!raw) {
        CloseHandle(h);
        return 0;
    }
    ReadFile(h, raw, want, &got, NULL);
    CloseHandle(h);
    start = 0;
    if (pos.QuadPart > 0) { /* empezamos a mitad de archivo: fuera la primera línea cortada */
        while ((DWORD)start < got && raw[start] != '\n')
            start++;
        start++;
    }
    if ((DWORD)start > got)
        start = (int)got;
    wn = MultiByteToWideChar(CP_UTF8, 0, raw + start, (int)got - start, NULL, 0);
    free(g_log_w);
    free(g_log_ls);
    g_log_w = (wchar_t *)malloc(((size_t)wn + 2) * sizeof(wchar_t));
    g_log_ls = (int *)malloc(((size_t)wn / 2 + 4) * sizeof(int));
    if (!g_log_w || !g_log_ls) {
        free(raw);
        g_log_n = 0;
        return 0;
    }
    wn = MultiByteToWideChar(CP_UTF8, 0, raw + start, (int)got - start, g_log_w, wn);
    free(raw);
    g_log_w[wn] = 0;
    first = 1;
    for (i = 0; i < wn; i++) {
        if (first) {
            g_log_ls[nl++] = i;
            first = 0;
        }
        if (g_log_w[i] == L'\n') {
            g_log_w[i] = 0;
            first = i + 1 < wn;
        } else if (g_log_w[i] == L'\r' || g_log_w[i] == L'\t') {
            g_log_w[i] = L' ';
        }
    }
    if (nl > LOG_MAX_LINES) { /* solo las últimas */
        memmove(g_log_ls, g_log_ls + (nl - LOG_MAX_LINES), LOG_MAX_LINES * sizeof(int));
        nl = LOG_MAX_LINES;
    }
    g_log_n = nl;
    wfmt(g_log_path, MAX_PATH, L"%ls", path);
    g_log_sig = sig;
    GetLocalTime(&st);
    wfmt(g_log_stamp, 16, L"%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    g_log_scroll = 1 << 30; /* pegado al final; paint lo recorta */
    return 1;
}

static void record_session_end(DWORD code) {
    wchar_t latest[MAX_PATH];
    ULONGLONG sig;
    g_last.duration_s = (long)((GetTickCount64() - g_t0) / 1000);
    g_last.code = g_stopped_by_user ? SESSION_STOPPED : (int)code;
    if (latest_log(latest, MAX_PATH, &sig))
        wfmt(g_last.log, MAX_PATH, L"%ls", latest);
    ini_put(L"session", L"start", g_last.start);
    ini_put_int(L"session", L"duration_s", (int)g_last.duration_s);
    ini_put_int(L"session", L"code", g_last.code);
    ini_put(L"session", L"log", g_last.log);
}

static void poll_game(void) {
    DWORD code = 0;
    if (g_proc && WaitForSingleObject(g_proc, 0) == WAIT_OBJECT_0) {
        GetExitCodeProcess(g_proc, &code);
        CloseHandle(g_proc);
        g_proc = NULL;
        record_session_end(code);
        g_log_sig = 0; /* releer el log definitivo */
        log_reload();
    }
}

static void start_game(void) {
    wchar_t cmd[4096];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    SYSTEMTIME st;
    if (!can_play())
        return;
    build_cmdline(cmd, 4096);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    /* El loader es de consola: sin CREATE_NO_WINDOW saldría una ventana negra detrás. */
    if (!CreateProcessW(g_loader, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, g_exe_dir, &si,
                        &pi)) {
        MessageBoxW(g_hwnd, L"No se pudo lanzar ngb-loader.exe.", L"Ninja Gaiden Black",
                    MB_OK | MB_ICONERROR);
        return;
    }
    AllowSetForegroundWindow(pi.dwProcessId); /* que la ventana del juego pueda tomar el foco */
    CloseHandle(pi.hThread);
    g_proc = pi.hProcess;
    g_pid = pi.dwProcessId;
    g_t0 = GetTickCount64();
    g_stopped_by_user = 0;
    GetLocalTime(&st);
    wfmt(g_last.start, 48, L"%02u/%02u/%04u %02u:%02u", st.wDay, st.wMonth, st.wYear, st.wHour,
         st.wMinute);
    g_open_combo = ID_NONE;
}

static void stop_game(void) {
    if (!g_proc)
        return;
    g_stopped_by_user = 1;
    TerminateProcess(g_proc, 1);
    WaitForSingleObject(g_proc, 3000);
    poll_game();
}

static void browse_xbe(void) {
    wchar_t file[MAX_PATH], dir[MAX_PATH];
    OPENFILENAMEW ofn;
    wfmt(file, MAX_PATH, L"%ls", g_set.xbe);
    wfmt(dir, MAX_PATH, L"%ls", g_set.xbe);
    {
        wchar_t *cut = wcsrchr(dir, L'\\');
        if (cut)
            *cut = 0;
        else
            dir[0] = 0;
    }
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"default.xbe\0default.xbe\0Archivos XBE (*.xbe)\0*.xbe\0Todos los archivos\0*.*\0";
    ofn.nFilterIndex = 2;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = dir[0] ? dir : NULL;
    ofn.lpstrTitle = L"Elige el default.xbe de tu copia de Ninja Gaiden Black";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        wfmt(g_set.xbe, MAX_PATH, L"%ls", file);
        save_settings();
    }
}

static void open_runs_folder(void) {
    CreateDirectoryW(g_runs, NULL);
    ShellExecuteW(g_hwnd, L"open", g_runs, NULL, NULL, SW_SHOWNORMAL);
}

/* ---------------------------------------------------------------------------- páginas ---- */

static void page_play(void) {
    int y = 128, cw = (PANEL_W - 20) / 2;
    wchar_t buf[256], t2[96];
    page_header(L"Jugar", L"Elige tu copia de Ninja Gaiden Black y pulsa PLAY.");
    section_title(y, L"Juego");
    y += 52;
    row_text(PANEL_X, y, PANEL_W, L"Archivo del juego (default.xbe)",
             L"El port no incluye nada del juego: apunta a la copia que extrajiste de tu disco.");
    y += 58;
    fill_rrect(PANEL_X, y, PANEL_W - 144, 44, 8, C_FIELD);
    stroke_rrect(PANEL_X, y, PANEL_W - 144, 44, 8, hot(ID_BROWSE) ? 0x4a4a58 : C_LINE);
    if (g_set.xbe[0])
        txt(F_BODY, C_TEXT, PANEL_X + 16, y, PANEL_W - 176, 44,
            DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS, g_set.xbe);
    else
        txt(F_BODY, 0x5e5e68, PANEL_X + 16, y, PANEL_W - 176, 44, T_LINE,
            L"Ninguno seleccionado: pulsa Examinar…");
    add_hit(PANEL_X, y, PANEL_W - 144, 44, ID_BROWSE);
    ui_button(ID_BROWSE, PANEL_X + PANEL_W - 128, y, 128, 44, L"Examinar…", 0, 1);
    y += 56;
    if (!g_set.xbe[0]) {
        status_dot(PANEL_X + 8, y + 10, C_WARN);
        txt(F_SMALL, C_WARN, PANEL_X + 26, y, 600, 20, T_LINE, L"Falta elegir el archivo del juego");
    } else if (!xbe_ok()) {
        status_dot(PANEL_X + 8, y + 10, C_BAD);
        txt(F_SMALL, C_BAD, PANEL_X + 26, y, 600, 20, T_LINE, L"No se encuentra ese archivo");
    } else {
        WIN32_FILE_ATTRIBUTE_DATA fa;
        double mb = 0;
        if (GetFileAttributesExW(g_set.xbe, GetFileExInfoStandard, &fa))
            mb = (double)(((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow) / 1048576.0;
        status_dot(PANEL_X + 8, y + 10, C_OK);
        wfmt(buf, 256, L"Archivo encontrado (%.2f MB)", mb);
        txt(F_SMALL, C_OK, PANEL_X + 26, y, 600, 20, T_LINE, buf);
    }
    y += 40;

    /* Tarjeta de estado y tarjeta de la última sesión. */
    fill_rrect(PANEL_X, y, cw, 196, 10, C_CARD);
    caption(PANEL_X + 22, y + 18, cw - 44, L"ESTADO");
    if (running()) {
        wchar_t d[48];
        fmt_duration((long)((GetTickCount64() - g_t0) / 1000), d, 48);
        status_dot(PANEL_X + 30, y + 54, C_OK);
        wfmt(buf, 256, L"En ejecución · %ls", d);
        txt(F_H2, C_TEXT, PANEL_X + 50, y + 38, cw - 70, 34, T_LINE, buf);
    } else if (can_play()) {
        status_dot(PANEL_X + 30, y + 54, C_GOLD);
        txt(F_H2, C_TEXT, PANEL_X + 50, y + 38, cw - 70, 34, T_LINE, L"Listo para jugar");
    } else {
        status_dot(PANEL_X + 30, y + 54, C_WARN);
        txt(F_H2, C_TEXT, PANEL_X + 50, y + 38, cw - 70, 34, T_LINE, L"Falta algo para jugar");
    }
    status_dot(PANEL_X + 30, y + 102, loader_ok() ? C_OK : C_BAD);
    txt(F_SMALL, C_TEXT, PANEL_X + 50, y + 92, cw - 70, 20, T_LINE,
        loader_ok() ? L"ngb-loader.exe junto al launcher" : L"No se encuentra ngb-loader.exe");
    status_dot(PANEL_X + 30, y + 130, xbe_ok() ? C_OK : C_BAD);
    txt(F_SMALL, C_TEXT, PANEL_X + 50, y + 120, cw - 70, 20, T_LINE,
        xbe_ok() ? L"default.xbe del juego" : L"default.xbe sin elegir o inexistente");
    status_dot(PANEL_X + 30, y + 158, dir_exists(g_runs) ? C_OK : C_DIM);
    txt(F_SMALL, C_TEXT, PANEL_X + 50, y + 148, cw - 70, 20, T_LINE | DT_PATH_ELLIPSIS,
        dir_exists(g_runs) ? L"Carpeta de registros (runs) lista" : L"La carpeta runs se creará al jugar");

    {
        int x2 = PANEL_X + cw + 20;
        fill_rrect(x2, y, cw, 196, 10, C_CARD);
        caption(x2 + 22, y + 18, cw - 44, L"ÚLTIMA SESIÓN");
        if (g_last.code == SESSION_NONE) {
            txt(F_BODY, C_DIM, x2 + 22, y + 48, cw - 44, 40, DT_WORDBREAK, L"Todavía no hay sesiones. Pulsa PLAY para empezar.");
        } else {
            exit_text(g_last.code, t2, 96);
            txt(F_SMALL, C_DIM, x2 + 22, y + 46, 80, 20, T_LINE, L"Inicio");
            txt(F_BODY, C_TEXT, x2 + 112, y + 44, cw - 134, 22, T_LINE, g_last.start);
            fmt_duration(g_last.duration_s, buf, 256);
            txt(F_SMALL, C_DIM, x2 + 22, y + 76, 80, 20, T_LINE, L"Duración");
            txt(F_BODY, C_TEXT, x2 + 112, y + 74, cw - 134, 22, T_LINE, buf);
            txt(F_SMALL, C_DIM, x2 + 22, y + 106, 80, 20, T_LINE, L"Resultado");
            txt(F_BODY, g_last.code == 0 || g_last.code == SESSION_STOPPED ? C_TEXT : C_WARN,
                x2 + 112, y + 104, cw - 134, 22, T_LINE, t2);
            txt(F_SMALL, C_DIM, x2 + 22, y + 136, 80, 20, T_LINE, L"Registro");
            txt(F_BODY, C_TEXT, x2 + 112, y + 134, cw - 134, 22, T_LINE,
                g_last.log[0] ? base_name(g_last.log) : L"—");
            ui_button(ID_VIEW_LOG, x2 + cw - 22 - 104, y + 10, 104, 30, L"Ver el log", 2, 1);
        }
    }
}

static void page_graphics(void) {
    int y = 128;
    page_header(L"Gráficos", L"Cómo se dibuja el juego y a qué ritmo.");
    section_title(y, L"Ventana y render");
    y += 52;
    ui_combo(ID_CB_RENDERER, PANEL_X, y, PANEL_W, L"Renderizador",
             L"Automático usa OpenGL 4.x si tu GPU lo ofrece. El rasterizador por software es lento y sirve de referencia.");
    y += ROW_H;
    ui_check(ID_CHK_PACING, PANEL_X, y, PANEL_W, L"Limitar a 60 fps",
             L"Cada frame espera al siguiente 1/60 s, como en la Xbox. Desactívalo para ver el máximo que da tu equipo.",
             g_set.pacing);
    y += ROW_H + 18;
    info_card(PANEL_X, y, PANEL_W, 56,
              L"La ventana del juego se abre centrada y se puede redimensionar: la imagen mantiene "
              L"el aspecto 4:3 con bandas negras. F12 guarda una captura en runs\\shots.");
}

static void page_audio(void) {
    int y = 128;
    page_header(L"Audio", L"Salida de sonido del juego.");
    section_title(y, L"Sonido");
    y += 52;
    ui_check(ID_CHK_MUTE, PANEL_X, y, PANEL_W, L"Silenciar",
             L"La APU emulada sigue avanzando; solo se apaga la salida al altavoz.", g_set.mute);
    y += ROW_H;
    ui_check(ID_CHK_WAV, PANEL_X, y, PANEL_W, L"Guardar el audio en un WAV",
             L"Escribe lo que suena en <registro>-audio.wav, junto al log de cada sesión.",
             g_set.audio_wav);
}

/* Fila de la tabla de controles: teclas (separadas por '|') y su efecto. */
static int control_row(int x, int y, int w, const wchar_t *keys, const wchar_t *what) {
    wchar_t one[24];
    const wchar_t *p = keys;
    int cx = x;
    while (*p) {
        size_t n = 0;
        int kw;
        while (p[n] && p[n] != L'|' && n < 22)
            one[n] = p[n], n++;
        one[n] = 0;
        kw = text_w(F_SMALL, one) + 18;
        if (kw < 30)
            kw = 30;
        fill_rrect(cx, y + 2, kw, 24, 6, 0x2a2a34);
        fill_rrect(cx, y + 2, kw, 22, 6, 0x34343f);
        txt(F_SMALL, C_TEXT, cx, y + 1, kw, 24, DT_SINGLELINE | DT_VCENTER | DT_CENTER, one);
        cx += kw + 6;
        p += n;
        if (*p == L'|')
            p++;
    }
    txt(F_BODY, C_DIM, x + w * 46 / 100, y, w - w * 46 / 100, 28, T_LINE, what);
    return y + 28;
}

static void page_controls(void) {
    int cw = (PANEL_W - 20) / 2, y0 = 128, y, x2 = PANEL_X + cw + 20;
    page_header(L"Controles", L"El mapeo de teclado y mando, tal como lo define el port (solo lectura).");
    fill_rrect(PANEL_X, y0, cw, 458, 10, C_CARD);
    fill_rrect(x2, y0, cw, 458, 10, C_CARD);
    caption(PANEL_X + 22, y0 + 18, cw, L"TECLADO");
    caption(x2 + 22, y0 + 18, cw, L"MANDO DE PC (XINPUT)");
    y = y0 + 44;
    y = control_row(PANEL_X + 22, y, cw - 44, L"Enter", L"Start y A");
    y = control_row(PANEL_X + 22, y, cw - 44, L"Esc", L"Back y B");
    y = control_row(PANEL_X + 22, y, cw - 44, L"↑|↓|←|→", L"Cruceta");
    y = control_row(PANEL_X + 22, y, cw - 44, L"W|A|S|D", L"Stick izquierdo");
    y = control_row(PANEL_X + 22, y, cw - 44, L"J", L"A");
    y = control_row(PANEL_X + 22, y, cw - 44, L"K", L"B");
    y = control_row(PANEL_X + 22, y, cw - 44, L"L", L"X");
    y = control_row(PANEL_X + 22, y, cw - 44, L"I", L"Y");
    y = control_row(PANEL_X + 22, y, cw - 44, L"Q", L"Blanco (White)");
    y = control_row(PANEL_X + 22, y, cw - 44, L"E", L"Negro (Black)");
    y = control_row(PANEL_X + 22, y, cw - 44, L"U", L"Gatillo izquierdo");
    y = control_row(PANEL_X + 22, y, cw - 44, L"O", L"Gatillo derecho");
    y = control_row(PANEL_X + 22, y, cw - 44, L"F12", L"Captura de pantalla");
    (void)control_row(PANEL_X + 22, y, cw - 44, L"F11", L"Captura RenderDoc");
    y = y0 + 44;
    y = control_row(x2 + 22, y, cw - 44, L"A|B|X|Y", L"A, B, X, Y");
    y = control_row(x2 + 22, y, cw - 44, L"LB", L"Blanco (White)");
    y = control_row(x2 + 22, y, cw - 44, L"RB", L"Negro (Black)");
    y = control_row(x2 + 22, y, cw - 44, L"LT|RT", L"Gatillos izquierdo y derecho");
    y = control_row(x2 + 22, y, cw - 44, L"Stick izq.", L"Stick izquierdo");
    y = control_row(x2 + 22, y, cw - 44, L"Stick der.", L"Stick derecho");
    y = control_row(x2 + 22, y, cw - 44, L"L3|R3", L"Pulsar los sticks");
    y = control_row(x2 + 22, y, cw - 44, L"Cruceta", L"Cruceta");
    y = control_row(x2 + 22, y, cw - 44, L"Start", L"Start");
    y = control_row(x2 + 22, y, cw - 44, L"Back", L"Back");
    txt(F_SMALL, C_DIM, x2 + 22, y + 14, cw - 44, 70, DT_WORDBREAK,
        L"El mando de PC y el teclado se suman: se puede jugar con los dos a la vez.");
}

static void page_advanced(void) {
    int y = 128;
    wchar_t cmd[4096];
    page_header(L"Avanzado", L"Depuración y registro. Lo normal es dejarlo como está.");
    section_title(y, L"Depuración");
    y += 52;
    ui_check(ID_CHK_RDOC, PANEL_X, y, PANEL_W, L"Captura con RenderDoc (F11)",
             L"Carga RenderDoc al arrancar y F11 captura el frame siguiente. Necesita RenderDoc instalado y el render OpenGL.",
             g_set.rdoc_key);
    y += ROW_H;
    ui_check(ID_CHK_LENIENT, PANEL_X, y, PANEL_W, L"Tolerar llamadas sin implementar",
             L"Una llamada del kernel sin implementar devuelve STATUS_SUCCESS y el juego sigue (puede ocultar fallos).",
             g_set.lenient);
    y += ROW_H + 14;
    section_title(y, L"Registro");
    y += 52;
    ui_combo(ID_CB_LOGCAP, PANEL_X, y, PANEL_W, L"Líneas de log por función del kernel",
             L"Pasado ese límite solo se cuentan las llamadas (--log-cap). Menos líneas, logs más cortos.");
    y += ROW_H + 10;
    build_cmdline(cmd, 4096);
    caption(PANEL_X, y, 300, L"LÍNEA DE COMANDOS DE PLAY");
    fill_rrect(PANEL_X, y + 22, PANEL_W, 76, 8, C_FIELD);
    stroke_rrect(PANEL_X, y + 22, PANEL_W, 76, 8, C_LINE);
    txt(F_MONO, 0xb9b5aa, PANEL_X + 14, y + 30, PANEL_W - 28, 62, DT_WORDBREAK | DT_END_ELLIPSIS, cmd);
}

static rgb_t log_line_color(const wchar_t *line) {
    wchar_t low[160];
    size_t i;
    for (i = 0; i < 159 && line[i]; i++)
        low[i] = (wchar_t)towlower(line[i]);
    low[i] = 0;
    if (wcsstr(low, L"crash") || wcsstr(low, L"excepci") || wcsstr(low, L"fallo") ||
        wcsstr(low, L"unimplemented") || wcsstr(low, L"sin implementar:"))
        return C_BAD;
    if (wcsstr(low, L"----") || wcsstr(low, L"====") || wcsstr(low, L"no se pudo"))
        return C_WARN;
    return 0xbdb9ae;
}

static void page_log(void) {
    int bx = PANEL_X, by = 118, bw = PANEL_W, bh = PANEL_BOTTOM - 118, lh = 17, vis, maxs, i;
    RECT clip;
    wchar_t buf[300];
    page_header(L"Log", L"La cola del último registro de ngb-loader (runs).");
    ui_button(ID_OPEN_RUNS, PANEL_X + PANEL_W - 170, 34, 170, 38, L"Abrir carpeta", 0, 1);
    fill_rrect(bx, by, bw, bh, 10, 0x09090c);
    stroke_rrect(bx, by, bw, bh, 10, C_LINE);
    if (!g_log_n) {
        txt(F_BODY, C_DIM, bx + 24, by + 24, bw - 48, 60, DT_WORDBREAK,
            L"Todavía no hay registros. Cada sesión escribe un h6-<fecha>.log en la carpeta runs.");
        return;
    }
    wfmt(buf, 300, L"%ls  ·  actualizado %ls", base_name(g_log_path), g_log_stamp);
    txt(F_SMALL, C_DIM, bx + 16, by + 8, bw - 32, 18, T_LINE, buf);
    vis = (bh - 44) / lh;
    maxs = g_log_n > vis ? g_log_n - vis : 0;
    if (g_log_scroll > maxs)
        g_log_scroll = maxs;
    if (g_log_scroll < 0)
        g_log_scroll = 0;
    clip.left = S(bx + 12);
    clip.top = S(by + 32);
    clip.right = S(bx + bw - 16);
    clip.bottom = S(by + bh - 8);
    gdi_sync();
    IntersectClipRect(g_mem, clip.left, clip.top, clip.right, clip.bottom);
    for (i = 0; i < vis && g_log_scroll + i < g_log_n; i++) {
        const wchar_t *line = g_log_w + g_log_ls[g_log_scroll + i];
        txt(F_MONO, log_line_color(line), bx + 16, by + 34 + i * lh, bw * 3, lh, DT_SINGLELINE | DT_VCENTER, line);
    }
    SelectClipRgn(g_mem, NULL);
    if (maxs > 0) { /* barra de desplazamiento */
        int th = bh - 52, kh = th * vis / g_log_n;
        if (kh < 24)
            kh = 24;
        fill_rrect(bx + bw - 12, by + 36, 4, th, 2, 0x16161c);
        fill_rrect(bx + bw - 12, by + 36 + (th - kh) * g_log_scroll / maxs, 4, kh, 2, 0x4a4a58);
    }
}

/* ------------------------------------------------------------------ pintura general ---- */

static void paint_sidebar(void) {
    int i;
    fill_rect(0, 0, SIDEBAR_W, LOGICAL_H, C_SIDE);
    fill_rect(SIDEBAR_W - 1, 0, 1, LOGICAL_H, C_LINE);
    draw_emblem(24, 26, 54);
    txt(F_BRAND, C_GOLD, 90, 24, 168, 28, DT_SINGLELINE | DT_VCENTER, L"NINJA GAIDEN");
    txt(F_BRAND, 0xc4372c, 90, 50, 168, 28, DT_SINGLELINE | DT_VCENTER, L"BLACK");
    txt(F_SMALL, C_DIM, 26, 92, 230, 18, DT_SINGLELINE, L"native port · Windows · v" NGB_VERSION_STR);
    fill_rect(24, 126, SIDEBAR_W - 48, 1, C_LINE);
    caption(26, 144, 200, L"SECCIONES");
    for (i = 0; i < PG_COUNT; i++) {
        int y = 168 + i * 46, id = ID_NAV0 + i, sel = g_page == i;
        if (sel) {
            fill_rrect(14, y, SIDEBAR_W - 28, 40, 9, 0x1b1417);
            fill_rrect(14, y + 9, 3, 22, 1, C_RED);
        } else if (hot(id)) {
            fill_rrect(14, y, SIDEBAR_W - 28, 40, 9, 0x141419);
        }
        txt(F_BOLD, sel ? C_TEXT : hot(id) ? 0xc8c4bc : C_DIM, 36, y, SIDEBAR_W - 60, 40, DT_SINGLELINE | DT_VCENTER, k_nav[i]);
        add_hit(14, y, SIDEBAR_W - 28, 40, id);
    }
    txt(F_TINY, 0x55555f, 26, LOGICAL_H - 54, 220, 36, DT_WORDBREAK,
        L"Proyecto pedagógico. No incluye nada del juego ni del XDK.");
}

static void paint_footer(void) {
    int x = SIDEBAR_W, y = LOGICAL_H - FOOT_H, ok = can_play();
    wchar_t buf[300], sum[300], d[48];
    rgb_t dot;
    fill_rect(x, y, LOGICAL_W - x, FOOT_H, C_SIDE);
    fill_rect(x, y, LOGICAL_W - x, 1, C_LINE);
    if (running()) {
        fmt_duration((long)((GetTickCount64() - g_t0) / 1000), d, 48);
        wfmt(buf, 300, L"En ejecución · PID %lu · %ls", (unsigned long)g_pid, d);
        dot = C_OK;
    } else if (!loader_ok()) {
        wfmt(buf, 300, L"No se encuentra ngb-loader.exe junto al launcher");
        dot = C_BAD;
    } else if (!xbe_ok()) {
        wfmt(buf, 300, L"Falta default.xbe: elígelo en Jugar");
        dot = C_WARN;
    } else {
        wfmt(buf, 300, L"Listo para jugar");
        dot = C_GOLD;
    }
    config_summary(sum, 300);
    status_dot(PANEL_X + 8, y + 33, dot);
    txt(F_BOLD, C_TEXT, PANEL_X + 28, y + 20, 520, 26, T_LINE, buf);
    txt(F_SMALL, C_DIM, PANEL_X + 28, y + 48, 520, 20, T_LINE, sum);
    ui_button(ID_STOP, LOGICAL_W - 48 - 224 - 14 - 110, y + 22, 110, 44, L"Stop", 0, running());
    ui_button(ID_PLAY, LOGICAL_W - 48 - 224, y + 15, 224, 58, L"PLAY", 1, ok);
}

static void paint_all(void) {
    g_nhits = 0;
    gdi_sync();
    fill_rect(0, 0, LOGICAL_W, LOGICAL_H, C_MAIN);
    paint_sidebar();
    switch (g_page) {
    case PG_PLAY: page_play(); break;
    case PG_GRAPHICS: page_graphics(); break;
    case PG_AUDIO: page_audio(); break;
    case PG_CONTROLS: page_controls(); break;
    case PG_ADVANCED: page_advanced(); break;
    default: page_log(); break;
    }
    paint_footer();
    paint_overlay();
    gdi_sync();
}

/* ------------------------------------------------------------------------ acciones ---- */

static void goto_page(int page) {
    g_page = page;
    g_open_combo = ID_NONE;
    if (page == PG_LOG) {
        g_log_sig = 0;
        log_reload();
    }
}

static void activate(int id) {
    if (id >= ID_NAV0 && id < ID_NAV0 + PG_COUNT) {
        goto_page(id - ID_NAV0);
        return;
    }
    if (id >= ID_ITEM0 && g_open_combo != ID_NONE) {
        int i = id - ID_ITEM0;
        if (g_open_combo == ID_CB_RENDERER)
            g_set.renderer = i;
        else
            g_set.log_cap = k_logcap_vals[i];
        g_open_combo = ID_NONE;
        save_settings();
        return;
    }
    g_open_combo = g_open_combo == id ? ID_NONE : (id == ID_CB_RENDERER || id == ID_CB_LOGCAP ? id : ID_NONE);
    switch (id) {
    case ID_PLAY: start_game(); break;
    case ID_STOP: stop_game(); break;
    case ID_BROWSE: browse_xbe(); break;
    case ID_VIEW_LOG: goto_page(PG_LOG); break;
    case ID_OPEN_RUNS: open_runs_folder(); break;
    case ID_CHK_PACING: g_set.pacing = !g_set.pacing; save_settings(); break;
    case ID_CHK_MUTE: g_set.mute = !g_set.mute; save_settings(); break;
    case ID_CHK_WAV: g_set.audio_wav = !g_set.audio_wav; save_settings(); break;
    case ID_CHK_RDOC: g_set.rdoc_key = !g_set.rdoc_key; save_settings(); break;
    case ID_CHK_LENIENT: g_set.lenient = !g_set.lenient; save_settings(); break;
    default: break;
    }
}

static void redraw(void) {
    if (g_hwnd)
        InvalidateRect(g_hwnd, NULL, FALSE);
}

static void set_hover(int id) {
    if (id != g_hover) {
        g_hover = id;
        redraw();
    }
}

static int to_logical(int v) { return MulDiv(v, 96, g_dpi); }

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint_all();
        BitBlt(dc, 0, 0, g_pw, g_ph, g_mem, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        if (!g_mouse_tracked) {
            TRACKMOUSEEVENT t;
            memset(&t, 0, sizeof t);
            t.cbSize = sizeof t;
            t.dwFlags = TME_LEAVE;
            t.hwndTrack = hwnd;
            TrackMouseEvent(&t);
            g_mouse_tracked = 1;
        }
        set_hover(hit_at(to_logical((short)LOWORD(lp)), to_logical((short)HIWORD(lp))));
        return 0;
    }
    case WM_MOUSELEAVE:
        g_mouse_tracked = 0;
        set_hover(ID_NONE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(NULL, g_hover != ID_NONE ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        int id = hit_at(to_logical((short)LOWORD(lp)), to_logical((short)HIWORD(lp)));
        if (g_open_combo != ID_NONE && id != g_open_combo && id < ID_ITEM0) {
            g_open_combo = ID_NONE; /* clic fuera: cierra el desplegable y no hace nada más */
            g_down = ID_NONE;
            redraw();
            return 0;
        }
        g_down = id;
        SetCapture(hwnd);
        redraw();
        return 0;
    }
    case WM_LBUTTONUP: {
        int id = hit_at(to_logical((short)LOWORD(lp)), to_logical((short)HIWORD(lp)));
        int was = g_down;
        g_down = ID_NONE;
        if (GetCapture() == hwnd)
            ReleaseCapture();
        if (was != ID_NONE && was == id)
            activate(id);
        redraw();
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (g_page == PG_LOG && g_log_n) {
            g_log_scroll -= (GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA) * 4;
            if (g_log_scroll < 0)
                g_log_scroll = 0;
            redraw();
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && g_open_combo != ID_NONE) {
            g_open_combo = ID_NONE;
            redraw();
        } else if (g_page == PG_LOG) {
            if (wp == VK_HOME)
                g_log_scroll = 0;
            else if (wp == VK_END)
                g_log_scroll = 1 << 30;
            else if (wp == VK_PRIOR)
                g_log_scroll -= 20;
            else if (wp == VK_NEXT)
                g_log_scroll += 20;
            else if (wp == VK_UP)
                g_log_scroll -= 1;
            else if (wp == VK_DOWN)
                g_log_scroll += 1;
            else
                break;
            if (g_log_scroll < 0)
                g_log_scroll = 0;
            redraw();
        }
        return 0;
    case WM_TIMER:
        if (wp == TIMER_TICK) {
            int was_running = running();
            poll_game();
            if (g_page == PG_LOG || running())
                log_reload();
            if (running() || was_running || g_page == PG_LOG)
                redraw();
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_TICK);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------------------------------------------------------------------------- arranque ---- */

static void make_backbuffer(int w, int h) {
    BITMAPINFO bi;
    HDC screen = GetDC(NULL);
    void *bits = NULL;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; /* de arriba abajo */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g_mem = CreateCompatibleDC(screen);
    g_bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, screen);
    g_bmp_old = (HBITMAP)SelectObject(g_mem, g_bmp);
    g_px = (uint32_t *)bits;
    g_pw = w;
    g_ph = h;
}

/* Barra de título oscura (Windows 10 20H1+ / 11); si dwmapi no la conoce, se queda como está. */
static void dark_title_bar(HWND hwnd) {
    typedef HRESULT(WINAPI * dwm_set_fn)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    dwm_set_fn set;
    BOOL on = TRUE;
    COLORREF cap = cref(C_SIDE), txtc = cref(C_TEXT);
    if (!dwm)
        return;
    set = (dwm_set_fn)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (set) {
        set(hwnd, 20, &on, sizeof on);   /* DWMWA_USE_IMMERSIVE_DARK_MODE */
        set(hwnd, 35, &cap, sizeof cap); /* DWMWA_CAPTION_COLOR (Windows 11) */
        set(hwnd, 34, &cap, sizeof cap); /* DWMWA_BORDER_COLOR */
        set(hwnd, 36, &txtc, sizeof txtc); /* DWMWA_TEXT_COLOR */
    }
    FreeLibrary(dwm);
}

static void init_paths(void) {
    wchar_t *cut, data[MAX_PATH];
    GetModuleFileNameW(NULL, g_exe_dir, MAX_PATH);
    cut = wcsrchr(g_exe_dir, L'\\');
    if (cut)
        cut[1] = 0;
    wfmt(g_ini, MAX_PATH, L"%lsngb.ini", g_exe_dir);
    wfmt(g_loader, MAX_PATH, L"%lsngb-loader.exe", g_exe_dir);
    data_dir(data, MAX_PATH);
    g_runs[0] = 0;
    if (data[0])
        wfmt(g_runs, MAX_PATH, L"%ls\\runs", data);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
    WNDCLASSEXW wc;
    RECT r, work;
    POINT cur;
    MSG msg;
    HDC dc;
    int x, y, ow, oh, argc = 0, i;
    LPWSTR *argv;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    (void)prev;
    (void)cmdline;
    (void)show;
    g_inst = inst;
    dc = GetDC(NULL);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    if (g_dpi < 96)
        g_dpi = 96;
    init_paths();
    ini_ensure();
    load_settings();
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (i = 1; argv && i + 1 < argc; i++)
        if (!wcscmp(argv[i], L"--page")) {
            int p = _wtoi(argv[i + 1]);
            if (p >= 0 && p < PG_COUNT)
                g_page = p;
        }
    if (argv)
        LocalFree(argv);
    make_fonts();
    make_backbuffer(S(LOGICAL_W), S(LOGICAL_H));
    if (g_page == PG_LOG)
        log_reload();

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                 GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                   LR_DEFAULTCOLOR);
    wc.lpszClassName = L"ngb-launcher";
    RegisterClassExW(&wc);

    /* Tamaño fijo, centrado en el monitor del cursor. */
    SetRect(&r, 0, 0, g_pw, g_ph);
    AdjustWindowRect(&r, style, FALSE);
    ow = r.right - r.left;
    oh = r.bottom - r.top;
    memset(&work, 0, sizeof work);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    if (GetCursorPos(&cur)) {
        MONITORINFO mi;
        HMONITOR mon = MonitorFromPoint(cur, MONITOR_DEFAULTTOPRIMARY);
        memset(&mi, 0, sizeof mi);
        mi.cbSize = sizeof mi;
        if (mon && GetMonitorInfoW(mon, &mi))
            work = mi.rcWork;
    }
    x = work.left + (work.right - work.left - ow) / 2;
    y = work.top + (work.bottom - work.top - oh) / 2;
    if (x < work.left)
        x = work.left;
    if (y < work.top)
        y = work.top;
    g_hwnd = CreateWindowExW(0, L"ngb-launcher", L"Ninja Gaiden Black · native port", style, x, y, ow,
                             oh, NULL, NULL, inst, NULL);
    if (!g_hwnd)
        return 1;
    dark_title_bar(g_hwnd);
    SetTimer(g_hwnd, TIMER_TICK, TICK_MS, NULL);
    ShowWindow(g_hwnd, SW_SHOWNORMAL);
    UpdateWindow(g_hwnd);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_proc)
        CloseHandle(g_proc); /* el juego sigue vivo si se cierra el launcher */
    return (int)msg.wParam;
}
