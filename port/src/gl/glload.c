/*
 * glload.c: the glload.h pointers and their loading with wglGetProcAddress.
 */
#include "glload.h"

#define NGL_DEFINE(ret, name, args) ngl_pfn_##name ngl_##name;
NGL_FUNCTIONS(NGL_DEFINE)
NGL_OPTIONAL_FUNCTIONS(NGL_DEFINE)
#undef NGL_DEFINE

/* wglGetProcAddress returns 0, 1, 2, 3 or -1 on some drivers when the function does not exist. */
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
#define NGL_LOAD_OPTIONAL(ret, name, args)                                                \
    if (phase == 1) {                                                                     \
        ngl_##name = (ngl_pfn_##name)lookup(#name);                                       \
    }
    NGL_OPTIONAL_FUNCTIONS(NGL_LOAD_OPTIONAL)
#undef NGL_LOAD_OPTIONAL
    return absent;
}
