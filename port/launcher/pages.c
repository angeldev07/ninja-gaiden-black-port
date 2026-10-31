/*
 * pages.c: painting of every page and state of the launcher (docs/target/launcher-laca-design.md).
 *
 * Immediate mode: each frame starts from the cached backdrop (art.c) and every widget registers
 * its click zone as it draws (ui.c). The logic and the state live in launcher.c
 * (launcher_int.h); nothing here changes settings.
 *
 * Layout, in logical pixels (1240 x 700):
 *   - top bar 0..64: crest and wordmark, the six sections, music toggle, ES / EN;
 *   - Play: the hero fills the window; title block at x 92; bottom bar 588..700 with the
 *     settings at a glance and the primary action at (964, 614, 232 x 60);
 *   - inner pages: left column 0..310 (ghost number, title, kanji, one sentence) and hairline
 *     rows from x 374 to 1176.
 */
#include "launcher_int.h"

#include <stdio.h>
#include <string.h>

#include "art.h"
#include "gameart.h"
#include "kamon.h"
#include "ui.h"
#include "update.h"
#include "version.h"

#define TOP_H ART_TOP_H
#define BAR_Y ART_BAR_Y
#define TX 92 /* text column of the Play page */
#define LX 374 /* list of the inner pages */
#define LW 802
#define ROW 80
#define PRIMARY_X 964
#define PRIMARY_Y 614
#define GHOST 0x1f1d1a /* cream at 5.5 % over the lacquer */
#define C_DESC 0x77726a /* cream at 42 %: row descriptions */

static int g_ovl_x, g_ovl_y, g_ovl_w;

static const int k_page_names[PG_COUNT] = {S_NAV_PLAY,     S_NAV_GRAPHICS, S_NAV_AUDIO,
                                           S_NAV_CONTROLS, S_NAV_ADVANCED, S_NAV_ABOUT};
static const wchar_t *const k_page_kanji[PG_COUNT] = {L"始", L"画", L"音", L"操", L"設", L"記"};

/* ------------------------------------------------------------------ helpers ---- */

static void kicker(int x, int y, const wchar_t *s) {
    fill_rect(x, y + 6, 36, 1, C_VERM);
    ui_label(x + 50, y, s, C_VERM);
}

static void vertical_kanji(int x, int y, const wchar_t *s) {
    wchar_t one[2] = {0, 0};
    if (!ui_has_kanji())
        return;
    for (; *s; s++, y += 24) {
        one[0] = *s;
        txt(F_KANJI, C_VERM, x, y, 24, 24, DT_SINGLELINE | DT_CENTER | DT_VCENTER, one);
    }
}

static void dot(int x, int y, rgb_t c) { fill_circle(x, y, 3, c); }

/* A thin frame (the drop hint, the command line box). */
static void hair_box(int x, int y, int w, int h, int alpha) {
    fill_rect_a(x, y, w, 1, C_CREAM, alpha);
    fill_rect_a(x, y + h - 1, w, 1, C_CREAM, alpha);
    fill_rect_a(x, y + 1, 1, h - 2, C_CREAM, alpha);
    fill_rect_a(x + w - 1, y + 1, 1, h - 2, C_CREAM, alpha);
}

/* ------------------------------------------------------------------ top bar ---- */

static void top_bar(void) {
    wchar_t num[8];
    int i, x = 330, rx;
    fill_rect_a(0, 0, LOGICAL_W, TOP_H, C_BG, 200);
    ui_hair(0, TOP_H - 1, LOGICAL_W);
    kamon_draw(58, 32, 14, C_VERM);
    txt_tracked(F_TAB, C_CREAM, 86, 18, 28, 0.34f, L"NINJA GAIDEN");
    txt_tracked(F_TAB, C_VERM, 86 + tracked_w(F_TAB, 0.34f, L"NINJA GAIDEN") + 10, 18, 28, 0.34f, L"BLACK");
    for (i = 0; i < PG_COUNT; i++)
        x += ui_tab(ID_NAV0 + i, x, 0, TOP_H, tr(k_page_names[i]), g_page == i) + 30;
    /* ES / EN, right-aligned */
    rx = LOGICAL_W - 44;
    {
        int we = tracked_w(F_TAB, 0.2f, L"EN"), ws = tracked_w(F_TAB, 0.2f, L"ES"), wsl = tracked_w(F_TAB, 0.2f, L"/");
        int xe = rx - we, xsl = xe - 12 - wsl, xs = xsl - 12 - ws;
        txt_tracked(F_TAB, g_lang == LANG_EN ? C_CREAM : hot(ID_LANG_EN) ? C_MUTED : C_DIMC, xe, 18, 28, 0.2f, L"EN");
        txt_tracked(F_TAB, C_DIMC, xsl, 18, 28, 0.2f, L"/");
        txt_tracked(F_TAB, g_lang == LANG_ES ? C_CREAM : hot(ID_LANG_ES) ? C_MUTED : C_DIMC, xs, 18, 28, 0.2f, L"ES");
        add_hit(xe - 6, 12, we + 12, 40, ID_LANG_EN);
        add_hit(xs - 6, 12, ws + 12, 40, ID_LANG_ES);
        rx = xs - 36;
    }
    /* music toggle, only where there is music */
    if (game_art_on()) {
        wchar_t u[32];
        int w;
        wfmt(u, 32, L"%ls", tr(S_TOP_MUSIC));
        CharUpperBuffW(u, (DWORD)wcslen(u));
        w = tracked_w(F_LABEL, 0.26f, u);
        txt_tracked(F_LABEL, g_set.music ? C_CREAM : hot(ID_TOP_MUSIC) ? C_MUTED : C_DIMC, rx - w, 18, 28, 0.26f, u);
        if (g_set.music)
            fill_rect(rx - w, 40, w - 3, 1, C_VERM);
        add_hit(rx - w - 6, 12, w + 12, 40, ID_TOP_MUSIC);
    }
    (void)num;
}

/* ------------------------------------------------------------------ play page ---- */

static void bottom_bar(void) {
    fill_rect_a(0, BAR_Y, LOGICAL_W, LOGICAL_H - BAR_Y, 0x0e0c0b, 224);
    ui_hair(0, BAR_Y, LOGICAL_W);
}

/* One "label / value" column of the bottom bar; returns the x of the next one. */
static int spec_col(int x, const wchar_t *label, const wchar_t *value, const wchar_t *suffix, int last) {
    int w = tracked_w(F_LABEL, 0.26f, label), vw = text_w(F_VALUE, value) + (suffix ? text_w(F_VALUE, suffix) : 0);
    ui_label(x, BAR_Y + 32, label, C_DIMC);
    txt(F_VALUE, C_CREAM, x, BAR_Y + 54, vw + 4, 24, T_LINE, value);
    if (suffix)
        txt(F_VALUE, C_MUTED, x + text_w(F_VALUE, value), BAR_Y + 54, vw, 24, T_LINE, suffix);
    if (vw > w)
        w = vw;
    if (!last)
        ui_vhair(x + w + 40, BAR_Y + 30, 50);
    return x + w + 80;
}

static void play_specs(void) {
    wchar_t res[48], ar[16];
    int w, h, x = TX;
    const wchar_t *rend = g_set.renderer == 1 ? L"OpenGL 4.x" : g_set.renderer == 2 ? L"Software" : tr(S_SPEC_AUTO);
    scaled_size(&w, &h);
    wfmt(res, 48, L"%d × %d", w, h);
    wfmt(ar, 16, L"  ·  %ls", g_set.widescreen ? L"16:9" : L"4:3");
    x = spec_col(x, tr(S_SPEC_RENDER), rend, NULL, 0);
    x = spec_col(x, tr(S_SPEC_RES), res, ar, 0);
    x = spec_col(x, tr(S_SPEC_PACE), g_set.pacing ? L"60 fps" : tr(S_PACE_FREE), NULL, 0);
    (void)spec_col(x, tr(S_SPEC_VERSION), L"" NGB_VERSION_STR, NULL, 1);
}

/* Data folder: "FILES WILL BE SAVED IN", the path and CHANGE / OPEN, right-aligned in the bar. */
static void data_block(void) {
    int r = LOGICAL_W - 44, wl = tracked_w(F_LABEL, 0.26f, tr(S_CTA_SAVE_IN)), w2, w1;
    wchar_t up2[32], up1[32];
    ui_label(r - wl, BAR_Y + 24, tr(S_CTA_SAVE_IN), C_DIMC);
    txt(F_LSMALL, C_CREAM, r - 420, BAR_Y + 44, 420, 22, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_PATH_ELLIPSIS, g_data);
    wfmt(up2, 32, L"%ls", tr(S_BTN_OPEN));
    wfmt(up1, 32, L"%ls", tr(S_BTN_CHANGE));
    CharUpperBuffW(up2, (DWORD)wcslen(up2));
    CharUpperBuffW(up1, (DWORD)wcslen(up1));
    w2 = tracked_w(F_LABEL, 0.22f, up2);
    w1 = tracked_w(F_LABEL, 0.22f, up1);
    ui_link(ID_DATA_OPEN, r - w2, BAR_Y + 76, tr(S_BTN_OPEN), 1);
    ui_link(ID_DATA_CHANGE, r - w2 - 24 - w1, BAR_Y + 76, tr(S_BTN_CHANGE), !importing() && !running());
}

static int note_line(int x, int y, int w) {
    wchar_t t[400];
    if (g_note.kind == NK_NONE)
        return y;
    note_text(t, 400);
    dot(x + 3, y + 10, g_note.kind == NK_ERR ? C_BADC : C_OKC);
    txt(F_LSMALL, g_note.kind == NK_ERR ? C_BADC : C_MUTED, x + 16, y, w - 16, 40, DT_WORDBREAK, t);
    return y + 40;
}

static void play_ready(void) {
    wchar_t line[200], d[48], res[96];
    const ga_image_t *logo = game_art_on() ? gameart_get(GA_LOGO) : NULL;
    const ga_image_t *black = game_art_on() ? gameart_get(GA_LOGO_BLACK) : NULL;
    rgb_t dc;
    int y;
    vertical_kanji(50, 262, L"忍者外伝");
    if (logo) {
        /* drawn at 560 and 290 px wide; the heights follow the (trimmed) masks */
        ui_mask(66, 250, 560, 560 * logo->h / logo->w, logo->alpha, logo->w, logo->h, C_CREAM, 245);
        if (black)
            ui_mask(206, 456, 290, 290 * black->h / black->w, black->alpha, black->w, black->h, C_VERM, 255);
        y = 540;
    } else {
        kicker(TX, 256, tr(S_PLAY_TAGLINE));
        txt_tracked(F_DISPLAY, C_CREAM, 84, 286, 100, 0.1f, L"NINJA GAIDEN");
        txt_tracked(F_SUB, C_MUTED, 94, 392, 32, 1.0f, L"BLACK");
        y = 446;
    }
    /* status */
    if (running()) {
        fmt_duration((long)((GetTickCount64() - g_t0) / 1000), d, 48);
        wfmt(line, 200, tr(S_ST_RUNNING), (unsigned long)g_pid, d);
        dc = C_OKC;
    } else if (!loader_ok()) {
        wfmt(line, 200, L"%ls", tr(S_ST_NO_LOADER));
        dc = C_BADC;
    } else if (xbe_ok() && g_game.is_ngb) {
        wfmt(line, 200, L"%ls%ls", tr(S_GAME_VERIFIED), tr(g_game.region & 1u ? S_GAME_NTSC : S_GAME_PAL));
        dc = C_OKC;
    } else if (xbe_ok()) {
        wfmt(line, 200, tr(S_GAME_UNKNOWN), g_game.title);
        dc = C_WARNC;
    } else {
        wfmt(line, 200, L"%ls", g_game_exists ? tr(S_GAME_BAD_XBE) : tr(S_GAME_MISSING));
        dc = C_BADC;
    }
    dot(TX + 3, y + 10, dc);
    txt(F_LSMALL, C_MUTED, TX + 16, y, 700, 20, T_LINE, line);
    y += 26;
    if (!running() && g_last.code != SESSION_NONE) {
        wchar_t dur[48];
        int w;
        exit_text(g_last.code, res, 96);
        fmt_duration(g_last.duration_s, dur, 48);
        wfmt(line, 200, tr(S_LAST_LINE), g_last.start, dur, res);
        w = text_w(F_LSMALL, line);
        txt(F_LSMALL, C_DIMC, TX + 16, y, w + 4, 20, T_LINE, line);
        if (g_last.log[0])
            ui_link(ID_VIEW_LOG, TX + 16 + w + 18, y + 2, tr(S_BTN_VIEW_LOG), 1);
        y += 26;
    }
    note_line(TX, y, 640);
    bottom_bar();
    if (can_play()) { /* beside the button, where the eye already is */
        int w = tracked_w(F_LABEL, 0.26f, tr(S_HINT_ENTER));
        ui_label(PRIMARY_X - 28 - w, PRIMARY_Y + 23, tr(S_HINT_ENTER), C_DIMC);
    }
    play_specs();
    if (running())
        ui_primary(ID_STOP, PRIMARY_X, PRIMARY_Y, 232, 60, tr(S_BTN_STOP), NULL, 1);
    else
        ui_primary(ID_PLAY, PRIMARY_X, PRIMARY_Y, 232, 60, tr(S_BTN_PLAY), L"始", can_play());
}

static void play_first_run(void) {
    static const int steps[3] = {S_CTA_STEP1, S_CTA_STEP2, S_CTA_STEP3};
    static const wchar_t *const nums[3] = {L"一", L"二", L"三"};
    int i, x = TX, y;
    vertical_kanji(50, 226, L"忍者外伝");
    kicker(TX, 212, tr(S_FR_KICKER));
    txt_tracked(F_HEAD, C_CREAM, 88, 234, 66, 0.06f, tr(S_FR_TITLE));
    txt(F_LBODY, C_MUTED, TX, 312, 500, 70, DT_WORDBREAK, tr(S_CTA_BODY));
    for (i = 0; i < 3; i++) {
        wchar_t n[4];
        if (ui_has_kanji()) {
            txt(F_KANJI, C_VERM, x, 390, 22, 24, DT_SINGLELINE | DT_VCENTER, nums[i]);
        } else {
            wfmt(n, 4, L"%d", i + 1);
            txt(F_LABEL, C_VERM, x, 390, 22, 24, DT_SINGLELINE | DT_VCENTER, n);
        }
        txt(F_LSMALL, C_MUTED, x + 26, 390, 260, 24, T_LINE, tr(steps[i]));
        x += 26 + text_w(F_LSMALL, tr(steps[i])) + 34;
    }
    hair_box(TX, 432, 500, 44, 46);
    txt(F_LSMALL, C_DIMC, TX + 18, 432, 470, 44, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS, tr(S_CTA_DROP));
    y = note_line(TX, 490, 560);
    txt(F_LSMALL, C_DIMC, TX, y > 520 ? y : 530, 560, 40, DT_WORDBREAK, tr(S_LEGAL));
    bottom_bar();
    ui_primary(ID_IMPORT_ISO, TX, PRIMARY_Y, 250, 60, tr(S_CTA_BTN_ISO), NULL, 1);
    ui_secondary(ID_IMPORT_FOLDER, TX + 266, PRIMARY_Y + 1, 220, 58, tr(S_CTA_BTN_FOLDER), 1);
    data_block();
}

static void play_importing(const imp_status_t *st) {
    wchar_t buf[300], line[400];
    int permille = st->total > 0 ? (int)(st->done * 1000 / st->total) : 0;
    double gb = 1024.0 * 1024.0 * 1024.0;
    vertical_kanji(50, 226, L"忍者外伝");
    kicker(TX, 212, tr(S_IMP_KICKER));
    txt_tracked(F_HEAD, C_CREAM, 88, 234, 66, 0.06f, tr(S_IMP_HEAD));
    if (st->phase == IMPH_SCAN)
        wfmt(line, 400, L"%ls", tr(S_IMP_SCAN));
    else if (st->phase == IMPH_FINISH)
        wfmt(line, 400, L"%ls", tr(S_IMP_FINISH));
    else
        wfmt(line, 400, tr(S_IMP_EXTRACT), st->current);
    txt(F_LSMALL, C_MUTED, TX, 316, 560, 22, DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS, line);
    ui_rule_progress(TX, 352, 560, permille);
    wfmt(buf, 300, tr(S_IMP_AMOUNT), permille / 10, (double)st->done / gb, (double)st->total / gb);
    txt(F_VALUE, C_CREAM, TX, 366, 300, 26, T_LINE, buf);
    if (st->phase == IMPH_EXTRACT && st->elapsed_ms > 3000 && st->done > 0) {
        double mbs = ((double)st->done / 1048576.0) / ((double)st->elapsed_ms / 1000.0);
        double left_s = ((double)(st->total - st->done) / 1048576.0) / (mbs > 0.01 ? mbs : 0.01);
        if (left_s < 60.0)
            wfmt(buf, 300, tr(S_IMP_SPEED_SEC), mbs);
        else
            wfmt(buf, 300, tr(S_IMP_SPEED), mbs, (long)(left_s / 60.0 + 0.5));
        txt(F_LSMALL, C_DIMC, TX + 260, 368, 300, 24, DT_SINGLELINE | DT_VCENTER | DT_RIGHT, buf);
    }
    txt(F_LSMALL, C_DIMC, TX, 410, 560, 22, T_LINE, tr(S_IMP_HINT));
    bottom_bar();
    ui_secondary(ID_IMPORT_CANCEL, TX, PRIMARY_Y + 1, 180, 58, tr(S_BTN_CANCEL), 1);
    data_block();
}

/* Band under the top bar: a newer release, its download, a failure, or the update that just
 * finished. Nothing when the check found nothing (or failed: offline is not news). */
static void update_band(void) {
    upd_status_t us;
    wchar_t line[200];
    int y = TOP_H, h = 44, r = LOGICAL_W - 44;
    upd_status(&us);
    if (!(us.state == UPD_AVAILABLE || us.state == UPD_DOWNLOADING || us.state == UPD_READY ||
          (us.state == UPD_ERROR && g_upd_wanted) || g_upd_fail[0] || g_updated_to[0]))
        return;
    fill_rect_a(0, y, LOGICAL_W, h, 0x1d1915, 235);
    ui_hair(0, y + h, LOGICAL_W);
    if (g_updated_to[0] && us.state != UPD_AVAILABLE) {
        wfmt(line, 200, tr(S_UPD_DONE), g_updated_to);
        dot(TX + 3, y + h / 2, C_OKC);
        txt(F_LSMALL, C_CREAM, TX + 16, y, 800, h, T_LINE, line);
        return;
    }
    if (us.state == UPD_DOWNLOADING) {
        int permille = us.total > 0 ? (int)(us.done * 1000 / us.total) : 0;
        wfmt(line, 200, tr(S_UPD_DOWNLOADING), us.man.version, permille / 10);
        txt(F_LSMALL, C_CREAM, TX, y, 500, h, T_LINE, line);
        ui_rule_progress(r - 320, y + h / 2, 320, permille);
        return;
    }
    if (g_upd_fail[0] || us.state == UPD_ERROR) {
        wfmt(line, 200, L"%ls %ls", tr(S_UPD_FAILED), g_upd_fail[0] ? g_upd_fail : tr(S_UPD_RETRY));
        dot(TX + 3, y + h / 2, C_WARNC);
    } else {
        wfmt(line, 200, tr(S_UPD_AVAILABLE), us.man.version);
        dot(TX + 3, y + h / 2, C_VERM);
    }
    txt(F_LSMALL, C_CREAM, TX + 16, y, 700, h, T_LINE, line);
    {
        wchar_t u[32];
        int wg;
        wfmt(u, 32, L"%ls", tr(S_UPD_GO));
        CharUpperBuffW(u, (DWORD)wcslen(u));
        wg = tracked_w(F_LABEL, 0.22f, u);
        ui_link(ID_UPD_GO, r - wg, y + 16, tr(S_UPD_GO),
                !running() && !importing() && (us.state != UPD_READY || g_upd_fail[0]));
        wfmt(u, 32, L"%ls", tr(S_UPD_NOTES));
        CharUpperBuffW(u, (DWORD)wcslen(u));
        ui_link(ID_UPD_NOTES, r - wg - 28 - tracked_w(F_LABEL, 0.22f, u), y + 16, tr(S_UPD_NOTES), 1);
    }
}

static void page_play(void) {
    imp_status_t st;
    get_import(&st);
    if (st.state == IMP_RUNNING)
        play_importing(&st);
    else if (!xbe_ok() && !g_game_exists)
        play_first_run();
    else
        play_ready();
    if (st.state != IMP_RUNNING)
        update_band();
}

/* -------------------------------------------------------------- inner pages ---- */

static void page_side(int number, const wchar_t *title, const wchar_t *kanji, const wchar_t *desc) {
    wchar_t num[8];
    ui_vhair(ART_SIDE_W, TOP_H, LOGICAL_H - TOP_H);
    wfmt(num, 8, L"%02d", number);
    txt(F_GHOST, GHOST, 76, 76, 230, 130, DT_SINGLELINE | DT_VCENTER, num);
    {
        float em; /* long titles ("Acerca de") give up tracking before touching the column rule */
        ui_fit_tracked(F_PAGE, 0.06f, title, ART_SIDE_W - 88 - 18, &em);
        txt_tracked(F_PAGE, C_CREAM, 88, 176, 60, em, title);
    }
    if (kanji && ui_has_kanji())
        txt(F_KANJI, C_VERM, TX, 244, 40, 24, DT_SINGLELINE | DT_VCENTER, kanji);
    txt(F_LSMALL, C_MUTED, TX, 282, 190, 160, DT_WORDBREAK, desc);
}

static int group(int y, const wchar_t *s) {
    fill_rect(LX, y + 6, 22, 1, C_VERM);
    ui_label(LX + 34, y, s, C_VERM);
    ui_hair(LX, y + 27, LW);
    return y + 28;
}

static void row_base(int id, int y, int text_w_max, const wchar_t *label, const wchar_t *desc) {
    if (id && hot(id))
        fill_rect_a(LX - 16, y, LW + 32, ROW, C_CREAM, 8);
    txt(F_VALUE, C_CREAM, LX, y + 12, text_w_max, 24, T_LINE, label);
    txt(F_LSMALL, C_DESC, LX, y + 37, text_w_max, 40, DT_WORDBREAK | DT_END_ELLIPSIS, desc);
    ui_hair(LX, y + ROW - 1, LW);
}

static int row_switch(int id, int y, const wchar_t *label, const wchar_t *desc, int on, int enabled) {
    row_base(enabled ? id : 0, y, LW - 110, label, desc);
    ui_switch(id, LX + LW - 44, y + 29, on, enabled);
    if (enabled)
        add_hit(LX - 16, y, LW + 32, ROW, id);
    return y + ROW;
}

static const int k_renderer_ids[] = {S_GFX_R_AUTO, S_GFX_R_GL, S_GFX_R_SOFT};
static const int k_scale_ids[] = {S_GFX_SCALE_AUTO, S_GFX_SCALE_1, S_GFX_SCALE_2, S_GFX_SCALE_3, S_GFX_SCALE_4};

int combo_count(int id) {
    return id == ID_CB_RENDERER ? 3 : id == ID_CB_SCALE ? 5 : (int)(sizeof k_logcap_vals / sizeof k_logcap_vals[0]);
}

static const wchar_t *combo_text(int id, int i) {
    static const wchar_t *caps[] = {L"500", NULL, L"10 000", L"50 000"};
    if (id == ID_CB_RENDERER)
        return tr(k_renderer_ids[i]);
    if (id == ID_CB_SCALE && i == 0) { /* auto: with the size it gives on this monitor */
        static wchar_t text[96];
        int w, h, keep = g_set.scale;
        g_set.scale = 0;
        scaled_size(&w, &h);
        g_set.scale = keep;
        wfmt(text, 96, tr(S_GFX_SCALE_AUTO), w, h);
        return text;
    }
    if (id == ID_CB_SCALE)
        return tr(k_scale_ids[i]);
    return caps[i] ? caps[i] : tr(S_ADV_LOGCAP_DEF);
}

static int combo_sel(int id) {
    int i;
    if (id == ID_CB_RENDERER)
        return g_set.renderer;
    if (id == ID_CB_SCALE)
        return g_set.scale; /* 0 auto, then 1x..4x */
    for (i = 0; i < combo_count(id); i++)
        if (k_logcap_vals[i] == g_set.log_cap)
            return i;
    return 1;
}

static int row_combo(int id, int y, const wchar_t *label, const wchar_t *desc) {
    int open = g_open_combo == id, vw = 300, vx = LX + LW - vw;
    row_base(id, y, LW - vw - 30, label, desc);
    txt(F_LBODY, open || hot(id) ? C_CREAM : C_MUTED, vx, y + 12, vw - 24, 24,
        DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS, combo_text(id, combo_sel(id)));
    ui_caret(LX + LW - 10, y + 22, open ? C_VERM : C_DIMC);
    add_hit(LX - 16, y, LW + 32, ROW, id);
    if (open) {
        g_ovl_x = vx;
        g_ovl_y = y + ROW - 8;
        g_ovl_w = vw + 4;
    }
    return y + ROW;
}

/* The open drop-down list: painted and registered last, so it sits on top. */
static void paint_overlay(void) {
    int n, i, h = 34;
    if (g_open_combo == ID_NONE)
        return;
    n = combo_count(g_open_combo);
    fill_rect_a(g_ovl_x + 3, g_ovl_y + 5, g_ovl_w, n * h + 8, 0x000000, 120);
    fill_rect(g_ovl_x, g_ovl_y, g_ovl_w, n * h + 8, 0x1b1815);
    hair_box(g_ovl_x, g_ovl_y, g_ovl_w, n * h + 8, 46);
    for (i = 0; i < n; i++) {
        int iy = g_ovl_y + 4 + i * h, id = ID_ITEM0 + i;
        if (hot(id))
            fill_rect_a(g_ovl_x + 1, iy, g_ovl_w - 2, h, C_CREAM, 16);
        txt(F_LBODY, i == combo_sel(g_open_combo) ? C_VERM : C_CREAM, g_ovl_x + 16, iy, g_ovl_w - 32, h, T_LINE,
            combo_text(g_open_combo, i));
        add_hit(g_ovl_x, iy, g_ovl_w, h, id);
    }
}

static void page_graphics(void) {
    int y = 104;
    page_side(2, tr(S_NAV_GRAPHICS), k_page_kanji[PG_GRAPHICS], tr(S_GFX_SUB));
    y = group(y, tr(S_GFX_SECTION));
    y = row_combo(ID_CB_RENDERER, y, tr(S_GFX_RENDERER), tr(S_GFX_RENDERER_DESC));
    y = row_combo(ID_CB_SCALE, y, tr(S_GFX_SCALE), tr(S_GFX_SCALE_DESC));
    y = row_switch(ID_CHK_WIDESCREEN, y, tr(S_GFX_WIDE), tr(S_GFX_WIDE_DESC), g_set.widescreen, 1);
    y = row_switch(ID_CHK_FULLSCREEN, y, tr(S_GFX_FULL), tr(S_GFX_FULL_DESC), g_set.fullscreen, 1);
    y = row_switch(ID_CHK_PACING, y, tr(S_GFX_FPS), tr(S_GFX_FPS_DESC), g_set.pacing, 1);
    y = group(y + 14, tr(S_GFX_LAUNCHER));
    (void)row_switch(ID_CHK_GAMEART, y, tr(S_GFX_GAMEART), tr(S_GFX_GAMEART_DESC), g_set.game_art, xbe_ok());
}

static void page_audio(void) {
    int y = 104;
    page_side(3, tr(S_NAV_AUDIO), k_page_kanji[PG_AUDIO], tr(S_AUD_SUB));
    y = group(y, tr(S_AUD_SECTION));
    y = row_switch(ID_CHK_MUTE, y, tr(S_AUD_MUTE), tr(S_AUD_MUTE_DESC), g_set.mute, 1);
    y = row_switch(ID_CHK_WAV, y, tr(S_AUD_WAV), tr(S_AUD_WAV_DESC), g_set.audio_wav, 1);
    y = group(y + 14, tr(S_GFX_LAUNCHER));
    (void)row_switch(ID_CHK_MUSIC, y, tr(S_AUD_MUSIC), tr(S_AUD_MUSIC_DESC), g_set.music, game_art_on());
}

/* One row of the controls table: key caps (separated by '|') and what they do. */
static int control_row(int x, int y, int w, const wchar_t *keys, const wchar_t *what) {
    wchar_t one[24];
    const wchar_t *p = keys;
    int cx = x;
    while (*p) {
        size_t n = 0;
        int kw;
        while (p[n] && p[n] != L'|' && n < 22) {
            one[n] = p[n];
            n++;
        }
        one[n] = 0;
        kw = text_w(F_LSMALL, one) + 16;
        if (kw < 26)
            kw = 26;
        hair_box(cx, y + 3, kw, 22, 56);
        txt(F_LSMALL, C_CREAM, cx, y + 3, kw, 22, DT_SINGLELINE | DT_VCENTER | DT_CENTER, one);
        cx += kw + 5;
        p += n;
        if (*p == L'|')
            p++;
    }
    txt(F_LSMALL, C_MUTED, x + w * 46 / 100, y, w - w * 46 / 100, 28, T_LINE, what);
    return y + 29;
}

static void page_controls(void) {
    int cw = (LW - 48) / 2, x2 = LX + cw + 48, y;
    page_side(4, tr(S_NAV_CONTROLS), k_page_kanji[PG_CONTROLS], tr(S_CTL_SUB));
    ui_label(LX, 104, tr(S_CTL_KEYBOARD), C_VERM);
    ui_label(x2, 104, tr(S_CTL_PAD), C_VERM);
    ui_hair(LX, 131, cw);
    ui_hair(x2, 131, cw);
    y = 144;
    y = control_row(LX, y, cw, L"Enter", tr(S_CTL_START_A));
    y = control_row(LX, y, cw, L"Esc", tr(S_CTL_BACK_B));
    y = control_row(LX, y, cw, L"↑|↓|←|→", tr(S_CTL_DPAD));
    y = control_row(LX, y, cw, L"W|A|S|D", tr(S_CTL_LSTICK));
    y = control_row(LX, y, cw, L"J", L"A");
    y = control_row(LX, y, cw, L"K", L"B");
    y = control_row(LX, y, cw, L"L", L"X");
    y = control_row(LX, y, cw, L"I", L"Y");
    y = control_row(LX, y, cw, L"Q", tr(S_CTL_WHITE));
    y = control_row(LX, y, cw, L"E", tr(S_CTL_BLACK));
    y = control_row(LX, y, cw, L"U", tr(S_CTL_LTRIG));
    y = control_row(LX, y, cw, L"O", tr(S_CTL_RTRIG));
    y = control_row(LX, y, cw, L"F12", tr(S_CTL_SHOT));
    (void)control_row(LX, y, cw, L"F11", tr(S_CTL_RDOC));
    y = 144;
    y = control_row(x2, y, cw, L"A|B|X|Y", L"A, B, X, Y");
    y = control_row(x2, y, cw, L"LB", tr(S_CTL_WHITE));
    y = control_row(x2, y, cw, L"RB", tr(S_CTL_BLACK));
    y = control_row(x2, y, cw, L"LT|RT", tr(S_CTL_TRIGS));
    y = control_row(x2, y, cw, tr(S_CTL_KEY_LSTICK), tr(S_CTL_LSTICK));
    y = control_row(x2, y, cw, tr(S_CTL_KEY_RSTICK), tr(S_CTL_RSTICK));
    y = control_row(x2, y, cw, L"L3|R3", tr(S_CTL_CLICK));
    y = control_row(x2, y, cw, tr(S_CTL_DPAD), tr(S_CTL_DPAD));
    y = control_row(x2, y, cw, L"Start", L"Start");
    y = control_row(x2, y, cw, L"Back", L"Back");
    txt(F_LSMALL, C_DIMC, x2, y + 14, cw, 60, DT_WORDBREAK, tr(S_CTL_BOTH));
}

static void page_advanced(void) {
    int y = 104, x = LX, b1, b2, b3, bh;
    wchar_t cmd[4096];
    const wchar_t *copy_label = g_copied_until ? tr(S_BTN_COPIED) : tr(S_BTN_COPY_INFO);
    page_side(5, tr(S_NAV_ADVANCED), k_page_kanji[PG_ADVANCED], tr(S_ADV_SUB));
    ui_hair(LX, y - 1, LW);
    y = row_switch(ID_CHK_UPDATES, y, tr(S_ADV_UPDATES), tr(S_ADV_UPDATES_DESC), g_set.check_updates, 1);
    y = group(y + 14, tr(S_ADV_DEBUG));
    y = row_switch(ID_CHK_RDOC, y, tr(S_ADV_RDOC), tr(S_ADV_RDOC_DESC), g_set.rdoc_key, 1);
    y = row_switch(ID_CHK_LENIENT, y, tr(S_ADV_LENIENT), tr(S_ADV_LENIENT_DESC), g_set.lenient, 1);
    y = row_switch(ID_CHK_DETAIL, y, tr(S_ADV_DETAIL), tr(S_ADV_DETAIL_DESC), g_set.detailed_log, 1);
    if (g_set.detailed_log) /* the cap only matters for the full log */
        y = row_combo(ID_CB_LOGCAP, y, tr(S_ADV_LOGCAP), tr(S_ADV_LOGCAP_DESC));
    /* The copy button keeps its width when its label turns into "Copied". */
    b1 = tracked_w(F_TAB, 0.3f, tr(S_BTN_VIEW_LOG)) + 56;
    b2 = tracked_w(F_TAB, 0.3f, tr(S_BTN_OPEN_RUNS)) + 56;
    b3 = tracked_w(F_TAB, 0.3f, tr(S_BTN_COPY_INFO)) + 56;
    if (tracked_w(F_TAB, 0.3f, tr(S_BTN_COPIED)) + 56 > b3)
        b3 = tracked_w(F_TAB, 0.3f, tr(S_BTN_COPIED)) + 56;
    y += 16;
    ui_secondary(ID_VIEW_LOG, x, y, b1, 40, tr(S_BTN_VIEW_LOG), 1);
    ui_secondary(ID_OPEN_RUNS, x + b1 + 12, y, b2, 40, tr(S_BTN_OPEN_RUNS), 1);
    ui_secondary(ID_COPY_INFO, x + b1 + b2 + 24, y, b3, 40, copy_label, 1);
    y += 40 + 16;
    bh = LOGICAL_H - 24 - (y + 22); /* what is left; the box shrinks when the combo is shown */
    if (bh > 70)
        bh = 70;
    if (bh < 34)
        return; /* no room with the log-cap combo: the command line stays hidden */
    build_cmdline(cmd, 4096);
    ui_label(LX, y, tr(S_ADV_CMDLINE), C_DIMC);
    hair_box(LX, y + 22, LW, bh, 36);
    txt(F_MONO, C_MUTED, LX + 14, y + 28, LW - 28, bh - 12, DT_WORDBREAK | DT_END_ELLIPSIS, cmd);
}

static rgb_t log_line_color(const wchar_t *line) {
    wchar_t low[160];
    size_t i;
    for (i = 0; i < 159 && line[i]; i++)
        low[i] = (wchar_t)towlower(line[i]);
    low[i] = 0;
    if (wcsstr(low, L"crash") || wcsstr(low, L"exception") || wcsstr(low, L"failed") ||
        wcsstr(low, L"unimplemented") || wcsstr(low, L"not implemented"))
        return C_BADC;
    if (wcsstr(low, L"----") || wcsstr(low, L"====") || wcsstr(low, L"could not"))
        return C_WARNC;
    return 0xbdb6a8;
}

static void page_log(void) {
    int bx = LX - 16, by = 96, bw = LW + 32 + 22, bh = LOGICAL_H - 24 - by, lh = 17, vis, maxs, i;
    wchar_t buf[300];
    page_side(5, tr(S_LOG_TITLE), tr(S_PAGE_LOG_KANJI), tr(S_LOG_SUB));
    ui_secondary(ID_LOG_BACK, TX, 470, 150, 40, tr(S_BTN_BACK), 1);
    ui_link(ID_OPEN_RUNS, TX, 530, tr(S_LOG_OPEN), 1);
    fill_rect_a(bx, by, bw, bh, 0x0b0a09, 200);
    hair_box(bx, by, bw, bh, 36);
    if (!g_log_n) {
        txt(F_LBODY, C_DIMC, bx + 24, by + 24, bw - 48, 60, DT_WORDBREAK, tr(S_LOG_EMPTY));
        return;
    }
    wfmt(buf, 300, tr(S_LOG_UPDATED), base_name(g_log_path), g_log_stamp);
    txt(F_LSMALL, C_DIMC, bx + 16, by + 8, bw - 32, 18, T_LINE, buf);
    vis = (bh - 44) / lh;
    maxs = g_log_n > vis ? g_log_n - vis : 0;
    if (g_log_scroll > maxs)
        g_log_scroll = maxs;
    if (g_log_scroll < 0)
        g_log_scroll = 0;
    ui_clip(bx + 12, by + 32, bw - 28, bh - 40);
    for (i = 0; i < vis && g_log_scroll + i < g_log_n; i++) {
        const wchar_t *line = g_log_w + g_log_ls[g_log_scroll + i];
        txt(F_MONO, log_line_color(line), bx + 16, by + 34 + i * lh, bw * 3, lh, DT_SINGLELINE | DT_VCENTER, line);
    }
    ui_unclip();
    if (maxs > 0) {
        int th = bh - 52, kh = th * vis / g_log_n;
        if (kh < 24)
            kh = 24;
        fill_rect_a(bx + bw - 10, by + 36, 2, th, C_CREAM, 20);
        fill_rect(bx + bw - 10, by + 36 + (th - kh) * g_log_scroll / maxs, 2, kh, C_VERM);
    }
}

/* One credit: the project, and what the port takes from it beside it. */
static int credit_row(int y, const wchar_t *name, const wchar_t *what) {
    txt(F_LBODY, C_CREAM, LX, y, 260, 24, T_LINE, name);
    txt(F_LSMALL, C_MUTED, LX + 270, y, LW - 270, 24, T_LINE, what);
    return y + 25;
}

static void page_about(void) {
    int y = 104, x;
    wchar_t buf[96];
    page_side(6, tr(S_NAV_ABOUT), k_page_kanji[PG_ABOUT], tr(S_ABOUT_SUB));
    kamon_draw(LX + LW - 50, 150, 46, C_VERM);
    txt(F_SUB, C_CREAM, LX, y, LW - 140, 30, T_LINE, tr(S_ABOUT_PRODUCT));
    wfmt(buf, 96, tr(S_ABOUT_VERSION), L"" NGB_VERSION_STR);
    txt(F_LSMALL, C_DIMC, LX, y + 36, LW - 140, 20, T_LINE, buf);
    txt(F_LBODY, C_MUTED, LX, y + 60, LW - 140, 22, T_LINE, tr(S_ABOUT_AUTHOR));
    x = LX;
    x += ui_link(ID_ABOUT_REPO, x, y + 98, tr(S_ABOUT_GITHUB), 1) + 30;
    x += ui_link(ID_ABOUT_CONTRIB, x, y + 98, tr(S_ABOUT_CONTRIB), 1) + 30;
    (void)ui_link(ID_ABOUT_LICENSE, x, y + 98, tr(S_ABOUT_LICENSE), 1);
    y = group(y + 140, tr(S_ABOUT_CREDITS)) + 10;
    y = credit_row(y, L"xemu", tr(S_CRED_XEMU));
    y = credit_row(y, L"nxdk", tr(S_CRED_NXDK));
    y = credit_row(y, tr(S_CRED_CXBX_NAME), tr(S_CRED_CXBX));
    y = credit_row(y, L"envytools", tr(S_CRED_ENVY));
    y = credit_row(y, L"RenderDoc", tr(S_CRED_RDOC));
    y = credit_row(y, L"Josefin Sans, Shippori Mincho", L"SIL OFL 1.1");
    txt(F_LSMALL, C_DIMC, LX, y + 4, LW, 20, T_LINE, tr(S_CRED_FULL));
    y = group(y + 40, tr(S_ABOUT_LEGAL)) + 10;
    txt(F_LSMALL, C_MUTED, LX, y, LW, 20, T_LINE, tr(S_LEGAL_1));
    txt(F_LSMALL, C_MUTED, LX, y + 21, LW, 20, T_LINE, tr(S_LEGAL_2));
    txt(F_LSMALL, C_MUTED, LX, y + 42, LW, 20, T_LINE, tr(S_LEGAL_3));
    txt(F_LSMALL, C_MUTED, LX, y + 63, LW, 20, T_LINE, tr(S_LEGAL_4));
}

/* --demo widgets: every lacquer widget and state on one screen, for review. */
static void paint_widgets_demo(void) {
    int x = 60, y = 130;
    ui_label(x, y, L"Primary", C_VERM);
    ui_primary(910, x, y + 24, 232, 60, tr(S_BTN_PLAY), L"始", 1);
    ui_primary(911, x + 260, y + 24, 232, 60, tr(S_CTA_BTN_ISO), NULL, 1);
    ui_primary(912, x + 520, y + 24, 232, 60, tr(S_BTN_PLAY), L"始", 0);
    y += 110;
    ui_label(x, y, L"Secondary and links", C_VERM);
    ui_secondary(920, x, y + 24, 210, 58, tr(S_CTA_BTN_FOLDER), 1);
    ui_secondary(921, x + 230, y + 24, 210, 58, tr(S_CTA_BTN_FOLDER), 0);
    ui_link(922, x + 470, y + 46, tr(S_BTN_CHANGE), 1);
    ui_link(923, x + 580, y + 46, tr(S_BTN_OPEN), 1);
    y += 110;
    ui_label(x, y, L"Switches and progress", C_VERM);
    ui_switch(930, x, y + 30, 1, 1);
    ui_switch(931, x + 70, y + 30, 0, 1);
    ui_switch(932, x + 140, y + 30, 0, 0);
    ui_rule_progress(x + 220, y + 40, 400, 620);
    ui_caret(x + 660, y + 38, C_DIMC);
}

/* ------------------------------------------------------------ general painting ---- */

void paint_all(void) {
    int kind = g_page == PG_PLAY && g_demo != 3 ? ART_HERO : ART_INNER;
    ui_begin(kind);
    if (g_demo == 3) {
        paint_widgets_demo();
    } else {
        switch (g_page) {
        case PG_PLAY: page_play(); break;
        case PG_GRAPHICS: page_graphics(); break;
        case PG_AUDIO: page_audio(); break;
        case PG_CONTROLS: page_controls(); break;
        case PG_ADVANCED:
            if (g_log_view)
                page_log();
            else
                page_advanced();
            break;
        default: page_about(); break;
        }
    }
    top_bar();
    paint_overlay();
    ui_end();
}
