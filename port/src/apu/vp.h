/*
 * vp.h: APU voice processor (VP) of the MCPX, minimal and with no sound yet.
 *
 * DirectSound (linked into the XBE) describes each voice in a RAM table (0x80 bytes
 * per voice, base at NV_PAPU_VPVADDR) and drives it with PIO methods written to the
 * MMIO page 0xFE820000 (VOICE_ON, VOICE_OFF, SET_VOICE_*...). On the hardware, the VP advances
 * the position of each active voice (CBO) at 48 kHz by its pitch, and DirectSound reads it for
 * GetCurrentPosition: Sofdec uses it to synchronize its video. Without VP the position does not
 * move and videos stay frozen.
 *
 * Offsets and bits: xemu hw/xbox/mcpx/apu/apu_regs.h (reference only; own code).
 * Pure code: the caller provides the voice memory and the elapsed time.
 */
#ifndef NGB_APU_VP_H
#define NGB_APU_VP_H

#include <stdint.h>

#define VP_MAX_VOICES 256
#define VP_VOICE_SIZE 0x80
#define VP_NO_VOICE 0xFFFFu

/* PIO methods (offset within the 0xFE820000 page) */
#define VP_PIO_SET_ANTECEDENT_VOICE 0x120
#define VP_PIO_VOICE_ON 0x124
#define VP_PIO_VOICE_OFF 0x128
#define VP_PIO_VOICE_RELEASE 0x12C
#define VP_PIO_VOICE_PAUSE 0x140
#define VP_PIO_SET_CURRENT_VOICE 0x2F8
#define VP_PIO_VOICE_LOCK 0x2FC
#define VP_PIO_SET_VOICE_CFG_VBIN 0x300 /* 0x300-0x318: words 0x00-0x18 of the voice */
#define VP_PIO_SET_VOICE_CFG_MISC 0x318
#define VP_PIO_SET_VOICE_TAR_VOLA 0x360 /* 0x360-0x368: words 0x60-0x68 (volumes) */
#define VP_PIO_SET_VOICE_TAR_VOLC 0x368
#define VP_PIO_SET_VOICE_TAR_PITCH 0x37C
#define VP_PIO_SET_VOICE_CFG_BUF_BASE 0x3A0
#define VP_PIO_SET_VOICE_CFG_BUF_LBO 0x3A4
#define VP_PIO_SET_VOICE_BUF_CBO 0x3D8
#define VP_PIO_SET_VOICE_CFG_BUF_EBO 0x3DC
#define VP_PIO_SET_CURRENT_INBUF_SGE 0x804        /* SGE table entry to write */
#define VP_PIO_SET_CURRENT_INBUF_SGE_OFFSET 0x808 /* its physical page (crash.c writes it) */

/* Structure of a voice (NV_PAVS_*) */
#define VP_VOICE_CFG_FMT 0x04
#define VP_FMT_LOOP (1u << 25)
#define VP_VOICE_CUR_PSL_START 0x20 /* buffer base (24 bits) */
#define VP_VOICE_CUR_PSH_SAMPLE 0x24 /* LBO: loop start, in samples */
#define VP_VOICE_PAR_STATE 0x54
#define VP_STATE_PAUSED (1u << 18)
#define VP_STATE_NEW_VOICE (1u << 20)
#define VP_STATE_ACTIVE (1u << 21)
#define VP_VOICE_PAR_OFFSET 0x58 /* CBO: current position, in samples (24 bits) */
#define VP_VOICE_PAR_NEXT 0x5C   /* EBO: last sample of the buffer (24 bits) */
#define VP_VOICE_TAR_PITCH_LINK 0x7C /* pitch (upper 16, signed) | next voice */
#define VP_OFFSET_MASK 0x00FFFFFFu
#define VP_IDLE_QUEUE 512

typedef struct vp_state {
    uint8_t *voices;  /* voice table (NULL until the game sets VPVADDR) */
    uint16_t current; /* SET_CURRENT_VOICE */
    uint32_t inbuf_sge; /* SET_CURRENT_INBUF_SGE */
    uint8_t active[VP_MAX_VOICES];
    double frac[VP_MAX_VOICES]; /* accumulated sample fraction */
    uint32_t voices_on, voices_ended; /* counters for the summary */
    /* Voices that have become free (VOICE_OFF, VOICE_RELEASE or end of buffer). The hardware
     * notifies each one with a front end trap (SE2FE_IDLE_VOICE) and the APU interrupt;
     * DirectSound only hands out one buffer per stop when it receives it. */
    uint16_t idle[VP_IDLE_QUEUE];
    uint32_t idle_head, idle_tail; /* circular queue: taken out at head, put in at tail */
    uint32_t idle_dropped;
} vp_state_t;

void vp_init(vp_state_t *s);
/* A PIO method written by the game (offset within 0xFE820000). */
void vp_method(vp_state_t *s, uint32_t method, uint32_t value);
/* Advances `seconds` of playback: CBO of every active, non-paused voice, at 48 kHz by its
 * pitch, with loop (LOOP) or end of voice at EBO. */
void vp_advance(vp_state_t *s, double seconds);
/* Samples per second of a voice according to its pitch (48000 · 2^(pitch/4096)). */
double vp_voice_rate(const uint8_t *voice);
/* Pieces of vp_advance that the mixer (mix.c) reuses:
 *   vp_voice_begin: voice `h` if it is active and not paused (clears NEW_VOICE), or NULL;
 *   vp_voice_move: advances its CBO `step` samples with loop or end; 0 if the voice ended. */
uint8_t *vp_voice_begin(vp_state_t *s, uint32_t h);
int vp_voice_move(vp_state_t *s, uint32_t h, uint8_t *v, uint32_t step);
/* Takes the next free voice pending notification; 0 if there is none. */
int vp_take_idle(vp_state_t *s, uint32_t *handle);

#endif
