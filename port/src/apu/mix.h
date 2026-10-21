/*
 * mix.h: lectura de muestras de las voces de la APU y mezcla a estéreo (código puro).
 *
 * Cada voz de búfer describe sus muestras en la tabla de voces (vp.h):
 *   - CFG_FMT: contenedor (1, 2 o 4 bytes, o ADPCM de 36 bytes por canal y bloque de 64),
 *     tamaño de muestra (U8/S16/S24/S32), estéreo y muestras por bloque;
 *   - CUR_PSL_START (BA): dirección lineal del búfer, que se traduce página a página
 *     (4 KB) con la tabla SGE de NV_PAPU_VPSGEADDR (8 bytes por entrada; la primera
 *     palabra es la página física);
 *   - TAR_VOLA/B/C: 8 atenuaciones de 12 bits (1/64 dB por paso, 0xFFF = silencio), una
 *     por mixbin, y CFG_VBIN/CFG_FMT dicen a qué mixbin va cada una.
 * DirectSound usa los mixbins 0 y 1 como frontal izquierdo y derecho; el 2 es el central
 * y el 4 y el 5 los traseros. El resto (efectos, LFE...) no se oye todavía.
 * Fuente de los campos: xemu (hw/xbox/mcpx/apu), solo como referencia.
 */
#ifndef NGB_APU_MIX_H
#define NGB_APU_MIX_H

#include <stdint.h>

#include "apu/vp.h"

#define MIX_ADPCM_BLOCK 36   /* bytes por canal y bloque */
#define MIX_ADPCM_SAMPLES 64 /* muestras por canal y bloque */

/* Traduce una dirección física de la Xbox a memoria del host; NULL si no es válida. */
typedef const uint8_t *(*mix_phys_fn)(uint32_t phys, uint32_t len);

typedef struct mix_voice_cache {
    uint32_t block; /* bloque ADPCM decodificado (+1; 0 = ninguno) */
    uint32_t ba;    /* búfer al que pertenece (la voz puede cambiar de búfer) */
    int16_t pcm[2][MIX_ADPCM_SAMPLES + 1];
} mix_voice_cache_t;

typedef struct mix_ctx {
    mix_phys_fn phys;
    uint32_t sge_phys; /* NV_PAPU_VPSGEADDR */
    mix_voice_cache_t cache[VP_MAX_VOICES];
} mix_ctx_t;

/* Decodifica un bloque ADPCM de Xbox (IMA: cabecera de 4 bytes y 32 bytes de nibbles por
 * canal, entrelazados cada 4 bytes) en `channels` canales de 65 muestras. */
void mix_adpcm_decode(const uint8_t *block, int channels, int16_t out[2][MIX_ADPCM_SAMPLES + 1]);

/* Muestra `ch` de la voz en la posición `cbo`, en [-1, 1]. 0 si no se puede leer. */
float mix_voice_sample(mix_ctx_t *m, uint32_t handle, const uint8_t *voice, uint32_t cbo, int ch);

/* Ganancias de la voz según sus volúmenes y mixbins: g[canal][0] va a la izquierda y
 * g[canal][1] a la derecha. El slot b de los 8 lleva el canal b % canales.
 * Las voces 3D (handle < MIX_3D_VOICES) mandan sus slots 0-3 a los submixes de
 * posicionamiento (HRTF) y no a CFG_VBIN: DirectSound pone ahí el panorama del sonido
 * (atenuación por oído). Sin HRTF emulado se toman como frontal izq./der. y trasero
 * izq./der. */
#define MIX_3D_VOICES 64
void mix_voice_gains(const uint8_t *voice, int is3d, float g[2][2]);

/* 1 si la voz es una de las que DirectSound alimenta con el DSP (bucle de menos de 256
 * muestras): sin DSP emulado sería ruido, así que no se oye. */
int mix_voice_is_dsp_feed(const uint8_t *voice);

/* Avanza `frames` muestras de salida a 48 kHz: igual que vp_advance (posición, bucle y fin
 * de voz) y, además, suma cada voz activa en `out` (estéreo entrelazado, sin limpiar). */
void mix_render(vp_state_t *s, mix_ctx_t *m, float *out, uint32_t frames);

#endif
