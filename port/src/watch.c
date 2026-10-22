/*
 * watch.c: watching game variables (--watch), for reverse engineering.
 *
 *   --watch 0xD7DCF9:1,0xC316B8,0xC316F8:4
 *
 * The DPC thread reads each address about 60 times per second and writes every change to the
 * log with the time and the kernel call number. It is useful to see when a flag or a state
 * found in Ghidra changes, without stopping the game.
 */
#include "ngb.h"

#include <stdlib.h>
#include <string.h>

#include "kernel/kernel.h"

#define WATCH_MAX 16

static struct {
    uint32_t addr, size, last;
    int seen;
} g_watch[WATCH_MAX];
static int g_nwatch;

int ngb_watch_parse(const char *spec) {
    const char *p = spec;
    while (*p) {
        char *end;
        uint32_t addr = (uint32_t)strtoul(p, &end, 16), size = 4;
        if (end == p || g_nwatch >= WATCH_MAX)
            return -1;
        p = end;
        if (*p == ':') {
            size = (uint32_t)strtoul(p + 1, &end, 10);
            if (end == p + 1 || (size != 1 && size != 2 && size != 4))
                return -1;
            p = end;
        }
        if (*p == ',')
            p++;
        else if (*p)
            return -1;
        g_watch[g_nwatch].addr = addr;
        g_watch[g_nwatch].size = size;
        g_nwatch++;
    }
    return 0;
}

static int readable(uint32_t addr, uint32_t size) {
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery((const void *)(uintptr_t)addr, &m, sizeof m) || m.State != MEM_COMMIT ||
        (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return 0;
    return addr + size <= (uint32_t)(uintptr_t)m.BaseAddress + (uint32_t)m.RegionSize;
}

void ngb_watch_tick(void) {
    int i;
    for (i = 0; i < g_nwatch; i++) {
        uint32_t v = 0;
        if (!readable(g_watch[i].addr, g_watch[i].size))
            continue;
        memcpy(&v, (const void *)(uintptr_t)g_watch[i].addr, g_watch[i].size);
        if (!g_watch[i].seen || v != g_watch[i].last) {
            ngb_log("    · watch 0x%08X: 0x%X -> 0x%X (%.3f s, call %lu)", g_watch[i].addr,
                    g_watch[i].last, v, ngb_elapsed(), ngb_kernel_call_seq());
            g_watch[i].last = v;
            g_watch[i].seen = 1;
        }
    }
}
