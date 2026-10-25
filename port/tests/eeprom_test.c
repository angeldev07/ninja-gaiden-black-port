/*
 * eeprom_test.c: the EEPROM image is deterministic and has its fields in the right place.
 */
#include <stdio.h>
#include <string.h>

#include "kernel/eeprom.h"

static int g_fail;

#define CHECK(c)                                                                                  \
    do {                                                                                          \
        if (!(c)) {                                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                  \
            g_fail = 1;                                                                           \
        }                                                                                         \
    } while (0)

static unsigned get32(const unsigned char *p) {
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

int main(void) {
    unsigned char a[EEPROM_SIZE], b[EEPROM_SIZE];
    uint32_t off = 0, len = 0;
    memset(a, 0xCC, sizeof a); /* different garbage in each buffer: the image overwrites all of it */
    memset(b, 0x5A, sizeof b);
    eeprom_image(a);
    eeprom_image(b);
    CHECK(!memcmp(a, b, EEPROM_SIZE));
    CHECK(get32(a + 0x2C) == EEPROM_GAME_REGION);
    CHECK(!memcmp(a + 0x34, EEPROM_SERIAL, 12));
    CHECK(get32(a + 0x58) == EEPROM_AV_REGION);
    CHECK(get32(a + 0x90) == EEPROM_LANGUAGE);
    CHECK(get32(a + 0xBC) == EEPROM_DVD_REGION);
    /* The key XAPI uses without XboxHDKey: offset 0x36, 16 bytes ("0000000000" + MAC at 0). */
    CHECK(!memcmp(a + 0x36, "0000000000\0\0\0\0\0\0", 16));
    CHECK(!eeprom_section(0xFFFF, &off, &len) && off == 0 && len == 256);
    CHECK(!eeprom_section(0x1FF, &off, &len) && off == 0x30 && len == 0x30);
    CHECK(!eeprom_section(0xFF, &off, &len) && off == 0x60 && len == 0xA0);
    CHECK(eeprom_section(0x7, &off, &len) == -1);
    /* AV capabilities: composite cable, PAL-I at 50 Hz plus PAL60 (60 Hz); NTSC-M at 60 Hz. */
    CHECK(eeprom_video_flags(0) == EEPROM_VIDEO_PAL_60HZ);
    CHECK(eeprom_av_capabilities(0) == 0x00C00301u);
    CHECK(eeprom_av_capabilities(1) == 0x00C10301u);
    eeprom_set_ntsc(1);
    CHECK(eeprom_video_flags(1) == EEPROM_VIDEO_WIDESCREEN);
    CHECK(eeprom_av_capabilities(0) == 0x00400101u);
    CHECK(eeprom_av_capabilities(1) == 0x00410101u);
    eeprom_set_ntsc(0);
    if (!g_fail)
        printf("eeprom_test: OK\n");
    return g_fail;
}
