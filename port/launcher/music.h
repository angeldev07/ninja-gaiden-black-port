/*
 * music.h: the title theme of the user's own copy, played by the launcher in game art mode.
 *
 * CRI ADX decoder (port of src/ngb_tools/audio/adx.py: header, version 3/4 loop block, 4-bit
 * ADPCM with the cutoff coefficients; encrypted streams are refused) and a waveOut player on a
 * worker thread that loops at the file's loop points and fades the volume.
 * The track is the bgm.afs entry "bgm_title.adx", else "bgm_startmenu.adx", else silence.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct adx_info {
    int channels, rate, version;
    uint32_t samples;                /* per channel */
    int has_loop;
    uint32_t loop_start, loop_end;   /* samples per channel */
    size_t data_offset;              /* first 18-byte block */
    int c1, c2;                      /* predictor coefficients, x4096 */
} adx_info_t;

typedef struct adx_state {
    int h1[8], h2[8]; /* history per channel */
} adx_state_t;

/* 1 if `p` is a supported ADX (standard encoding, 18-byte blocks, 4 bits, not encrypted). */
int adx_parse(const uint8_t *p, size_t n, adx_info_t *out);

/* Decodes the 32-sample frames [frame0, frame0 + nframes) of every channel into `out`
 * (interleaved PCM16, 32 * nframes * channels values), continuing from `st`. Returns the
 * number of frames decoded (fewer at the end of the data). */
uint32_t adx_decode(const adx_info_t *h, const uint8_t *p, size_t n, uint32_t frame0, uint32_t nframes,
                    int16_t *out, adx_state_t *st);

/* Player. All calls come from the UI thread. */
void music_start(const wchar_t *game_dir); /* loads and starts silent, then fades in */
void music_fade_to(float volume, int ms);  /* 0..1 */
void music_stop(void);                     /* stops and frees */
int music_playing(void);
/* Buffers handed to waveOut since the last music_start (diagnostics and tests). */
unsigned music_buffers_written(void);
