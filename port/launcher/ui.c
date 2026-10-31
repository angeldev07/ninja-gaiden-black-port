/*
 * ui.c: the launcher's immediate-mode toolkit. See ui.h.
 *
 * Shapes are drawn pixel by pixel into the back buffer with signed-distance edges, so they
 * are antialiased without GDI+. Text goes through GDI (ClearType for body text, grayscale
 * for the chrome headings, whose glyph mask we recolour ourselves).
 */
#define UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"

int g_dpi = 96;
int g_hover = 0, g_down = 0;

static HDC g_mem;
static HBITMAP g_bmp, g_bmp_old;
static uint32_t *g_px;
static int g_pw, g_ph;
static HFONT g_fonts[F_COUNT];
static uint32_t *g_bg[2];

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
        {L"Segoe UI", 150, FW_NORMAL, CLEARTYPE_QUALITY, NULL},
        {L"Segoe UI", 120, FW_NORMAL, CLEARTYPE_QUALITY, NULL},
        {L"Segoe UI", 150, FW_SEMIBOLD, CLEARTYPE_QUALITY, NULL},
        {L"Segoe UI", 110, FW_SEMIBOLD, CLEARTYPE_QUALITY, NULL},
        {L"Bahnschrift", 340, FW_SEMIBOLD, ANTIALIASED_QUALITY, L"Segoe UI"},
        {L"Bahnschrift", 210, FW_SEMIBOLD, ANTIALIASED_QUALITY, L"Segoe UI"},
        {L"Bahnschrift", 220, FW_BOLD, ANTIALIASED_QUALITY, L"Segoe UI"},
        {L"Consolas", 120, FW_NORMAL, CLEARTYPE_QUALITY, NULL},
        {L"Bahnschrift", 150, FW_SEMIBOLD, ANTIALIASED_QUALITY, L"Segoe UI"},
        {L"Josefin Sans", 140, FW_LIGHT, CLEARTYPE_QUALITY, L"Segoe UI Light"},
        {L"Josefin Sans", 125, FW_LIGHT, CLEARTYPE_QUALITY, L"Segoe UI Light"},
        {L"Josefin Sans", 105, FW_SEMIBOLD, CLEARTYPE_QUALITY, L"Segoe UI Semibold"},
        {L"Josefin Sans", 110, FW_SEMIBOLD, CLEARTYPE_QUALITY, L"Segoe UI Semibold"},
        {L"Josefin Sans", 160, FW_LIGHT, CLEARTYPE_QUALITY, L"Segoe UI Light"},
        {L"Josefin Sans", 880, FW_THIN, ANTIALIASED_QUALITY, L"Segoe UI Light"},
        {L"Josefin Sans", 460, FW_THIN, ANTIALIASED_QUALITY, L"Segoe UI Light"},
        {L"Josefin Sans", 540, FW_THIN, ANTIALIASED_QUALITY, L"Segoe UI Light"},
        {L"Josefin Sans", 150, FW_SEMIBOLD, ANTIALIASED_QUALITY, L"Segoe UI Semibold"},
        {L"Josefin Sans", 1280, FW_THIN, ANTIALIASED_QUALITY, L"Segoe UI Light"},
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
            int w = spec[i].weight == FW_BOLD && !_wcsicmp(spec[i].face, L"Bahnschrift")
                        ? FW_BOLD
                        : spec[i].weight;
            DeleteObject(g_fonts[i]);
            g_fonts[i] = font_try(px, w, spec[i].quality, spec[i].fallback, got);
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
            art_backdrop(&c, kind);
        }
    }
    gdi_sync();
    if (g_bg[kind])
        memcpy(g_px, g_bg[kind], bytes);
    else
        memset(g_px, 0, bytes);
}

void ui_end(void) { gdi_sync(); }

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

void status_dot(int x, int y, rgb_t c) {
    int X = S(x), Y = S(y), R = S(11), i, j;
    gdi_sync();
    for (j = Y - R; j <= Y + R; j++)
        for (i = X - R; i <= X + R; i++) {
            float d = hypotf(i + 0.5f - X, j + 0.5f - Y);
            if (d < (float)R) {
                float a = 1.0f - d / (float)R;
                add_px(i, j, (int)(((c >> 16) & 255) * a * a * 0.45f),
                       (int)(((c >> 8) & 255) * a * a * 0.45f), (int)((c & 255) * a * a * 0.45f));
            }
        }
    fill_circle(x, y, 4, c);
}

void ui_clip(int x, int y, int w, int h) {
    gdi_sync();
    IntersectClipRect(g_mem, S(x), S(y), S(x + w), S(y + h));
}
void ui_unclip(void) { SelectClipRgn(g_mem, NULL); }

void ui_card(int x, int y, int w, int h) {
    fill_rrect_a(x, y, w, h, 5, 0x131318, 232);
    stroke_rrect(x, y, w, h, 5, 0x2a2a33);
    fill_rect(x + 16, y, 30, 2, C_RED);
    fill_rect_a(x + 16, y + 2, 30, 3, C_RED, 70);
}

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

static void blur_box(uint8_t *a, uint8_t *tmp, int w, int h, int r) {
    int x, y, d;
    if (r < 1)
        return;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int sum = 0;
            for (d = -r; d <= r; d++)
                if (x + d >= 0 && x + d < w)
                    sum += a[(size_t)y * w + x + d];
            tmp[(size_t)y * w + x] = (uint8_t)(sum / (2 * r + 1));
        }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int sum = 0;
            for (d = -r; d <= r; d++)
                if (y + d >= 0 && y + d < h)
                    sum += tmp[(size_t)(y + d) * w + x];
            a[(size_t)y * w + x] = (uint8_t)(sum / (2 * r + 1));
        }
}

void txt_chrome(int font, int x, int y, int w, int h, UINT fmt, int red_style, int spacing,
                const wchar_t *s) {
    int pad = S(7), pw = S(w), ph = S(h), mw = pw + 2 * pad, mh = ph + 2 * pad, i, j;
    BITMAPINFO bi;
    HDC dc = GetDC(NULL), mdc;
    HBITMAP bmp, old_bmp;
    HGDIOBJ old_font;
    void *bits = NULL;
    uint8_t *mask, *glow, *tmp;
    RECT r;
    int top = mh, bot = -1, ox = S(x) - pad, oy = S(y) - pad;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = mw;
    bi.bmiHeader.biHeight = -mh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    mdc = CreateCompatibleDC(dc);
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!bmp || !bits) {
        DeleteDC(mdc);
        return;
    }
    old_bmp = (HBITMAP)SelectObject(mdc, bmp);
    old_font = SelectObject(mdc, g_fonts[font]);
    SetBkMode(mdc, TRANSPARENT);
    SetTextColor(mdc, RGB(255, 255, 255));
    SetTextCharacterExtra(mdc, S(spacing));
    r.left = pad;
    r.top = pad;
    r.right = pad + pw;
    r.bottom = pad + ph;
    DrawTextW(mdc, s, -1, &r, fmt | DT_NOPREFIX);
    GdiFlush();
    mask = (uint8_t *)malloc((size_t)mw * mh);
    glow = (uint8_t *)malloc((size_t)mw * mh);
    tmp = (uint8_t *)malloc((size_t)mw * mh);
    if (mask && glow && tmp) {
        const uint32_t *src = (const uint32_t *)bits;
        for (j = 0; j < mh; j++)
            for (i = 0; i < mw; i++) {
                uint8_t v = (uint8_t)(src[(size_t)j * mw + i] & 255);
                mask[(size_t)j * mw + i] = v;
                if (v > 60) {
                    if (j < top)
                        top = j;
                    if (j > bot)
                        bot = j;
                }
            }
        memcpy(glow, mask, (size_t)mw * mh);
        blur_box(glow, tmp, mw, mh, S(2));
        blur_box(glow, tmp, mw, mh, S(2));
        gdi_sync();
        for (j = 0; j < mh; j++)
            for (i = 0; i < mw; i++) {
                size_t k = (size_t)j * mw + i;
                float m = mask[k] / 255.0f, g = fminf(1.0f, glow[k] / 255.0f * 1.9f);
                float cr, cg, cb, t;
                add_px(ox + i, oy + j, (int)(190 * g * g), (int)(8 * g * g), (int)(16 * g * g));
                if (!mask[k] || bot < top)
                    continue;
                t = (float)(j - top) / (float)(bot - top + 1);
                art_chrome(t, red_style, &cr, &cg, &cb);
                blend_px(ox + i, oy + j,
                         ((rgb_t)clampf(cr, 0, 255) << 16) | ((rgb_t)clampf(cg, 0, 255) << 8) |
                             (rgb_t)clampf(cb, 0, 255),
                         (int)(m * 255));
            }
    }
    free(mask);
    free(glow);
    free(tmp);
    SelectObject(mdc, old_font);
    SelectObject(mdc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mdc);
}

void caption_col(int x, int y, int w, rgb_t col, const wchar_t *s) {
    SetTextCharacterExtra(g_mem, S(2));
    txt(F_TINY, col, x, y, w, 16, DT_SINGLELINE, s);
    SetTextCharacterExtra(g_mem, 0);
}
void caption(int x, int y, int w, const wchar_t *s) { caption_col(x, y, w, C_DIM, s); }

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
static float chamfer_sdf(float px, float py, float hw, float hh, float ch) {
    float qx = fabsf(px) - hw, qy = fabsf(py) - hh;
    float d_box = fmaxf(qx, qy);
    float d_cut = (fabsf(px) + fabsf(py) - (hw + hh - ch)) * 0.70710678f;
    return fmaxf(d_box, d_cut);
}

/* state: 0 idle, 1 hot, 2 pressed, 3 disabled. */
static void plate_dev(int X, int Y, int W, int H, int ch, int state) {
    int M = S(30), i, j;
    float hw = W * 0.5f, hh = H * 0.5f, cx = X + hw, cy = Y + hh, glow_k = S(11) * 1.0f;
    float gi = state == 1 ? 0.95f : state == 2 ? 0.5f : state == 3 ? 0.0f : 0.62f;
    gdi_sync();
    for (j = Y - M; j < Y + H + M; j++)
        for (i = X - M; i < X + W + M; i++) {
            float d = chamfer_sdf(i + 0.5f - cx, j + 0.5f - cy, hw, hh, (float)ch);
            float v = clampf((j + 0.5f - Y) / (float)H, 0, 1);
            if (d > 0.5f) {
                float a = expf(-d / glow_k) * gi;
                if (a > 0.012f)
                    add_px(i, j, (int)(255 * a * 0.55f), (int)(24 * a * 0.55f), (int)(34 * a * 0.55f));
                continue;
            }
            {
                float cov = clampf(0.5f - d, 0, 1), depth = -d, r, g, b, cr, cg, cb;
                int bw = S(2) < 2 ? 2 : S(2);
                if (state == 3) {
                    r = 74 - 36 * v;
                    g = 40 - 20 * v;
                    b = 44 - 22 * v;
                } else {
                    float s = state == 2 ? 0.72f : state == 1 ? 1.12f : 1.0f;
                    r = (238 - 140 * v) * s;
                    g = (52 - 44 * v) * s;
                    b = (56 - 42 * v) * s;
                    if (v < 0.5f) { /* gloss on the upper half */
                        float gl = (1.0f - v / 0.5f) * 40.0f;
                        r += gl;
                        g += gl * 0.7f;
                        b += gl * 0.7f;
                    }
                }
                r += (float)((int)(((uint32_t)(j * 2654435761u) >> 28)) - 8) * 0.5f; /* brushed rows */
                g += (float)((int)(((uint32_t)(j * 2654435761u) >> 28)) - 8) * 0.25f;
                if (depth < (float)bw) { /* steel rim */
                    art_chrome(v, 0, &cr, &cg, &cb);
                    if (state == 3) {
                        cr *= 0.35f;
                        cg *= 0.35f;
                        cb *= 0.38f;
                    }
                    r = cr;
                    g = cg;
                    b = cb;
                } else if (depth < (float)bw + 1.5f) {
                    r *= 0.35f;
                    g *= 0.35f;
                    b *= 0.35f;
                }
                blend_px(i, j, ((rgb_t)clampf(r, 0, 255) << 16) | ((rgb_t)clampf(g, 0, 255) << 8) |
                                   (rgb_t)clampf(b, 0, 255),
                         (int)(cov * 255));
            }
        }
}

void ui_plate(int id, int x, int y, int w, int h, const wchar_t *label, int enabled) {
    int state = !enabled ? 3 : pressed(id) ? 2 : hot(id) ? 1 : 0;
    int off = state == 2 ? 1 : 0, font = h >= 48 ? F_PLAY : F_BOLD, space = h >= 48 ? 3 : 1;
    if (text_w(font, label) + space * (int)wcslen(label) > w - 24) {
        font = F_BOLD; /* the label does not fit in the big font */
        space = 1;
    }
    plate_dev(S(x), S(y), S(w), S(h), S(h >= 48 ? 11 : 6), state);
    {
        /* GDI does not count the extra letter spacing when centring, so centre by hand */
        int tx = x + (w - (text_w(font, label) + space * ((int)wcslen(label) - 1))) / 2;
        SetTextCharacterExtra(g_mem, S(space));
        if (enabled)
            txt(font, 0x2a0408, tx + 1, y + 2 + off, w, h, DT_SINGLELINE | DT_VCENTER, label);
        txt(font, enabled ? 0xfff4ee : 0x8a6e70, tx, y + 1 + off, w, h, DT_SINGLELINE | DT_VCENTER,
            label);
        SetTextCharacterExtra(g_mem, 0);
    }
    if (enabled)
        add_hit(x, y, w, h, id);
}

void ui_button(int id, int x, int y, int w, int h, const wchar_t *label, int style, int enabled) {
    int over = enabled && hot(id);
    if (style == 1) {
        ui_plate(id, x, y, w, h, label, enabled);
        return;
    }
    if (style == 2) {
        rgb_t col = !enabled ? C_FAINT : over ? C_RED_HOT : C_STEEL;
        int tw = text_w(F_BOLD, label);
        txt(F_BOLD, col, x, y, w, h, DT_SINGLELINE | DT_VCENTER | DT_CENTER, label);
        if (over)
            fill_rect(x + (w - tw) / 2, y + h / 2 + 10, tw, 1, C_RED_HOT);
        if (enabled)
            add_hit(x, y, w, h, id);
        return;
    }
    if (over)
        fill_rrect_a(x - 2, y - 2, w + 4, h + 4, 7, C_RED, 70);
    fill_rrect_grad(x, y, w, h, 5, !enabled ? 0x121216 : over ? 0x26262e : 0x1c1c23,
                    !enabled ? 0x0e0e12 : over ? 0x18181e : 0x121217);
    stroke_rrect(x, y, w, h, 5, !enabled ? 0x23232a : over ? 0xb02430 : 0x3a3d48);
    fill_rect_a(x + 4, y + 1, w - 8, 1, 0xffffff, over ? 40 : 22);
    txt(F_BOLD, !enabled ? C_FAINT : C_TEXT, x, y, w, h, DT_SINGLELINE | DT_VCENTER | DT_CENTER,
        label);
    if (enabled)
        add_hit(x, y, w, h, id);
}

void ui_progress(int x, int y, int w, int h, int permille, int active) {
    int fw = w * (permille < 0 ? 0 : permille > 1000 ? 1000 : permille) / 1000, i, j;
    fill_rrect(x, y, w, h, h / 2, C_FIELD);
    stroke_rrect(x, y, w, h, h / 2, 0x33333d);
    if (fw > h / 2) {
        int X = S(x), Y = S(y), W = S(fw), H = S(h);
        long phase = active ? (long)(GetTickCount64() / 18) : 0;
        fill_rrect_a(x - 3, y - 3, fw + 6, h + 6, h / 2 + 3, C_RED, 46);
        fill_rrect_grad(x, y, fw, h, h / 2, 0xff4a52, 0x8e0f18);
        gdi_sync();
        for (j = Y + 1; j < Y + H - 1; j++)
            for (i = X + H / 2; i < X + W - H / 2; i++)
                if (((i + j - (int)phase) & 31) < 11)
                    blend_px(i, j, 0xffffff, 26);
    }
}

void ui_checkbox(int id, int x, int y, int on) {
    if (on) {
        fill_rrect_grad(x, y + 3, 22, 22, 5, 0xe8343c, 0x9a1018);
        stroke_rrect(x, y + 3, 22, 22, 5, 0xff6a70);
        capsule((float)x + 5.5f, (float)y + 14.5f, (float)x + 9.5f, (float)y + 18.5f, 2.4f, 0xffffff);
        capsule((float)x + 9.5f, (float)y + 18.5f, (float)x + 17, (float)y + 9.5f, 2.4f, 0xffffff);
    } else {
        fill_rrect(x, y + 3, 22, 22, 5, C_FIELD);
        stroke_rrect(x, y + 3, 22, 22, 5, hot(id) ? 0x7a7e8c : 0x3c3f4a);
    }
}

void ui_chevron(int x, int y) {
    capsule((float)x, (float)y, (float)x + 6, (float)y + 6, 2.0f, C_DIM);
    capsule((float)x + 6, (float)y + 6, (float)x + 12, (float)y, 2.0f, C_DIM);
}
