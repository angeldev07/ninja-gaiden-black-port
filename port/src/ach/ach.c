/*
 * ach.c: see ach.h. Detectors of phases 1 and 1b (docs/target/achievements-design.md § Detectors).
 */
#include "ach.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ngb.h"
#include "../gfx/present.h"
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
#define NEW_GAME_FRAMES 1200u /* a run whose play time is under 20 s with no karma is a New Game */
#define CHECKPOINT_SLACK 600u /* a loaded save's play time may run up to 10 s before it is read */
#define CHECKPOINTS 40
#define INVENTORY_SLOTS 128
#define RUN_CLEAN_UMN (ACH_RUN_HIT | ACH_RUN_HEAL | ACH_RUN_NINPO | ACH_RUN_UPGRADE)
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
static int g_run_seen, g_last_mode = -1;
static uint32_t g_last_play_time, g_last_scarabs;
static struct {
    int move, lines;
    uint32_t kills, health, ki;
    uint16_t ninpo[8];
    uint8_t seen[256]; /* move ids already captured */
} g_trace = {-1, 0, 0, 0, 0, {0}, {0}};
/* The story run followed for the challenge achievements (rules.h, ACH_RUN_*). */
static struct {
    int on, difficulty, resync, have_body;
    unsigned marks;
    uint32_t last_time;
    ach_body body;
} g_run;
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

/* The run's play time. A load copies the whole save block, so the time jumps; 1 when it moved on
 * steadily since the last read and no jump happened in the last two seconds (in case the copy was
 * read half done). A jump asks the run tracker to find out which run the loaded save belongs to; a
 * change of mode (a menu, the title) only pauses it. */
static int clock_tick(DWORD now, int mode) {
    uint32_t t = peek32(g_va.play_time);
    int steady = g_run_seen && t >= g_last_play_time && t - g_last_play_time <= FRAMES_STEADY;
    if (!steady || mode != g_last_mode) {
        g_quiet_until = now + QUIET_AFTER_LOAD;
        g_run.resync |= !steady;
        if (!steady && g_run_seen && g_run.on)
            ngb_log("ach: play time jumped %u -> %u", g_last_play_time, t);
        g_run.have_body = 0;
    }
    g_last_play_time = t;
    g_last_mode = mode;
    g_run_seen = 1;
    return steady && (LONG)(now - g_quiet_until) >= 0;
}

/* Golden Scarabs, handed to Muramasa (PAL FUN_00187400 adds the ones carried to the total and caps
 * it at 50). `n` was read before the play time, which sits lower in the save block, so a total
 * copied by a load always comes with a jump of the time and is never counted. */
static void scarab_tick(uint32_t n, int calm, int mode) {
    if (calm && mode == MODE_STORY && n > g_last_scarabs && n <= SCARABS) {
        ngb_log("ach: golden scarabs handed in: %u", n);
        ach_store_set_progress(&g_store, "golden-scarabs", n);
        save();
        if (n == SCARABS)
            unlock("golden-scarabs", peek8(g_va.difficulty));
    }
    g_last_scarabs = n;
}

static void read_body(ach_body *b) {
    uint32_t h = peek32(g_va.health), k = peek32(g_va.ki), at;
    int i;
    b->health_max = h & 0xFFFF;
    b->health = h >> 16;
    b->ki_max = k & 0xFFFF;
    b->ki = k >> 16;
    b->weapon = *(volatile const uint16_t *)(uintptr_t)g_va.weapon;
    b->heal_items = 0;
    for (i = 0, at = g_va.inventory; i < INVENTORY_SLOTS; i++, at += 4) {
        uint32_t slot = peek32(at);
        if ((slot & 0xFFFF) != 0xFFFF && ach_is_healing_item(slot & 0xFFFF))
            b->heal_items += (slot >> 16) & 0xFF;
    }
}

/* Checkpoints: "run-TTTTTTTT-KKKKKKKK" (play time and karma of a save the game wrote during a
 * followed run) = marks | difficulty << 8 | 1 << 16, the oldest dropped past CHECKPOINTS. */
static void run_key(char *key, size_t n, uint32_t t, uint32_t karma) {
    snprintf(key, n, "run-%08X-%08X", t, karma);
}

/* A New Game: the run's play time under 20 s, no karma, chapter 1. */
static int new_game(uint32_t t, uint32_t karma) { return t < NEW_GAME_FRAMES && karma == 0 && peek8(g_va.chapter) == 0; }

static void run_start(void) {
    ngb_log("ach: run followed from New Game (difficulty %d)", peek8(g_va.difficulty));
    g_run.on = 1;
    g_run.difficulty = peek8(g_va.difficulty);
    g_run.marks = 0;
}

/* After a load (the play time jumped): which run the save belongs to. */
static void run_resync(void) {
    uint32_t t = peek32(g_va.play_time), karma = peek32(g_va.karma), ct, ck;
    int i;
    if (new_game(t, karma)) {
        run_start();
        return;
    }
    for (i = g_store.nprogress - 1; i >= 0; i--)
        if (sscanf(g_store.progress[i].id, "run-%8X-%8X", &ct, &ck) == 2 && ck == karma && ct <= t &&
            t - ct <= CHECKPOINT_SLACK) {
            g_run.on = 1;
            g_run.marks = (unsigned)(g_store.progress[i].value & 0xFF);
            g_run.difficulty = (int)(g_store.progress[i].value >> 8 & 0xFF);
            ngb_log("ach: run resumed from a save (marks 0x%X, difficulty %d)", g_run.marks, g_run.difficulty);
            return;
        }
    if (g_run.on)
        ngb_log("ach: the save loaded (play time %u, karma %u) is not from a followed run", t, karma);
    g_run.on = 0;
}

static void run_tick(int calm, int mode) {
    ach_body b;
    unsigned m;
    if (mode != MODE_STORY || !calm) {
        g_run.have_body = 0;
        return;
    }
    if (g_run.resync) {
        g_run.resync = 0;
        run_resync();
    } else if (new_game(peek32(g_va.play_time), peek32(g_va.karma)) &&
               (!g_run.on || peek32(g_va.play_time) < g_run.last_time)) {
        run_start(); /* from the title, where the time was already 0 */
    }
    g_run.last_time = peek32(g_va.play_time);
    read_body(&b);
    if (g_run.on && g_run.have_body && (m = ach_run_marks(&g_run.body, &b) & ~g_run.marks) != 0) {
        g_run.marks |= m;
        ngb_log("ach: run marked 0x%X (health %u/%u, ki %u/%u, weapon 0x%04X)", m, b.health, b.health_max, b.ki,
                b.ki_max, b.weapon);
    }
    g_run.body = b;
    g_run.have_body = 1;
}

/* The story ended naturally on `difficulty`. */
static void on_story_end(int difficulty) {
    if (!g_run.on)
        return;
    ngb_log("ach: followed run ended (marks 0x%X, started on difficulty %d)", g_run.marks, g_run.difficulty);
    if (g_run.difficulty == DIFFICULTY_MASTER_NINJA && difficulty == DIFFICULTY_MASTER_NINJA &&
        !(g_run.marks & RUN_CLEAN_UMN))
        unlock("true-ultimate", difficulty);
    if (!(g_run.marks & ACH_RUN_OTHER_WEAPON))
        unlock("dragon-sword-only", difficulty);
    g_run.on = 0;
}

static uint16_t peek16(uint32_t va) { return *(volatile const uint16_t *)(uintptr_t)va; }

/* --ach-trace: what the phase 2 detectors will need, as it changes. */
static void trace_tick(void) {
    uint32_t p = readable(g_va.player, 4) ? peek32(g_va.player) : 0, kills, health, ki;
    int move = p && readable(p, 0xBC0) ? peek8(p + 4) : -1, i, ninpo_changed = 0;
    if (g_trace.lines > 20000)
        return;
    kills = peek32(g_va.kills);
    health = peek32(g_va.health);
    ki = peek32(g_va.ki);
    for (i = 0; i < 8; i++)
        if (peek16(g_va.ninpo + i * 2) != g_trace.ninpo[i]) {
            g_trace.ninpo[i] = peek16(g_va.ninpo + i * 2);
            ninpo_changed = 1;
        }
    if (move != g_trace.move) {
        int first = move >= 0 && !g_trace.seen[move];
        ngb_log("ach-trace: move 0x%02X (weapon 0x%04X, play time %u)%s", move, peek16(g_va.weapon),
                peek32(g_va.play_time), first ? ", first time: captured" : "");
        if (first) { /* one picture per move id, to name them */
            g_trace.seen[move] = 1;
            gfx_request_shot();
        }
    }
    if (kills != g_trace.kills)
        ngb_log("ach-trace: kills %u, move 0x%02X, weapon 0x%04X, combo %u", kills, move, peek16(g_va.weapon),
                p ? peek16(p + 0x60E) : 0);
    if (health != g_trace.health)
        ngb_log("ach-trace: health %u / %u", health >> 16, health & 0xFFFF);
    if (ki != g_trace.ki)
        ngb_log("ach-trace: ki %u / %u", ki >> 16, ki & 0xFFFF);
    if (ninpo_changed)
        ngb_log("ach-trace: ninpo %u %u %u %u %u %u %u %u", g_trace.ninpo[0], g_trace.ninpo[1], g_trace.ninpo[2],
                g_trace.ninpo[3], g_trace.ninpo[4], g_trace.ninpo[5], g_trace.ninpo[6], g_trace.ninpo[7]);
    g_trace.lines += (move != g_trace.move) + (kills != g_trace.kills) + (health != g_trace.health) +
                     (ki != g_trace.ki) + ninpo_changed;
    g_trace.move = move;
    g_trace.kills = kills;
    g_trace.health = health;
    g_trace.ki = ki;
}

static void game_tick(DWORD now) {
    int chapter, saved, difficulty, calm, mode = peek8(g_va.mode);
    uint32_t karma_last, scarabs = peek32(g_va.scarabs); /* before the clock: see scarab_tick */
    if (mode >= MODE_STORY && mode <= 4) /* story or missions: a game is being played */
        unlock("port-first-session", -1);
    if (g_ngb_opts.ach_trace)
        trace_tick();
    calm = clock_tick(now, mode);
    death_tick(mode >= MODE_STORY && mode <= 4);
    mission_tick(mode);
    scarab_tick(scarabs, calm, mode);
    run_tick(calm, mode);
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
        if (g_last_chapter == ACH_CHAPTERS - 1 && chapter >= ACH_CHAPTERS && difficulty < ACH_DIFFICULTIES) {
            unlock(k_story[difficulty], difficulty);
            on_story_end(difficulty);
        }
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

void ngb_ach_game_saved(void) {
    char key[32];
    int i, n = 0;
    if (!g_ready || !g_on)
        return;
    AcquireSRWLockExclusive(&g_lock);
    if (g_run.on && peek8(g_va.mode) == MODE_STORY) {
        run_key(key, sizeof key, peek32(g_va.play_time), peek32(g_va.karma));
        if (!ach_store_progress(&g_store, key))
            ngb_log("ach: run checkpoint %s (marks 0x%X)", key, g_run.marks);
        ach_store_set_progress(&g_store, key, g_run.marks | (unsigned)g_run.difficulty << 8 | 1u << 16);
        for (i = 0; i < g_store.nprogress; i++)
            n += !strncmp(g_store.progress[i].id, "run-", 4);
        for (i = 0; n > CHECKPOINTS && i < g_store.nprogress; i++)
            if (!strncmp(g_store.progress[i].id, "run-", 4)) { /* the oldest come first */
                ach_store_remove_progress(&g_store, g_store.progress[i].id);
                n--;
                i--;
            }
        save();
    }
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
