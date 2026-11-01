/*
 * catalog.h: the port's achievements, one table shared by the loader (detectors and toast) and the
 * launcher (the Achievements page). See docs/target/achievements-design.md.
 *
 *   const ach_def *a = ach_find("chapter-01");
 *   wprintf(L"%ls: %ls", a->name[ACH_LANG_ES], a->desc[ACH_LANG_ES]);
 *
 * Pure data, no globals besides the table: tested by port/tests/ach_catalog_test.c.
 */
#ifndef NGB_ACH_CATALOG_H
#define NGB_ACH_CATALOG_H

#include <wchar.h>

enum ach_lang { ACH_LANG_EN, ACH_LANG_ES, ACH_LANGS };

typedef enum ach_category {
    ACH_CAT_STORY,
    ACH_CAT_RANK,
    ACH_CAT_MISSION,
    ACH_CAT_COUNTER,
    ACH_CAT_COLLECTION,
    ACH_CAT_CHALLENGE,
    ACH_CAT_PORT,
    ACH_CAT_COUNT
} ach_category;

typedef struct ach_def {
    const char *id;           /* stable: it is the key in achievements.json */
    ach_category category;
    int points;               /* honor (誉) */
    int secret;               /* 1: shown as "Secret achievement" until unlocked */
    unsigned goal;            /* progress target, 0 = none (port-100-hours counts seconds) */
    const wchar_t *name[ACH_LANGS], *desc[ACH_LANGS];
} ach_def;

/* The achievements the port detects, the only ones listed and counted. */
extern const ach_def ach_catalog[];
extern const int ach_catalog_count;
/* Designed but not detected yet (their variables or moves are still to be found); ach_find
 * does not see them. */
extern const ach_def ach_parked[];
extern const int ach_parked_count;

/* The entry with that id, or NULL. */
const ach_def *ach_find(const char *id);
/* The kanji drawn on the category's tile (from the embedded Shippori Mincho subset). */
const wchar_t *ach_category_glyph(ach_category c);
/* "STORY" / "HISTORIA" and so on, upper case. */
const wchar_t *ach_category_name(ach_category c, int lang);
/* Points of the whole catalog. */
int ach_total_points(void);

#endif
