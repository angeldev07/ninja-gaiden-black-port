/*
 * glload.c: los punteros de glload.h y su carga con wglGetProcAddress.
 */
#include "glload.h"

#define NGL_DEFINE(ret, name, args) ngl_pfn_##name ngl_##name;
NGL_FUNCTIONS(NGL_DEFINE)
#undef NGL_DEFINE

/* wglGetProcAddress devuelve 0, 1, 2, 3 o -1 en algunos drivers cuando no hay función. */
static PROC lookup(const char *name) {
    PROC proc = wglGetProcAddress(name);
    INT_PTR value = (INT_PTR)proc;
    if (value >= -1 && value <= 3) {
        return NULL;
    }
    return proc;
}

int ngl_load(int phase, const char **missing) {
    int absent = 0;
    if (missing) {
        *missing = NULL;
    }
#define NGL_LOAD(ret, name, args)                                                         \
    if ((phase == 0) == (sizeof(#name) > 3 && #name[0] == 'w')) {                        \
        ngl_##name = (ngl_pfn_##name)lookup(#name);                              \
        if (!ngl_##name) {                                                                \
            if (missing && !*missing) {                                                   \
                *missing = #name;                                                         \
            }                                                                             \
            absent++;                                                                     \
        }                                                                                 \
    }
    NGL_FUNCTIONS(NGL_LOAD)
#undef NGL_LOAD
    return absent;
}
