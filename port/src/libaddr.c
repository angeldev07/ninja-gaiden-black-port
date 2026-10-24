/*
 * libaddr.c: see libaddr.h.
 */
#include "libaddr.h"

#include <string.h>

/* The library sections of the reference build (PAL default.xbe, title id 0x5443000D, version 3):
 * name, VA and virtual size, from its section headers. The NTSC-U release (version 5) has the
 * same sizes. */
static const struct {
    const char *name;
    uint32_t va, vsize;
} k_ref[] = {
    {"D3DX", 0x0037CB40u, 0x00228u},   {"DSOUND", 0x0037CD80u, 0x1C7FCu},
    {"XONLINE", 0x00399580u, 0x188FCu}, {"XNET", 0x003B1E80u, 0x11D98u},
    {"PSFD00", 0x003C3C20u, 0x02BB8u}, {"XGRPH", 0x003C67E0u, 0x01124u},
    {"D3D", 0x003C7920u, 0x14380u},    {"XPP", 0x003DBCA0u, 0x07E34u},
    {"DOLBY", 0x00E13C00u, 0x07180u},
};

#define N_REF (sizeof k_ref / sizeof k_ref[0])

static int ref_index(uint32_t ref_va) {
    size_t i;
    for (i = 0; i < N_REF; i++)
        if (ref_va >= k_ref[i].va && ref_va - k_ref[i].va < k_ref[i].vsize)
            return (int)i;
    return -1;
}

const char *libaddr_section(uint32_t ref_va) {
    int i = ref_index(ref_va);
    return i < 0 ? NULL : k_ref[i].name;
}

uint32_t libaddr_va(const xbe_t *xbe, uint32_t ref_va) {
    int r = ref_index(ref_va);
    uint32_t s;
    if (r < 0 || !xbe || !xbe->sections)
        return 0;
    for (s = 0; s < xbe->section_count; s++) {
        const xbe_section_t *sec = &xbe->sections[s];
        if (strcmp(sec->name, k_ref[r].name))
            continue;
        if (sec->vsize != k_ref[r].vsize)
            return 0; /* another library build: the offsets mean nothing */
        return sec->va + (ref_va - k_ref[r].va);
    }
    return 0;
}
