/*
 * hostpath.c: ver hostpath.h.
 */
#include "hostpath.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>

#define HOSTPATH_MAX 1024

static int component_ok(const char *c, size_t n) {
    size_t i;
    if (n == 0)
        return 1; /* "\\" doble: se ignora */
    if ((n == 1 && c[0] == '.') || (n == 2 && c[0] == '.' && c[1] == '.'))
        return 0;
    for (i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)c[i];
        if (ch < 0x20 || strchr(":*?\"<>|", ch))
            return 0;
    }
    /* Windows quita puntos y espacios finales: "..." o ". ." acabarían siendo "..". */
    if (c[n - 1] == '.' || c[n - 1] == ' ')
        return 0;
    return 1;
}

int hostpath_rel_ok(const char *rel) {
    const char *p = rel, *start;
    if (!rel)
        return 0;
    if (*p && *p != '\\')
        return 0; /* tiene que seguir al prefijo del dispositivo con '\' o estar vacío */
    while (*p) {
        while (*p == '\\')
            p++;
        start = p;
        while (*p && *p != '\\' && *p != '/')
            p++;
        if (*p == '/')
            return 0; /* '/' también separa en Windows: no se admite */
        if (!component_ok(start, (size_t)(p - start)))
            return 0;
    }
    return 1;
}

int hostpath_join(const char *root, const char *rel, char *out, size_t outlen) {
    char joined[HOSTPATH_MAX], full_root[HOSTPATH_MAX], full[HOSTPATH_MAX];
    DWORD n, nr;
    size_t lr;
    if (outlen)
        out[0] = 0;
    if (!root || !*root || !hostpath_rel_ok(rel))
        return -1;
    nr = GetFullPathNameA(root, sizeof full_root, full_root, NULL);
    if (!nr || nr >= sizeof full_root)
        return -1;
    lr = strlen(full_root);
    while (lr > 3 && full_root[lr - 1] == '\\')
        full_root[--lr] = 0;
    if ((size_t)snprintf(joined, sizeof joined, "%s%s", full_root, rel) >= sizeof joined)
        return -1;
    n = GetFullPathNameA(joined, sizeof full, full, NULL);
    if (!n || n >= sizeof full || n >= outlen)
        return -1;
    /* La ruta final es la raíz o cuelga de ella ("root\..."), sin distinguir mayúsculas. */
    if (_strnicmp(full, full_root, lr) != 0 ||
        (full[lr] != 0 && full[lr] != '\\' && full_root[lr - 1] != '\\'))
        return -1;
    memcpy(out, full, (size_t)n + 1);
    return 0;
}
