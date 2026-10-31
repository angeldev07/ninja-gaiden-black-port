/*
 * store.c: see store.h.
 */
#include "store.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>

int ach_store_find(const ach_store *s, const char *id) {
    int i;
    for (i = 0; i < s->nunlocked; i++)
        if (!strcmp(s->unlocked[i].id, id))
            return i;
    return -1;
}

int ach_store_unlock(ach_store *s, const char *id, int difficulty, const char *time) {
    if (ach_store_find(s, id) >= 0 || s->nunlocked >= ACH_STORE_MAX || strlen(id) >= sizeof s->unlocked[0].id)
        return 0;
    snprintf(s->unlocked[s->nunlocked].id, sizeof s->unlocked[0].id, "%s", id);
    snprintf(s->unlocked[s->nunlocked].time, sizeof s->unlocked[0].time, "%s", time ? time : "");
    s->unlocked[s->nunlocked].difficulty = difficulty;
    s->nunlocked++;
    return 1;
}

uint64_t ach_store_progress(const ach_store *s, const char *id) {
    int i;
    for (i = 0; i < s->nprogress; i++)
        if (!strcmp(s->progress[i].id, id))
            return s->progress[i].value;
    return 0;
}

void ach_store_set_progress(ach_store *s, const char *id, uint64_t value) {
    int i;
    for (i = 0; i < s->nprogress; i++)
        if (!strcmp(s->progress[i].id, id)) {
            s->progress[i].value = value;
            return;
        }
    if (s->nprogress >= ACH_STORE_MAX || strlen(id) >= sizeof s->progress[0].id)
        return;
    snprintf(s->progress[s->nprogress].id, sizeof s->progress[0].id, "%s", id);
    s->progress[s->nprogress].value = value;
    s->nprogress++;
}

int ach_store_load(ach_store *s, const char *path) {
    char line[512], id[32], time[24];
    unsigned long long value;
    int difficulty;
    FILE *f;
    memset(s, 0, sizeof *s);
    snprintf(s->path, sizeof s->path, "%s", path);
    if ((f = fopen(path, "r")) == NULL)
        return GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES ? 0 : -1;
    while (fgets(line, sizeof line, f)) {
        const char *p = strstr(line, "{\"id\": \"");
        if (!p)
            continue;
        if (sscanf(p, "{\"id\": \"%31[^\"]\", \"time\": \"%23[^\"]\", \"difficulty\": %d", id, time, &difficulty) == 3) {
            if (strcmp(id, "demo")) /* the spike's test unlock */
                ach_store_unlock(s, id, difficulty, time);
        } else if (sscanf(p, "{\"id\": \"%31[^\"]\", \"value\": %llu", id, &value) == 2) {
            ach_store_set_progress(s, id, value);
        }
    }
    fclose(f);
    return 0;
}

int ach_store_save(const ach_store *s) {
    char tmp[sizeof s->path + 4];
    FILE *f;
    int i;
    snprintf(tmp, sizeof tmp, "%s.tmp", s->path);
    if ((f = fopen(tmp, "w")) == NULL)
        return -1;
    fprintf(f, "{\n  \"version\": 2,\n  \"unlocked\": [\n");
    for (i = 0; i < s->nunlocked; i++)
        fprintf(f, "    {\"id\": \"%s\", \"time\": \"%s\", \"difficulty\": %d}%s\n", s->unlocked[i].id,
                s->unlocked[i].time, s->unlocked[i].difficulty, i + 1 < s->nunlocked ? "," : "");
    fprintf(f, "  ],\n  \"progress\": [\n");
    for (i = 0; i < s->nprogress; i++)
        fprintf(f, "    {\"id\": \"%s\", \"value\": %llu}%s\n", s->progress[i].id,
                (unsigned long long)s->progress[i].value, i + 1 < s->nprogress ? "," : "");
    fprintf(f, "  ]\n}\n");
    if (fclose(f)) {
        DeleteFileA(tmp);
        return -1;
    }
    return MoveFileExA(tmp, s->path, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
}
