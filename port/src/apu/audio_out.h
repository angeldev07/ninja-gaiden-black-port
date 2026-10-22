/*
 * audio_out.h: waveOut audio output (48 kHz, stereo, 16 bits). See audio_out.c.
 */
#ifndef NGB_AUDIO_OUT_H
#define NGB_AUDIO_OUT_H

#include <windows.h>
#include <stdint.h>

int audio_out_open(void);                                    /* 0 if there is a device */
void audio_out_write(const float *stereo, uint32_t frames); /* interleaved L,R in [-1, 1] */
uint32_t audio_out_dropped(void);                           /* frames dropped */
uint32_t audio_out_queued(void);    /* frames queued and not yet played */
uint32_t audio_out_underruns(void); /* times the device ran out of data */

#endif
