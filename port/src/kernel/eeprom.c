/*
 * eeprom.c: Xbox EEPROM image. See eeprom.h.
 */
#include "eeprom.h"

#include <string.h>

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

void eeprom_image(uint8_t out[EEPROM_SIZE]) {
    memset(out, 0, EEPROM_SIZE);
    /* Encrypted section: hash, confounder and disk key at zero; XBE region at 0x2C. */
    put32(out + 0x2C, eeprom_game_region());
    /* Factory: serial number (0x34, 12 bytes), MAC (0x40, zero) and video standard. */
    memcpy(out + 0x34, EEPROM_SERIAL, 12);
    put32(out + 0x58, eeprom_av_region());
    /* User: time zone at zero, language, video and audio flags, parental control,
     * network and DVD region. */
    put32(out + 0x90, EEPROM_LANGUAGE);
    put32(out + 0xBC, eeprom_dvd_region());
}

int eeprom_section(uint32_t index, uint32_t *offset, uint32_t *length) {
    switch (index) {
    case 0xFFu: *offset = EEPROM_USER_OFFSET; *length = EEPROM_USER_SIZE; return 0;
    case 0x1FFu: *offset = EEPROM_FACTORY_OFFSET; *length = EEPROM_FACTORY_SIZE; return 0;
    case 0xFFFFu: *offset = 0; *length = EEPROM_SIZE; return 0;
    default: return -1;
    }
}

static int g_ntsc;

void eeprom_set_ntsc(int ntsc) { g_ntsc = ntsc != 0; }
uint32_t eeprom_av_region(void) { return g_ntsc ? EEPROM_AV_REGION_NTSC : EEPROM_AV_REGION; }
uint32_t eeprom_game_region(void) { return g_ntsc ? EEPROM_GAME_REGION_NTSC : EEPROM_GAME_REGION; }
uint32_t eeprom_dvd_region(void) { return g_ntsc ? EEPROM_DVD_REGION_NTSC : EEPROM_DVD_REGION; }
