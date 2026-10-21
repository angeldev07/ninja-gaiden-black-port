/*
 * vp.h: procesador de voces (VP) de la APU del MCPX, mínimo y sin sonido todavía.
 *
 * DirectSound (enlazado en el XBE) describe cada voz en una tabla de la RAM (0x80 bytes
 * por voz, base en NV_PAPU_VPVADDR) y la gobierna con métodos PIO escritos en la página
 * MMIO 0xFE820000 (VOICE_ON, VOICE_OFF, SET_VOICE_*...). En el hardware, el VP avanza la
 * posición de cada voz activa (CBO) a 48 kHz por su pitch, y DirectSound la lee para
 * GetCurrentPosition: con ella sincroniza Sofdec su vídeo. Sin VP la posición no se mueve
 * y los vídeos se quedan congelados.
 *
 * Offsets y bits: hw/xbox/mcpx/apu/apu_regs.h de xemu (solo referencia; código propio).
 * Código puro: el llamador da la memoria de voces y el tiempo transcurrido.
 */
#ifndef NGB_APU_VP_H
#define NGB_APU_VP_H

#include <stdint.h>

#define VP_MAX_VOICES 256
#define VP_VOICE_SIZE 0x80
#define VP_NO_VOICE 0xFFFFu

/* Métodos PIO (offset dentro de la página 0xFE820000) */
#define VP_PIO_SET_ANTECEDENT_VOICE 0x120
#define VP_PIO_VOICE_ON 0x124
#define VP_PIO_VOICE_OFF 0x128
#define VP_PIO_VOICE_RELEASE 0x12C
#define VP_PIO_VOICE_PAUSE 0x140
#define VP_PIO_SET_CURRENT_VOICE 0x2F8
#define VP_PIO_VOICE_LOCK 0x2FC
#define VP_PIO_SET_VOICE_CFG_VBIN 0x300 /* 0x300-0x318: palabras 0x00-0x18 de la voz */
#define VP_PIO_SET_VOICE_CFG_MISC 0x318
#define VP_PIO_SET_VOICE_TAR_PITCH 0x37C
#define VP_PIO_SET_VOICE_CFG_BUF_BASE 0x3A0
#define VP_PIO_SET_VOICE_CFG_BUF_LBO 0x3A4
#define VP_PIO_SET_VOICE_BUF_CBO 0x3D8
#define VP_PIO_SET_VOICE_CFG_BUF_EBO 0x3DC

/* Estructura de una voz (NV_PAVS_*) */
#define VP_VOICE_CFG_FMT 0x04
#define VP_FMT_LOOP (1u << 25)
#define VP_VOICE_CUR_PSL_START 0x20 /* base del búfer (24 bits) */
#define VP_VOICE_CUR_PSH_SAMPLE 0x24 /* LBO: inicio del bucle, en muestras */
#define VP_VOICE_PAR_STATE 0x54
#define VP_STATE_PAUSED (1u << 18)
#define VP_STATE_NEW_VOICE (1u << 20)
#define VP_STATE_ACTIVE (1u << 21)
#define VP_VOICE_PAR_OFFSET 0x58 /* CBO: posición actual, en muestras (24 bits) */
#define VP_VOICE_PAR_NEXT 0x5C   /* EBO: última muestra del búfer (24 bits) */
#define VP_VOICE_TAR_PITCH_LINK 0x7C /* pitch (16 altos, con signo) | siguiente voz */
#define VP_OFFSET_MASK 0x00FFFFFFu

typedef struct vp_state {
    uint8_t *voices;  /* tabla de voces (NULL hasta que el juego fija VPVADDR) */
    uint16_t current; /* SET_CURRENT_VOICE */
    uint8_t active[VP_MAX_VOICES];
    double frac[VP_MAX_VOICES]; /* fracción de muestra acumulada */
    uint32_t voices_on, voices_ended; /* contadores para el resumen */
} vp_state_t;

void vp_init(vp_state_t *s);
/* Un método PIO escrito por el juego (offset dentro de 0xFE820000). */
void vp_method(vp_state_t *s, uint32_t method, uint32_t value);
/* Avanza `seconds` de reproducción: CBO de cada voz activa y no pausada, a 48 kHz por su
 * pitch, con bucle (LOOP) o fin de voz en EBO. */
void vp_advance(vp_state_t *s, double seconds);
/* Muestras por segundo de una voz según su pitch (48000 · 2^(pitch/4096)). */
double vp_voice_rate(const uint8_t *voice);

#endif
