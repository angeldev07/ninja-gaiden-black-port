/*
 * audio_out.h: salida de audio por waveOut (48 kHz, estéreo, 16 bits). Ver audio_out.c.
 */
#ifndef NGB_AUDIO_OUT_H
#define NGB_AUDIO_OUT_H

#include <windows.h>
#include <stdint.h>

int audio_out_open(void);                                    /* 0 si hay dispositivo */
void audio_out_write(const float *stereo, uint32_t frames); /* entrelazado L,R en [-1, 1] */
uint32_t audio_out_dropped(void);                           /* frames descartados */

#endif
