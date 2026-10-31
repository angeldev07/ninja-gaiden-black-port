/*
 * ach.c: see ach.h.
 */
#include "ach.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ngb.h"
#include "../gfx/toast.h"

/* Game variables of the reference build (PAL, title id 0x5443000D), found with Ghidra and checked
 * with --watch and --dump-ram (docs/reverse-engineering/game-state.md). */
#define VA_CHAPTER 0x00E130D4u       /* u8, 0-based: FUN_002ee750 sets it when a chapter starts */
#define VA_SAVED_CHAPTER 0x005B4977u /* u8, its copy in the save block: a load restores it (FUN_002fe3c0) */
#define VA_DIFFICULTY 0x005B4975u    /* u8: 0 Ninja Dog, 1 Normal, 2 Hard, 3 Very Hard, 4 Master Ninja */
#define VA_MODE 0x005B497Au          /* u8: 1 story; 2 to 4 missions */
#define VA_SAVE_NAME_FMT 0x00406A20u /* L"%d. CHAPTER %d %s", the save name (FUN_001dab20) */
#define TITLE_ID 0x5443000Du
#define CHAPTERS 16
#define MAX_UNLOCKED 256

static const wchar_t *const k_difficulty[5] = {L"Ninja Dog", L"Normal", L"Hard", L"Very Hard", L"Master Ninja"};

static struct {
    char id[32], time[24];
    int difficulty;
} g_unlocked[MAX_UNLOCKED];
static int g_nunlocked;
static int g_on, g_demo;
static int g_last_chapter = -1;
static DWORD g_start;
static char g_path[MAX_PATH];

static uint8_t peek(uint32_t va) { return *(volatile const uint8_t *)(uintptr_t)va; }

static int is_unlocked(const char *id) {
    int i;
    for (i = 0; i < g_nunlocked; i++)
        if (!strcmp(g_unlocked[i].id, id))
            return 1;
    return 0;
}

/* The file holds one unlock per line, as written by save(): a reader that only needs the ids
 * can parse it line by line. */
static void load(void) {
    char line[256];
    FILE *f = fopen(g_path, "r");
    if (!f)
        return;
    while (fgets(line, sizeof line, f) && g_nunlocked < MAX_UNLOCKED) {
        const char *p = strstr(line, "{\"id\": \"");
        if (p && sscanf(p, "{\"id\": \"%31[^\"]\", \"time\": \"%23[^\"]\", \"difficulty\": %d",
                        g_unlocked[g_nunlocked].id, g_unlocked[g_nunlocked].time,
                        &g_unlocked[g_nunlocked].difficulty) == 3)
            g_nunlocked++;
    }
    fclose(f);
}

/* Written to a temporary file and renamed over the old one: a crash never leaves half a file. */
static int save(void) {
    char tmp[MAX_PATH + 4];
    FILE *f;
    int i;
    snprintf(tmp, sizeof tmp, "%s.tmp", g_path);
    if ((f = fopen(tmp, "w")) == NULL)
        return -1;
    fprintf(f, "{\n  \"version\": 1,\n  \"unlocked\": [\n");
    for (i = 0; i < g_nunlocked; i++)
        fprintf(f, "    {\"id\": \"%s\", \"time\": \"%s\", \"difficulty\": %d}%s\n", g_unlocked[i].id,
                g_unlocked[i].time, g_unlocked[i].difficulty, i + 1 < g_nunlocked ? "," : "");
    fprintf(f, "  ]\n}\n");
    if (fclose(f))
        return -1;
    return MoveFileExA(tmp, g_path, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
}

static void unlock(const char *id, int difficulty, const wchar_t *name) {
    SYSTEMTIME st;
    if (is_unlocked(id) || g_nunlocked >= MAX_UNLOCKED)
        return;
    GetSystemTime(&st);
    snprintf(g_unlocked[g_nunlocked].id, sizeof g_unlocked[0].id, "%s", id);
    snprintf(g_unlocked[g_nunlocked].time, sizeof g_unlocked[0].time, "%04u-%02u-%02uT%02u:%02u:%02uZ",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    g_unlocked[g_nunlocked].difficulty = difficulty;
    g_nunlocked++;
    ngb_log("ach: unlocked %s (difficulty %d)%s", id, difficulty,
            save() ? "; could not write achievements.json" : "");
    toast_show(L"Achievement unlocked", name);
}

void ngb_ach_init(const xbe_t *xbe, int demo) {
    static const wchar_t fmt[] = L"%d. CHAPTER %d %s";
    const char *data = getenv("NGB_DATA"), *home = getenv("USERPROFILE");
    const uint8_t *p = xbe_va_ptr(xbe, VA_SAVE_NAME_FMT, sizeof fmt);
    if (data && *data)
        snprintf(g_path, sizeof g_path, "%s\\achievements.json", data);
    else if (home && *home)
        snprintf(g_path, sizeof g_path, "%s\\ngb-data\\achievements.json", home);
    else
        return;
    load();
    g_demo = demo;
    g_start = GetTickCount();
    g_on = xbe->title_id == TITLE_ID && p && !memcmp(p, fmt, sizeof fmt);
    ngb_log("ach: %d unlocked in %s; detectors %s", g_nunlocked, g_path,
            g_on ? "on (reference build)" : "off (not the reference build)");
}

void ngb_ach_tick(void) {
    int chapter, saved, difficulty;
    if (g_demo && GetTickCount() - g_start > 5000) {
        g_demo = 0;
        unlock("demo", -1, L"Spike test: the toast and the file work");
    }
    if (!g_on)
        return;
    if (peek(VA_MODE) != 1) {
        g_last_chapter = -1;
        return;
    }
    chapter = peek(VA_CHAPTER);
    saved = peek(VA_SAVED_CHAPTER);
    difficulty = peek(VA_DIFFICULTY);
    if (chapter >= CHAPTERS || chapter == g_last_chapter)
        return;
    ngb_log("ach: chapter %d -> %d (save copy %d, difficulty %d)", g_last_chapter, chapter, saved, difficulty);
    /* A chapter that starts one after the last, while the save block still holds the old one:
     * the game moved on by itself. A load sets both to the same value. */
    if (g_last_chapter >= 0 && chapter == g_last_chapter + 1 && saved != chapter) {
        char id[32];
        wchar_t name[96];
        snprintf(id, sizeof id, "chapter-%02d", chapter);
        swprintf(name, sizeof name / sizeof name[0], L"Chapter %d complete (%ls)", chapter,
                 difficulty < 5 ? k_difficulty[difficulty] : L"?");
        unlock(id, difficulty, name);
    }
    g_last_chapter = chapter;
}
