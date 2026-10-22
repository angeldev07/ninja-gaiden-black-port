/*
 * hostpath.c: see hostpath.h.
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
        return 1; /* double "\\": ignored */
    if ((n == 1 && c[0] == '.') || (n == 2 && c[0] == '.' && c[1] == '.'))
        return 0;
    for (i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)c[i];
        if (ch < 0x20 || strchr(":*?\"<>|", ch))
            return 0;
    }
    /* Windows strips trailing dots and spaces: "..." or ". ." would end up as "..". */
    if (c[n - 1] == '.' || c[n - 1] == ' ')
        return 0;
    return 1;
}

int hostpath_rel_ok(const char *rel) {
    const char *p = rel, *start;
    if (!rel)
        return 0;
    if (*p && *p != '\\')
        return 0; /* it has to follow the device prefix with '\' or be empty */
    while (*p) {
        while (*p == '\\')
            p++;
        start = p;
        while (*p && *p != '\\' && *p != '/')
            p++;
        if (*p == '/')
            return 0; /* '/' also separates on Windows: not accepted */
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
    /* The final path is the root or hangs from it ("root\..."), case-insensitively. */
    if (_strnicmp(full, full_root, lr) != 0 ||
        (full[lr] != 0 && full[lr] != '\\' && full_root[lr - 1] != '\\'))
        return -1;
    memcpy(out, full, (size_t)n + 1);
    return 0;
}
