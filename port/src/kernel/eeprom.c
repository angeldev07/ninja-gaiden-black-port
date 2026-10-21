/*
 * eeprom.c: imagen de la EEPROM de la Xbox. Ver eeprom.h.
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
    /* Sección cifrada: hash, confusor y clave del disco a cero; región del XBE en 0x2C. */
    put32(out + 0x2C, EEPROM_GAME_REGION);
    /* Fábrica: número de serie (0x34, 12 bytes), MAC (0x40, a cero) y estándar de vídeo. */
    memcpy(out + 0x34, EEPROM_SERIAL, 12);
    put32(out + 0x58, EEPROM_AV_REGION);
    /* Usuario: zona horaria a cero, idioma, banderas de vídeo y audio, control parental,
     * red y región del DVD. */
    put32(out + 0x90, EEPROM_LANGUAGE);
    put32(out + 0xBC, EEPROM_DVD_REGION);
}

int eeprom_section(uint32_t index, uint32_t *offset, uint32_t *length) {
    switch (index) {
    case 0xFFu: *offset = EEPROM_USER_OFFSET; *length = EEPROM_USER_SIZE; return 0;
    case 0x1FFu: *offset = EEPROM_FACTORY_OFFSET; *length = EEPROM_FACTORY_SIZE; return 0;
    case 0xFFFFu: *offset = 0; *length = EEPROM_SIZE; return 0;
    default: return -1;
    }
}
