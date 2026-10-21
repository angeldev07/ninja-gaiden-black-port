/*
 * eeprom.h: imagen de la EEPROM de la Xbox (256 bytes) para ExQueryNonVolatileSetting.
 *
 * XAPI pide la EEPROM entera (índice XC_MAX_ALL = 0xFFFF) cuando XboxHDKey es cero: la
 * firma "no itinerante" de los archivos del disco duro (XCalculateSignatureBeginEx) usa
 * entonces como clave los 16 bytes del offset 0x36 de la imagen. Antes solo se escribían 4
 * bytes y el resto era basura de la pila del juego, así que la firma de T:\system.dat
 * cambiaba de una ejecución a otra ("Unable to load game settings").
 *
 * Disposición (xboxdevwiki, "EEPROM"; Cxbx-Reloaded, XBOX_EEPROM): sección cifrada
 * 0x00-0x2F, de fábrica 0x30-0x5F y de usuario 0x60-0xFF. Pura: sin Windows ni loader.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define EEPROM_SIZE 256u
#define EEPROM_FACTORY_OFFSET 0x30u
#define EEPROM_FACTORY_SIZE 0x30u
#define EEPROM_USER_OFFSET 0x60u
#define EEPROM_USER_SIZE 0xA0u

/* Valores que devuelve el kernel emulado (los mismos que los índices sueltos). */
#define EEPROM_LANGUAGE 1u              /* inglés */
#define EEPROM_AV_REGION 0x00800300u    /* PAL-I */
#define EEPROM_GAME_REGION 4u           /* resto del mundo */
#define EEPROM_DVD_REGION 2u
#define EEPROM_SERIAL "000000000000"    /* 12 caracteres */

/* Rellena `out` (EEPROM_SIZE bytes) con una EEPROM determinista: claves a cero, número de
 * serie y regiones de arriba y ajustes de usuario por defecto. */
void eeprom_image(uint8_t out[EEPROM_SIZE]);

/* Bloque que devuelven los índices XC_MAX_OS (0xFF), XC_MAX_FACTORY (0x1FF) y XC_MAX_ALL
 * (0xFFFF): offset y longitud dentro de la imagen. 0 si `index` es uno de ellos; -1 si no. */
int eeprom_section(uint32_t index, uint32_t *offset, uint32_t *length);
