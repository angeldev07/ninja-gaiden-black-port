/*
 * ach.c: see ach.h.
 */
#include "ach.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ngb.h"
#include "../gfx/toast.h"

#define TITLE_ID 0x5443000Du
#define CHAPTERS 16
#define MAX_UNLOCKED 256

static const wchar_t *const k_difficulty[5] = {L"Ninja Dog", L"Normal", L"Hard", L"Very Hard", L"Master Ninja"};

/* Game variables (docs/reverse-engineering/game-state.md). Their addresses move between releases,
 * so find_vars() reads them out of the game's own code at start; PAL values in the comments. */
static struct {
    uint32_t chapter;    /* u8, 0-based (0x00E130D4): set when a chapter starts */
    uint32_t saved;      /* u8, its copy in the save block (0x005B4977): a load restores it */
    uint32_t difficulty; /* u8 (0x005B4975): 0 Ninja Dog, 1 Normal, 2 Hard, 3 Very Hard, 4 Master Ninja */
    uint32_t mode;       /* u8 (0x005B497A): 1 story; 2 to 4 missions */
} g_va;

static struct {
    char id[32], time[24];
    int difficulty;
} g_unlocked[MAX_UNLOCKED];
static int g_nunlocked;
static int g_on, g_demo;
static int g_last_chapter = -1;
static DWORD g_start;
static char g_path[MAX_PATH];

static uint32_t rd32(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }

/* First `pat` in [from, to) of `hay`, or -1. */
static long find(const uint8_t *hay, size_t from, size_t to, const uint8_t *pat, size_t n) {
    size_t i;
    for (i = from; i + n <= to; i++)
        if (hay[i] == pat[0] && !memcmp(hay + i, pat, n))
            return (long)i;
    return -1;
}

/*
 * The save name routine (PAL FUN_001dab20) reads both the mode and the chapter:
 *     mov al, [mode]  ...  cmp al, 2  ...  movzx eax, byte [chapter]  ...  push L"%d. CHAPTER %d %s"
 * and the load routine (PAL FUN_002fe3c0) restores the chapter from the save block:
 *     mov cl, [saved]  ...  mov [chapter], cl
 * The save block has the same layout in every release (saves load across them), so the
 * difficulty sits 2 bytes before the saved chapter and the mode 3 bytes after it: the second
 * fact checks the first. Same code in PAL, NTSC-U and modified builds of either.
 */
static int find_vars(const xbe_t *xbe) {
    static const wchar_t fmt[] = L"%d. CHAPTER %d %s";
    const xbe_section_t *text = NULL;
    const uint8_t *t;
    uint32_t fmt_va = 0, s;
    uint8_t pat[6];
    long at, i;
    for (s = 0; s < xbe->section_count; s++) {
        const xbe_section_t *sec = &xbe->sections[s];
        if (!strcmp(sec->name, ".text"))
            text = sec;
        if (!fmt_va && sec->raw + sec->rsize <= xbe->size &&
            (at = find(xbe->data, sec->raw, sec->raw + sec->rsize, (const uint8_t *)fmt, sizeof fmt)) >= 0)
            fmt_va = sec->va + (uint32_t)at - sec->raw;
    }
    if (!fmt_va || !text || (t = xbe_va_ptr(xbe, text->va, text->rsize)) == NULL || text->rsize < 0x60)
        return -1;
    pat[0] = 0x68; /* push imm32 */
    memcpy(pat + 1, &fmt_va, 4);
    if ((at = find(t, 0x60, text->rsize, pat, 5)) < 0 || find(t, (size_t)at + 1, text->rsize, pat, 5) >= 0)
        return -1;
    for (i = at - 7; i >= at - 0x20 && !g_va.chapter; i--)
        if (t[i] == 0x0F && t[i + 1] == 0xB6 && t[i + 2] == 0x05) /* movzx eax, byte [imm32] */
            g_va.chapter = rd32(t + i + 3);
    for (i = at - 0x60; i < at - 5 && !g_va.mode; i++)
        if (t[i] == 0xA0 && find(t, (size_t)i + 5, (size_t)i + 13, (const uint8_t *)"\x3C\x02", 2) >= 0)
            g_va.mode = rd32(t + i + 1); /* mov al, [imm32] followed by cmp al, 2 */
    if (!g_va.chapter || !g_va.mode)
        return -1;
    pat[0] = 0x88; /* mov [imm32], cl */
    pat[1] = 0x0D;
    memcpy(pat + 2, &g_va.chapter, 4);
    for (at = find(t, 0x30, text->rsize, pat, 6); at >= 0 && !g_va.saved; at = find(t, (size_t)at + 1, text->rsize, pat, 6))
        for (i = at - 6; i >= at - 0x30; i--)
            if (t[i] == 0x8A && t[i + 1] == 0x0D && rd32(t + i + 2) + 3 == g_va.mode) { /* mov cl, [imm32] */
                g_va.saved = rd32(t + i + 2);
                break;
            }
    if (!g_va.saved)
        return -1;
    g_va.difficulty = g_va.saved - 2;
    return 0;
}

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
    const char *data = getenv("NGB_DATA"), *home = getenv("USERPROFILE");
    uint32_t end = xbe->base + xbe->size_of_image;
    if (data && *data)
        snprintf(g_path, sizeof g_path, "%s\\achievements.json", data);
    else if (home && *home)
        snprintf(g_path, sizeof g_path, "%s\\ngb-data\\achievements.json", home);
    else
        return;
    load();
    g_demo = demo;
    g_start = GetTickCount();
    /* The variables live in the game's image, which is mapped for the whole run. */
    g_on = xbe->title_id == TITLE_ID && !find_vars(xbe) && g_va.chapter < end && g_va.mode < end &&
           g_va.difficulty >= xbe->base;
    if (g_on)
        ngb_log("ach: %d unlocked in %s; chapter 0x%08X, difficulty 0x%08X, mode 0x%08X, save copy 0x%08X",
                g_nunlocked, g_path, g_va.chapter, g_va.difficulty, g_va.mode, g_va.saved);
    else
        ngb_log("ach: %d unlocked in %s; detectors off (the game variables were not found in this XBE)",
                g_nunlocked, g_path);
}

void ngb_ach_tick(void) {
    int chapter, saved, difficulty;
    if (g_demo && GetTickCount() - g_start > 5000) {
        g_demo = 0;
        unlock("demo", -1, L"Spike test: the toast and the file work");
    }
    if (!g_on)
        return;
    if (peek(g_va.mode) != 1) {
        g_last_chapter = -1;
        return;
    }
    chapter = peek(g_va.chapter);
    saved = peek(g_va.saved);
    difficulty = peek(g_va.difficulty);
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
