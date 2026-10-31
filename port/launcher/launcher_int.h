/*
 * launcher_int.h: state shared by the launcher's logic (launcher.c: settings, import, game
 * process, log, updater, window) and its painting (pages.c). Not a public API.
 */
#pragma once

#define UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "import.h"
#include "strings.h"

#define LOGICAL_W ART_W
#define LOGICAL_H ART_H
#define DEFAULT_LOG_CAP 2000

/* The log viewer is not a page: it is a sub-view of Advanced (g_log_view). */
enum page_id { PG_PLAY, PG_GRAPHICS, PG_AUDIO, PG_CONTROLS, PG_ADVANCED, PG_ABOUT, PG_COUNT };

enum ui_id {
    ID_NONE = 0,
    ID_NAV0 = 1, /* ..ID_NAV0 + PG_COUNT - 1 */
    ID_PLAY = 20, ID_STOP, ID_IMPORT_ISO, ID_IMPORT_FOLDER, ID_IMPORT_CANCEL, ID_DATA_CHANGE,
    ID_DATA_OPEN, ID_VIEW_LOG, ID_OPEN_RUNS, ID_CB_RENDERER, ID_CB_SCALE, ID_CB_LOGCAP, ID_CHK_PACING, ID_CHK_FULLSCREEN, ID_CHK_WIDESCREEN,
    ID_CHK_MUTE, ID_CHK_WAV, ID_CHK_RDOC, ID_CHK_LENIENT, ID_CHK_DETAIL, ID_LANG_EN, ID_LANG_ES,
    ID_LOG_BACK, ID_COPY_INFO, ID_ABOUT_REPO, ID_ABOUT_CONTRIB, ID_ABOUT_LICENSE,
    ID_CHK_UPDATES, ID_UPD_NOTES, ID_UPD_GO, ID_CHK_GAMEART,
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
    int game_art;   /* [launcher] game_art: art from the user's copy */
    int hero_index; /* [launcher] hero_index: which key art this launch shows */
} settings_t;

typedef struct session {
    wchar_t start[48]; /* "2026-10-22 14:03" */
    long duration_s;
    int code; /* exit code; SESSION_STOPPED / SESSION_NONE */
    wchar_t log[MAX_PATH];
} session_t;
#define SESSION_NONE (-2)
#define SESSION_STOPPED (-1)

/* A message shown on the Play page; stored as a code so it follows the language. */
enum note_kind { NK_NONE, NK_OK_IMPORTED, NK_OK_FOLDER, NK_ERR };
typedef struct note {
    int kind, err;
    wchar_t detail[64];
    double num;
} note_t;

/* ---- launcher.c ---- */
extern int g_lang;
extern settings_t g_set;
extern session_t g_last;
extern note_t g_note;
extern xbe_info_t g_game;
extern int g_game_exists;
extern wchar_t g_data[MAX_PATH];
extern int g_page, g_open_combo, g_log_view, g_demo;
extern DWORD g_pid;
extern ULONGLONG g_t0, g_copied_until;
extern wchar_t *g_log_w;
extern int *g_log_ls;
extern int g_log_n, g_log_scroll;
extern wchar_t g_log_path[MAX_PATH], g_log_stamp[16];
extern wchar_t g_updated_to[32], g_upd_fail[160];
extern int g_upd_wanted;
extern const int k_logcap_vals[4];

const wchar_t *tr(int id);
void wfmt(wchar_t *buf, size_t n, const wchar_t *fmt, ...);
const wchar_t *base_name(const wchar_t *path);
void fmt_duration(long s, wchar_t *out, size_t n);
int running(void);
int importing(void);
int loader_ok(void);
int xbe_ok(void);
int can_play(void);
int game_art_on(void); /* the setting is on and a game is imported */
void get_import(imp_status_t *st);
void scaled_size(int *w, int *h);
void build_cmdline(wchar_t *out, size_t n);
void note_text(wchar_t *out, size_t n);
void exit_text(int code, wchar_t *out, size_t n);

/* ---- pages.c ---- */
void paint_all(void);
/* Number of entries of a drop-down (for the click on an item). */
int combo_count(int id);
