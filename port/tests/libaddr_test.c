/* libaddr_test.c: moving reference (PAL) library addresses to another build (src/libaddr.c). */
#include <stdio.h>
#include <string.h>

#include "libaddr.h"

static int g_fail;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

static xbe_section_t section(const char *name, uint32_t va, uint32_t vsize) {
    xbe_section_t s;
    memset(&s, 0, sizeof s);
    strcpy(s.name, name);
    s.va = va;
    s.vsize = vsize;
    return s;
}

int main(void) {
    xbe_section_t secs[3];
    xbe_t xbe;
    memset(&xbe, 0, sizeof xbe);
    /* An NTSC-U-like layout: the same library sections, at other bases. */
    secs[0] = section(".text", 0x00011000u, 0x369000u);
    secs[1] = section("D3D", 0x003C5260u, 0x14380u);
    secs[2] = section("XPP", 0x003D95E0u, 0x07E34u);
    xbe.sections = secs;
    xbe.section_count = 3;

    /* XInputGetState and D3D_g_pDevice (in D3D's zero-filled part, past its raw data). */
    CHECK(libaddr_va(&xbe, 0x003DE22Bu) == 0x003DBB6Bu);
    CHECK(libaddr_va(&xbe, 0x003DAA88u) == 0x003D83C8u);
    CHECK(!strcmp(libaddr_section(0x003DE22Bu), "XPP"));
    /* The game's own code, or a library this XBE lacks, has no equivalent. */
    CHECK(libaddr_va(&xbe, 0x00100000u) == 0);
    CHECK(libaddr_section(0x00100000u) == NULL);
    CHECK(libaddr_va(&xbe, 0x0037CD90u) == 0); /* DSOUND */
    /* A library of another size is another build: refused. */
    secs[2].vsize = 0x07E38u;
    CHECK(libaddr_va(&xbe, 0x003DE22Bu) == 0);
    /* The reference build maps to itself. */
    secs[1] = section("D3D", 0x003C7920u, 0x14380u);
    secs[2] = section("XPP", 0x003DBCA0u, 0x07E34u);
    CHECK(libaddr_va(&xbe, 0x003DE22Bu) == 0x003DE22Bu);
    CHECK(libaddr_va(&xbe, 0x003DAA88u) == 0x003DAA88u);

    if (g_fail) {
        printf("libaddr_test: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("libaddr_test: OK\n");
    return 0;
}
