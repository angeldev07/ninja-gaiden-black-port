/*
 * symbols.c: nombres de funciones del XBE para el log (--symbols CSV).
 *
 * El CSV sale del análisis con Ghidra (H5): `address,name,library,kind,section`, con la
 * dirección en hex. Vive en NGB_DATA (nunca en el repo). Solo se usan las filas de tipo
 * "function"; una dirección se nombra como la función más cercana por debajo (+offset).
 */
#include "ngb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SYMBOLS 8192
#define MAX_SYMBOL_DISTANCE 0x4000u /* más lejos que esto no se atribuye a la función */

typedef struct symbol {
    uint32_t addr;
    char name[64];
} symbol_t;

static symbol_t *g_syms;
static size_t g_nsyms;

static int cmp_sym(const void *a, const void *b) {
    uint32_t x = ((const symbol_t *)a)->addr, y = ((const symbol_t *)b)->addr;
    return x < y ? -1 : x > y;
}

int ngb_symbols_load(const char *path) {
    FILE *f = NULL;
    char line[512];
    if (fopen_s(&f, path, "r") || !f)
        return -1;
    g_syms = (symbol_t *)calloc(MAX_SYMBOLS, sizeof *g_syms);
    if (!g_syms) {
        fclose(f);
        return -1;
    }
    while (fgets(line, sizeof line, f) && g_nsyms < MAX_SYMBOLS) {
        char *addr = strtok(line, ","), *name = strtok(NULL, ","), *lib = strtok(NULL, ",");
        char *kind = strtok(NULL, ",");
        (void)lib;
        if (!addr || !name || !kind || strncmp(addr, "0x", 2) || strcmp(kind, "function"))
            continue;
        g_syms[g_nsyms].addr = (uint32_t)strtoul(addr, NULL, 16);
        snprintf(g_syms[g_nsyms].name, sizeof g_syms[g_nsyms].name, "%s", name);
        g_nsyms++;
    }
    fclose(f);
    qsort(g_syms, g_nsyms, sizeof *g_syms, cmp_sym);
    ngb_log("símbolos: %u funciones de %s", (unsigned)g_nsyms, path);
    return 0;
}

const char *ngb_symbol(uint32_t addr, char *buf, size_t buflen) {
    size_t lo = 0, hi = g_nsyms;
    buf[0] = 0;
    if (!g_nsyms)
        return buf;
    while (lo < hi) { /* última entrada con addr <= dirección */
        size_t mid = (lo + hi) / 2;
        if (g_syms[mid].addr <= addr)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo && addr - g_syms[lo - 1].addr < MAX_SYMBOL_DISTANCE)
        snprintf(buf, buflen, " <%s+0x%X>", g_syms[lo - 1].name, addr - g_syms[lo - 1].addr);
    return buf;
}
