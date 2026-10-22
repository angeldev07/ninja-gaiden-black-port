/*
 * ui.h: the launcher's immediate-mode toolkit (Win32 + GDI, no controls).
 *
 * The window is one back buffer (a 32-bit DIB). Every frame starts from a cached backdrop
 * (see art.c), then widgets draw themselves and register their click zones in the same call,
 * so drawing and hit-testing cannot disagree. Positions are logical pixels (96 dpi) and are
 * scaled with S() to the system DPI.
 */
#pragma once

#include <stdint.h>
#include <windows.h>

#include "art.h"

typedef uint32_t rgb_t;

/* Palette 0xRRGGBB: black and charcoal, cold steel text, red accents. */
#define C_MAIN 0x0b0b0e
#define C_CARD 0x141419
#define C_CARD_HOT 0x1d1d24
#define C_FIELD 0x09090c
#define C_LINE 0x2c2c35
#define C_STEEL 0xc4cad6
#define C_TEXT 0xe6e8ee
#define C_DIM 0x8f93a0
#define C_FAINT 0x5d606b
#define C_RED 0xd01c26
#define C_RED_HOT 0xff3a42
#define C_OK 0x5fbf78
#define C_WARN 0xe0a840
#define C_BAD 0xe0584e

enum font_id {
    F_BODY, F_SMALL, F_BOLD, F_TINY, F_H1, F_H2, F_PLAY, F_MONO, F_TAG, F_COUNT
};

#define T_LINE (DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS)

extern int g_dpi;
extern int g_hover, g_down;

int S(int v);
int to_logical(int v);
rgb_t lerp_rgb(rgb_t a, rgb_t b, int t256);

/* Lifecycle. ui_begin starts a frame: clears the click zones and copies the backdrop. */
void ui_init(int dpi);
void ui_begin(int backdrop_kind);
void ui_end(void);
void ui_present(HDC dc);
int ui_save_bmp(const wchar_t *path);

/* Shapes (logical coordinates). */
void fill_rect(int x, int y, int w, int h, rgb_t c);
void fill_rect_a(int x, int y, int w, int h, rgb_t c, int alpha);
void fill_rrect(int x, int y, int w, int h, int r, rgb_t c);
void fill_rrect_a(int x, int y, int w, int h, int r, rgb_t c, int alpha);
void fill_rrect_grad(int x, int y, int w, int h, int r, rgb_t c1, rgb_t c2);
void stroke_rrect(int x, int y, int w, int h, int r, rgb_t c);
void fill_circle(int cx, int cy, int r, rgb_t c);
void capsule(float ax, float ay, float bx, float by, float thick, rgb_t col);
void status_dot(int x, int y, rgb_t c);
/* Restricts GDI text to a box until ui_unclip. */
void ui_clip(int x, int y, int w, int h);
void ui_unclip(void);
/* Charcoal panel with a hairline border and a red tick on its top edge. */
void ui_card(int x, int y, int w, int h);

/* Text. */
void txt(int font, rgb_t col, int x, int y, int w, int h, UINT fmt, const wchar_t *s);
int text_w(int font, const wchar_t *s);
/* Text filled with a vertical chrome gradient and a red halo (headings). */
void txt_chrome(int font, int x, int y, int w, int h, UINT fmt, int red_style, int spacing,
                const wchar_t *s);
void caption(int x, int y, int w, const wchar_t *s);
void caption_col(int x, int y, int w, rgb_t col, const wchar_t *s);

/* Click zones. */
void add_hit(int x, int y, int w, int h, int id);
int hit_at(int lx, int ly);
int hot(int id);
int pressed(int id);

/* Widgets. style: 0 steel button, 1 red plate (primary), 2 plain text link. */
void ui_button(int id, int x, int y, int w, int h, const wchar_t *label, int style, int enabled);
/* Big glowing red steel plate (PLAY / IMPORT). */
void ui_plate(int id, int x, int y, int w, int h, const wchar_t *label, int enabled);
void ui_progress(int x, int y, int w, int h, int permille, int active);
void ui_checkbox(int id, int x, int y, int on);
void ui_chevron(int x, int y);
