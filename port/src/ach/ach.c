/*
 * ach.c: see ach.h. Detectors of phases 1 and 1b (docs/target/achievements-design.md § Detectors).
 */
#include "ach.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ngb.h"
#include "../gfx/toast.h"
#include "catalog.h"
#include "rules.h"
#include "sig.h"
#include "store.h"

#define MODE_STORY 1
#define MODE_MISSION 3
#define STAGE_GAME_OVER 0x80
#define DIFFICULTY_MASTER_NINJA 4
#define SCARABS 50
#define FRAMES_STEADY 600u   /* play time read twice 5 ms apart: more than this apart is a load */
#define QUIET_AFTER_LOAD 2000u
#define SAVE_EVERY_MS 60000u
#define FIRST_SESSION_MS 300000u /* when the detectors are off, 5 minutes in counts as a session */

static const char *const k_story[ACH_DIFFICULTIES] = {"story-ninja-dog", "story-normal", "story-hard",
                                                       "story-very-hard", "story-master-ninja"};

static SRWLOCK g_lock = SRWLOCK_INIT;
static ach_store g_store;
static ach_vars g_va;
static int g_ready, g_on, g_demo, g_table_ok = -1;
static int g_last_chapter = -1;
static int g_last_mission_chapter = -1, g_last_stage_end = -1;
static int g_run_seen;
static uint32_t g_last_play_time, g_last_scarabs;
static DWORD g_quiet_until;
static uint32_t g_last_karma_last;
static DWORD g_start, g_last_tick, g_last_save, g_ms; /* g_ms: play time not yet counted in seconds */

static int readable(uint32_t va, uint32_t len) {
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery((const void *)(uintptr_t)va, &m, sizeof m) || m.State != MEM_COMMIT ||
        (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return 0;
    return (uint64_t)va + len <= (uint64_t)(uintptr_t)m.BaseAddress + m.RegionSize;
}

static uint8_t peek8(uint32_t va) { return *(volatile const uint8_t *)(uintptr_t)va; }
static uint32_t peek32(uint32_t va) { return *(volatile const uint32_t *)(uintptr_t)va; }

static int lang(void) { return g_ngb_opts.lang == 1 ? ACH_LANG_ES : ACH_LANG_EN; }

static void announce(const ach_def *a) {
    static const wchar_t *const k_label[2][ACH_LANGS] = {
        {L"ACHIEVEMENT UNLOCKED", L"LOGRO DESBLOQUEADO"},
        {L"SECRET ACHIEVEMENT UNLOCKED", L"LOGRO SECRETO DESBLOQUEADO"},
    };
    wchar_t label[96];
    if (!a || g_ngb_opts.ach_toast_off)
        return;
    swprintf(label, sizeof label / sizeof label[0], L"%ls  ·  %d 誉", k_label[a->secret != 0][lang()], a->points);
    toast_show(label, a->name[lang()], a->points >= 100 || a->secret);
}

static void save(void) {
    if (ach_store_save(&g_store))
        ngb_log("ach: could not write %s", g_store.path);
    g_last_save = GetTickCount();
}

/* With g_lock held. */
static void unlock(const char *id, int difficulty) {
    SYSTEMTIME st;
    char t[24];
    GetSystemTime(&st);
    snprintf(t, sizeof t, "%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
             st.wSecond);
    if (!ach_find(id) || !ach_store_unlock(&g_store, id, difficulty, t))
        return;
    ngb_log("ach: unlocked %s (difficulty %d)", id, difficulty);
    save();
    announce(ach_find(id));
}

/* The game's {max, min} pair for `chapter` (16 = the whole game), from its data file in RAM:
 * table = base + [base + [base + 4] + 0x18], base = [rank_data] (PAL FUN_002ed790). */
static int rank_pair(int chapter, uint32_t *max, uint32_t *min) {
    uint32_t base, at, tab;
    int i;
    if (!readable(g_va.rank_data, 4) || (base = peek32(g_va.rank_data)) == 0 || !readable(base + 4, 4))
        return -1;
    at = base + peek32(base + 4) + 0x18;
    if (!readable(at, 4) || !readable(tab = base + peek32(at), (ACH_CHAPTERS + 1) * 8))
        return -1;
    if (g_table_ok < 0) { /* once: a table that does not look like the game's switches the rank off */
        g_table_ok = 1;
        for (i = 0; i <= ACH_CHAPTERS; i++)
            if (peek32(tab + i * 8) <= peek32(tab + i * 8 + 4) || peek32(tab + i * 8) > 100000000u)
                g_table_ok = 0;
        ngb_log("ach: rank table at 0x%08X %s", tab, g_table_ok ? "looks right" : "does not look right: rank off");
    }
    if (!g_table_ok)
        return -1;
    *max = peek32(tab + chapter * 8);
    *min = peek32(tab + chapter * 8 + 4);
    return 0;
}

/* A chapter's rank was just computed by the game: the karma at the last chapter end moved from
 * `before` to `after`. */
static void on_rank(int chapter, int difficulty, uint32_t before, uint32_t after) {
    uint32_t max, min, mask;
    char key[32];
    int rank, d, best = 0;
    if (rank_pair(chapter, &max, &min))
        return;
    rank = ach_rank(after - before, max, min, difficulty);
    ngb_log("ach: chapter %d rank %d (karma %u, Master Ninja from %u, difficulty %d)", chapter + 1, rank,
            after - before, max, difficulty);
    if (rank != ACH_RANK_MASTER_NINJA || difficulty < 0 || difficulty >= ACH_DIFFICULTIES)
        return;
    snprintf(key, sizeof key, "rank-seals-%d", difficulty);
    mask = (uint32_t)ach_store_progress(&g_store, key) | 1u << chapter;
    ach_store_set_progress(&g_store, key, mask);
    for (d = 0; d < ACH_DIFFICULTIES; d++) {
        snprintf(key, sizeof key, "rank-seals-%d", d);
        if (ach_seal_count((uint32_t)ach_store_progress(&g_store, key)) > best)
            best = ach_seal_count((uint32_t)ach_store_progress(&g_store, key));
    }
    ach_store_set_progress(&g_store, "rank-sixteen-seals", (uint64_t)best);
    save();
    unlock("rank-master-ninja", difficulty);
    if (ach_seal_count(mask) == ACH_CHAPTERS)
        unlock("rank-sixteen-seals", difficulty);
}

/* A progress counter one step up; unlocks at the catalog's goal. */
static void count_up(const char *id, int difficulty) {
    uint64_t n = ach_store_progress(&g_store, id) + 1;
    ach_store_set_progress(&g_store, id, n);
    save();
    if (n >= ach_find(id)->goal)
        unlock(id, difficulty);
}

/* The game cleared `mission` (its own index) on `difficulty`. */
static void on_mission(int mission, int difficulty) {
    int slot = ach_mission_slot(mission);
    uint64_t mask;
    ngb_log("ach: mission %d cleared (menu slot %d, difficulty %d)", mission, slot, difficulty);
    if (mission == ACH_MISSION_ETERNAL_LEGEND) {
        unlock("mission-eternal-legend", difficulty);
        if (difficulty == DIFFICULTY_MASTER_NINJA)
            unlock("mission-eternal-legend-mn", difficulty);
    }
    if (slot < 0 || difficulty != DIFFICULTY_MASTER_NINJA)
        return;
    mask = ach_store_progress(&g_store, "missions-mn-mask") | 1ull << slot;
    ach_store_set_progress(&g_store, "missions-mn-mask", mask);
    ach_store_set_progress(&g_store, "missions-all-mn", (uint64_t)ach_bit_count(mask));
    save();
    if (ach_bit_count(mask) == ACH_MISSIONS)
        unlock("missions-all-mn", difficulty);
}

/* Mission mode: the results screen (PAL FUN_002e3860) sets the chapter to 0x81 (0xFE in some
 * states), the same mark the story's ending uses; the profile records the clear right after. */
static void mission_tick(int mode) {
    int chapter = peek8(g_va.chapter);
    if (mode == MODE_MISSION && g_last_mission_chapter >= 0 && g_last_mission_chapter < 0x80 &&
        (chapter == 0x81 || chapter == 0xFE))
        on_mission(peek8(g_va.mission), peek8(g_va.difficulty));
    g_last_mission_chapter = mode == MODE_MISSION ? chapter : -1;
}

/* Game over, in story or missions: the stage end state turns 0x80 (PAL FUN_002e3f60 when the
 * health runs out with no way back, FUN_002ff940 when a mission fails). */
static void death_tick(int playing) {
    int end;
    if (!g_va.stage_end)
        return;
    end = peek8(g_va.stage_end);
    if (playing && g_last_stage_end >= 0 && g_last_stage_end != STAGE_GAME_OVER && end == STAGE_GAME_OVER) {
        ngb_log("ach: game over");
        count_up("deaths-100", peek8(g_va.difficulty));
    }
    g_last_stage_end = end;
}

/* Golden Scarabs, handed to Muramasa (PAL FUN_00187400 adds the ones carried to the total and caps
 * it at 50). A load copies the whole save block, total included, so a rise only counts while the
 * run's play time moves on steadily: the load makes it jump, and the next two seconds are ignored
 * in case the copy was read half done. The total is read before the time, which sits lower in the
 * block: a new total always comes with a new time. */
static void scarab_tick(DWORD now, int mode) {
    uint32_t n = peek32(g_va.scarabs), t;
    int steady;
    t = peek32(g_va.play_time);
    steady = g_run_seen && t >= g_last_play_time && t - g_last_play_time <= FRAMES_STEADY;
    if (!steady)
        g_quiet_until = now + QUIET_AFTER_LOAD;
    else if (mode == MODE_STORY && n > g_last_scarabs && n <= SCARABS && (LONG)(now - g_quiet_until) >= 0) {
        ngb_log("ach: golden scarabs handed in: %u", n);
        ach_store_set_progress(&g_store, "golden-scarabs", n);
        save();
        if (n == SCARABS)
            unlock("golden-scarabs", peek8(g_va.difficulty));
    }
    g_last_play_time = t;
    g_last_scarabs = n;
    g_run_seen = 1;
}

static void game_tick(DWORD now) {
    int chapter, saved, difficulty, mode = peek8(g_va.mode);
    uint32_t karma_last;
    if (mode >= MODE_STORY && mode <= 4) /* story or missions: a game is being played */
        unlock("port-first-session", -1);
    death_tick(mode >= MODE_STORY && mode <= 4);
    mission_tick(mode);
    scarab_tick(now, mode);
    if (mode != MODE_STORY) {
        g_last_chapter = -1;
        return;
    }
    chapter = peek8(g_va.chapter);
    saved = peek8(g_va.saved);
    difficulty = peek8(g_va.difficulty);
    karma_last = peek32(g_va.karma_last);
    /* Rank: the game sets the karma at the last chapter end to the run's karma when it shows the
     * rank, before the next chapter starts. A load brings both from the save block too, but then
     * the save copy is ahead of the chapter or the value goes down. */
    if (g_last_chapter >= 0 && chapter == g_last_chapter && chapter < ACH_CHAPTERS &&
        karma_last != g_last_karma_last && karma_last > g_last_karma_last &&
        karma_last == peek32(g_va.karma) && (saved == chapter || saved + 1 == chapter))
        on_rank(chapter, difficulty, g_last_karma_last, karma_last);
    g_last_karma_last = karma_last;
    if (chapter == g_last_chapter)
        return;
    ngb_log("ach: chapter %d -> %d (save copy %d, difficulty %d)", g_last_chapter, chapter, saved, difficulty);
    /* The game moved on by itself: the chapter is one more (or, after the last one, 16 or more)
     * while the save copy still holds another value. A load sets both to the same one. */
    if (g_last_chapter >= 0 && saved != chapter) {
        if (g_last_chapter == 0 && chapter == 1)
            unlock("chapter-01", difficulty);
        if (g_last_chapter == ACH_CHAPTERS - 1 && chapter >= ACH_CHAPTERS && difficulty < ACH_DIFFICULTIES)
            unlock(k_story[difficulty], difficulty);
    }
    g_last_chapter = chapter < ACH_CHAPTERS ? chapter : -1;
}

/* Seconds played in the port, saved every minute. */
static void time_tick(DWORD now) {
    uint64_t secs;
    DWORD dt = g_last_tick ? now - g_last_tick : 0;
    g_last_tick = now;
    g_ms += dt < 1000 ? dt : 1000; /* a stalled main thread does not count */
    if (g_ms < 1000)
        return;
    secs = ach_store_progress(&g_store, "port-100-hours") + g_ms / 1000;
    g_ms %= 1000;
    ach_store_set_progress(&g_store, "port-100-hours", secs);
    if (secs >= ach_find("port-100-hours")->goal)
        unlock("port-100-hours", -1);
    if (!g_on && now - g_start > FIRST_SESSION_MS)
        unlock("port-first-session", -1);
    if (now - g_last_save >= SAVE_EVERY_MS)
        save();
}

void ngb_ach_init(const xbe_t *xbe) {
    const char *data = getenv("NGB_DATA"), *home = getenv("USERPROFILE");
    char path[MAX_PATH];
    uint32_t end = xbe->base + xbe->size_of_image;
    if (data && *data)
        snprintf(path, sizeof path, "%s\\achievements.json", data);
    else if (home && *home)
        snprintf(path, sizeof path, "%s\\ngb-data\\achievements.json", home);
    else
        return;
    if (ach_store_load(&g_store, path))
        ngb_log("ach: could not read %s; starting empty", path);
    g_demo = g_ngb_opts.ach_demo;
    g_start = g_last_save = GetTickCount();
    /* The variables live in the game's image, mapped for the whole run. */
    g_on = !ach_find_vars(xbe, &g_va) && g_va.karma >= xbe->base && g_va.chapter < end && g_va.mode < end;
    if (g_on)
        ngb_log("ach: %d unlocked in %s; chapter 0x%08X, difficulty 0x%08X, mode 0x%08X, save copy 0x%08X, "
                "karma 0x%08X, rank data 0x%08X, mission 0x%08X, stage end 0x%08X", g_store.nunlocked, path,
                g_va.chapter, g_va.difficulty, g_va.mode, g_va.saved, g_va.karma, g_va.rank_data, g_va.mission,
                g_va.stage_end);
    else
        ngb_log("ach: %d unlocked in %s; game detectors off (the game variables were not found in this XBE)",
                g_store.nunlocked, path);
    g_ready = 1;
}

void ngb_ach_tick(void) {
    DWORD now = GetTickCount();
    if (!g_ready)
        return;
    AcquireSRWLockExclusive(&g_lock);
    if (g_demo && now - g_start > 5000) { /* --ach-demo: both toast variants, nothing recorded */
        g_demo = 0;
        announce(ach_find("rank-master-ninja"));
        announce(ach_find("true-ultimate"));
    }
    if (g_on)
        game_tick(now);
    time_tick(now);
    ReleaseSRWLockExclusive(&g_lock);
}

void ngb_ach_screenshot(void) {
    if (!g_ready)
        return;
    AcquireSRWLockExclusive(&g_lock);
    unlock("port-screenshot", -1);
    ReleaseSRWLockExclusive(&g_lock);
}

void ngb_ach_shutdown(void) {
    int i;
    if (!g_ready)
        return;
    /* ngb_stop may come from any thread; the main thread never holds the lock for long. */
    for (i = 0; i < 50 && !TryAcquireSRWLockExclusive(&g_lock); i++)
        Sleep(2);
    if (i == 50)
        return;
    save();
    g_ready = 0;
    ReleaseSRWLockExclusive(&g_lock);
}
