/*
 * ui.c: the launcher's immediate-mode toolkit. See ui.h.
 *
 * Shapes are drawn pixel by pixel into the back buffer with signed-distance edges, so they
 * are antialiased without GDI+. Text goes through GDI (ClearType for body text, grayscale
 * antialiasing for the large display type).
 */
#define UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "kamon.h"

int g_dpi = 96;
int g_hover = 0, g_down = 0;

static HDC g_mem;
static HBITMAP g_bmp, g_bmp_old;
static uint32_t *g_px;
static int g_pw, g_ph;
static HFONT g_fonts[F_COUNT];
static uint32_t *g_bg[2];
static int g_bg_game_art;
static double g_motion_t;
static float g_motion_x, g_motion_y;

typedef struct hit {
    int x, y, w, h, id;
} hit_t;
static hit_t g_hits[200];
static int g_nhits;

/* ------------------------------------------------------------------------ basics ---- */

int S(int v) { return MulDiv(v, g_dpi, 96); }
int to_logical(int v) { return MulDiv(v, 96, g_dpi); }

static COLORREF cref(rgb_t c) { return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255); }

rgb_t lerp_rgb(rgb_t a, rgb_t b, int t256) {
    int r = (int)(((a >> 16) & 255) * (256 - t256) + ((b >> 16) & 255) * t256) >> 8;
    int g = (int)(((a >> 8) & 255) * (256 - t256) + ((b >> 8) & 255) * t256) >> 8;
    int bl = (int)((a & 255) * (256 - t256) + (b & 255) * t256) >> 8;
    return ((rgb_t)r << 16) | ((rgb_t)g << 8) | (rgb_t)bl;
}

static void gdi_sync(void) { GdiFlush(); }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void blend_px(int x, int y, rgb_t c, int a) {
    uint32_t *p, d;
    int ia, r, g, b;
    if ((unsigned)x >= (unsigned)g_pw || (unsigned)y >= (unsigned)g_ph || a <= 0)
        return;
    p = &g_px[(size_t)y * g_pw + x];
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

static void add_px(int x, int y, int r, int g, int b) {
    uint32_t *p, d;
    if ((unsigned)x >= (unsigned)g_pw || (unsigned)y >= (unsigned)g_ph)
        return;
    p = &g_px[(size_t)y * g_pw + x];
    d = *p;
    r += (int)((d >> 16) & 255);
    g += (int)((d >> 8) & 255);
    b += (int)(d & 255);
    *p = ((rgb_t)(r > 255 ? 255 : r) << 16) | ((rgb_t)(g > 255 ? 255 : g) << 8) |
         (rgb_t)(b > 255 ? 255 : b);
}

/* ------------------------------------------------------------------- lifecycle ---- */

int g_no_embedded_fonts;
static int g_kanji_ok;
static int g_fonts_loaded;

/* The embedded fonts are RCDATA 101..104 of the exe (launcher.rc); private to the process. */
static void load_embedded_fonts(void) {
    int id;
    if (g_fonts_loaded || g_no_embedded_fonts)
        return;
    g_fonts_loaded = 1;
    for (id = 101; id <= 104; id++) {
        HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
        HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
        void *p = g ? LockResource(g) : NULL;
        DWORD n = 0;
        if (p)
            AddFontMemResourceEx(p, SizeofResource(NULL, r), NULL, &n);
    }
}

static HFONT font_try(int px, int weight, BYTE quality, const wchar_t *face, wchar_t *got) {
    HFONT f = CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, quality,
                          DEFAULT_PITCH | FF_DONTCARE, face);
    HGDIOBJ old = SelectObject(g_mem, f);
    got[0] = 0;
    GetTextFaceW(g_mem, LF_FACESIZE, got);
    SelectObject(g_mem, old);
    return f;
}

static void make_fonts(void) {
    static const struct {
        const wchar_t *face;
        int px10, weight; /* px10: size in tenths of a logical pixel */
        BYTE quality;
        const wchar_t *fallback;
    } spec[F_COUNT] = {
        {L"Consolas", 120, FW_NORMAL, CLEARTYPE_QUALITY, NULL},
        {L"Josefin Sans", 140, FW_LIGHT, CLEARTYPE_QUALITY, L"Segoe UI Light"},
        {L"Josefin Sans", 125, FW_LIGHT, CLEARTYPE_QUALITY, L"Segoe UI Light"},
        {L"Josefin Sans", 105, FW_SEMIBOLD, CLEARTYPE_QUALITY, L"Segoe UI Semibold"},
        {L"Josefin Sans", 110, FW_SEMIBOLD, CLEARTYPE_QUALITY, L"Segoe UI Semibold"},
        {L"Josefin Sans", 160, FW_LIGHT, CLEARTYPE_QUALITY, L"Segoe UI Light"},
        {L"Josefin Sans", 880, FW_THIN, ANTIALIASED_QUALITY, L"Segoe UI Light"},
        {L"Josefin Sans", 420, FW_THIN, ANTIALIASED_QUALITY, L"Segoe UI Light"},
        {L"Josefin Sans", 540, FW_THIN, ANTIALIASED_QUALITY, L"Segoe UI Light"},
        {L"Josefin Sans", 150, FW_SEMIBOLD, ANTIALIASED_QUALITY, L"Segoe UI Semibold"},
        {L"Josefin Sans", 1280, FW_THIN, ANTIALIASED_QUALITY, L"Segoe UI Light"},
        {L"Josefin Sans", 210, FW_LIGHT, ANTIALIASED_QUALITY, L"Segoe UI Light"},
        {L"Shippori Mincho B1", 160, FW_SEMIBOLD, ANTIALIASED_QUALITY, L"Yu Mincho"},
    };
    int i;
    load_embedded_fonts();
    g_kanji_ok = 1;
    for (i = 0; i < F_COUNT; i++) {
        wchar_t got[LF_FACESIZE];
        int px = MulDiv(spec[i].px10, g_dpi, 960);
        if (g_fonts[i])
            DeleteObject(g_fonts[i]);
        g_fonts[i] = font_try(px, spec[i].weight, spec[i].quality, spec[i].face, got);
        if (_wcsicmp(got, spec[i].face) && spec[i].weight != FW_NORMAL && spec[i].weight != FW_BOLD) {
            /* Static weights outside regular/bold register their legacy family name. */
            wchar_t legacy[LF_FACESIZE];
            _snwprintf(legacy, LF_FACESIZE, L"%ls %ls", spec[i].face,
                       spec[i].weight == FW_SEMIBOLD ? L"SemiBold"
                       : spec[i].weight == FW_THIN   ? L"Thin"
                                                     : L"Light");
            legacy[LF_FACESIZE - 1] = 0;
            DeleteObject(g_fonts[i]);
            g_fonts[i] = font_try(px, FW_NORMAL, spec[i].quality, legacy, got);
            if (!_wcsicmp(got, legacy))
                continue;
            DeleteObject(g_fonts[i]);
            g_fonts[i] = font_try(px, spec[i].weight, spec[i].quality, spec[i].face, got);
        }
        if (_wcsicmp(got, spec[i].face) && spec[i].fallback) { /* face missing: GDI substituted */
            DeleteObject(g_fonts[i]);
            g_fonts[i] = font_try(px, spec[i].weight, spec[i].quality, spec[i].fallback, got);
            if (i == F_KANJI && _wcsicmp(got, spec[i].fallback)) {
                DeleteObject(g_fonts[i]);
                g_fonts[i] = font_try(px, spec[i].weight, spec[i].quality, L"MS Mincho", got);
                g_kanji_ok = !_wcsicmp(got, L"MS Mincho");
            }
        }
    }
}

int ui_has_kanji(void) { return g_kanji_ok; }

void ui_font_face(int font, wchar_t *out, int n) {
    HGDIOBJ old = SelectObject(g_mem, g_fonts[font]);
    out[0] = 0;
    GetTextFaceW(g_mem, n, out);
    SelectObject(g_mem, old);
}

void ui_init(int dpi) {
    BITMAPINFO bi;
    HDC screen = GetDC(NULL);
    void *bits = NULL;
    g_dpi = dpi < 96 ? 96 : dpi;
    g_pw = S(ART_W);
    g_ph = S(ART_H);
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = g_pw;
    bi.bmiHeader.biHeight = -g_ph; /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g_mem = CreateCompatibleDC(screen);
    g_bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, screen);
    g_bmp_old = (HBITMAP)SelectObject(g_mem, g_bmp);
    g_px = (uint32_t *)bits;
    make_fonts();
}

void ui_begin(int kind) {
    canvas_t c;
    size_t bytes = (size_t)g_pw * g_ph * sizeof(uint32_t);
    g_nhits = 0;
    if (kind < 0 || kind > 1)
        kind = 0;
    if (!g_bg[kind]) {
        g_bg[kind] = (uint32_t *)malloc(bytes);
        if (g_bg[kind]) {
            c.px = g_bg[kind];
            c.w = g_pw;
            c.h = g_ph;
            c.k = (float)g_dpi / 96.0f;
            art_backdrop(&c, kind, g_bg_game_art);
        }
    }
    gdi_sync();
    if (g_bg[kind]) {
        canvas_t frame, cached;
        frame.px = g_px;
        cached.px = g_bg[kind];
        frame.w = cached.w = g_pw;
        frame.h = cached.h = g_ph;
        frame.k = cached.k = (float)g_dpi / 96.0f;
        art_animate(&frame, &cached, kind, g_bg_game_art, g_motion_t, g_motion_x, g_motion_y);
    } else {
        memset(g_px, 0, bytes);
    }
}

void ui_set_motion(double t, float mx, float my) {
    g_motion_t = t;
    g_motion_x = mx;
    g_motion_y = my;
}

void ui_end(void) { gdi_sync(); }

void ui_invalidate_backdrop(void) {
    int i;
    for (i = 0; i < 2; i++) {
        free(g_bg[i]);
        g_bg[i] = NULL;
    }
}

void ui_set_game_art(int on) {
    if (on != g_bg_game_art) {
        g_bg_game_art = on;
        ui_invalidate_backdrop();
    }
}

void ui_mask(int x, int y, int w, int h, const uint8_t *a, int aw, int ah, rgb_t col, int opacity) {
    int X0 = S(x), Y0 = S(y), W = S(w), H = S(h), i, j;
    if (!a || W <= 0 || H <= 0)
        return;
    gdi_sync();
    for (j = 0; j < H; j++) {
        float v = ((float)j + 0.5f) * (float)ah / (float)H - 0.5f;
        int v0 = v < 0.0f ? 0 : (int)v, v1 = v0 + 1 < ah ? v0 + 1 : ah - 1;
        float fv = v - (float)v0 < 0.0f ? 0.0f : v - (float)v0;
        for (i = 0; i < W; i++) {
            float u = ((float)i + 0.5f) * (float)aw / (float)W - 0.5f;
            int u0 = u < 0.0f ? 0 : (int)u, u1 = u0 + 1 < aw ? u0 + 1 : aw - 1;
            float fu = u - (float)u0 < 0.0f ? 0.0f : u - (float)u0;
            float t = a[(size_t)v0 * aw + u0] + (a[(size_t)v0 * aw + u1] - a[(size_t)v0 * aw + u0]) * fu;
            float b = a[(size_t)v1 * aw + u0] + (a[(size_t)v1 * aw + u1] - a[(size_t)v1 * aw + u0]) * fu;
            int al = (int)((t + (b - t) * fv) * (float)opacity / 255.0f + 0.5f);
            if (al > 0)
                blend_px(X0 + i, Y0 + j, col, al);
        }
    }
}

void ui_present(HDC dc) { BitBlt(dc, 0, 0, g_pw, g_ph, g_mem, 0, 0, SRCCOPY); }

int ui_save_bmp(const wchar_t *path) {
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    DWORD n;
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    gdi_sync();
    memset(&fh, 0, sizeof fh);
    memset(&ih, 0, sizeof ih);
    ih.biSize = sizeof ih;
    ih.biWidth = g_pw;
    ih.biHeight = -g_ph;
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    fh.bfType = 0x4d42;
    fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + (DWORD)((size_t)g_pw * g_ph * 4);
    WriteFile(h, &fh, sizeof fh, &n, NULL);
    WriteFile(h, &ih, sizeof ih, &n, NULL);
    WriteFile(h, g_px, (DWORD)((size_t)g_pw * g_ph * 4), &n, NULL);
    CloseHandle(h);
    return 1;
}

/* ------------------------------------------------------------------------ shapes ---- */

void fill_rect(int x, int y, int w, int h, rgb_t c) {
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
            g_px[(size_t)j * g_pw + i] = c;
}

void fill_rect_a(int x, int y, int w, int h, rgb_t c, int alpha) {
    int x0 = S(x), y0 = S(y), x1 = S(x + w), y1 = S(y + h), i, j;
    gdi_sync();
    for (j = y0; j < y1; j++)
        for (i = x0; i < x1; i++)
            blend_px(i, j, c, alpha);
}

/* Rounded rectangle in device pixels. stroke = 0: fill with a vertical gradient c1 -> c2;
 * stroke > 0: only the inner border of that thickness. */
static void rrect_dev(int X, int Y, int W, int H, int R, rgb_t c1, rgb_t c2, int stroke, int alpha) {
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

void fill_rrect(int x, int y, int w, int h, int r, rgb_t c) {
    rrect_dev(S(x), S(y), S(x + w) - S(x), S(y + h) - S(y), S(r), c, c, 0, 255);
}
void fill_rrect_a(int x, int y, int w, int h, int r, rgb_t c, int alpha) {
    rrect_dev(S(x), S(y), S(x + w) - S(x), S(y + h) - S(y), S(r), c, c, 0, alpha);
}
void fill_rrect_grad(int x, int y, int w, int h, int r, rgb_t c1, rgb_t c2) {
    rrect_dev(S(x), S(y), S(x + w) - S(x), S(y + h) - S(y), S(r), c1, c2, 0, 255);
}
void stroke_rrect(int x, int y, int w, int h, int r, rgb_t c) {
    int t = S(1) < 1 ? 1 : S(1);
    rrect_dev(S(x), S(y), S(x + w) - S(x), S(y + h) - S(y), S(r), c, c, t, 255);
}
void fill_circle(int cx, int cy, int r, rgb_t c) { fill_rrect(cx - r, cy - r, 2 * r, 2 * r, r, c); }

void capsule(float ax, float ay, float bx, float by, float thick, rgb_t col) {
    float k = (float)g_dpi / 96.0f, dx, dy, len2, rad = thick * k * 0.5f;
    int i, j, x0, x1, y0, y1;
    gdi_sync();
    ax *= k;
    ay *= k;
    bx *= k;
    by *= k;
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
            t = clampf(t, 0, 1);
            qx = px - dx * t;
            qy = py - dy * t;
            d = sqrtf(qx * qx + qy * qy) - rad;
            cov = clampf(0.5f - d, 0, 1);
            if (cov > 0)
                blend_px(i, j, col, (int)(cov * 255));
        }
}


void kamon_draw(int cx, int cy, int r, rgb_t col) {
    int X = S(cx), Y = S(cy), R = S(r), i, j;
    float aa, thick;
    if (R < 2)
        return;
    aa = 1.0f / (float)R;
    thick = R < 16 ? 1.6f : 1.0f; /* hairlines vanish at tiny sizes */
    gdi_sync();
    for (j = Y - R; j <= Y + R; j++)
        for (i = X - R; i <= X + R; i++) {
            float c = kamon_coverage((i + 0.5f - X) / R, (j + 0.5f - Y) / R, aa, thick);
            if (c > 0.0f)
                blend_px(i, j, col, (int)(c * 255.0f + 0.5f));
        }
}

void ui_clip(int x, int y, int w, int h) {
    gdi_sync();
    IntersectClipRect(g_mem, S(x), S(y), S(x + w), S(y + h));
}
void ui_unclip(void) { SelectClipRgn(g_mem, NULL); }


/* ------------------------------------------------------------------------- text ---- */

void txt(int font, rgb_t col, int x, int y, int w, int h, UINT fmt, const wchar_t *s) {
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

int text_w(int font, const wchar_t *s) {
    SIZE sz;
    HGDIOBJ old = SelectObject(g_mem, g_fonts[font]);
    GetTextExtentPoint32W(g_mem, s, (int)wcslen(s), &sz);
    SelectObject(g_mem, old);
    return MulDiv(sz.cx, 96, g_dpi);
}

static int font_em_px(int font) {
    TEXTMETRICW tm;
    HGDIOBJ old = SelectObject(g_mem, g_fonts[font]);
    GetTextMetricsW(g_mem, &tm);
    SelectObject(g_mem, old);
    return tm.tmHeight - tm.tmInternalLeading; /* em height in device pixels */
}

void txt_tracked(int font, rgb_t col, int x, int y, int h, float em, const wchar_t *s) {
    RECT r;
    HGDIOBJ old = SelectObject(g_mem, g_fonts[font]);
    SetBkMode(g_mem, TRANSPARENT);
    SetTextColor(g_mem, cref(col));
    SetTextCharacterExtra(g_mem, (int)(em * (float)font_em_px(font) + 0.5f));
    r.left = S(x);
    r.top = S(y);
    r.right = S(x + 4000);
    r.bottom = S(y + h);
    DrawTextW(g_mem, s, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SetTextCharacterExtra(g_mem, 0);
    SelectObject(g_mem, old);
}

int tracked_w(int font, float em, const wchar_t *s) {
    SIZE sz;
    int n = (int)wcslen(s);
    HGDIOBJ old = SelectObject(g_mem, g_fonts[font]);
    int extra = (int)(em * (float)font_em_px(font) + 0.5f);
    GetTextExtentPoint32W(g_mem, s, n, &sz);
    SelectObject(g_mem, old);
    /* the last glyph's spacing is trailing; do not count it */
    return MulDiv(sz.cx + extra * (n > 0 ? n - 1 : 0), 96, g_dpi);
}




/* ------------------------------------------------------------------ click zones ---- */

void add_hit(int x, int y, int w, int h, int id) {
    if (g_nhits < (int)(sizeof g_hits / sizeof g_hits[0])) {
        hit_t *t = &g_hits[g_nhits++];
        t->x = x;
        t->y = y;
        t->w = w;
        t->h = h;
        t->id = id;
    }
}

int hit_at(int lx, int ly) {
    int i;
    for (i = g_nhits - 1; i >= 0; i--) {
        const hit_t *t = &g_hits[i];
        if (lx >= t->x && lx < t->x + t->w && ly >= t->y && ly < t->y + t->h)
            return t->id;
    }
    return 0;
}

int hot(int id) { return g_hover == id; }
int pressed(int id) { return g_down == id && g_hover == id; }

/* ---------------------------------------------------------------------- widgets ---- */

/* Chamfered box: signed distance (device pixels) of a point relative to the centre. */

/* state: 0 idle, 1 hot, 2 pressed, 3 disabled. */






/* ------------------------------------------------------------ lacquer widgets ---- */

#define A_SEP 26    /* cream at 10 % */
#define A_FRAME 72  /* cream at 28 %: inner frame of the primary button */
#define A_OUTLINE 82 /* cream at 32 %: outline of the secondary button */

static void upper(const wchar_t *s, wchar_t *out, int n) {
    wcsncpy(out, s, (size_t)n - 1);
    out[n - 1] = 0;
    CharUpperBuffW(out, (DWORD)wcslen(out));
}

void ui_hair(int x, int y, int w) { fill_rect_a(x, y, w, 1, C_CREAM, A_SEP); }
void ui_vhair(int x, int y, int h) { fill_rect_a(x, y, 1, h, C_CREAM, A_SEP); }

static void frame_a(int x, int y, int w, int h, rgb_t c, int a) {
    fill_rect_a(x, y, w, 1, c, a);
    fill_rect_a(x, y + h - 1, w, 1, c, a);
    fill_rect_a(x, y + 1, 1, h - 2, c, a);
    fill_rect_a(x + w - 1, y + 1, 1, h - 2, c, a);
}

int ui_fit_tracked(int font, float em, const wchar_t *s, int max_w, float *em_out) {
    float e = em;
    while (tracked_w(font, e, s) > max_w && e > 0.08f)
        e -= 0.02f;
    if (e < 0.08f)
        e = 0.08f;
    *em_out = e;
    return tracked_w(font, e, s) <= max_w;
}

void ui_label(int x, int y, const wchar_t *s, rgb_t col) {
    wchar_t u[160];
    upper(s, u, 160);
    txt_tracked(F_LABEL, col, x, y, 14, 0.26f, u);
}

int ui_tab(int id, int x, int y, int h, const wchar_t *label, int active) {
    wchar_t u[64];
    int w;
    upper(label, u, 64);
    w = tracked_w(F_TAB, 0.24f, u);
    txt_tracked(F_TAB, active ? C_CREAM : hot(id) ? C_MUTED : C_DIMC, x, y, h, 0.24f, u);
    if (active)
        fill_rect(x, y + h - 1, w, 1, C_VERM);
    add_hit(x - 8, y, w + 16, h, id);
    return w;
}

static void centred_label(int font, float em, rgb_t col, int x, int y, int w, int h, const wchar_t *u,
                          const wchar_t *kanji) {
    int kw = kanji && kanji[0] && ui_has_kanji() ? text_w(F_KANJI, kanji) + 18 : 0;
    float e;
    int lw, tx;
    if (!ui_fit_tracked(font, em, u, w - 32 - kw, &e) && font == F_BTN) {
        font = F_TAB; /* still too long: the smaller face */
        ui_fit_tracked(font, em, u, w - 32 - kw, &e);
    }
    lw = tracked_w(font, e, u);
    tx = x + (w - lw - kw) / 2;
    txt_tracked(font, col, tx, y, h, e, u);
    if (kw)
        txt(F_KANJI, lerp_rgb(col, C_VERM, 90), tx + lw + 18, y, kw, h, DT_SINGLELINE | DT_VCENTER, kanji);
}

void ui_primary(int id, int x, int y, int w, int h, const wchar_t *label, const wchar_t *kanji,
                int enabled) {
    wchar_t u[64];
    rgb_t fill = !enabled ? 0x3a2320 : pressed(id) ? C_VERM_DOWN : hot(id) ? C_VERM_HOT : C_VERM;
    upper(label, u, 64);
    fill_rect(x, y, w, h, fill);
    frame_a(x + 4, y + 4, w - 8, h - 8, 0xfbf4e8, enabled ? A_FRAME : 30);
    centred_label(F_BTN, 0.52f, enabled ? 0xfbf4e8 : C_DIMC, x, y, w, h, u, kanji);
    if (enabled)
        add_hit(x, y, w, h, id);
}

void ui_secondary(int id, int x, int y, int w, int h, const wchar_t *label, int enabled) {
    wchar_t u[64];
    upper(label, u, 64);
    if (enabled && hot(id))
        fill_rect_a(x, y, w, h, C_CREAM, 10);
    frame_a(x, y, w, h, C_CREAM, !enabled ? 30 : hot(id) ? 150 : A_OUTLINE);
    centred_label(F_TAB, 0.3f, enabled ? C_CREAM : C_DIMC, x, y, w, h, u, NULL);
    if (enabled)
        add_hit(x, y, w, h, id);
}

int ui_link(int id, int x, int y, const wchar_t *label, int enabled) {
    wchar_t u[64];
    int w;
    upper(label, u, 64);
    w = tracked_w(F_LABEL, 0.22f, u);
    txt_tracked(F_LABEL, !enabled ? C_DIMC : hot(id) ? C_VERM_HOT : C_VERM, x, y, 16, 0.22f, u);
    if (enabled && hot(id))
        fill_rect(x, y + 14, w, 1, C_VERM_HOT);
    if (enabled)
        add_hit(x - 4, y - 4, w + 8, 24, id);
    return w;
}

void ui_switch(int id, int x, int y, int on, int enabled) {
    rgb_t edge = !enabled ? C_SEP : on ? C_VERM : hot(id) ? C_MUTED : 0x4a4640;
    frame_a(x, y, 44, 22, edge, 255);
    fill_rect(on ? x + 26 : x + 4, y + 4, 14, 14, !enabled ? C_SEP : on ? C_VERM : C_DIMC);
    if (enabled)
        add_hit(x - 4, y - 4, 52, 30, id);
}

void ui_rule_progress(int x, int y, int w, int permille) {
    int fw = w * (permille < 0 ? 0 : permille > 1000 ? 1000 : permille) / 1000;
    fill_rect_a(x, y, w, 2, C_CREAM, A_SEP);
    fill_rect(x, y, fw, 2, C_VERM);
}

void ui_caret(int x, int y, rgb_t col) {
    capsule((float)x, (float)y, (float)x + 4, (float)y + 4, 1.4f, col);
    capsule((float)x + 4, (float)y + 4, (float)x + 8, (float)y, 1.4f, col);
}
