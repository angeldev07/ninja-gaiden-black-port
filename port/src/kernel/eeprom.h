/*
 * eeprom.h: Xbox EEPROM image (256 bytes) for ExQueryNonVolatileSetting.
 *
 * XAPI asks for the whole EEPROM (index XC_MAX_ALL = 0xFFFF) when XboxHDKey is zero: the
 * "non-roaming" signature of the hard drive files (XCalculateSignatureBeginEx) then
 * uses as its key the 16 bytes at offset 0x36 of the image. Before, only 4 bytes were
 * written and the rest was garbage from the game's stack, so the signature of T:\system.dat
 * changed from one run to the next ("Unable to load game settings").
 *
 * Layout (xboxdevwiki, "EEPROM"; Cxbx-Reloaded, XBOX_EEPROM): encrypted section
 * 0x00-0x2F, factory 0x30-0x5F and user 0x60-0xFF. Pure: no Windows or loader.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define EEPROM_SIZE 256u
#define EEPROM_FACTORY_OFFSET 0x30u
#define EEPROM_FACTORY_SIZE 0x30u
#define EEPROM_USER_OFFSET 0x60u
#define EEPROM_USER_SIZE 0xA0u

/* Values returned by the emulated kernel (the same as for the individual indices). The
 * regions are those of a PAL console unless eeprom_set_ntsc() turns it into a North American
 * one (an NTSC-U XBE: see main.c). */
#define EEPROM_LANGUAGE 1u              /* English */
#define EEPROM_AV_REGION 0x00800300u    /* PAL-I */
#define EEPROM_GAME_REGION 4u           /* rest of the world */
#define EEPROM_DVD_REGION 2u
#define EEPROM_AV_REGION_NTSC 0x00400100u /* NTSC-M */
#define EEPROM_GAME_REGION_NTSC 1u        /* North America */
#define EEPROM_DVD_REGION_NTSC 1u
#define EEPROM_SERIAL "000000000000"    /* 12 characters */

/* Fills `out` (EEPROM_SIZE bytes) with a deterministic EEPROM: keys at zero, serial
 * number and the regions above, and default user settings. */
void eeprom_image(uint8_t out[EEPROM_SIZE]);

/* 1: the console is North American (NTSC-M video, game and DVD region 1); 0: PAL (default). */
void eeprom_set_ntsc(int ntsc);
uint32_t eeprom_av_region(void);
uint32_t eeprom_game_region(void);
uint32_t eeprom_dvd_region(void);

/* User video flags (XC_VIDEO_FLAGS): 16:9 if `widescreen`, and PAL60 on a PAL console. The
 * port's vblank is always 60 Hz; a PAL console without PAL60 makes D3D emulate 60 Hz on a 50 Hz
 * TV with a kernel timer on the GPU clock, which the port does not run. */
#define EEPROM_VIDEO_WIDESCREEN 0x00010000u
#define EEPROM_VIDEO_PAL_60HZ 0x00400000u
uint32_t eeprom_video_flags(int widescreen);

/* AvSendTVEncoderOption(AV_QUERY_AV_CAPABILITIES): a composite cable (AV_PACK_STANDARD, 480i)
 * plus the standard and refresh rate of the AV region and the user video flags (PAL60 adds
 * 60 Hz). Layout: xboxdevwiki, "AV". */
#define EEPROM_AV_PACK_STANDARD 1u
uint32_t eeprom_av_capabilities(int widescreen);

/* Block returned by the indices XC_MAX_OS (0xFF), XC_MAX_FACTORY (0x1FF) and XC_MAX_ALL
 * (0xFFFF): offset and length inside the image. 0 if `index` is one of them; -1 if not. */
int eeprom_section(uint32_t index, uint32_t *offset, uint32_t *length);
