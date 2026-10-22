/*
 * mix.h: reading APU voice samples and mixing to stereo (pure code).
 *
 * Each buffer voice describes its samples in the voice table (vp.h):
 *   - CFG_FMT: container (1, 2 or 4 bytes, or ADPCM of 36 bytes per channel and 64-sample block),
 *     sample size (U8/S16/S24/S32), stereo and samples per block;
 *   - CUR_PSL_START (BA): linear address of the buffer, translated page by page
 *     (4 KB) with the SGE table of NV_PAPU_VPSGEADDR (8 bytes per entry; the first
 *     word is the physical page);
 *   - TAR_VOLA/B/C: 8 12-bit attenuations (1/64 dB per step, 0xFFF = silence), one
 *     per mixbin, and CFG_VBIN/CFG_FMT say which mixbin each one goes to.
 * DirectSound uses mixbins 0 and 1 as front left and right; 2 is the center
 * and 4 and 5 the rears. The rest (effects, LFE...) is not heard yet.
 * Source of the fields: xemu (hw/xbox/mcpx/apu), as a reference only.
 */
#ifndef NGB_APU_MIX_H
#define NGB_APU_MIX_H

#include <stdint.h>

#include "apu/vp.h"

#define MIX_ADPCM_BLOCK 36   /* bytes per channel and block */
#define MIX_ADPCM_SAMPLES 64 /* samples per channel and block */

/* Translates an Xbox physical address into host memory; NULL if it is not valid. */
typedef const uint8_t *(*mix_phys_fn)(uint32_t phys, uint32_t len);

typedef struct mix_voice_cache {
    uint32_t block; /* decoded ADPCM block (+1; 0 = none) */
    uint32_t ba;    /* buffer it belongs to (the voice may change buffer) */
    int16_t pcm[2][MIX_ADPCM_SAMPLES + 1];
} mix_voice_cache_t;

typedef struct mix_ctx {
    mix_phys_fn phys;
    uint32_t sge_phys; /* NV_PAPU_VPSGEADDR */
    mix_voice_cache_t cache[VP_MAX_VOICES];
} mix_ctx_t;

/* Decodes an Xbox ADPCM block (IMA: 4-byte header and 32 bytes of nibbles per
 * channel, interleaved every 4 bytes) into `channels` channels of 65 samples. */
void mix_adpcm_decode(const uint8_t *block, int channels, int16_t out[2][MIX_ADPCM_SAMPLES + 1]);

/* Sample `ch` of the voice at position `cbo`, in [-1, 1]. 0 if it cannot be read. */
float mix_voice_sample(mix_ctx_t *m, uint32_t handle, const uint8_t *voice, uint32_t cbo, int ch);

/* Voice gains according to its volumes and mixbins: g[channel][0] goes to the left and
 * g[channel][1] to the right. Slot b of the 8 carries channel b % channels.
 * 3D voices (handle < MIX_3D_VOICES) send their slots 0-3 to the positioning
 * submixes (HRTF) and not to CFG_VBIN: DirectSound puts the sound's pan there
 * (per-ear attenuation). Without emulated HRTF they are taken as front L/R and rear
 * L/R. */
#define MIX_3D_VOICES 64
void mix_voice_gains(const uint8_t *voice, int is3d, float g[2][2]);

/* 1 if the voice is one of those that DirectSound feeds with the DSP (loop of fewer than 256
 * samples): without an emulated DSP it would be noise, so it is not heard. */
int mix_voice_is_dsp_feed(const uint8_t *voice);

/* Advances `frames` output samples at 48 kHz: same as vp_advance (position, loop and end
 * of voice) and, in addition, adds every active voice into `out` (interleaved stereo, not cleared). */
void mix_render(vp_state_t *s, mix_ctx_t *m, float *out, uint32_t frames);

#endif
