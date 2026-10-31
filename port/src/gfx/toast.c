/*
 * toast.c: see toast.h.
 *
 * toast_show() only queues the strings. The render thread takes the next one in toast_draw(),
 * draws it with GDI into a DIB at the size it will have on screen (so the text is sharp at any
 * window size), fixes the alpha by geometry (GDI leaves it at 0), uploads it as a texture and
 * composites it with one textured quad. Fonts: Josefin Sans and the Shippori Mincho subset,
 * embedded in ngb-core.dll (port/res/ngb.rc) and loaded privately.
 */
#include "toast.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../ngb.h"
#include <mmsystem.h> /* PlaySound; windows.h left it out (WIN32_LEAN_AND_MEAN) */

#include "../gl/glbackend.h"
#include "../gl/glload.h"

#define QUEUE 8
#define T_IN 0.35  /* seconds to rise and fade in */
#define T_OUT 0.40 /* seconds to fade out */

/* Lacquer and ink (docs/target/launcher-laca-design.md). */
#define C_BG RGB(19, 17, 15)
#define C_CREAM RGB(239, 231, 214)
#define C_VERM RGB(210, 63, 47)

static SRWLOCK g_lock = SRWLOCK_INIT;
static struct {
    wchar_t label[96], name[128];
    int rare;
} g_queue[QUEUE];
static int g_head, g_count;

/* Render thread only. */
static struct {
    int active, rare, w, h, unit; /* texture size and the 480-line unit it was built for */
    LARGE_INTEGER t0;
    GLuint tex;
} g_cur;
static GLuint g_prog, g_vao;
static GLint g_u_rect, g_u_alpha;

static const char k_vs[] =
    "#version 400\n"
    "uniform vec4 rect;\n" /* NDC: left, bottom, right, top */
    "out vec2 uv;\n"
    "void main() {\n"
    "    vec2 p = vec2(gl_VertexID & 1, gl_VertexID >> 1);\n"
    "    uv = vec2(p.x, 1.0 - p.y);\n" /* the DIB rows go top to bottom */
    "    gl_Position = vec4(mix(rect.xy, rect.zw, p), 0.0, 1.0);\n"
    "}\n";
static const char k_fs[] =
    "#version 400\n"
    "in vec2 uv;\n"
    "uniform sampler2D tex;\n"
    "uniform float alpha;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "    vec4 t = texture(tex, uv);\n"
    "    color = vec4(t.rgb, t.a * alpha);\n"
    "}\n";

void toast_show(const wchar_t *label, const wchar_t *name, int rare) {
    AcquireSRWLockExclusive(&g_lock);
    if (g_count < QUEUE) {
        int i = (g_head + g_count) % QUEUE;
        wcsncpy_s(g_queue[i].label, sizeof g_queue[i].label / sizeof g_queue[i].label[0], label, _TRUNCATE);
        wcsncpy_s(g_queue[i].name, sizeof g_queue[i].name / sizeof g_queue[i].name[0], name, _TRUNCATE);
        g_queue[i].rare = rare;
        g_count++;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

/* ---- sound: a low drum hit, made here (no game audio) ---- */

static uint8_t g_wav[44 + 2 * 22050 * 45 / 100];

static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void put16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }

/* 0.45 s at 22,050 Hz: a sine falling from 150 to 55 Hz under a fast decay, a soft skin click and
 * a quieter second partial. */
static void make_drum(void) {
    uint32_t n = 22050 * 45 / 100, i, seed = 0x2545F491u;
    double phase = 0, phase2 = 0;
    memcpy(g_wav, "RIFF", 4);
    put32(g_wav + 4, 36 + n * 2);
    memcpy(g_wav + 8, "WAVEfmt ", 8);
    put32(g_wav + 16, 16);
    put16(g_wav + 20, 1);
    put16(g_wav + 22, 1);
    put32(g_wav + 24, 22050);
    put32(g_wav + 28, 22050 * 2);
    put16(g_wav + 32, 2);
    put16(g_wav + 34, 16);
    memcpy(g_wav + 36, "data", 4);
    put32(g_wav + 40, n * 2);
    for (i = 0; i < n; i++) {
        double t = i / 22050.0, f = 55.0 + 95.0 * exp(-t * 18.0), v;
        phase += 2.0 * 3.14159265358979 * f / 22050.0;
        phase2 += 2.0 * 3.14159265358979 * f * 1.6 / 22050.0;
        seed = seed * 1664525u + 1013904223u;
        v = 0.62 * sin(phase) * exp(-t * 7.5) + 0.12 * sin(phase2) * exp(-t * 16.0) +
            0.10 * ((double)(seed >> 9) / 4194304.0 - 1.0) * exp(-t * 140.0);
        if (t < 0.002)
            v *= t / 0.002;
        put16(g_wav + 44 + i * 2, (uint16_t)(int16_t)(v * 26000.0));
    }
}

static void play_drum(void) {
    if (g_ngb_opts.mute)
        return;
    if (!g_wav[0])
        make_drum();
    PlaySoundW((LPCWSTR)(const void *)g_wav, NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

/* ---- drawing ---- */

static void load_fonts(void) {
    static int done;
    HMODULE self = NULL;
    int id;
    if (done)
        return;
    done = 1;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)(void *)load_fonts, &self);
    for (id = 102; id <= 104; id++) { /* Josefin Light, Josefin SemiBold, Shippori kanji */
        HRSRC r = FindResourceW(self, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
        HGLOBAL g = r ? LoadResource(self, r) : NULL;
        void *p = g ? LockResource(g) : NULL;
        DWORD n = 0;
        if (!p || !AddFontMemResourceEx(p, SizeofResource(self, r), NULL, &n))
            ngb_log("toast: embedded font %d not loaded; using a system font", id);
    }
}

static HFONT font(int px, int weight, const wchar_t *face) {
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
}

static int text_w(HDC dc, const wchar_t *s, int track) {
    SIZE e;
    int w = 0;
    for (; *s; s++)
        if (GetTextExtentPoint32W(dc, s, 1, &e))
            w += e.cx + track;
    return w;
}

/* GDI has no letter-spacing: tracked text glyph by glyph, as the launcher does. */
static void text_tracked(HDC dc, int x, int y, const wchar_t *s, int track) {
    SIZE e;
    for (; *s; s++) {
        TextOutW(dc, x, y, s, 1);
        if (GetTextExtentPoint32W(dc, s, 1, &e))
            x += e.cx + track;
    }
}

static int inside(int x, int y, const RECT *r) { return x >= r->left && x < r->right && y >= r->top && y < r->bottom; }

/* Distance from (x, y) to the rectangle, 0 inside. */
static double dist(int x, int y, const RECT *r) {
    double dx = x < r->left ? r->left - x : x >= r->right ? x - r->right + 1 : 0;
    double dy = y < r->top ? r->top - y : y >= r->bottom ? y - r->bottom + 1 : 0;
    return sqrt(dx * dx + dy * dy);
}

static int build(int unit, const wchar_t *label, const wchar_t *name, int rare) {
    int h = (42 * unit + 8) / 16, m = h / 2, pad = h / 3, ruler = unit >= 32 ? unit / 16 : 1, track, w, panel_w, i, x;
    int lab_px = h * 19 / 100 > 7 ? h * 19 / 100 : 7;
    BITMAPINFO bi;
    uint8_t *px = NULL;
    HDC dc;
    HBITMAP bmp;
    HGDIOBJ old;
    HFONT f_lab, f_name, f_seal;
    HBRUSH b;
    RECT panel, seal, strip, r;
    load_fonts();
    if ((dc = CreateCompatibleDC(NULL)) == NULL)
        return -1;
    /* Static weights other than regular and bold register under their legacy family names. */
    f_lab = font(lab_px, FW_SEMIBOLD, L"Josefin Sans SemiBold");
    f_name = font(h * 34 / 100, FW_LIGHT, L"Josefin Sans Light");
    f_seal = font(h * 50 / 100, FW_SEMIBOLD, L"Shippori Mincho B1 SemiBold");
    track = lab_px * 26 / 100;
    old = SelectObject(dc, f_lab);
    panel_w = text_w(dc, label, track);
    SelectObject(dc, f_name);
    i = text_w(dc, name, 0);
    panel_w = h + pad + (i > panel_w ? i : panel_w) + pad * 4 / 3;
    w = panel_w + 2 * m; /* the margin holds the shadow and the rare rule */
    SelectObject(dc, old);
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -(h + 2 * m); /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
    if (!bmp || !px) {
        DeleteObject(f_lab);
        DeleteObject(f_name);
        DeleteObject(f_seal);
        DeleteDC(dc);
        return -1;
    }
    old = SelectObject(dc, bmp);
    SetRect(&panel, m, m, m + panel_w, m + h);
    SetRect(&seal, m, m, m + h, m + h);
    SetRect(&strip, m + h, m, m + panel_w, m + h);
    b = CreateSolidBrush(C_BG);
    FillRect(dc, &strip, b);
    DeleteObject(b);
    b = CreateSolidBrush(C_VERM);
    FillRect(dc, &seal, b);
    SetRect(&r, strip.left, strip.top, strip.right, strip.top + ruler);
    FillRect(dc, &r, b);
    DeleteObject(b);
    SetBkMode(dc, TRANSPARENT);
    if (rare) { /* an ensō: an open brush circle */
        HPEN pen = CreatePen(PS_SOLID, h / 11 > 1 ? h / 11 : 1, C_CREAM);
        HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        int c = m + h / 2, rr = h * 30 / 100;
        /* counter-clockwise from about 70 degrees round to about 15: an opening at the upper right */
        Arc(dc, c - rr, c - rr, c + rr, c + rr, c + rr / 3, c - rr, c + rr, c - rr / 4);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(pen);
    } else {
        SelectObject(dc, f_seal);
        SetTextColor(dc, C_CREAM);
        DrawTextW(dc, L"誉", -1, &seal, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    x = strip.left + pad;
    SelectObject(dc, f_lab);
    SetTextColor(dc, RGB(150, 144, 133)); /* cream at 56 % over the lacquer */
    text_tracked(dc, x, m + h * 17 / 100, label, track);
    SelectObject(dc, f_name);
    SetTextColor(dc, C_CREAM);
    TextOutW(dc, x, m + h * 42 / 100, name, (int)wcslen(name));
    GdiFlush();
    for (i = 0; i < w * (h + 2 * m); i++) {
        int y = i / w, xx = i % w;
        uint8_t *p = px + i * 4;
        RECT outer;
        if (inside(xx, y, &seal))
            p[3] = 255;
        else if (inside(xx, y, &panel))
            p[3] = 240;
        else {
            /* the soft shadow, lowered a little */
            double d = dist(xx, y - h / 10, &panel), a = d < m ? 0.42 * (1.0 - d / m) * (1.0 - d / m) : 0.0;
            p[0] = p[1] = p[2] = 0;
            p[3] = (uint8_t)(a * 255.0);
            if (rare) { /* the second vermilion rule, 3 units outside the panel */
                int g = 3 * unit / 16 + 1;
                SetRect(&outer, panel.left - g - ruler, panel.top - g - ruler, panel.right + g + ruler,
                        panel.bottom + g + ruler);
                if (inside(xx, y, &outer) && dist(xx, y, &panel) > g) {
                    p[0] = 47; /* C_VERM as B, G, R */
                    p[1] = 63;
                    p[2] = 210;
                    p[3] = 255;
                }
            }
        }
    }
    if (!g_cur.tex)
        glGenTextures(1, &g_cur.tex);
    glBindTexture(GL_TEXTURE_2D, g_cur.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h + 2 * m, 0, GL_BGRA, GL_UNSIGNED_BYTE, px);
    SelectObject(dc, old);
    DeleteObject(f_lab);
    DeleteObject(f_name);
    DeleteObject(f_seal);
    DeleteObject(bmp);
    DeleteDC(dc);
    g_cur.w = w;
    g_cur.h = h + 2 * m;
    g_cur.unit = unit;
    return 0;
}

static int ensure_program(void) {
    char log[512];
    if (g_prog)
        return 0;
    if (glb_compile(k_vs, k_fs, &g_prog, log, sizeof log)) {
        ngb_log("toast: shader failed: %s", log);
        return -1;
    }
    g_u_rect = ngl_glGetUniformLocation(g_prog, "rect");
    g_u_alpha = ngl_glGetUniformLocation(g_prog, "alpha");
    ngl_glUseProgram(g_prog);
    ngl_glUniform1i(ngl_glGetUniformLocation(g_prog, "tex"), 0);
    ngl_glGenVertexArrays(1, &g_vao);
    return 0;
}

/* Takes the next queued toast, if any, and builds its texture for a picture `gh` pixels high. */
static void next_toast(int gh) {
    wchar_t label[96], name[128];
    int rare = 0, have = 0;
    AcquireSRWLockExclusive(&g_lock);
    if (g_count) {
        memcpy(label, g_queue[g_head].label, sizeof label);
        memcpy(name, g_queue[g_head].name, sizeof name);
        rare = g_queue[g_head].rare;
        g_head = (g_head + 1) % QUEUE;
        g_count--;
        have = 1;
    }
    ReleaseSRWLockExclusive(&g_lock);
    /* unit: 1/16 of a pixel per 480-line pixel, so 42 px at 480 lines and 94 at 1080 */
    if (!have || ensure_program() || build(gh * 16 / 480 > 8 ? gh * 16 / 480 : 8, label, name, rare))
        return;
    g_cur.rare = rare;
    g_cur.active = 1;
    QueryPerformanceCounter(&g_cur.t0);
    play_drum();
}

void toast_draw(int cw, int ch, const int r[4]) {
    LARGE_INTEGER now, freq;
    double t, total, u, alpha;
    int gw = r[2] - r[0], gh = r[3] - r[1], x0, top, m;
    float rect[4];
    GLfloat a;
    if (cw <= 0 || ch <= 0 || gw <= 0 || gh <= 0)
        return;
    if (!g_cur.active)
        next_toast(gh);
    if (!g_cur.active)
        return;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    t = (double)(now.QuadPart - g_cur.t0.QuadPart) / (double)freq.QuadPart;
    total = g_cur.rare ? 7.0 : 5.0;
    if (t >= total) {
        g_cur.active = 0;
        return;
    }
    u = t < T_IN ? t / T_IN : 1.0;
    u = 1.0 - (1.0 - u) * (1.0 - u) * (1.0 - u); /* ease out */
    alpha = t < T_IN ? u : t > total - T_OUT ? (total - t) / T_OUT : 1.0;
    m = ((42 * g_cur.unit + 8) / 16) / 2; /* the texture's margin around the panel */
    x0 = (r[0] + r[2]) / 2 - g_cur.w / 2;
    top = r[3] - 22 * g_cur.unit / 16 - (g_cur.h - m) + (int)((1.0 - u) * 12.0 * g_cur.unit / 16.0);
    rect[0] = 2.0f * (float)x0 / (float)cw - 1.0f;
    rect[1] = 1.0f - 2.0f * (float)(top + g_cur.h) / (float)ch;
    rect[2] = 2.0f * (float)(x0 + g_cur.w) / (float)cw - 1.0f;
    rect[3] = 1.0f - 2.0f * (float)top / (float)ch;
    glViewport(0, 0, cw, ch);
    glEnable(GL_SCISSOR_TEST); /* never outside the game picture */
    glScissor(r[0], ch - r[3], gw, gh);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    ngl_glBlendEquation(0x8006 /* GL_FUNC_ADD */);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ngl_glUseProgram(g_prog);
    ngl_glUniform4fv(g_u_rect, 1, rect);
    a = (GLfloat)alpha;
    ngl_glUniform1fv(g_u_alpha, 1, &a);
    ngl_glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_cur.tex);
    ngl_glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    ngl_glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
}
