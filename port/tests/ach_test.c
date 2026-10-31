/* ach_test.c: the achievements' pure parts: the catalog, the store, the rank rules and the search
 * for the game variables in synthetic code (src/ach/). No game data. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>

#include "ach/catalog.h"
#include "ach/rules.h"
#include "ach/sig.h"
#include "ach/store.h"

static int g_fail;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

static void test_catalog(void) {
    int i, j, l;
    CHECK(ach_catalog_count == 27);
    CHECK(ach_total_points() == 1680);
    for (i = 0; i < ach_catalog_count; i++) {
        const ach_def *a = &ach_catalog[i];
        CHECK(strlen(a->id) < 32);
        CHECK(a->points > 0 && (unsigned)a->category < ACH_CAT_COUNT);
        for (l = 0; l < ACH_LANGS; l++)
            CHECK(a->name[l] && a->name[l][0] && a->desc[l] && a->desc[l][0]);
        for (j = i + 1; j < ach_catalog_count; j++)
            CHECK(strcmp(a->id, ach_catalog[j].id));
        CHECK(ach_find(a->id) == a);
    }
    CHECK(ach_find("nope") == NULL);
    CHECK(ach_find("rank-sixteen-seals")->goal == 16);
    CHECK(!wcscmp(ach_category_glyph(ACH_CAT_RANK), L"極"));
}

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void test_store(const char *dir) {
    char path[MAX_PATH];
    ach_store s, t;
    snprintf(path, sizeof path, "%s\\ach_store_test.json", dir);
    DeleteFileA(path);
    CHECK(ach_store_load(&s, path) == 0 && s.nunlocked == 0 && s.nprogress == 0);
    CHECK(ach_store_unlock(&s, "chapter-01", 1, "2026-10-31T20:38:34Z") == 1);
    CHECK(ach_store_unlock(&s, "chapter-01", 2, "2026-11-01T00:00:00Z") == 0);
    ach_store_set_progress(&s, "port-100-hours", 111600);
    ach_store_set_progress(&s, "rank-seals-1", 1);
    ach_store_set_progress(&s, "rank-seals-1", 3);
    CHECK(ach_store_save(&s) == 0);
    CHECK(ach_store_load(&t, path) == 0);
    CHECK(t.nunlocked == 1 && !strcmp(t.unlocked[0].id, "chapter-01") && t.unlocked[0].difficulty == 1);
    CHECK(!strcmp(t.unlocked[0].time, "2026-10-31T20:38:34Z"));
    CHECK(ach_store_progress(&t, "port-100-hours") == 111600 && ach_store_progress(&t, "rank-seals-1") == 3);
    CHECK(ach_store_progress(&t, "missing") == 0);
    /* The spike's version 1 file: its records stay, its demo goes. */
    write_file(path, "{\n  \"version\": 1,\n  \"unlocked\": [\n"
                     "    {\"id\": \"demo\", \"time\": \"2026-10-31T20:03:36Z\", \"difficulty\": -1},\n"
                     "    {\"id\": \"chapter-01\", \"time\": \"2026-10-31T20:38:34Z\", \"difficulty\": 1}\n  ]\n}\n");
    CHECK(ach_store_load(&t, path) == 0 && t.nunlocked == 1 && ach_store_find(&t, "chapter-01") == 0);
    /* A truncated file keeps what it had. */
    write_file(path, "{\n  \"version\": 2,\n  \"unlocked\": [\n    {\"id\": \"chapter-01\", \"time\": \"2026-10-31T2");
    CHECK(ach_store_load(&t, path) == 0 && t.nunlocked == 0);
    DeleteFileA(path);
}

static void test_rules(void) {
    /* Chapter 1 of the owner's runs: Master Ninja from 260,000, rank 4 below 120,000. */
    CHECK(ach_rank(405327, 260000, 120000, 1) == ACH_RANK_MASTER_NINJA);
    CHECK(ach_rank(260000, 260000, 120000, 1) == 0);
    /* third = 46,666: bounds 166,666, 213,332 and 259,998 (the game's, under max) */
    CHECK(ach_rank(259998, 260000, 120000, 1) == 0);
    CHECK(ach_rank(259997, 260000, 120000, 1) == 1);
    CHECK(ach_rank(213332, 260000, 120000, 1) == 1);
    CHECK(ach_rank(213331, 260000, 120000, 1) == 2);
    CHECK(ach_rank(166666, 260000, 120000, 1) == 2);
    CHECK(ach_rank(166665, 260000, 120000, 1) == 3);
    CHECK(ach_rank(119999, 260000, 120000, 1) == 4);
    /* The game's last bound is min + 3 * third, a little under max when the third rounds down. */
    CHECK(ach_rank(366666, 366667, 170000, 2) == 0);
    /* Ninja Dog never reaches rank 0 or 1. */
    CHECK(ach_rank(405327, 260000, 120000, 0) == 2);
    CHECK(ach_rank(0, 260000, 120000, 0) == 4);
    CHECK(ach_seal_count(0) == 0 && ach_seal_count(0xFFFFu) == 16 && ach_seal_count(0x10005u) == 2);
}

/* A synthetic XBE with the two routines the search reads, at the PAL addresses. */
static void put32(unsigned char *p, uint32_t v) { memcpy(p, &v, 4); }

static int sig_case(int variant, ach_vars *v) {
    static unsigned char data[0x3000];
    static const wchar_t fmt[] = L"%d. CHAPTER %d %s";
    xbe_section_t secs[2];
    xbe_t xbe;
    unsigned char *t = data + 0x1000;
    const uint32_t chapter = 0x00E130D4u, saved = 0x005B4977u, mode = 0x005B497Au, fmt_va = 0x00406A20u;
    memset(data, 0xCC, sizeof data);
    memset(secs, 0, sizeof secs);
    memset(&xbe, 0, sizeof xbe);
    strcpy(secs[0].name, ".text");
    secs[0].va = 0x00011000u;
    secs[0].raw = 0x1000u;
    secs[0].rsize = secs[0].vsize = 0x1000u;
    strcpy(secs[1].name, ".rdata");
    secs[1].va = 0x00406000u;
    secs[1].raw = 0x2000u;
    secs[1].rsize = secs[1].vsize = 0x1000u;
    memcpy(data + 0x2000 + (fmt_va - 0x00406000u), fmt, sizeof fmt);
    /* save name: mov al,[mode]; add esp,4; cmp al,2 ... movzx eax,byte [chapter]; ... push fmt */
    t[0x100] = 0xA0;
    put32(t + 0x101, mode);
    memcpy(t + 0x105, "\x83\xC4\x04\x3C\x02", 5);
    memcpy(t + 0x130, "\x0F\xB6\x05", 3);
    put32(t + 0x133, chapter);
    memcpy(t + 0x137, "\x8D\x54\x24\x08\x52\x40\x50\x43\x53", 9);
    if (variant != 1) { /* 1: no push of the format string */
        t[0x140] = 0x68;
        put32(t + 0x141, fmt_va);
    }
    if (variant == 2) { /* 2: a second push of it */
        t[0x200] = 0x68;
        put32(t + 0x201, fmt_va);
    }
    /* load: mov al,[stage]; mov cl,[saved]; ... mov [chapter],cl */
    memcpy(t + 0x400, "\xA0\x76\x49\x5B\x00\x8A\x0D", 7);
    put32(t + 0x407, saved);
    memcpy(t + 0x40B, "\x66\x8B\x15\x7E\x49\x5B\x00", 7);
    memcpy(t + 0x412, "\x88\x0D", 2);
    put32(t + 0x414, chapter);
    secs[0].va = 0x00011000u;
    xbe.data = data;
    xbe.size = sizeof data;
    xbe.base = 0x10000u;
    xbe.sections = secs;
    xbe.section_count = 2;
    return ach_find_vars(&xbe, v);
}

static void test_sig(void) {
    ach_vars v;
    CHECK(sig_case(0, &v) == 0);
    CHECK(v.chapter == 0x00E130D4u && v.saved == 0x005B4977u && v.mode == 0x005B497Au);
    CHECK(v.difficulty == 0x005B4975u && v.karma == 0x005B4780u && v.karma_last == 0x005B4784u);
    CHECK(v.rank_data == 0x00E12EC4u);
    CHECK(sig_case(1, &v) == -1 && v.chapter == 0);
    CHECK(sig_case(2, &v) == -1);
}

int main(void) {
    char dir[MAX_PATH];
    if (!GetTempPathA(sizeof dir, dir))
        strcpy(dir, ".");
    if (dir[strlen(dir) - 1] == '\\')
        dir[strlen(dir) - 1] = 0;
    test_catalog();
    test_store(dir);
    test_rules();
    test_sig();
    printf("ach_test: %s\n", g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}
