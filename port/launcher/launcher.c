/*
 * launcher.c: ngb-launcher.exe, the graphical launcher of the Ninja Gaiden Black PC port.
 *
 * One window (1240x700 logical pixels, scaled to the system DPI or to what fits the screen;
 * maximized, the frame is redrawn at the largest scale that fits, centred) drawn entirely by
 * ui.c on a back buffer. This file holds the logic; pages.c paints (launcher_int.h). The
 * backdrop is our own ink-wash landscape (art.c, ink.c) or, in game art mode, artwork read at
 * runtime from the user's own copy (gameart.c). Nothing from the game ships with it.
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
 *       --demo NAME    fake a state for screenshots: progress | error | widgets
 *       --maximized    open maximized
 *       --no-game-art  the original art only, whatever ngb.ini says (README screenshots)
 *       --dpi N        render as if the system DPI were N (review aid)
 *       --fonts FILE   write the faces GDI picked for the fonts and exit (tests)
 *       --perf-anim FILE  write the mean ms of an animated frame and exit (tests)
 *       --no-embedded-fonts  skip the embedded fonts (tests the fallback)
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
#include "launcher_int.h"
#include "gameart.h"
#include "version.h"

/* ------------------------------------------------------------------ constants ---- */

#define TIMER_TICK 1
#define TICK_MS 100
#define SLOW_EVERY 5 /* the game/log poll runs every 5th tick */
#define WM_GAMEART_READY (WM_APP + 1)
#define TIMER_ANIM 2
#define ANIM_MS 50 /* 20 fps: the motion is slow, the CPU stays low */
#define HERO_HOLD_S 14.0 /* each game art hero stays this long... */
#define HERO_FADE_S 1.2  /* ...then crossfades into the next */

#define URL_REPO L"https://github.com/angeldev07/ninja-gaiden-black-port"
#define URL_CONTRIB URL_REPO L"/graphs/contributors"
#define URL_LICENSE URL_REPO L"/blob/main/LICENSE"
#define COPIED_MS 2000 /* how long "Copy session info" says "Copied" */

/* -------------------------------------------------------------------- state ---- */

static const wchar_t *const k_str[S_COUNT][2] = {
#define X(id, en, es) {en, es},
    STRING_TABLE(X)
#undef X
};
int g_lang = LANG_EN;

static HWND g_hwnd;
static wchar_t g_exe_dir[MAX_PATH]; /* with trailing backslash */
static wchar_t g_ini[MAX_PATH];
static wchar_t g_loader[MAX_PATH];
wchar_t g_data[MAX_PATH]; /* resolved data folder */
static wchar_t g_runs[MAX_PATH];
static wchar_t g_upd_url[1024] = UPD_MANIFEST_URL; /* --update-url replaces it (tests) */
static int g_upd_forced;                          /* --update-url: check now, whatever the day */
int g_upd_wanted;                          /* the user pressed Update */
wchar_t g_upd_fail[160];                   /* why the last apply failed */
wchar_t g_updated_to[32];                  /* --updated X: the update that just finished */
static void poll_update(void);
settings_t g_set;
session_t g_last;
note_t g_note;
xbe_info_t g_game; /* probe of g_set.xbe */
int g_game_exists;

int g_page = PG_PLAY;
int g_open_combo = ID_NONE;
static int g_mouse_tracked;
static int g_tick;

static HANDLE g_proc;
DWORD g_pid;
ULONGLONG g_t0;
static int g_stopped_by_user;

wchar_t *g_log_w;
int *g_log_ls;
int g_log_n, g_log_scroll;
wchar_t g_log_path[MAX_PATH];
static ULONGLONG g_log_sig;
wchar_t g_log_stamp[16];
int g_log_view; /* the log viewer is open (a sub-view of the Advanced page) */
ULONGLONG g_copied_until; /* GetTickCount64() deadline of the "Copied" label; 0 = none */

static wchar_t g_shot_path[MAX_PATH];
static int g_shot_delay = 0;
static int g_shot_after_import; /* with --iso: take the shot when the import ends */
static int g_import_seen;
int g_demo; /* 1 progress, 2 error, 3 widgets */
static int g_no_game_art;
static int g_start_maximized; /* --maximized */ /* --no-game-art: the original art only (README screenshots) */

const wchar_t *tr(int id) { return k_str[id][g_lang]; }

/* ------------------------------------------------------------------ utilities ---- */

void wfmt(wchar_t *buf, size_t n, const wchar_t *fmt, ...) {
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

const wchar_t *base_name(const wchar_t *path) {
    const wchar_t *s = wcsrchr(path, L'\\');
    return s ? s + 1 : path;
}


void fmt_duration(long s, wchar_t *out, size_t n) {
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

const int k_logcap_vals[4] = {500, DEFAULT_LOG_CAP, 10000, 50000};

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
    ini_put_int(L"launcher", L"game_art", g_set.game_art);
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
    g_set.game_art = ini_int(L"launcher", L"game_art", 1) != 0;
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

int loader_ok(void) { return file_exists(g_loader); }
int xbe_ok(void) { return g_game_exists && g_game.valid; }
int running(void) { return g_proc != NULL; }
static int log_view(void) { return g_page == PG_ADVANCED && g_log_view; }

void get_import(imp_status_t *st) {
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
int importing(void) {
    imp_status_t st;
    get_import(&st);
    return st.state == IMP_RUNNING;
}
int can_play(void) { return !running() && !importing() && loader_ok() && xbe_ok(); }
int game_art_on(void) { return g_set.game_art && xbe_ok() && !g_no_game_art; }


/* (Re)starts or stops the game art after anything that can change them:
 * start-up, an import, another data folder, the switches. The art arrives on WM_GAMEART_READY. */
static void game_dir(wchar_t *dir) {
    wfmt(dir, MAX_PATH, L"%ls", g_set.xbe);
    if (wcsrchr(dir, L'\\'))
        *wcsrchr(dir, L'\\') = 0;
}

static void update_window_icon(void);

static void refresh_game_art(void) {
    wchar_t dir[MAX_PATH];
    ui_set_game_art(0);
    ui_set_hero(-1, -1, 0.0f);
    if (g_hwnd)
        update_window_icon();
    if (!game_art_on()) {
        gameart_stop();
        return;
    }
    game_dir(dir);
    gameart_notify(g_hwnd, WM_GAMEART_READY);
    gameart_start(dir);
}


/* Internal resolution the chosen scale gives; "auto" follows the loader: enough to cover the
 * picture on the monitor the launcher is on, on both axes (4:3 or 16:9), between 1 and 4. */
void scaled_size(int *w, int *h) {
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
void build_cmdline(wchar_t *out, size_t n) {
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

void note_text(wchar_t *out, size_t n) {
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
    refresh_game_art();
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
        refresh_game_art();
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
    refresh_game_art();
}

/* ----------------------------------------------------------------- the game ---- */

void exit_text(int code, wchar_t *out, size_t n) {
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
    case ID_CHK_GAMEART:
        g_set.game_art = !g_set.game_art;
        save_settings();
        refresh_game_art();
        break;
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

/* --perf-anim FILE: mean milliseconds to paint one whole animated frame (120 frames each of Play
 * and of Graphics), written as "hero_ms=X inner_ms=Y". */
static int write_perf_and_quit(const wchar_t *path) {
    LARGE_INTEGER f, t0, t1;
    double ms[2];
    int kind, i;
    FILE *out;
    QueryPerformanceFrequency(&f);
    for (kind = 0; kind < 2; kind++) {
        g_page = kind == 0 ? PG_PLAY : PG_GRAPHICS;
        paint_all(); /* builds the cached backdrop outside the timing */
        QueryPerformanceCounter(&t0);
        for (i = 0; i < 120; i++) {
            ui_set_motion(i / 20.0);
            paint_all(); /* the whole frame: backdrop, page, top bar */
        }
        QueryPerformanceCounter(&t1);
        ms[kind] = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart / 120.0;
    }
    out = _wfopen(path, L"wb");
    if (!out)
        return 1;
    fprintf(out, "hero_ms=%.3f inner_ms=%.3f\n", ms[0], ms[1]);
    fclose(out);
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

/* ------------------------------------------------------------- living backdrop ---- */

static ULONGLONG g_anim_t0;
static int g_anim_on;
static HICON g_icon_big, g_icon_small;       /* the exe's own icons */
static HICON g_game_icon_big, g_game_icon_small; /* made from the user's copy, game art mode */

/* Which hero the Play page shows at time t: the game's emblem, Ryu and Ayane in turn when the
 * copy has them, else the ink landscape. */
static void update_hero(double t) {
    int list[ART_HERO_COUNT], n = art_heroes(game_art_on(), list), cur, next;
    float mix;
    if (!n) {
        ui_set_hero(-1, -1, 0.0f);
        return;
    }
    art_hero_at(t, n, HERO_HOLD_S, HERO_FADE_S, &cur, &next, &mix);
    ui_set_hero(list[cur], list[next], mix);
}

/* A square icon cut from the game's emblem (the silhouette in its red glow), scaled to size x size. */
static HICON emblem_icon(const ga_image_t *em, int size) {
    BITMAPINFO bi;
    void *bits = NULL;
    HDC dc = GetDC(NULL);
    HBITMAP color, mask;
    ICONINFO ii;
    HICON icon;
    int x, y, cx = em->w * 215 / 512, cy = em->h * 172 / 512, half = em->w * 150 / 512;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!color || !bits)
        return NULL;
    for (y = 0; y < size; y++) /* box filter over the source pixels each icon pixel covers */
        for (x = 0; x < size; x++) {
            int x0 = cx - half + x * 2 * half / size, x1 = cx - half + (x + 1) * 2 * half / size;
            int y0 = cy - half + y * 2 * half / size, y1 = cy - half + (y + 1) * 2 * half / size;
            unsigned r = 0, g = 0, b = 0, n = 0;
            int i, j;
            for (j = y0; j < (y1 > y0 ? y1 : y0 + 1); j++)
                for (i = x0; i < (x1 > x0 ? x1 : x0 + 1); i++)
                    if (i >= 0 && j >= 0 && i < em->w && j < em->h) {
                        uint32_t p = em->px[(size_t)j * em->w + i];
                        r += (p >> 16) & 255;
                        g += (p >> 8) & 255;
                        b += p & 255;
                        n++;
                    }
            ((uint32_t *)bits)[(size_t)y * size + x] =
                0xFF000000u | (n ? ((r / n) << 16) | ((g / n) << 8) | (b / n) : 0);
        }
    mask = CreateBitmap(size, size, 1, 1, NULL);
    memset(&ii, 0, sizeof ii);
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

/* The window and taskbar icons follow the art: the game's emblem in game art mode, else the exe's. */
static void update_window_icon(void) {
    const ga_image_t *em = game_art_on() ? gameart_get(GA_EMBLEM) : NULL;
    if (g_game_icon_big)
        DestroyIcon(g_game_icon_big);
    if (g_game_icon_small)
        DestroyIcon(g_game_icon_small);
    g_game_icon_big = em ? emblem_icon(em, GetSystemMetrics(SM_CXICON)) : NULL;
    g_game_icon_small = em ? emblem_icon(em, GetSystemMetrics(SM_CXSMICON)) : NULL;
    SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)(g_game_icon_big ? g_game_icon_big : g_icon_big));
    SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)(g_game_icon_small ? g_game_icon_small : g_icon_small));
}

/* The backdrop moves only where something moves (the Play hero, the ring of the inner pages
 * in game art mode), while the window is in front, the game is not running and Windows' "Show
 * animations in Windows" setting is on. Screenshots are always still. */
static int anim_wanted(void) {
    BOOL anim = TRUE;
    if (g_shot_path[0] || g_demo || running() || IsIconic(g_hwnd) || GetForegroundWindow() != g_hwnd)
        return 0;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0);
    if (!anim)
        return 0;
    return g_page == PG_PLAY || game_art_on();
}

static void anim_update(void) {
    int want = anim_wanted();
    if (want && !g_anim_on) {
        if (!g_anim_t0)
            g_anim_t0 = GetTickCount64();
        SetTimer(g_hwnd, TIMER_ANIM, g_dpi > 120 ? ANIM_MS * 4 / 3 : ANIM_MS, NULL); /* 15 fps on big DPIs */
    } else if (!want && g_anim_on) {
        KillTimer(g_hwnd, TIMER_ANIM);
    }
    g_anim_on = want;
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
        set_hover(hit_at(ui_mouse_lx((short)LOWORD(lp)), ui_mouse_ly((short)HIWORD(lp))));
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
        int id = hit_at(ui_mouse_lx((short)LOWORD(lp)), ui_mouse_ly((short)HIWORD(lp)));
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
        int id = hit_at(ui_mouse_lx((short)LOWORD(lp)), ui_mouse_ly((short)HIWORD(lp)));
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
        if (wp == VK_RETURN && g_page == PG_PLAY && g_open_combo == ID_NONE && can_play()) {
            start_game();
            redraw();
        } else if (wp == VK_ESCAPE && g_open_combo != ID_NONE) {
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
    case WM_ACTIVATEAPP:
        anim_update();
        break;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED && LOWORD(lp) > 0 && HIWORD(lp) > 0) {
            /* Maximized or restored: redraw at the largest scale that fits, centred. */
            int dpi = art_fit_dpi(4 * 96, LOWORD(lp), HIWORD(lp), 0, 0);
            if (dpi != g_dpi) {
                ui_init(dpi);
                KillTimer(hwnd, TIMER_ANIM); /* its rate depends on the scale */
                g_anim_on = 0;
            }
            ui_set_view(LOWORD(lp), HIWORD(lp));
            anim_update();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case WM_GAMEART_READY:
        /* Always rebuild: a READY from a load that was replaced may have cached the ink. */
        ui_set_game_art(1);
        ui_invalidate_backdrop();
        g_anim_t0 = GetTickCount64(); /* the rotation starts with the emblem */
        update_hero(0.0);
        update_window_icon();
        redraw();
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
        if (wp == TIMER_ANIM) {
            ui_set_motion((double)(GetTickCount64() - g_anim_t0) / 1000.0);
            update_hero((double)(GetTickCount64() - g_anim_t0) / 1000.0);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (wp == TIMER_TICK) {
            anim_update();
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
        KillTimer(hwnd, TIMER_ANIM);
        gameart_stop();
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
    wchar_t iso[MAX_PATH], fonts_path[MAX_PATH] = L"", perf_path[MAX_PATH] = L"";
    LPWSTR *argv;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
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
        if (!wcscmp(a, L"--maximized")) {
            g_start_maximized = 1;
            continue;
        }
        if (!wcscmp(a, L"--no-game-art")) {
            g_no_game_art = 1;
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
        } else if (!wcscmp(a, L"--perf-anim")) {
            wfmt(perf_path, MAX_PATH, L"%ls", v);
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
    if (!g_shot_path[0] && !perf_path[0] && !fonts_path[0]) {
        /* On a 1080p laptop at 150 % the window would be taller than the screen and PLAY would
         * sit under the taskbar: draw at the largest scale that fits the work area instead. */
        RECT fr, wa;
        POINT pt;
        MONITORINFO mi;
        SetRect(&fr, 0, 0, 0, 0);
        AdjustWindowRect(&fr, style, FALSE);
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        memset(&mi, 0, sizeof mi);
        mi.cbSize = sizeof mi;
        if (GetCursorPos(&pt) && GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi))
            wa = mi.rcWork;
        dpi = art_fit_dpi(dpi, wa.right - wa.left, wa.bottom - wa.top, fr.right - fr.left, fr.bottom - fr.top);
    }
    ui_init(dpi);
    if (fonts_path[0])
        return write_fonts_and_quit(fonts_path);
    if (perf_path[0])
        return write_perf_and_quit(perf_path);
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
    g_icon_big = wc.hIcon;
    g_icon_small = wc.hIconSm;
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
    ShowWindow(g_hwnd, g_start_maximized ? SW_SHOWMAXIMIZED : g_shot_path[0] ? SW_SHOWNOACTIVATE : SW_SHOWNORMAL);
    UpdateWindow(g_hwnd);
    refresh_game_art();
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
