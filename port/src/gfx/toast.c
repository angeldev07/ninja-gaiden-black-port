/*
 * toast.c: see toast.h.
 *
 * toast_show() only copies the strings and bumps a generation counter. The render thread sees
 * the new generation in toast_draw(), draws the panel with GDI into a DIB at the size it will
 * have on screen (so the text is sharp at any window size), uploads it as a texture and
 * composites it with a textured quad. One toast at a time: a newer one replaces the one shown.
 */
#include "toast.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../ngb.h"
#include "../gl/glbackend.h"
#include "../gl/glload.h"

#define TOAST_IN 0.35    /* seconds to slide in */
#define TOAST_OUT 0.40   /* seconds to fade out */
#define TOAST_TOTAL 5.0  /* seconds on screen, fade included */

static SRWLOCK g_lock = SRWLOCK_INIT;
static wchar_t g_title[64], g_text[128];
static volatile LONG g_gen;

/* Render thread only. */
static LONG g_drawn_gen;
static int g_active, g_tex_w, g_tex_h;
static GLuint g_tex, g_prog, g_vao;
static GLint g_u_rect, g_u_alpha;
static LARGE_INTEGER g_t0;

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

void toast_show(const wchar_t *title, const wchar_t *text) {
    AcquireSRWLockExclusive(&g_lock);
    wcsncpy_s(g_title, sizeof g_title / sizeof g_title[0], title, _TRUNCATE);
    wcsncpy_s(g_text, sizeof g_text / sizeof g_text[0], text, _TRUNCATE);
    ReleaseSRWLockExclusive(&g_lock);
    InterlockedIncrement(&g_gen);
}

static HFONT font(int px, int weight) {
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

/* Dark lacquer panel, a vermilion bar on the left, a small label and the achievement name. */
static int build_texture(int h) {
    wchar_t title[64], text[128];
    BITMAPINFO bi;
    void *bits = NULL;
    HDC dc;
    HBITMAP bmp;
    HGDIOBJ old;
    HFONT small, big;
    HBRUSH bg, bar;
    RECT all, r;
    SIZE ext;
    int w, i;
    AcquireSRWLockShared(&g_lock);
    memcpy(title, g_title, sizeof title);
    memcpy(text, g_text, sizeof text);
    ReleaseSRWLockShared(&g_lock);
    if ((dc = CreateCompatibleDC(NULL)) == NULL)
        return -1;
    small = font(h * 22 / 100, FW_NORMAL);
    big = font(h * 32 / 100, FW_SEMIBOLD);
    old = SelectObject(dc, big); /* the panel grows with the name, between 3 and 8 heights */
    if (!GetTextExtentPoint32W(dc, text, (int)wcslen(text), &ext))
        ext.cx = h * 4;
    w = ext.cx + h / 4 + h / 3;
    w = w < h * 3 ? h * 3 : w > h * 8 ? h * 8 : w;
    SelectObject(dc, old);
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) {
        DeleteObject(small);
        DeleteObject(big);
        DeleteDC(dc);
        return -1;
    }
    old = SelectObject(dc, bmp);
    SetRect(&all, 0, 0, w, h);
    bg = CreateSolidBrush(RGB(22, 17, 19));
    FillRect(dc, &all, bg);
    SetRect(&r, 0, 0, h / 12 + 1, h);
    bar = CreateSolidBrush(RGB(214, 58, 38));
    FillRect(dc, &r, bar);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, small);
    SetTextColor(dc, RGB(200, 184, 160));
    SetRect(&r, h / 4, h * 14 / 100, w - h / 6, h / 2);
    DrawTextW(dc, title, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, big);
    SetTextColor(dc, RGB(244, 238, 228));
    SetRect(&r, h / 4, h * 42 / 100, w - h / 6, h);
    DrawTextW(dc, text, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    GdiFlush();
    for (i = 0; i < w * h; i++) /* GDI leaves alpha at 0: the panel is opaque, the fade is a uniform */
        ((uint8_t *)bits)[i * 4 + 3] = 0xF0;
    if (!g_tex)
        glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, bits);
    SelectObject(dc, old);
    DeleteObject(small);
    DeleteObject(big);
    DeleteObject(bg);
    DeleteObject(bar);
    DeleteObject(bmp);
    DeleteDC(dc);
    g_tex_w = w;
    g_tex_h = h;
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

void toast_draw(int cw, int ch, const int r[4]) {
    LONG gen = g_gen;
    LARGE_INTEGER now, freq;
    double t, u, slide, alpha;
    int gw = r[2] - r[0], gh = r[3] - r[1], margin, x0, top;
    float rect[4];
    GLfloat a;
    if (cw <= 0 || ch <= 0 || gw <= 0 || gh <= 0)
        return;
    if (gen != g_drawn_gen) {
        g_drawn_gen = gen;
        g_active = !ensure_program() && !build_texture(gh * 13 / 100 > 24 ? gh * 13 / 100 : 24);
        QueryPerformanceCounter(&g_t0);
    }
    if (!g_active)
        return;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    t = (double)(now.QuadPart - g_t0.QuadPart) / (double)freq.QuadPart;
    if (t >= TOAST_TOTAL) {
        g_active = 0;
        return;
    }
    u = t < TOAST_IN ? t / TOAST_IN : 1.0;
    slide = 1.0 - (1.0 - u) * (1.0 - u) * (1.0 - u); /* ease out */
    alpha = t > TOAST_TOTAL - TOAST_OUT ? (TOAST_TOTAL - t) / TOAST_OUT : 1.0;
    margin = gh * 6 / 100;
    x0 = (r[0] + r[2]) / 2 - g_tex_w / 2;
    top = r[3] - margin - g_tex_h + (int)((1.0 - slide) * (double)(g_tex_h + margin));
    rect[0] = 2.0f * (float)x0 / (float)cw - 1.0f;
    rect[1] = 1.0f - 2.0f * (float)(top + g_tex_h) / (float)ch;
    rect[2] = 2.0f * (float)(x0 + g_tex_w) / (float)cw - 1.0f;
    rect[3] = 1.0f - 2.0f * (float)top / (float)ch;
    glViewport(0, 0, cw, ch);
    glEnable(GL_SCISSOR_TEST); /* it slides in from the bottom edge of the picture, not of the window */
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
    glBindTexture(GL_TEXTURE_2D, g_tex);
    ngl_glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    ngl_glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
}
