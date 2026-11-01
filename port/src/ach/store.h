/*
 * store.h: %NGB_DATA%\achievements.json, written by the loader and read by the launcher.
 *
 * One record per line, so both sides read it with sscanf and no JSON library:
 *     {"id": "chapter-01", "time": "2026-10-31T20:38:34Z", "difficulty": 1}   (in "unlocked")
 *     {"id": "port-100-hours", "value": 111600}                                (in "progress")
 * Version 1 files (the spike's: only "unlocked") load as they are; the spike's "demo" record is
 * dropped. Saving writes a temporary file and renames it over the old one. Tested by
 * port/tests/ach_store_test.c.
 */
#ifndef NGB_ACH_STORE_H
#define NGB_ACH_STORE_H

#include <stdint.h>

#define ACH_STORE_MAX 128

typedef struct ach_store {
    char path[260];
    struct {
        char id[32], time[24];
        int difficulty; /* -1 when it does not apply */
    } unlocked[ACH_STORE_MAX];
    int nunlocked;
    struct {
        char id[32];
        uint64_t value;
    } progress[ACH_STORE_MAX];
    int nprogress;
} ach_store;

/* Reads `path` (a missing file is an empty store). Always sets s->path; 0, or -1 if the file
 * exists and could not be read. */
int ach_store_load(ach_store *s, const char *path);
int ach_store_save(const ach_store *s);

/* Index of the unlock record, or -1. */
int ach_store_find(const ach_store *s, const char *id);
/* 1 if it is new (recorded with `time`), 0 if it was already there or the store is full. */
int ach_store_unlock(ach_store *s, const char *id, int difficulty, const char *time);

uint64_t ach_store_progress(const ach_store *s, const char *id);
void ach_store_set_progress(ach_store *s, const char *id, uint64_t value);

#endif
