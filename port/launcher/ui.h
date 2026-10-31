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

/* "Lacquer and ink" tokens (docs/target/launcher-laca-design.md). One accent: vermilion.
 * muted, dim and sep are cream at 56 %, 36 % and 10 % pre-blended over bg. */
#define C_BG 0x13110f
#define C_CREAM 0xefe7d6
#define C_MUTED 0x8f8a80
#define C_DIMC 0x625d56
#define C_SEP 0x2a2724
#define C_VERM 0xd23f2f
#define C_VERM_HOT 0xdc4a3a
#define C_VERM_DOWN 0xb8362a
#define C_OKC 0x8fb08a
#define C_WARNC 0xd9a648
#define C_BADC 0xd8584c

enum font_id {
    F_BODY, F_SMALL, F_BOLD, F_TINY, F_H1, F_H2, F_PLAY, F_MONO, F_TAG,
    /* lacquer set: Josefin Sans (embedded) and Yu Mincho for kanji */
    F_LBODY,   /* Josefin 300, 14 */
    F_LSMALL,  /* Josefin 300, 12.5 */
    F_LABEL,   /* Josefin 600, 10.5 (tracked uppercase micro labels) */
    F_TAB,     /* Josefin 600, 11 */
    F_VALUE,   /* Josefin 300, 16 */
    F_DISPLAY, /* Josefin 100, 88 (Play title) */
    F_PAGE,    /* Josefin 100, 46 (inner page titles) */
    F_HEAD,    /* Josefin 100, 54 (first-run headline) */
    F_BTN,     /* Josefin 600, 15 */
    F_GHOST,   /* Josefin 100, 128 (ghost page number) */
    F_KANJI,   /* Yu Mincho 600, 16 */
    F_COUNT
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
/* Text with letter-spacing of `em` times the font size (GDI character extra). */
void txt_tracked(int font, rgb_t col, int x, int y, int h, float em, const wchar_t *s);
int tracked_w(int font, float em, const wchar_t *s);
/* 1 if a Mincho face is available for the kanji ornaments (they are skipped otherwise). */
int ui_has_kanji(void);
/* Face actually selected for a font (for diagnostics). */
void ui_font_face(int font, wchar_t *out, int n);
/* Set before ui_init: skip loading the embedded fonts (tests the fallback). */
extern int g_no_embedded_fonts;

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

/* Lacquer widgets (docs/target/launcher-laca-design.md). Square, flat, one accent. */
void ui_hair(int x, int y, int w);  /* 1 px horizontal hairline, cream at 10 % */
void ui_vhair(int x, int y, int h); /* vertical hairline */
/* Tracked uppercase micro label (Josefin 600, 0.26 em). */
void ui_label(int x, int y, const wchar_t *s, rgb_t col);
/* Lowers the tracking of `s` (down to 0.08 em) until it fits max_w; 0 if it still does not. */
int ui_fit_tracked(int font, float em, const wchar_t *s, int max_w, float *em_out);
/* Top-bar tab: uppercase, tracked; a 1 px vermilion rule under the active one. Returns its width. */
int ui_tab(int id, int x, int y, int h, const wchar_t *label, int active);
/* Primary action: vermilion plate with a 1 px inner frame, tracked label and an optional kanji. */
void ui_primary(int id, int x, int y, int w, int h, const wchar_t *label, const wchar_t *kanji,
                int enabled);
/* Secondary action: a 1 px cream outline. */
void ui_secondary(int id, int x, int y, int w, int h, const wchar_t *label, int enabled);
/* Vermilion text link (micro label). Returns its width. */
int ui_link(int id, int x, int y, const wchar_t *label, int enabled);
/* Square 44 x 22 switch. */
void ui_switch(int id, int x, int y, int on, int enabled);
/* 2 px progress rule. */
void ui_rule_progress(int x, int y, int w, int permille);
/* Small down caret (8 x 4) for drop-downs. */
void ui_caret(int x, int y, rgb_t col);
