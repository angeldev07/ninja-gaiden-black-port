/*
 * launcher.c: ngb-launcher.exe, the graphical launcher of the Ninja Gaiden Black PC port.
 *
 * One fixed-size window (1240x700 logical pixels, scaled to the system DPI) drawn entirely
 * by ui.c on a back buffer; art.c paints the original backdrop (red mist, chrome wordmark,
 * an abstract shuriken emblem). Nothing here comes from the game.
 *
 *   - Settings live in ngb.ini next to the exe (UTF-16 so paths with accents survive).
 *   - First run: the Play page asks the user to import their own copy, either a disc image
 *     (.iso, extracted to <data>\game on a worker thread, see import.c) or a folder that
 *     already holds default.xbe. The result is validated through the XBE certificate.
 *   - PLAY runs ngb-loader.exe <xbe> --window ... with NGB_DATA set to the data folder.
 *   - Only options that the loader's argument parser (port\src\main.c) understands are shown.
 *
 *   ngb-launcher.exe [--page N] [--lang en|es] [--iso FILE] [--shot FILE.bmp [--shot-delay MS]]
 *       --page N       start on page N (0 Play .. 5 About)
 *       --log-view     with --page 4: open the log viewer (the sub-view of Advanced)
 *       --lang         force a language for this run (not saved)
 *       --iso FILE     start importing that disc image right away
 *       --shot FILE    render the window to a BMP and exit (used to review the interface)
 *       --demo NAME    fake a state for screenshots: progress | error
 */
#define UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <commdlg.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "import.h"
#include "update.h"
#include "strings.h"
#include "ui.h"
#include "kamon.h"
#include "version.h"

/* ------------------------------------------------------------------ constants ---- */

#define LOGICAL_W ART_W
#define LOGICAL_H ART_H
#define SIDEBAR_W ART_SIDE_W
#define FOOT_H ART_FOOT_H
#define PANEL_X (SIDEBAR_W + 48)
#define PANEL_W (LOGICAL_W - SIDEBAR_W - 96)
#define PANEL_BOTTOM (LOGICAL_H - FOOT_H - 16)
#define ROW_H 72
#define TIMER_TICK 1
#define TICK_MS 100
#define SLOW_EVERY 5 /* the game/log poll runs every 5th tick */
#define DEFAULT_LOG_CAP 2000
#define HERO_CARD_Y 318
#define HERO_CARD_H 270

/* The log viewer is not a page: it is a sub-view of Advanced (g_log_view). */
enum page_id { PG_PLAY, PG_GRAPHICS, PG_AUDIO, PG_CONTROLS, PG_ADVANCED, PG_ABOUT, PG_COUNT };

#define URL_REPO L"https://github.com/angeldev07/ninja-gaiden-black-port"
#define URL_CONTRIB URL_REPO L"/graphs/contributors"
#define URL_LICENSE URL_REPO L"/blob/main/LICENSE"
#define COPIED_MS 2000 /* how long "Copy session info" says "Copied" */

enum ui_id {
    ID_NONE = 0,
    ID_NAV0 = 1, /* ..ID_NAV0 + PG_COUNT - 1 */
    ID_PLAY = 20, ID_STOP, ID_IMPORT_ISO, ID_IMPORT_FOLDER, ID_IMPORT_CANCEL, ID_DATA_CHANGE,
    ID_DATA_OPEN, ID_VIEW_LOG, ID_OPEN_RUNS, ID_CB_RENDERER, ID_CB_SCALE, ID_CB_LOGCAP, ID_CHK_PACING, ID_CHK_FULLSCREEN, ID_CHK_WIDESCREEN,
    ID_CHK_MUTE, ID_CHK_WAV, ID_CHK_RDOC, ID_CHK_LENIENT, ID_CHK_DETAIL, ID_LANG_EN, ID_LANG_ES,
    ID_LOG_BACK, ID_COPY_INFO, ID_ABOUT_REPO, ID_ABOUT_CONTRIB, ID_ABOUT_LICENSE,
    ID_CHK_UPDATES, ID_UPD_NOTES, ID_UPD_GO,
    ID_ITEM0 = 100 /* items of the open drop-down */
};

enum lang_id { LANG_EN, LANG_ES };

typedef struct settings {
    wchar_t xbe[MAX_PATH];
    wchar_t data[MAX_PATH]; /* data folder chosen by the user ("" = automatic) */
    int renderer;  /* 0 auto, 1 gl, 2 soft */
    int scale;      /* --scale: internal resolution 1-4; 0 = auto (match the monitor) */
    int fullscreen; /* --fullscreen */
    int widescreen; /* --widescreen */
    int pacing;   /* 1: 60 fps; 0: --no-pacing */
    int mute;      /* --mute */
    int audio_wav; /* --audio-wav */
    int rdoc_key;  /* --rdoc-key */
    int lenient;   /* --lenient */
    int detailed_log; /* 0: --log-detail minimal; 1: the loader's full log */
    int log_cap;   /* --log-cap */
    int check_updates; /* look for a new release once a day (update.h) */
    int update_day;    /* day number of the last check */
} settings_t;

typedef struct session {
    wchar_t start[48]; /* "2026-10-22 14:03" */
    long duration_s;
    int code; /* exit code; SESSION_STOPPED / SESSION_NONE */
    wchar_t log[MAX_PATH];
} session_t;
#define SESSION_NONE (-2)
#define SESSION_STOPPED (-1)

/* A message shown in the game card; stored as a code so it follows the language. */
enum note_kind { NK_NONE, NK_OK_IMPORTED, NK_OK_FOLDER, NK_ERR };
typedef struct note {
    int kind, err;
    wchar_t detail[64];
    double num;
} note_t;

/* -------------------------------------------------------------------- state ---- */

static const wchar_t *const k_str[S_COUNT][2] = {
#define X(id, en, es) {en, es},
    STRING_TABLE(X)
#undef X
};
static int g_lang = LANG_EN;

static HWND g_hwnd;
static wchar_t g_exe_dir[MAX_PATH]; /* with trailing backslash */
static wchar_t g_ini[MAX_PATH];
static wchar_t g_loader[MAX_PATH];
static wchar_t g_data[MAX_PATH]; /* resolved data folder */
static wchar_t g_runs[MAX_PATH];
static wchar_t g_upd_url[1024] = UPD_MANIFEST_URL; /* --update-url replaces it (tests) */
static int g_upd_forced;                          /* --update-url: check now, whatever the day */
static int g_upd_wanted;                          /* the user pressed Update */
static wchar_t g_upd_fail[160];                   /* why the last apply failed */
static wchar_t g_updated_to[32];                  /* --updated X: the update that just finished */
static void poll_update(void);
static settings_t g_set;
static session_t g_last;
static note_t g_note;
static xbe_info_t g_game; /* probe of g_set.xbe */
static int g_game_exists;

static int g_page = PG_PLAY;
static int g_open_combo = ID_NONE;
static int g_ovl_x, g_ovl_y, g_ovl_w;
static int g_mouse_tracked;
static int g_tick;

static HANDLE g_proc;
static DWORD g_pid;
static ULONGLONG g_t0;
static int g_stopped_by_user;

static wchar_t *g_log_w;
static int *g_log_ls;
static int g_log_n, g_log_scroll;
static wchar_t g_log_path[MAX_PATH];
static ULONGLONG g_log_sig;
static wchar_t g_log_stamp[16];
static int g_log_view; /* the log viewer is open (a sub-view of the Advanced page) */
static ULONGLONG g_copied_until; /* GetTickCount64() deadline of the "Copied" label; 0 = none */

static wchar_t g_shot_path[MAX_PATH];
static int g_shot_delay = 0;
static int g_shot_after_import; /* with --iso: take the shot when the import ends */
static int g_import_seen;
static int g_demo; /* 1 progress, 2 error, 3 widgets */

static const wchar_t *tr(int id) { return k_str[id][g_lang]; }

/* ------------------------------------------------------------------ utilities ---- */

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

static const wchar_t *base_name(const wchar_t *path) {
    const wchar_t *s = wcsrchr(path, L'\\');
    return s ? s + 1 : path;
}

static void upper_copy(const wchar_t *s, wchar_t *out, size_t n) {
    wcsncpy(out, s, n - 1);
    out[n - 1] = 0;
    CharUpperBuffW(out, (DWORD)wcslen(out));
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

static void strip_trailing_slash(wchar_t *p) {
    size_t n = wcslen(p);
    while (n > 3 && (p[n - 1] == L'\\' || p[n - 1] == L'/'))
        p[--n] = 0;
}

/* --------------------------------------------------------------- data folder ---- */

/* Order: the folder the user chose; $NGB_DATA; an existing %USERPROFILE%\ngb-data (the
 * developer's layout); %LOCALAPPDATA%\NGBPort. */
static void resolve_data(void) {
    wchar_t v[MAX_PATH], probe[MAX_PATH];
    DWORD len;
    g_data[0] = 0;
    if (g_set.data[0]) {
        wfmt(g_data, MAX_PATH, L"%ls", g_set.data);
    } else if ((len = GetEnvironmentVariableW(L"NGB_DATA", v, MAX_PATH)) != 0 && len < MAX_PATH) {
        wfmt(g_data, MAX_PATH, L"%ls", v);
    } else {
        int found = 0;
        len = GetEnvironmentVariableW(L"USERPROFILE", v, MAX_PATH);
        if (len && len < MAX_PATH) {
            wfmt(probe, MAX_PATH, L"%ls\\ngb-data\\game\\default.xbe", v);
            if (file_exists(probe)) {
                wfmt(g_data, MAX_PATH, L"%ls\\ngb-data", v);
                found = 1;
            }
        }
        if (!found) {
            len = GetEnvironmentVariableW(L"LOCALAPPDATA", probe, MAX_PATH);
            if (len && len < MAX_PATH)
                wfmt(g_data, MAX_PATH, L"%ls\\NGBPort", probe);
            else if (v[0])
                wfmt(g_data, MAX_PATH, L"%ls\\NGBPort", v);
        }
    }
    strip_trailing_slash(g_data);
    g_runs[0] = 0;
    if (g_data[0])
        wfmt(g_runs, MAX_PATH, L"%ls\\runs", g_data);
}

/* ------------------------------------------------------------------- settings ---- */

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

/* A new ngb.ini is UTF-16 with a BOM so that paths with accents are stored correctly. */
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

static const int k_logcap_vals[] = {500, DEFAULT_LOG_CAP, 10000, 50000};

static void save_settings(void) {
    ini_put(L"game", L"xbe", g_set.xbe);
    ini_put(L"game", L"data", g_set.data);
    ini_put(L"ui", L"lang", g_lang == LANG_ES ? L"es" : L"en");
    ini_put(L"graphics", L"renderer",
            g_set.renderer == 1 ? L"gl" : g_set.renderer == 2 ? L"soft" : L"auto");
    if (g_set.scale)
        ini_put_int(L"graphics", L"resolution", g_set.scale);
    else
        ini_put(L"graphics", L"resolution", L"auto");
    ini_put_int(L"graphics", L"fullscreen", g_set.fullscreen);
    ini_put_int(L"graphics", L"widescreen", g_set.widescreen);
    ini_put_int(L"graphics", L"limit_fps", g_set.pacing);
    ini_put_int(L"audio", L"mute", g_set.mute);
    ini_put_int(L"audio", L"wav", g_set.audio_wav);
    ini_put_int(L"advanced", L"rdoc_key", g_set.rdoc_key);
    ini_put_int(L"advanced", L"lenient", g_set.lenient);
    ini_put_int(L"advanced", L"detailed_log", g_set.detailed_log);
    ini_put_int(L"advanced", L"log_cap", g_set.log_cap);
    ini_put_int(L"update", L"check", g_set.check_updates);
    ini_put_int(L"update", L"last_day", g_set.update_day);
}

static void game_refresh(void) {
    memset(&g_game, 0, sizeof g_game);
    g_game_exists = g_set.xbe[0] && file_exists(g_set.xbe);
    if (g_game_exists)
        xbe_probe_file(g_set.xbe, &g_game);
}

/* If the chosen xbe is unset or gone and <data>\game\default.xbe exists, use that one. With
 * `prefer` (a data folder was just chosen), the data folder's own game wins even over a valid
 * one: the saves and the virtual hard disk live in the data folder, so they go with its game. */
static void adopt_default_game(int prefer) {
    wchar_t def[MAX_PATH];
    if (!g_data[0])
        return;
    wfmt(def, MAX_PATH, L"%ls\\game\\default.xbe", g_data);
    if ((prefer || !g_set.xbe[0] || !file_exists(g_set.xbe)) && file_exists(def))
        wfmt(g_set.xbe, MAX_PATH, L"%ls", def);
}

static void load_settings(void) {
    wchar_t t[32];
    int i, ok = 0;
    ini_text(L"game", L"xbe", L"", g_set.xbe, MAX_PATH);
    ini_text(L"game", L"data", L"", g_set.data, MAX_PATH);
    ini_text(L"ui", L"lang", L"", t, 32);
    if (!wcscmp(t, L"es"))
        g_lang = LANG_ES;
    else if (!wcscmp(t, L"en"))
        g_lang = LANG_EN;
    else
        g_lang = (GetUserDefaultUILanguage() & 0x3ff) == LANG_SPANISH ? LANG_ES : LANG_EN;
    ini_text(L"graphics", L"renderer", L"auto", t, 32);
    g_set.renderer = !wcscmp(t, L"gl") ? 1 : !wcscmp(t, L"soft") ? 2 : 0;
    ini_text(L"graphics", L"resolution", L"auto", t, 32);
    g_set.scale = _wtoi(t); /* "auto" (and anything unknown) reads as 0 */
    if (g_set.scale < 1 || g_set.scale > 4)
        g_set.scale = 0;
    g_set.fullscreen = ini_int(L"graphics", L"fullscreen", 1) != 0;
    g_set.widescreen = ini_int(L"graphics", L"widescreen", 1) != 0;
    g_set.pacing = ini_int(L"graphics", L"limit_fps", 1) != 0;
    g_set.mute = ini_int(L"audio", L"mute", 0) != 0;
    g_set.audio_wav = ini_int(L"audio", L"wav", 0) != 0;
    g_set.rdoc_key = ini_int(L"advanced", L"rdoc_key", 0) != 0;
    g_set.lenient = ini_int(L"advanced", L"lenient", 0) != 0;
    g_set.detailed_log = ini_int(L"advanced", L"detailed_log", 0) != 0;
    g_set.log_cap = ini_int(L"advanced", L"log_cap", DEFAULT_LOG_CAP);
    g_set.check_updates = ini_int(L"update", L"check", 1) != 0;
    g_set.update_day = ini_int(L"update", L"last_day", 0);
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

    resolve_data();
    adopt_default_game(0);
    game_refresh();
    save_settings();
}

static int loader_ok(void) { return file_exists(g_loader); }
static int xbe_ok(void) { return g_game_exists && g_game.valid; }
static int running(void) { return g_proc != NULL; }
static int log_view(void) { return g_page == PG_ADVANCED && g_log_view; }

static void get_import(imp_status_t *st) {
    import_status(st);
    if (g_demo == 1 && st->state == IMP_IDLE) {
        memset(st, 0, sizeof *st);
        st->state = IMP_RUNNING;
        st->phase = IMPH_EXTRACT;
        st->done = 2530LL * 1024 * 1024;
        st->total = 6490LL * 1024 * 1024;
        st->elapsed_ms = 31000;
        wcscpy(st->current, L"movie\\opening_us.sfd");
    }
}
static int importing(void) {
    imp_status_t st;
    get_import(&st);
    return st.state == IMP_RUNNING;
}
static int can_play(void) { return !running() && !importing() && loader_ok() && xbe_ok(); }

/* Internal resolution the chosen scale gives; "auto" follows the loader: enough to cover the
 * picture on the monitor the launcher is on, on both axes (4:3 or 16:9), between 1 and 4. */
static void scaled_size(int *w, int *h) {
    double s = g_set.scale;
    if (!s) {
        MONITORINFO mi;
        memset(&mi, 0, sizeof mi);
        mi.cbSize = sizeof mi;
        s = 1.0;
        if (GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            double a = g_set.widescreen ? 16.0 / 9.0 : 4.0 / 3.0;
            double mw = mi.rcMonitor.right - mi.rcMonitor.left, mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
            double pw = mh * a < mw ? mh * a : mw, ph = pw / a;
            s = pw / 720.0 > ph / 480.0 ? pw / 720.0 : ph / 480.0;
        }
        s = s < 1.0 ? 1.0 : s > 4.0 ? 4.0 : s;
    }
    *w = (int)(720 * s + 0.5);
    *h = (int)(480 * s + 0.5);
}

/* PLAY's command line (also shown on the Advanced page). */
static void build_cmdline(wchar_t *out, size_t n) {
    wfmt(out, n, L"\"%ls\" \"%ls\" --window --timeout 0 --max-calls 0 --idle 0", g_loader, g_set.xbe);
    if (g_set.renderer == 1)
        wcat(out, n, L" --renderer gl");
    else if (g_set.renderer == 2)
        wcat(out, n, L" --renderer soft");
    if (!g_set.scale)
        wcat(out, n, L" --scale auto");
    else if (g_set.scale > 1) {
        wchar_t t[24];
        wfmt(t, 24, L" --scale %d", g_set.scale);
        wcat(out, n, t);
    }
    if (g_set.fullscreen)
        wcat(out, n, L" --fullscreen");
    if (g_set.widescreen)
        wcat(out, n, L" --widescreen");
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
    if (!g_set.detailed_log) /* the loader's default is the full log */
        wcat(out, n, L" --log-detail minimal");
    if (g_set.log_cap != DEFAULT_LOG_CAP) {
        wchar_t t[40];
        wfmt(t, 40, L" --log-cap %d", g_set.log_cap);
        wcat(out, n, t);
    }
}

static void config_summary(wchar_t *out, size_t n) {
    const wchar_t *rend = g_set.renderer == 1   ? tr(S_SUM_GL)
                          : g_set.renderer == 2 ? tr(S_SUM_SOFT)
                                                : tr(S_SUM_AUTO);
    wfmt(out, n, L"%ls  ·  %ls  ·  %ls", rend, g_set.pacing ? tr(S_SUM_FPS60) : tr(S_SUM_NOFPS),
         g_set.mute ? tr(S_SUM_MUTED) : tr(S_SUM_SOUND));
    if (g_set.scale != 1 && g_set.renderer != 2) {
        wchar_t t[32];
        int w, h;
        scaled_size(&w, &h);
        wfmt(t, 32, L"  ·  %dx%d", w, h);
        wcat(out, n, t);
    }
    if (g_set.widescreen) {
        wcat(out, n, L"  ·  ");
        wcat(out, n, tr(S_SUM_WIDE));
    }
    if (g_set.fullscreen) {
        wcat(out, n, L"  ·  ");
        wcat(out, n, tr(S_SUM_FULL));
    }
    if (g_set.rdoc_key)
        wcat(out, n, L"  ·  RenderDoc");
    if (g_set.lenient)
        wcat(out, n, L"  ·  Lenient");
}

/* ----------------------------------------------------------- notes and errors ---- */

static void note_clear(void) { memset(&g_note, 0, sizeof g_note); }

static void note_error(int err, const wchar_t *detail, double num) {
    memset(&g_note, 0, sizeof g_note);
    g_note.kind = NK_ERR;
    g_note.err = err;
    if (detail)
        wfmt(g_note.detail, 64, L"%ls", detail);
    g_note.num = num;
}

static void note_text(wchar_t *out, size_t n) {
    out[0] = 0;
    if (g_note.kind == NK_OK_IMPORTED) {
        wfmt(out, n, L"%ls", tr(S_NOTE_IMPORTED));
    } else if (g_note.kind == NK_OK_FOLDER) {
        wfmt(out, n, L"%ls", tr(S_NOTE_FOLDER_OK));
    } else if (g_note.kind == NK_ERR) {
        switch (g_note.err) {
        case IMPE_OPEN: wfmt(out, n, L"%ls", tr(S_ERR_OPEN)); break;
        case IMPE_NOT_XISO: wfmt(out, n, L"%ls", tr(S_ERR_NOT_XISO)); break;
        case IMPE_NO_XBE: wfmt(out, n, L"%ls", tr(S_ERR_NO_XBE)); break;
        case IMPE_NOT_NGB: wfmt(out, n, tr(S_ERR_NOT_NGB), g_note.detail); break;
        case IMPE_READ: wfmt(out, n, L"%ls", tr(S_ERR_READ)); break;
        case IMPE_CORRUPT: wfmt(out, n, L"%ls", tr(S_ERR_CORRUPT)); break;
        case IMPE_WRITE: wfmt(out, n, L"%ls", tr(S_ERR_WRITE)); break;
        case IMPE_NOSPACE: wfmt(out, n, tr(S_ERR_NOSPACE), g_note.num); break;
        case IMPE_CANCELLED: wfmt(out, n, L"%ls", tr(S_ERR_CANCELLED)); break;
        case IMPE_XBE_BAD: wfmt(out, n, L"%ls", tr(S_ERR_XBE_BAD)); break;
        case IMPE_FOLDER_NO_XBE: wfmt(out, n, L"%ls", tr(S_ERR_FOLDER_NO_XBE)); break;
        default: wfmt(out, n, L"%ls", tr(S_ERR_INTERNAL)); break;
        }
    }
}

/* ------------------------------------------------------------------- import ---- */

static int pick_iso(wchar_t *out) {
    OPENFILENAMEW ofn;
    out[0] = 0;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = tr(S_DLG_FILTER_ISO);
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = out;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = tr(S_DLG_PICK_ISO);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn);
}

static int pick_folder(const wchar_t *title, wchar_t *out) {
    BROWSEINFOW bi;
    PIDLIST_ABSOLUTE pidl;
    int ok = 0;
    memset(&bi, 0, sizeof bi);
    bi.hwndOwner = g_hwnd;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        ok = SHGetPathFromIDListW(pidl, out);
        CoTaskMemFree(pidl);
    }
    return ok;
}

static void begin_iso_import(const wchar_t *path, int ask) {
    wchar_t existing[MAX_PATH];
    if (importing() || running())
        return;
    wfmt(existing, MAX_PATH, L"%ls\\game\\default.xbe", g_data);
    if (ask && file_exists(existing) &&
        MessageBoxW(g_hwnd, tr(S_DLG_REPLACE), tr(S_DLG_TITLE), MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    note_clear();
    g_page = PG_PLAY;
    if (!g_data[0] || !import_start_iso(path, g_data))
        note_error(IMPE_INTERNAL, NULL, 0);
}

/* A folder that already holds default.xbe: validate it and use it where it is. */
static void use_folder(const wchar_t *dir) {
    wchar_t xbe[MAX_PATH];
    xbe_info_t info;
    wfmt(xbe, MAX_PATH, L"%ls\\default.xbe", dir);
    strip_trailing_slash(xbe);
    if (!file_exists(xbe)) {
        note_error(IMPE_FOLDER_NO_XBE, NULL, 0);
        return;
    }
    if (!xbe_probe_file(xbe, &info) || !info.valid) {
        note_error(IMPE_XBE_BAD, NULL, 0);
        return;
    }
    if (!info.is_ngb) {
        note_error(IMPE_NOT_NGB, info.title, 0);
        return;
    }
    wfmt(g_set.xbe, MAX_PATH, L"%ls", xbe);
    save_settings();
    game_refresh();
    note_clear();
    g_note.kind = NK_OK_FOLDER;
}

static void import_from_path(const wchar_t *p, int ask) {
    size_t n = wcslen(p);
    if (dir_exists(p)) {
        use_folder(p);
    } else if (n >= 11 && !_wcsicmp(p + n - 11, L"default.xbe")) {
        wchar_t dir[MAX_PATH];
        wfmt(dir, MAX_PATH, L"%ls", p);
        dir[n - 11] = 0;
        use_folder(dir);
    } else {
        begin_iso_import(p, ask);
    }
}

static void poll_import(void) {
    imp_status_t st;
    if (g_demo)
        return;
    import_status(&st);
    if (st.state == IMP_DONE) {
        wchar_t def[MAX_PATH];
        wfmt(def, MAX_PATH, L"%ls\\game\\default.xbe", g_data);
        wfmt(g_set.xbe, MAX_PATH, L"%ls", def);
        save_settings();
        game_refresh();
        note_clear();
        g_note.kind = NK_OK_IMPORTED;
        import_reset();
    } else if (st.state == IMP_ERROR) {
        note_error(st.err, st.detail, st.need_gb);
        import_reset();
    }
}

static void change_data_folder(void) {
    wchar_t dir[MAX_PATH];
    if (importing() || running() || !pick_folder(tr(S_DLG_PICK_DATA), dir))
        return;
    strip_trailing_slash(dir);
    wfmt(g_set.data, MAX_PATH, L"%ls", dir);
    resolve_data();
    adopt_default_game(1);
    game_refresh();
    g_log_sig = 0;
    save_settings();
}

/* ----------------------------------------------------------------- the game ---- */

static void exit_text(int code, wchar_t *out, size_t n) {
    switch (code) {
    case SESSION_NONE: wfmt(out, n, L"—"); break;
    case SESSION_STOPPED: wfmt(out, n, L"%ls", tr(S_EXIT_STOPPED)); break;
    case 0: wfmt(out, n, L"%ls", tr(S_EXIT_0)); break;
    case 1: wfmt(out, n, L"%ls", tr(S_EXIT_1)); break;
    case 2: wfmt(out, n, L"%ls", tr(S_EXIT_2)); break;
    case 3: wfmt(out, n, L"%ls", tr(S_EXIT_3)); break;
    case 4: wfmt(out, n, L"%ls", tr(S_EXIT_4)); break;
    case 5: wfmt(out, n, L"%ls", tr(S_EXIT_5)); break;
    case 6: wfmt(out, n, L"%ls", tr(S_EXIT_6)); break;
    default: wfmt(out, n, tr(S_EXIT_OTHER), code); break;
    }
}

/* The newest .log of the runs folder (without the -pgraph ones). */
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

/* Re-reads the tail of the newest log if it changed. Returns 1 when there is new content. */
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
    if (pos.QuadPart > 0) { /* we start mid-file: drop the cut first line */
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
    if (nl > LOG_MAX_LINES) {
        memmove(g_log_ls, g_log_ls + (nl - LOG_MAX_LINES), LOG_MAX_LINES * sizeof(int));
        nl = LOG_MAX_LINES;
    }
    g_log_n = nl;
    wfmt(g_log_path, MAX_PATH, L"%ls", path);
    g_log_sig = sig;
    GetLocalTime(&st);
    wfmt(g_log_stamp, 16, L"%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    g_log_scroll = 1 << 30; /* stick to the end; the page clamps it */
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
        g_log_sig = 0;
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
    /* The loader reads NGB_DATA for its runs, logs and virtual hard disk. */
    SetEnvironmentVariableW(L"NGB_DATA", g_data);
    CreateDirectoryW(g_data, NULL);
    /* The loader is a console program: without CREATE_NO_WINDOW a black window would open. */
    if (!CreateProcessW(g_loader, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, g_exe_dir, &si,
                        &pi)) {
        MessageBoxW(g_hwnd, tr(S_DLG_LAUNCH_FAIL), tr(S_DLG_TITLE), MB_OK | MB_ICONERROR);
        return;
    }
    AllowSetForegroundWindow(pi.dwProcessId); /* let the game window take focus */
    CloseHandle(pi.hThread);
    g_proc = pi.hProcess;
    g_pid = pi.dwProcessId;
    g_t0 = GetTickCount64();
    g_stopped_by_user = 0;
    GetLocalTime(&st);
    wfmt(g_last.start, 48, L"%04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
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

static void open_folder(const wchar_t *dir) {
    CreateDirectoryW(dir, NULL);
    ShellExecuteW(g_hwnd, L"open", dir, NULL, NULL, SW_SHOWNORMAL);
}

/* ------------------------------------------------------------- session info ---- */

#define LOG_HEAD_BYTES (256 * 1024)

/* The text inside the parentheses of the "pgraph: OpenGL render (...)" line. The loader writes
 * it during start-up, so the head of the log is enough. */
static int log_gpu_text(const wchar_t *path, wchar_t *out, size_t n) {
    static const char key[] = "pgraph: OpenGL render (";
    char *raw, *p, *end, *close;
    HANDLE h;
    DWORD got = 0;
    int found = 0;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    raw = (char *)malloc(LOG_HEAD_BYTES + 1);
    if (!raw) {
        CloseHandle(h);
        return 0;
    }
    ReadFile(h, raw, LOG_HEAD_BYTES, &got, NULL);
    CloseHandle(h);
    raw[got] = 0;
    for (p = raw; (p = strstr(p, key)) != NULL; p += sizeof key - 1) {
        if (p != raw && p[-1] != '\n')
            continue; /* the line must start with the key */
        p += sizeof key - 1;
        end = p + strcspn(p, "\r\n");
        *end = 0;
        close = strrchr(p, ')');
        if (close)
            *close = 0;
        found = MultiByteToWideChar(CP_UTF8, 0, p, -1, out, (int)n) > 1;
        break;
    }
    free(raw);
    return found;
}

static void windows_version(wchar_t *out, size_t n) {
    typedef LONG(WINAPI * rtl_get_version_fn)(OSVERSIONINFOW *);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    rtl_get_version_fn get = nt ? (rtl_get_version_fn)(void *)GetProcAddress(nt, "RtlGetVersion") : NULL;
    OSVERSIONINFOW v;
    memset(&v, 0, sizeof v);
    v.dwOSVersionInfoSize = sizeof v;
    if (get && get(&v) == 0)
        wfmt(out, n, L"Windows %lu.%lu.%lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
    else
        wfmt(out, n, L"Windows (unknown version)");
}

/* Replaces the user's profile directory with %USERPROFILE%, so a pasted block does not carry
 * the Windows user name. */
static void scrub_profile(wchar_t *s, size_t n) {
    wchar_t prof[MAX_PATH], *tmp;
    static const wchar_t rep[] = L"%USERPROFILE%";
    size_t plen, i = 0, o = 0;
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH);
    if (!len || len >= MAX_PATH)
        return;
    strip_trailing_slash(prof);
    plen = wcslen(prof);
    tmp = (wchar_t *)malloc(n * sizeof(wchar_t));
    if (plen < 4 || !tmp) { /* shorter than "C:\x": not a profile directory */
        free(tmp);
        return;
    }
    while (s[i] && o + 1 < n) {
        if (!_wcsnicmp(s + i, prof, plen) && !iswalnum(s[i + plen])) { /* not C:\Users\annabel when it is C:\Users\anna */
            size_t k;
            for (k = 0; k < wcslen(rep) && o + 1 < n; k++)
                tmp[o++] = rep[k];
            i += plen;
        } else {
            tmp[o++] = s[i++];
        }
    }
    tmp[o] = 0;
    memcpy(s, tmp, (o + 1) * sizeof(wchar_t));
    free(tmp);
}

/* The plain-text block for a bug report. Always English, whatever the launcher language. */
static void build_session_info(wchar_t *out, size_t n) {
    static const wchar_t *const rend[] = {L"auto", L"opengl", L"software"};
    wchar_t logp[MAX_PATH], gpu[400], line[600], dur[48], res[96];
    ULONGLONG sig;
    int w = 720, h = 480, keep = g_lang;
    int have_log = g_runs[0] && latest_log(logp, MAX_PATH, &sig);
    g_lang = LANG_EN;
    out[0] = 0;
    wfmt(line, 600, L"Ninja Gaiden Black native port %ls\r\n", L"" NGB_VERSION_STR);
    wcat(out, n, line);
    windows_version(gpu, 400);
    wcat(out, n, gpu);
    wcat(out, n, L"\r\n");
    if (have_log && log_gpu_text(logp, gpu, 400))
        wfmt(line, 600, L"GPU: %ls\r\n", gpu);
    else
        wfmt(line, 600, L"Renderer: software/unknown\r\n");
    wcat(out, n, line);
    if (g_set.renderer != 2)
        scaled_size(&w, &h);
    wfmt(res, 96, L"%dx%d", w, h);
    wfmt(line, 600,
         L"Settings: renderer %ls, internal resolution %ls, widescreen %ls, fullscreen %ls, fps limit %ls, "
         L"audio %ls, detailed log %ls\r\n",
         rend[g_set.renderer], res, g_set.widescreen ? L"on" : L"off", g_set.fullscreen ? L"on" : L"off",
         g_set.pacing ? L"60" : L"off", g_set.mute ? L"muted" : L"on", g_set.detailed_log ? L"on" : L"off");
    wcat(out, n, line);
    if (running()) {
        wfmt(line, 600, L"Last session: %ls, still running\r\n", g_last.start);
    } else if (g_last.code == SESSION_NONE) {
        wfmt(line, 600, L"Last session: none\r\n");
    } else {
        fmt_duration(g_last.duration_s, dur, 48);
        exit_text(g_last.code, gpu, 400);
        wfmt(line, 600, L"Last session: %ls, %ls, %ls\r\n", g_last.start, dur, gpu);
    }
    wcat(out, n, line);
    wfmt(line, 600, L"Log file: %ls\r\n", have_log ? base_name(logp) : L"none");
    wcat(out, n, line);
    g_lang = keep;
    scrub_profile(out, n);
}

static int clipboard_put(const wchar_t *text) {
    size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    void *p;
    int ok = 0;
    if (!mem)
        return 0;
    p = GlobalLock(mem);
    if (!p) {
        GlobalFree(mem);
        return 0;
    }
    memcpy(p, text, bytes);
    GlobalUnlock(mem);
    if (OpenClipboard(g_hwnd)) {
        EmptyClipboard();
        ok = SetClipboardData(CF_UNICODETEXT, mem) != NULL;
        CloseClipboard();
    }
    if (!ok)
        GlobalFree(mem); /* the clipboard owns the memory only after a successful SetClipboardData */
    return ok;
}

static void copy_session_info(void) {
    wchar_t info[2048];
    build_session_info(info, 2048);
    if (clipboard_put(info))
        g_copied_until = GetTickCount64() + COPIED_MS;
}

/* ---------------------------------------------------------- shared page parts ---- */

static void page_header(const wchar_t *title, const wchar_t *sub) {
    wchar_t up[64];
    upper_copy(title, up, 64);
    txt_chrome(F_H1, PANEL_X, 24, PANEL_W, 50, DT_SINGLELINE | DT_VCENTER, 0, 3, up);
    txt(F_SMALL, C_DIM, PANEL_X + 2, 78, PANEL_W, 20, DT_SINGLELINE, sub);
}

static void section_title(int y, const wchar_t *s) {
    wchar_t up[96];
    upper_copy(s, up, 96);
    fill_rect(PANEL_X, y + 9, 3, 16, C_RED);
    txt_chrome(F_H2, PANEL_X + 14, y, PANEL_W - 14, 34, DT_SINGLELINE | DT_VCENTER, 0, 2, up);
    fill_rect(PANEL_X, y + 38, PANEL_W, 1, C_LINE);
    fill_rect(PANEL_X, y + 38, 60, 1, C_RED);
}

static void row_text(int x, int y, int w, const wchar_t *label, const wchar_t *desc) {
    txt(F_BODY, C_TEXT, x, y, w, 22, T_LINE, label);
    txt(F_SMALL, C_DIM, x, y + 24, w, 36, DT_WORDBREAK, desc);
}

static void row_frame(int id, int x, int y, int w) {
    if (hot(id))
        fill_rrect_a(x - 14, y - 6, w + 28, ROW_H - 4, 8, 0xffffff, 10);
    fill_rect(x, y + ROW_H - 8, w, 1, 0x1d1d25);
}

static void ui_check(int id, int x, int y, int w, const wchar_t *label, const wchar_t *desc, int on) {
    row_frame(id, x, y, w);
    ui_checkbox(id, x, y, on);
    row_text(x + 38, y, w - 38, label, desc);
    add_hit(x - 14, y - 6, w + 28, ROW_H - 4, id);
}

static const int k_renderer_ids[] = {S_GFX_R_AUTO, S_GFX_R_GL, S_GFX_R_SOFT};
static const int k_scale_ids[] = {S_GFX_SCALE_AUTO, S_GFX_SCALE_1, S_GFX_SCALE_2, S_GFX_SCALE_3,
                                  S_GFX_SCALE_4};

static int combo_count(int id) {
    return id == ID_CB_RENDERER ? 3
           : id == ID_CB_SCALE  ? 5
                                : (int)(sizeof k_logcap_vals / sizeof k_logcap_vals[0]);
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

static void ui_combo(int id, int x, int y, int w, const wchar_t *label, const wchar_t *desc) {
    int bw = 300, bx = x + w - bw, by = y + 2, open = g_open_combo == id, over = hot(id);
    row_frame(id, x, y, w);
    row_text(x, y, w - bw - 24, label, desc);
    fill_rrect_grad(bx, by, bw, 40, 5, open || over ? 0x24242c : 0x1a1a21, 0x121217);
    stroke_rrect(bx, by, bw, 40, 5, open ? C_RED : over ? 0x6a2a32 : 0x3a3d48);
    txt(F_BODY, C_TEXT, bx + 14, by, bw - 48, 40, T_LINE, combo_text(id, combo_sel(id)));
    ui_chevron(bx + bw - 28, by + 17);
    add_hit(bx, by, bw, 40, id);
    if (open) {
        g_ovl_x = bx;
        g_ovl_y = by + 44;
        g_ovl_w = bw;
    }
}

/* The open drop-down list: painted and registered last, so it sits on top. */
static void paint_overlay(void) {
    int n, i, h = 36;
    if (g_open_combo == ID_NONE)
        return;
    n = combo_count(g_open_combo);
    fill_rrect_a(g_ovl_x + 2, g_ovl_y + 4, g_ovl_w, n * h + 8, 8, 0x000000, 140);
    fill_rrect(g_ovl_x, g_ovl_y, g_ovl_w, n * h + 8, 7, 0x16161c);
    stroke_rrect(g_ovl_x, g_ovl_y, g_ovl_w, n * h + 8, 7, 0x4a2a30);
    for (i = 0; i < n; i++) {
        int iy = g_ovl_y + 4 + i * h, id = ID_ITEM0 + i;
        if (hot(id))
            fill_rrect_a(g_ovl_x + 4, iy, g_ovl_w - 8, h, 5, C_RED, 70);
        txt(F_BODY, i == combo_sel(g_open_combo) ? C_RED_HOT : C_TEXT, g_ovl_x + 16, iy, g_ovl_w - 32,
            h, T_LINE, combo_text(g_open_combo, i));
        add_hit(g_ovl_x + 4, iy, g_ovl_w - 8, h, id);
    }
}

/* ------------------------------------------------------------------ play page ---- */

static void game_title(wchar_t *out, size_t n) {
    if (g_game.valid && g_game.title[0])
        wfmt(out, n, L"%ls", g_game.title);
    else
        wfmt(out, n, L"NINJA GAIDEN BLACK");
}

/* "Change..." and "Open" links, right-aligned in the box (x, y, w, 28). */
static void data_links(int x, int y, int w) {
    int bw1 = text_w(F_BOLD, tr(S_BTN_CHANGE)) + 24, bw2 = text_w(F_BOLD, tr(S_BTN_OPEN)) + 24;
    ui_button(ID_DATA_CHANGE, x + w - bw1 - bw2 - 4, y, bw1, 28, tr(S_BTN_CHANGE), 2,
              !importing() && !running());
    ui_button(ID_DATA_OPEN, x + w - bw2, y, bw2, 28, tr(S_BTN_OPEN), 2, 1);
}

static void data_path(int x, int y, int w) {
    txt(F_SMALL, C_STEEL, x, y, w, 22, DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS, g_data);
}

static void note_line(int x, int y, int w, int h) {
    wchar_t t[400];
    if (g_note.kind == NK_NONE)
        return;
    note_text(t, 400);
    status_dot(x + 8, y + 10, g_note.kind == NK_ERR ? C_BAD : C_OK);
    txt(F_SMALL, g_note.kind == NK_ERR ? C_BAD : C_OK, x + 26, y, w - 26, h, DT_WORDBREAK, t);
}

static void play_game_card(int x, int y, int w, int h) {
    wchar_t buf[300], title[64];
    ui_card(x, y, w, h);
    caption(x + 22, y + 18, w - 44, tr(S_CARD_GAME));
    game_title(title, 64);
    txt_chrome(F_H2, x + 22, y + 36, w - 44, 32, DT_SINGLELINE | DT_VCENTER, 0, 1, title);
    if (xbe_ok() && g_game.is_ngb) {
        status_dot(x + 30, y + 86, C_OK);
        wchar_t verified[96];
        /* The release, as the port will run it (main.c: bit 0 makes an NTSC console). */
        wfmt(verified, 96, L"%ls%ls", tr(S_GAME_VERIFIED), tr(g_game.region & 1u ? S_GAME_NTSC : S_GAME_PAL));
        txt(F_SMALL, C_OK, x + 50, y + 76, w - 72, 20, T_LINE, verified);
    } else if (xbe_ok()) {
        status_dot(x + 30, y + 86, C_WARN);
        wfmt(buf, 300, tr(S_GAME_UNKNOWN), g_game.title);
        txt(F_SMALL, C_WARN, x + 50, y + 76, w - 72, 20, T_LINE, buf);
    } else {
        status_dot(x + 30, y + 86, C_BAD);
        txt(F_SMALL, C_BAD, x + 50, y + 76, w - 72, 20, T_LINE,
            g_game_exists ? tr(S_GAME_BAD_XBE) : tr(S_GAME_MISSING));
    }
    if (g_game_exists) {
        wfmt(buf, 300, tr(S_GAME_SIZE), (double)g_game.size / 1048576.0);
        txt(F_SMALL, C_DIM, x + 22, y + 100, w - 44, 18, T_LINE, buf);
    }
    txt(F_SMALL, C_FAINT, x + 22, y + 118, w - 44, 18, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS,
        g_set.xbe);
    caption(x + 22, y + 156, w - 44, tr(S_DATA_FOLDER));
    data_links(x + 22, y + 148, w - 44);
    data_path(x + 22, y + 174, w - 44);
    note_line(x + 22, y + 198, w - 44, 20);
    {
        int b1 = text_w(F_BOLD, tr(S_BTN_IMPORT_ISO)) + 36, b2 = text_w(F_BOLD, tr(S_BTN_USE_FOLDER)) + 36;
        ui_button(ID_IMPORT_ISO, x + 22, y + h - 48, b1, 36, tr(S_BTN_IMPORT_ISO), 0, !running());
        ui_button(ID_IMPORT_FOLDER, x + 22 + b1 + 10, y + h - 48, b2, 36, tr(S_BTN_USE_FOLDER), 0, !running());
    }
}

static void play_last_card(int x, int y, int w, int h) {
    wchar_t buf[96], t2[96];
    ui_card(x, y, w, h);
    caption(x + 22, y + 18, w - 44, tr(S_CARD_LAST));
    if (running()) {
        wchar_t d[48];
        fmt_duration((long)((GetTickCount64() - g_t0) / 1000), d, 48);
        status_dot(x + 30, y + 62, C_OK);
        wfmt(buf, 96, tr(S_ST_RUNNING), (unsigned long)g_pid, d);
        txt(F_BOLD, C_TEXT, x + 50, y + 50, w - 70, 26, T_LINE, buf);
    } else if (g_last.code == SESSION_NONE) {
        txt(F_BODY, C_DIM, x + 22, y + 52, w - 44, 60, DT_WORDBREAK, tr(S_LS_NONE));
    } else {
        exit_text(g_last.code, t2, 96);
        txt(F_SMALL, C_DIM, x + 22, y + 56, 90, 20, T_LINE, tr(S_LS_START));
        txt(F_BODY, C_TEXT, x + 122, y + 54, w - 144, 22, T_LINE, g_last.start);
        fmt_duration(g_last.duration_s, buf, 96);
        txt(F_SMALL, C_DIM, x + 22, y + 90, 90, 20, T_LINE, tr(S_LS_DURATION));
        txt(F_BODY, C_TEXT, x + 122, y + 88, w - 144, 22, T_LINE, buf);
        txt(F_SMALL, C_DIM, x + 22, y + 124, 90, 20, T_LINE, tr(S_LS_RESULT));
        txt(F_BODY, g_last.code == 0 || g_last.code == SESSION_STOPPED ? C_TEXT : C_WARN, x + 122,
            y + 122, w - 144, 22, T_LINE, t2);
        txt(F_SMALL, C_DIM, x + 22, y + 158, 90, 20, T_LINE, tr(S_LS_LOG));
        txt(F_BODY, C_TEXT, x + 122, y + 156, w - 144, 22, T_LINE,
            g_last.log[0] ? base_name(g_last.log) : L"—");
        ui_button(ID_VIEW_LOG, x + w - 22 - 120, y + h - 50, 120, 36, tr(S_BTN_VIEW_LOG), 0, 1);
    }
}

static void step_row(int x, int y, int n, const wchar_t *text) {
    wchar_t d[4];
    fill_circle(x + 12, y + 12, 12, 0x6c0f16);
    fill_circle(x + 12, y + 12, 11, 0x240a0d);
    wfmt(d, 4, L"%d", n);
    txt(F_BOLD, C_RED_HOT, x, y, 24, 24, DT_SINGLELINE | DT_VCENTER | DT_CENTER, d);
    txt(F_BODY, C_TEXT, x + 38, y, 250, 24, T_LINE, text);
}

static void play_cta_card(int x, int y, int w, int h) {
    int rx = x + 566;
    ui_card(x, y, w, h);
    int cw = w - 566 - 26;
    txt_chrome(F_H1, x + 28, y + 20, 520, 46, DT_SINGLELINE | DT_VCENTER, 0, 3, tr(S_CTA_TITLE));
    txt(F_BODY, C_DIM, x + 28, y + 76, 506, 66, DT_WORDBREAK, tr(S_CTA_BODY));
    if (g_note.kind != NK_NONE)
        note_line(x + 28, y + 146, 506, 40);
    ui_plate(ID_IMPORT_ISO, x + 28, y + h - 72, 290, 52, tr(S_CTA_BTN_ISO), 1);
    ui_button(ID_IMPORT_FOLDER, x + 28 + 304, y + h - 72, 200, 52, tr(S_CTA_BTN_FOLDER), 0, 1);
    fill_rect(rx - 24, y + 24, 1, h - 48, C_LINE);
    step_row(rx, y + 28, 1, tr(S_CTA_STEP1));
    step_row(rx, y + 64, 2, tr(S_CTA_STEP2));
    step_row(rx, y + 100, 3, tr(S_CTA_STEP3));
    caption(rx, y + 150, cw, tr(S_CTA_SAVE_IN));
    data_path(rx, y + 170, cw);
    data_links(rx, y + 192, cw);
    txt(F_TINY, C_FAINT, rx, y + 224, cw, 34, DT_WORDBREAK, tr(S_CTA_DROP));
}

static void play_import_card(int x, int y, int w, int h, const imp_status_t *st) {
    wchar_t buf[300], line[400];
    int permille = st->total > 0 ? (int)(st->done * 1000 / st->total) : 0;
    double gb = 1024.0 * 1024.0 * 1024.0;
    ui_card(x, y, w, h);
    txt_chrome(F_H1, x + 28, y + 20, w - 56, 46, DT_SINGLELINE | DT_VCENTER, 0, 3, tr(S_IMP_TITLE));
    if (st->phase == IMPH_SCAN)
        wfmt(line, 400, L"%ls", tr(S_IMP_SCAN));
    else if (st->phase == IMPH_FINISH)
        wfmt(line, 400, L"%ls", tr(S_IMP_FINISH));
    else
        wfmt(line, 400, tr(S_IMP_EXTRACT), st->current);
    txt(F_BODY, C_DIM, x + 28, y + 78, w - 56, 24, DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS, line);
    ui_progress(x + 28, y + 120, w - 56, 16, permille, 1);
    wfmt(buf, 300, tr(S_IMP_AMOUNT), permille / 10, (double)st->done / gb, (double)st->total / gb);
    txt(F_BOLD, C_TEXT, x + 28, y + 150, w / 2, 24, T_LINE, buf);
    if (st->phase == IMPH_EXTRACT && st->elapsed_ms > 3000 && st->done > 0) {
        double mbs = ((double)st->done / 1048576.0) / ((double)st->elapsed_ms / 1000.0);
        double left_s = ((double)(st->total - st->done) / 1048576.0) / (mbs > 0.01 ? mbs : 0.01);
        if (left_s < 60.0)
            wfmt(buf, 300, tr(S_IMP_SPEED_SEC), mbs);
        else
            wfmt(buf, 300, tr(S_IMP_SPEED), mbs, (long)(left_s / 60.0 + 0.5));
        txt(F_SMALL, C_DIM, x + w / 2, y + 152, w / 2 - 28, 22, DT_SINGLELINE | DT_VCENTER | DT_RIGHT, buf);
    }
    ui_button(ID_IMPORT_CANCEL, x + 28, y + h - 58, 140, 38, tr(S_BTN_CANCEL), 0, 1);
    txt(F_TINY, C_FAINT, x + 190, y + h - 58, w - 218, 38, DT_WORDBREAK | DT_VCENTER, tr(S_IMP_HINT));
}

/* Band over the hero art: a newer release, its download, a failure, or the update that just
 * finished. Nothing when the check found nothing (or failed: offline is not news). */
static void play_update_band(int x, int y, int w) {
    upd_status_t us;
    wchar_t line[200];
    int bw1, bw2, h = 48;
    upd_status(&us);
    if (g_updated_to[0] && us.state != UPD_AVAILABLE) {
        ui_card(x, y, w, h);
        wfmt(line, 200, tr(S_UPD_DONE), g_updated_to);
        txt(F_BOLD, C_TEXT, x + 20, y, w - 40, h, T_LINE, line);
        return;
    }
    if (!(us.state == UPD_AVAILABLE || us.state == UPD_DOWNLOADING || us.state == UPD_READY ||
          (us.state == UPD_ERROR && g_upd_wanted) || g_upd_fail[0]))
        return;
    ui_card(x, y, w, h);
    bw1 = text_w(F_BOLD, tr(S_UPD_NOTES)) + 32;
    bw2 = text_w(F_BOLD, tr(S_UPD_GO)) + 32;
    if (us.state == UPD_DOWNLOADING) {
        int permille = us.total > 0 ? (int)(us.done * 1000 / us.total) : 0;
        wfmt(line, 200, tr(S_UPD_DOWNLOADING), us.man.version, permille / 10);
        txt(F_BOLD, C_TEXT, x + 20, y, w / 2, h, T_LINE, line);
        ui_progress(x + w / 2, y + h / 2 - 6, w / 2 - 20, 12, permille, 1);
        return;
    }
    if (g_upd_fail[0] || us.state == UPD_ERROR) {
        wfmt(line, 200, L"%ls %ls", tr(S_UPD_FAILED), g_upd_fail[0] ? g_upd_fail : tr(S_UPD_RETRY));
        txt(F_BOLD, C_WARN, x + 20, y, w - 60 - bw1 - bw2, h, T_LINE, line);
    } else {
        wfmt(line, 200, tr(S_UPD_AVAILABLE), us.man.version);
        txt(F_BOLD, C_TEXT, x + 20, y, w - 60 - bw1 - bw2, h, T_LINE, line);
    }
    ui_button(ID_UPD_NOTES, x + w - 20 - bw2 - 10 - bw1, y + 8, bw1, 32, tr(S_UPD_NOTES), 2, 1);
    ui_button(ID_UPD_GO, x + w - 20 - bw2, y + 8, bw2, 32, tr(S_UPD_GO), 0,
              !running() && !importing() && (us.state != UPD_READY || g_upd_fail[0]));
}

static void page_play(void) {
    imp_status_t st;
    int x = PANEL_X, w = PANEL_W, cw1 = 450, cw2 = PANEL_W - 450 - 20;
    get_import(&st);
    /* The tagline sits under the speed line of the hero artwork. */
    txt_chrome(F_TAG, 650, 186, 340, 24, DT_SINGLELINE | DT_VCENTER, 0, 4, tr(S_PLAY_TAGLINE));
    if (st.state == IMP_RUNNING)
        play_import_card(x, HERO_CARD_Y, w, HERO_CARD_H, &st);
    else if (!xbe_ok() && !g_game_exists)
        play_cta_card(x, HERO_CARD_Y, w, HERO_CARD_H);
    else {
        play_game_card(x, HERO_CARD_Y, cw1, HERO_CARD_H);
        play_last_card(x + cw1 + 20, HERO_CARD_Y, cw2, HERO_CARD_H);
    }
    if (st.state != IMP_RUNNING)
        play_update_band(x, HERO_CARD_Y - 48 - 12, w);
}

/* -------------------------------------------------------------- other pages ---- */

static void page_graphics(void) {
    int y = 128;
    page_header(tr(S_NAV_GRAPHICS), tr(S_GFX_SUB));
    section_title(y, tr(S_GFX_SECTION));
    y += 52;
    ui_combo(ID_CB_RENDERER, PANEL_X, y, PANEL_W, tr(S_GFX_RENDERER), tr(S_GFX_RENDERER_DESC));
    y += ROW_H;
    ui_combo(ID_CB_SCALE, PANEL_X, y, PANEL_W, tr(S_GFX_SCALE), tr(S_GFX_SCALE_DESC));
    y += ROW_H;
    ui_check(ID_CHK_WIDESCREEN, PANEL_X, y, PANEL_W, tr(S_GFX_WIDE), tr(S_GFX_WIDE_DESC), g_set.widescreen);
    y += ROW_H;
    ui_check(ID_CHK_FULLSCREEN, PANEL_X, y, PANEL_W, tr(S_GFX_FULL), tr(S_GFX_FULL_DESC), g_set.fullscreen);
    y += ROW_H;
    ui_check(ID_CHK_PACING, PANEL_X, y, PANEL_W, tr(S_GFX_FPS), tr(S_GFX_FPS_DESC), g_set.pacing);
    y += ROW_H + 18;
    ui_card(PANEL_X, y, PANEL_W, 60);
    txt(F_SMALL, C_DIM, PANEL_X + 22, y + 14, PANEL_W - 44, 40, DT_WORDBREAK, tr(S_GFX_NOTE));
}

static void page_audio(void) {
    int y = 128;
    page_header(tr(S_NAV_AUDIO), tr(S_AUD_SUB));
    section_title(y, tr(S_AUD_SECTION));
    y += 52;
    ui_check(ID_CHK_MUTE, PANEL_X, y, PANEL_W, tr(S_AUD_MUTE), tr(S_AUD_MUTE_DESC), g_set.mute);
    y += ROW_H;
    ui_check(ID_CHK_WAV, PANEL_X, y, PANEL_W, tr(S_AUD_WAV), tr(S_AUD_WAV_DESC), g_set.audio_wav);
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
        kw = text_w(F_SMALL, one) + 18;
        if (kw < 30)
            kw = 30;
        fill_rrect(cx, y + 2, kw, 24, 5, 0x30303a);
        fill_rrect_grad(cx, y + 2, kw, 22, 5, 0x3c3c48, 0x24242d);
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
    int cw = (PANEL_W - 20) / 2, y0 = 128, y, x2 = PANEL_X + cw + 20, xa = PANEL_X + 22, w = cw - 44;
    page_header(tr(S_NAV_CONTROLS), tr(S_CTL_SUB));
    ui_card(PANEL_X, y0, cw, 458);
    ui_card(x2, y0, cw, 458);
    caption(PANEL_X + 22, y0 + 18, cw, tr(S_CTL_KEYBOARD));
    caption(x2 + 22, y0 + 18, cw, tr(S_CTL_PAD));
    y = y0 + 44;
    y = control_row(xa, y, w, L"Enter", tr(S_CTL_START_A));
    y = control_row(xa, y, w, L"Esc", tr(S_CTL_BACK_B));
    y = control_row(xa, y, w, L"↑|↓|←|→", tr(S_CTL_DPAD));
    y = control_row(xa, y, w, L"W|A|S|D", tr(S_CTL_LSTICK));
    y = control_row(xa, y, w, L"J", L"A");
    y = control_row(xa, y, w, L"K", L"B");
    y = control_row(xa, y, w, L"L", L"X");
    y = control_row(xa, y, w, L"I", L"Y");
    y = control_row(xa, y, w, L"Q", tr(S_CTL_WHITE));
    y = control_row(xa, y, w, L"E", tr(S_CTL_BLACK));
    y = control_row(xa, y, w, L"U", tr(S_CTL_LTRIG));
    y = control_row(xa, y, w, L"O", tr(S_CTL_RTRIG));
    y = control_row(xa, y, w, L"F12", tr(S_CTL_SHOT));
    (void)control_row(xa, y, w, L"F11", tr(S_CTL_RDOC));
    y = y0 + 44;
    xa = x2 + 22;
    y = control_row(xa, y, w, L"A|B|X|Y", L"A, B, X, Y");
    y = control_row(xa, y, w, L"LB", tr(S_CTL_WHITE));
    y = control_row(xa, y, w, L"RB", tr(S_CTL_BLACK));
    y = control_row(xa, y, w, L"LT|RT", tr(S_CTL_TRIGS));
    y = control_row(xa, y, w, tr(S_CTL_KEY_LSTICK), tr(S_CTL_LSTICK));
    y = control_row(xa, y, w, tr(S_CTL_KEY_RSTICK), tr(S_CTL_RSTICK));
    y = control_row(xa, y, w, L"L3|R3", tr(S_CTL_CLICK));
    y = control_row(xa, y, w, tr(S_CTL_DPAD), tr(S_CTL_DPAD));
    y = control_row(xa, y, w, L"Start", L"Start");
    y = control_row(xa, y, w, L"Back", L"Back");
    txt(F_SMALL, C_DIM, xa, y + 14, w, 70, DT_WORDBREAK, tr(S_CTL_BOTH));
}

static void page_advanced(void) {
    int y = 128, x = PANEL_X, bh, b1, b2, b3;
    wchar_t cmd[4096];
    const wchar_t *copy_label = g_copied_until ? tr(S_BTN_COPIED) : tr(S_BTN_COPY_INFO);
    page_header(tr(S_NAV_ADVANCED), tr(S_ADV_SUB));
    ui_check(ID_CHK_UPDATES, PANEL_X, y, PANEL_W, tr(S_ADV_UPDATES), tr(S_ADV_UPDATES_DESC),
             g_set.check_updates);
    y += ROW_H;
    section_title(y, tr(S_ADV_DEBUG));
    y += 52;
    ui_check(ID_CHK_RDOC, PANEL_X, y, PANEL_W, tr(S_ADV_RDOC), tr(S_ADV_RDOC_DESC), g_set.rdoc_key);
    y += ROW_H;
    ui_check(ID_CHK_LENIENT, PANEL_X, y, PANEL_W, tr(S_ADV_LENIENT), tr(S_ADV_LENIENT_DESC),
             g_set.lenient);
    y += ROW_H;
    ui_check(ID_CHK_DETAIL, PANEL_X, y, PANEL_W, tr(S_ADV_DETAIL), tr(S_ADV_DETAIL_DESC),
             g_set.detailed_log);
    y += ROW_H;
    if (g_set.detailed_log) { /* the cap only matters for the full log */
        ui_combo(ID_CB_LOGCAP, PANEL_X, y, PANEL_W, tr(S_ADV_LOGCAP), tr(S_ADV_LOGCAP_DESC));
        y += ROW_H;
    }
    /* The copy button keeps its width when its label turns into "Copied". */
    b1 = text_w(F_BOLD, tr(S_BTN_VIEW_LOG)) + 36;
    b2 = text_w(F_BOLD, tr(S_BTN_OPEN_RUNS)) + 36;
    b3 = text_w(F_BOLD, tr(S_BTN_COPY_INFO)) + 36;
    if (text_w(F_BOLD, tr(S_BTN_COPIED)) + 36 > b3)
        b3 = text_w(F_BOLD, tr(S_BTN_COPIED)) + 36;
    ui_button(ID_VIEW_LOG, x, y, b1, 36, tr(S_BTN_VIEW_LOG), 0, 1);
    ui_button(ID_OPEN_RUNS, x + b1 + 10, y, b2, 36, tr(S_BTN_OPEN_RUNS), 0, 1);
    ui_button(ID_COPY_INFO, x + b1 + b2 + 20, y, b3, 36, copy_label, 0, 1);
    y += 36 + 10;
    build_cmdline(cmd, 4096);
    caption(PANEL_X, y, 400, tr(S_ADV_CMDLINE));
    bh = PANEL_BOTTOM - (y + 22); /* what is left; the box shrinks when the combo is shown */
    if (bh > 76)
        bh = 76;
    if (bh < 40)
        return; /* no room with the log-cap combo open: the command line stays hidden */
    fill_rrect(PANEL_X, y + 22, PANEL_W, bh, 5, C_FIELD);
    stroke_rrect(PANEL_X, y + 22, PANEL_W, bh, 5, C_LINE);
    txt(F_MONO, 0xb9bcc6, PANEL_X + 14, y + 28, PANEL_W - 28, bh - 12, DT_WORDBREAK | DT_END_ELLIPSIS, cmd);
}

static rgb_t log_line_color(const wchar_t *line) {
    wchar_t low[160];
    size_t i;
    for (i = 0; i < 159 && line[i]; i++)
        low[i] = (wchar_t)towlower(line[i]);
    low[i] = 0;
    if (wcsstr(low, L"crash") || wcsstr(low, L"exception") || wcsstr(low, L"failed") ||
        wcsstr(low, L"unimplemented") || wcsstr(low, L"not implemented"))
        return C_BAD;
    if (wcsstr(low, L"----") || wcsstr(low, L"====") || wcsstr(low, L"could not"))
        return C_WARN;
    return 0xbdc0ca;
}

static void page_log(void) {
    int bx = PANEL_X, by = 118, bw = PANEL_W, bh = PANEL_BOTTOM - 118, lh = 17, vis, maxs, i;
    wchar_t buf[300];
    int back_w = text_w(F_BOLD, tr(S_BTN_BACK)) + 36;
    page_header(tr(S_LOG_TITLE), tr(S_LOG_SUB));
    ui_button(ID_OPEN_RUNS, PANEL_X + PANEL_W - 170, 34, 170, 38, tr(S_LOG_OPEN), 0, 1);
    ui_button(ID_LOG_BACK, PANEL_X + PANEL_W - 170 - 10 - back_w, 34, back_w, 38, tr(S_BTN_BACK), 0, 1);
    fill_rrect_a(bx, by, bw, bh, 6, 0x07070a, 240);
    stroke_rrect(bx, by, bw, bh, 6, C_LINE);
    if (!g_log_n) {
        txt(F_BODY, C_DIM, bx + 24, by + 24, bw - 48, 60, DT_WORDBREAK, tr(S_LOG_EMPTY));
        return;
    }
    wfmt(buf, 300, tr(S_LOG_UPDATED), base_name(g_log_path), g_log_stamp);
    txt(F_SMALL, C_DIM, bx + 16, by + 8, bw - 32, 18, T_LINE, buf);
    vis = (bh - 44) / lh;
    maxs = g_log_n > vis ? g_log_n - vis : 0;
    if (g_log_scroll > maxs)
        g_log_scroll = maxs;
    if (g_log_scroll < 0)
        g_log_scroll = 0;
    ui_clip(bx + 12, by + 32, bw - 28, bh - 40);
    for (i = 0; i < vis && g_log_scroll + i < g_log_n; i++) {
        const wchar_t *line = g_log_w + g_log_ls[g_log_scroll + i];
        txt(F_MONO, log_line_color(line), bx + 16, by + 34 + i * lh, bw * 3, lh,
            DT_SINGLELINE | DT_VCENTER, line);
    }
    ui_unclip();
    if (maxs > 0) {
        int th = bh - 52, kh = th * vis / g_log_n;
        if (kh < 24)
            kh = 24;
        fill_rrect(bx + bw - 12, by + 36, 4, th, 2, 0x16161c);
        fill_rrect(bx + bw - 12, by + 36 + (th - kh) * g_log_scroll / maxs, 4, kh, 2, 0x5a3036);
    }
}

/* One credit: the project in bold, what the port takes from it beside it. */
static int credit_row(int x, int y, int w, const wchar_t *name, const wchar_t *what) {
    txt(F_BOLD, C_TEXT, x, y, 290, 24, T_LINE, name);
    txt(F_SMALL, C_DIM, x + 300, y, w - 300, 24, T_LINE, what);
    return y + 24;
}

#define ABOUT_PRODUCT_H 138
#define ABOUT_CREDITS_H 194

static void page_about(void) {
    int y = 116, x = PANEL_X + 22, w = PANEL_W - 44, b1, b2, b3;
    wchar_t buf[96];
    page_header(tr(S_NAV_ABOUT), tr(S_ABOUT_SUB));
    /* product */
    ui_card(PANEL_X, y, PANEL_W, ABOUT_PRODUCT_H);
    txt_chrome(F_H2, x, y + 12, w, 32, DT_SINGLELINE | DT_VCENTER, 0, 1, tr(S_ABOUT_PRODUCT));
    wfmt(buf, 96, tr(S_ABOUT_VERSION), L"" NGB_VERSION_STR);
    txt(F_SMALL, C_DIM, x, y + 46, w, 20, T_LINE, buf);
    txt(F_BODY, C_TEXT, x, y + 66, w, 24, T_LINE, tr(S_ABOUT_AUTHOR));
    b1 = text_w(F_BOLD, tr(S_ABOUT_GITHUB)) + 36;
    b2 = text_w(F_BOLD, tr(S_ABOUT_CONTRIB)) + 36;
    b3 = text_w(F_BOLD, tr(S_ABOUT_LICENSE)) + 36;
    ui_button(ID_ABOUT_REPO, x, y + 94, b1, 34, tr(S_ABOUT_GITHUB), 0, 1);
    ui_button(ID_ABOUT_CONTRIB, x + b1 + 10, y + 94, b2, 34, tr(S_ABOUT_CONTRIB), 0, 1);
    ui_button(ID_ABOUT_LICENSE, x + b1 + b2 + 20, y + 94, b3, 34, tr(S_ABOUT_LICENSE), 0, 1);
    /* credits */
    y += ABOUT_PRODUCT_H + 10;
    ui_card(PANEL_X, y, PANEL_W, ABOUT_CREDITS_H);
    caption(x, y + 14, w, tr(S_ABOUT_CREDITS));
    y += 38;
    y = credit_row(x, y, w, L"xemu", tr(S_CRED_XEMU));
    y = credit_row(x, y, w, L"nxdk", tr(S_CRED_NXDK));
    y = credit_row(x, y, w, tr(S_CRED_CXBX_NAME), tr(S_CRED_CXBX));
    y = credit_row(x, y, w, L"envytools", tr(S_CRED_ENVY));
    y = credit_row(x, y, w, L"RenderDoc", tr(S_CRED_RDOC));
    txt(F_SMALL, C_FAINT, x, y + 6, w, 20, T_LINE, tr(S_CRED_FULL));
    /* legal */
    y = 116 + ABOUT_PRODUCT_H + 10 + ABOUT_CREDITS_H + 10;
    ui_card(PANEL_X, y, PANEL_W, PANEL_BOTTOM - y);
    caption(x, y + 12, w, tr(S_ABOUT_LEGAL));
    y += 34;
    txt(F_SMALL, C_DIM, x, y, w, 20, T_LINE, tr(S_LEGAL_1));
    txt(F_SMALL, C_DIM, x, y + 21, w, 20, T_LINE, tr(S_LEGAL_2));
    txt(F_SMALL, C_DIM, x, y + 42, w, 20, T_LINE, tr(S_LEGAL_3));
    txt(F_SMALL, C_DIM, x, y + 63, w, 20, T_LINE, tr(S_LEGAL_4));
}

/* ------------------------------------------------------------ general painting ---- */

static void paint_sidebar(void) {
    static const int names[PG_COUNT] = {S_NAV_PLAY,     S_NAV_GRAPHICS, S_NAV_AUDIO,
                                        S_NAV_CONTROLS, S_NAV_ADVANCED, S_NAV_ABOUT};
    wchar_t buf[96];
    int i, y;
    wfmt(buf, 96, tr(S_SIDE_TAG), L"" NGB_VERSION_STR);
    txt(F_SMALL, C_DIM, 26, 86, 230, 18, DT_SINGLELINE, buf);
    caption_col(26, 130, 200, C_FAINT, tr(S_SIDE_SECTIONS));
    for (i = 0; i < PG_COUNT; i++) {
        int id = ID_NAV0 + i, sel = g_page == i;
        y = 154 + i * 46;
        if (sel) {
            fill_rrect_grad(14, y, SIDEBAR_W - 28, 40, 5, 0x2a1014, 0x14090b);
            fill_rrect(14, y + 8, 3, 24, 1, C_RED_HOT);
            fill_rrect_a(10, y + 8, 8, 24, 3, C_RED, 60);
        } else if (hot(id)) {
            fill_rrect_a(14, y, SIDEBAR_W - 28, 40, 5, 0xffffff, 12);
        }
        txt(F_BOLD, sel ? 0xffffff : hot(id) ? C_STEEL : C_DIM, 38, y, SIDEBAR_W - 60, 40,
            DT_SINGLELINE | DT_VCENTER, tr(names[i]));
        add_hit(14, y, SIDEBAR_W - 28, 40, id);
    }
    /* language selector */
    caption_col(26, 548, 200, C_FAINT, tr(S_SIDE_LANGUAGE));
    for (i = 0; i < 2; i++) {
        int id = i == 0 ? ID_LANG_EN : ID_LANG_ES, x = 24 + i * 110, sel = g_lang == i;
        if (sel) {
            fill_rrect_grad(x, 572, 102, 32, 5, 0xc4212b, 0x6a0d14);
            stroke_rrect(x, 572, 102, 32, 5, 0xff6a70);
        } else {
            fill_rrect_grad(x, 572, 102, 32, 5, hot(id) ? 0x24242c : 0x17171d, 0x0f0f13);
            stroke_rrect(x, 572, 102, 32, 5, hot(id) ? 0x7a2a32 : 0x33363f);
        }
        txt(F_BOLD, sel ? 0xffffff : C_STEEL, x, 572, 102, 32, DT_SINGLELINE | DT_VCENTER | DT_CENTER,
            tr(i == 0 ? S_LANG_EN_NAME : S_LANG_ES_NAME));
        add_hit(x, 572, 102, 32, id);
    }
    txt(F_TINY, C_FAINT, 26, 620, 218, 70, DT_WORDBREAK, tr(S_LEGAL));
}

static void paint_footer(void) {
    int x = SIDEBAR_W, y = LOGICAL_H - FOOT_H, px = LOGICAL_W - 48 - 224;
    wchar_t buf[300], sum[300], d[48];
    rgb_t dot;
    imp_status_t st;
    int imp;
    get_import(&st);
    imp = st.state == IMP_RUNNING;
    if (running()) {
        fmt_duration((long)((GetTickCount64() - g_t0) / 1000), d, 48);
        wfmt(buf, 300, tr(S_ST_RUNNING), (unsigned long)g_pid, d);
        dot = C_OK;
    } else if (imp) {
        wfmt(buf, 300, tr(S_ST_IMPORTING),
             st.total > 0 ? (int)(st.done * 100 / st.total) : 0);
        dot = C_WARN;
    } else if (!loader_ok()) {
        wfmt(buf, 300, L"%ls", tr(S_ST_NO_LOADER));
        dot = C_BAD;
    } else if (!xbe_ok()) {
        wfmt(buf, 300, L"%ls", tr(S_ST_NO_GAME));
        dot = C_WARN;
    } else {
        wfmt(buf, 300, L"%ls", tr(S_ST_READY));
        dot = C_OK;
    }
    config_summary(sum, 300);
    status_dot(PANEL_X + 8, y + 33, dot);
    txt(F_BOLD, C_TEXT, PANEL_X + 28, y + 20, 520, 26, T_LINE, buf);
    txt(F_SMALL, C_DIM, PANEL_X + 28, y + 48, 520, 20, T_LINE, sum);
    if (running())
        ui_button(ID_STOP, px - 14 - 110, y + 22, 110, 44, tr(S_BTN_STOP), 0, 1);
    if (imp)
        ui_plate(ID_PLAY, px, y + 15, 224, 58, tr(S_BTN_IMPORTING_PLATE), 0);
    else if (!xbe_ok() && !running())
        ui_plate(ID_IMPORT_ISO, px, y + 15, 224, 58, tr(S_BTN_IMPORT_PLATE), 1);
    else
        ui_plate(ID_PLAY, px, y + 15, 224, 58, tr(S_BTN_PLAY), can_play());
    (void)x;
}

/* --demo widgets: every lacquer widget and state on one screen, for review. */
static void paint_widgets_demo(void) {
    int x = 60, y = 40, i;
    fill_rect(0, 0, LOGICAL_W, LOGICAL_H, C_BG);
    kamon_draw(80, 70, 14, C_VERM);
    txt_tracked(F_TAB, C_CREAM, 104, 56, 28, 0.34f, L"NINJA GAIDEN");
    for (i = 0; i < 4; i++)
        ui_tab(900 + i, 300 + i * 110, 42, 56, i == 0 ? tr(S_NAV_PLAY) : i == 1 ? tr(S_NAV_GRAPHICS) : i == 2 ? tr(S_NAV_CONTROLS) : tr(S_NAV_ABOUT), i == 0);
    ui_hair(0, 98, LOGICAL_W);
    y = 130;
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
    y += 90;
    ui_hair(x, y, 700);
    txt(F_VALUE, C_CREAM, x, y + 14, 400, 24, T_LINE, L"Resolución interna");
    txt(F_LSMALL, C_DIMC, x, y + 40, 600, 20, T_LINE, L"Dibuja por encima de los 720×480 de la Xbox.");
    ui_hair(x, y + 76, 700);
    if (ui_has_kanji())
        txt(F_KANJI, C_VERM, x + 760, 140, 40, 200, DT_WORDBREAK, L"忍者外伝");
    txt(F_DISPLAY, C_CREAM, x, 560, 1000, 100, T_LINE, L"NINJA GAIDEN");
}

static void paint_all(void) {
    if (g_demo == 3) {
        ui_begin(ART_INNER);
        paint_widgets_demo();
        ui_end();
        return;
    }
    ui_begin(g_page == PG_PLAY ? ART_HERO : ART_INNER);
    paint_sidebar();
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
    paint_footer();
    paint_overlay();
    ui_end();
}

/* -------------------------------------------------------------------- actions ---- */

static void goto_page(int page) {
    g_page = page;
    g_open_combo = ID_NONE;
    g_log_view = 0;
}

static void open_log_view(void) {
    g_page = PG_ADVANCED;
    g_open_combo = ID_NONE;
    g_log_view = 1;
    g_log_sig = 0;
    log_reload();
}

static void set_language(int lang) {
    g_lang = lang;
    save_settings();
    if (g_hwnd)
        SetWindowTextW(g_hwnd, tr(S_WIN_TITLE));
}

static void activate(int id) {
    wchar_t path[MAX_PATH];
    if (id >= ID_NAV0 && id < ID_NAV0 + PG_COUNT) {
        goto_page(id - ID_NAV0);
        return;
    }
    if (id >= ID_ITEM0 && g_open_combo != ID_NONE) {
        int i = id - ID_ITEM0;
        if (g_open_combo == ID_CB_RENDERER)
            g_set.renderer = i;
        else if (g_open_combo == ID_CB_SCALE)
            g_set.scale = i;
        else
            g_set.log_cap = k_logcap_vals[i];
        g_open_combo = ID_NONE;
        save_settings();
        return;
    }
    g_open_combo = g_open_combo == id ? ID_NONE
                                      : (id == ID_CB_RENDERER || id == ID_CB_SCALE || id == ID_CB_LOGCAP
                                             ? id
                                             : ID_NONE);
    switch (id) {
    case ID_PLAY: start_game(); break;
    case ID_STOP: stop_game(); break;
    case ID_IMPORT_ISO:
        if (!importing() && pick_iso(path))
            begin_iso_import(path, 1);
        break;
    case ID_IMPORT_FOLDER:
        if (!importing() && pick_folder(tr(S_DLG_PICK_FOLDER), path))
            use_folder(path);
        break;
    case ID_IMPORT_CANCEL:
        if (g_demo)
            g_demo = 0;
        else
            import_cancel();
        break;
    case ID_DATA_CHANGE: change_data_folder(); break;
    case ID_DATA_OPEN: open_folder(g_data); break;
    case ID_VIEW_LOG: open_log_view(); break;
    case ID_LOG_BACK: g_log_view = 0; break;
    case ID_OPEN_RUNS: open_folder(g_runs); break;
    case ID_COPY_INFO: copy_session_info(); break;
    case ID_ABOUT_REPO: ShellExecuteW(g_hwnd, L"open", URL_REPO, NULL, NULL, SW_SHOWNORMAL); break;
    case ID_ABOUT_CONTRIB: ShellExecuteW(g_hwnd, L"open", URL_CONTRIB, NULL, NULL, SW_SHOWNORMAL); break;
    case ID_ABOUT_LICENSE: ShellExecuteW(g_hwnd, L"open", URL_LICENSE, NULL, NULL, SW_SHOWNORMAL); break;
    case ID_CHK_PACING: g_set.pacing = !g_set.pacing; save_settings(); break;
    case ID_CHK_FULLSCREEN: g_set.fullscreen = !g_set.fullscreen; save_settings(); break;
    case ID_CHK_WIDESCREEN: g_set.widescreen = !g_set.widescreen; save_settings(); break;
    case ID_CHK_MUTE: g_set.mute = !g_set.mute; save_settings(); break;
    case ID_CHK_WAV: g_set.audio_wav = !g_set.audio_wav; save_settings(); break;
    case ID_CHK_RDOC: g_set.rdoc_key = !g_set.rdoc_key; save_settings(); break;
    case ID_CHK_LENIENT: g_set.lenient = !g_set.lenient; save_settings(); break;
    case ID_CHK_DETAIL: g_set.detailed_log = !g_set.detailed_log; save_settings(); break;
    case ID_CHK_UPDATES: g_set.check_updates = !g_set.check_updates; save_settings(); break;
    case ID_UPD_NOTES: {
        upd_status_t us;
        wchar_t url[256];
        upd_status(&us);
        wfmt(url, 256, L"%hs", us.man.notes); /* https:// checked by upd_parse_manifest */
        if (url[0])
            ShellExecuteW(g_hwnd, L"open", url, NULL, NULL, SW_SHOWNORMAL);
        break;
    }
    case ID_UPD_GO: {
        upd_status_t us;
        upd_status(&us);
        g_upd_fail[0] = 0;
        if (running() || importing() || !g_data[0])
            break;
        if (us.state == UPD_READY || upd_download_start(g_data)) /* READY: a failed apply, retried */
            g_upd_wanted = 1;
        break;
    }
    case ID_LANG_EN: set_language(LANG_EN); break;
    case ID_LANG_ES: set_language(LANG_ES); break;
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

/* --fonts FILE: the face GDI selected for the lacquer fonts, one "key=face" per line (UTF-8). */
static int write_fonts_and_quit(const wchar_t *path) {
    static const struct {
        const char *key;
        int font;
    } list[] = {{"body", F_LBODY}, {"label", F_LABEL}, {"display", F_DISPLAY}, {"kanji", F_KANJI}};
    FILE *f = _wfopen(path, L"wb");
    size_t i;
    if (!f)
        return 1;
    for (i = 0; i < sizeof list / sizeof list[0]; i++) {
        wchar_t face[LF_FACESIZE];
        char u8[LF_FACESIZE * 3];
        ui_font_face(list[i].font, face, LF_FACESIZE);
        if (list[i].font == F_KANJI && !ui_has_kanji())
            wcscpy(face, L"none");
        WideCharToMultiByte(CP_UTF8, 0, face, -1, u8, (int)sizeof u8, NULL, NULL);
        fprintf(f, "%s=%s\n", list[i].key, u8);
    }
    fclose(f);
    return 0;
}

static void write_shot_and_quit(void) {
    imp_status_t st;
    paint_all();
    ui_save_bmp(g_shot_path);
    import_status(&st);
    if (st.state == IMP_RUNNING) {
        int i;
        import_cancel();
        for (i = 0; i < 200 && st.state == IMP_RUNNING; i++) {
            Sleep(50);
            import_status(&st);
        }
    }
    PostQuitMessage(0);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint_all();
        ui_present(dc);
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
            g_open_combo = ID_NONE; /* a click outside closes the drop-down and does nothing else */
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
        if (log_view() && g_log_n) {
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
        } else if (log_view()) {
            if (wp == VK_ESCAPE) {
                g_log_view = 0;
                redraw();
                return 0;
            }
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
    case WM_DROPFILES: {
        wchar_t p[MAX_PATH];
        if (DragQueryFileW((HDROP)wp, 0, p, MAX_PATH) && !importing())
            import_from_path(p, 1);
        DragFinish((HDROP)wp);
        redraw();
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_TICK) {
            int was_running = running(), was_importing = importing();
            g_tick++;
            poll_import();
            poll_update();
            if (g_tick % SLOW_EVERY == 0) {
                poll_game();
                if (log_view() || running())
                    log_reload();
            }
            if (g_copied_until && GetTickCount64() >= g_copied_until) {
                g_copied_until = 0;
                redraw();
            }
            if (importing())
                g_import_seen = 1;
            if (g_shot_path[0] && (g_shot_after_import ? (g_import_seen && !importing() && g_tick > 5)
                                                       : g_tick * TICK_MS >= g_shot_delay)) {
                write_shot_and_quit();
                return 0;
            }
            if (importing() || was_importing ||
                (g_tick % SLOW_EVERY == 0 && (running() || was_running || log_view())))
                redraw();
        }
        return 0;
    case WM_CLOSE:
        if (importing()) {
            int i;
            imp_status_t st;
            if (MessageBoxW(hwnd, tr(S_DLG_CLOSE), tr(S_DLG_TITLE), MB_YESNO | MB_ICONQUESTION) != IDYES)
                return 0;
            import_cancel();
            for (i = 0; i < 300; i++) { /* the worker removes its partial copy */
                import_status(&st);
                if (st.state != IMP_RUNNING)
                    break;
                Sleep(50);
            }
        }
        DestroyWindow(hwnd);
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

/* ------------------------------------------------------------------ start-up ---- */

/* Dark title bar (Windows 10 20H1+ / 11); if dwmapi does not know it, nothing changes. */
static void dark_title_bar(HWND hwnd) {
    typedef HRESULT(WINAPI * dwm_set_fn)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    dwm_set_fn set;
    BOOL on = TRUE;
    COLORREF cap = RGB(5, 5, 8), txtc = RGB(230, 232, 238);
    if (!dwm)
        return;
    set = (dwm_set_fn)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (set) {
        set(hwnd, 20, &on, sizeof on);     /* DWMWA_USE_IMMERSIVE_DARK_MODE */
        set(hwnd, 35, &cap, sizeof cap);   /* DWMWA_CAPTION_COLOR (Windows 11) */
        set(hwnd, 34, &cap, sizeof cap);   /* DWMWA_BORDER_COLOR */
        set(hwnd, 36, &txtc, sizeof txtc); /* DWMWA_TEXT_COLOR */
    }
    FreeLibrary(dwm);
}

static void upd_remove_old_dir(void) {
    wchar_t dir[MAX_PATH];
    size_t n;
    wfmt(dir, MAX_PATH, L"%ls", g_exe_dir);
    n = wcslen(dir);
    if (n && dir[n - 1] == L'\\')
        dir[n - 1] = 0;
    upd_remove_old(dir);
}

/* Update work driven by the tick: delete what a previous update left (the old launcher may
 * still be exiting), redraw while downloading, and once the zip is verified and extracted,
 * swap the binaries and restart into the new launcher. */
static void poll_update(void) {
    static int last_state = -1;
    upd_status_t us;
    wchar_t dir[MAX_PATH], exe[MAX_PATH], cmd[MAX_PATH + 64];
    size_t n;
    if (g_tick <= 50 && g_tick % 10 == 1)
        upd_remove_old_dir();
    upd_status(&us);
    if (us.state != last_state || us.state == UPD_DOWNLOADING)
        redraw();
    last_state = us.state;
    if (us.state != UPD_READY || !g_upd_wanted || running() || importing())
        return;
    g_upd_wanted = 0;
    wfmt(dir, MAX_PATH, L"%ls", g_exe_dir);
    n = wcslen(dir);
    if (n && dir[n - 1] == L'\\')
        dir[n - 1] = 0;
    if (!upd_apply(dir, us.staged, g_upd_fail, 160)) {
        redraw();
        return;
    }
    wfmt(exe, MAX_PATH, L"%lsngb-launcher.exe", g_exe_dir);
    wfmt(cmd, MAX_PATH + 64, L"\"%ls\" --updated %hs", exe, us.man.version);
    {
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, g_exe_dir, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            DestroyWindow(g_hwnd);
        } else { /* the new binaries are in place: they start with the next launch */
            wfmt(g_upd_fail, 160, L"%ls", tr(S_UPD_RESTART));
            redraw();
        }
    }
}

static void init_paths(void) {
    wchar_t *cut;
    GetModuleFileNameW(NULL, g_exe_dir, MAX_PATH);
    cut = wcsrchr(g_exe_dir, L'\\');
    if (cut)
        cut[1] = 0;
    wfmt(g_ini, MAX_PATH, L"%lsngb.ini", g_exe_dir);
    wfmt(g_loader, MAX_PATH, L"%lsngb-loader.exe", g_exe_dir);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
    WNDCLASSEXW wc;
    RECT r, work;
    POINT cur;
    MSG msg;
    HDC dc;
    int x, y, ow, oh, argc = 0, i, dpi, forced_lang = -1;
    wchar_t iso[MAX_PATH], fonts_path[MAX_PATH] = L"";
    LPWSTR *argv;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    (void)prev;
    (void)cmdline;
    (void)show;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    dc = GetDC(NULL);
    dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    init_paths();
    ini_ensure();
    load_settings();
    iso[0] = 0;
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (i = 1; argv && i < argc; i++) {
        const wchar_t *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : L"";
        if (!wcscmp(a, L"--shot-after-import")) {
            g_shot_after_import = 1;
            continue;
        }
        if (!wcscmp(a, L"--log-view")) {
            g_log_view = 1;
            continue;
        }
        if (!wcscmp(a, L"--no-embedded-fonts")) {
            g_no_embedded_fonts = 1;
            continue;
        }
        if (!wcscmp(a, L"--page")) {
            int pg = _wtoi(v);
            if (pg >= 0 && pg < PG_COUNT)
                g_page = pg;
        } else if (!wcscmp(a, L"--lang")) {
            forced_lang = !wcscmp(v, L"es") ? LANG_ES : LANG_EN;
        } else if (!wcscmp(a, L"--iso")) {
            wfmt(iso, MAX_PATH, L"%ls", v);
        } else if (!wcscmp(a, L"--shot")) {
            wfmt(g_shot_path, MAX_PATH, L"%ls", v);
        } else if (!wcscmp(a, L"--shot-delay")) {
            g_shot_delay = _wtoi(v);
        } else if (!wcscmp(a, L"--update-url")) {
            wfmt(g_upd_url, 1024, L"%ls", v);
            g_upd_forced = 1;
        } else if (!wcscmp(a, L"--updated")) {
            wfmt(g_updated_to, 32, L"%ls", v);
        } else if (!wcscmp(a, L"--dpi")) {
            dpi = _wtoi(v); /* review aid: render as if the system DPI were this */
        } else if (!wcscmp(a, L"--fonts")) {
            wfmt(fonts_path, MAX_PATH, L"%ls", v);
        } else if (!wcscmp(a, L"--demo")) {
            g_demo = !wcscmp(v, L"progress") ? 1 : !wcscmp(v, L"widgets") ? 3 : 2;
        } else {
            continue; /* unknown: do not swallow the next argument */
        }
        i++;
    }
    if (argv)
        LocalFree(argv);
    if (forced_lang >= 0)
        g_lang = forced_lang;
    if (g_demo == 2)
        note_error(IMPE_NOT_NGB, L"Halo: Combat Evolved", 0);
    ui_init(dpi);
    if (fonts_path[0])
        return write_fonts_and_quit(fonts_path);
    if (g_updated_to[0] && g_data[0]) { /* the update is in place: its zip and staging can go */
        wchar_t upd[MAX_PATH];
        wfmt(upd, MAX_PATH, L"%ls\\updates", g_data);
        remove_tree(upd);
    }
    /* One check a day at most (or now with --update-url); silent when offline. */
    if (!g_shot_path[0] || g_upd_forced) {
        SYSTEMTIME now;
        FILETIME ft;
        ULARGE_INTEGER t;
        int day;
        GetSystemTime(&now);
        SystemTimeToFileTime(&now, &ft);
        t.LowPart = ft.dwLowDateTime;
        t.HighPart = ft.dwHighDateTime;
        day = (int)(t.QuadPart / (10000000ULL * 86400ULL));
        if (g_upd_forced || (g_set.check_updates && day != g_set.update_day)) {
            char ver[32];
            WideCharToMultiByte(CP_UTF8, 0, TEXT(NGB_VERSION_STR), -1, ver, sizeof ver, NULL, NULL);
            if (upd_check_start(g_upd_url, ver) && !g_upd_forced) {
                g_set.update_day = day;
                ini_put_int(L"update", L"last_day", day);
            }
        }
    }
    if (!log_view())
        g_log_view = 0; /* --log-view only applies to the Advanced page */
    else
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

    /* Fixed size, centred on the monitor under the cursor. */
    SetRect(&r, 0, 0, S(LOGICAL_W), S(LOGICAL_H));
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
    g_hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, L"ngb-launcher", tr(S_WIN_TITLE), style, x, y, ow, oh,
                             NULL, NULL, inst, NULL);
    if (!g_hwnd)
        return 1;
    dark_title_bar(g_hwnd);
    SetTimer(g_hwnd, TIMER_TICK, TICK_MS, NULL);
    ShowWindow(g_hwnd, g_shot_path[0] ? SW_SHOWNOACTIVATE : SW_SHOWNORMAL);
    UpdateWindow(g_hwnd);
    if (iso[0])
        import_from_path(iso, 0);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_proc)
        CloseHandle(g_proc); /* the game stays alive if the launcher closes */
    CoUninitialize();
    return (int)msg.wParam;
}
