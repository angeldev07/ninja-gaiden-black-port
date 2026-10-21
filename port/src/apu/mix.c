/*
 * mix.c: lectura de muestras de las voces de la APU y mezcla a estéreo. Ver mix.h.
 */
#include "apu/mix.h"

#include <math.h>
#include <string.h>

static const int16_t k_ima_step[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t k_ima_index[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

static uint32_t rd32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

void mix_adpcm_decode(const uint8_t *block, int channels, int16_t out[2][MIX_ADPCM_SAMPLES + 1]) {
    int c, i;
    for (c = 0; c < channels; c++) {
        const uint8_t *h = block + c * 4;
        int pred = (int16_t)(h[0] | h[1] << 8), index = h[2] > 88 ? 88 : h[2], n = 1;
        out[c][0] = (int16_t)pred;
        /* Datos: grupos de 4 bytes (8 nibbles) por canal, entrelazados, nibble bajo primero. */
        for (i = 0; i < 8; i++) {
            const uint8_t *g = block + channels * 4 + (i * channels + c) * 4;
            int k;
            for (k = 0; k < 8; k++) {
                int nib = (g[k / 2] >> ((k & 1) * 4)) & 0xF, step = k_ima_step[index];
                int diff = step >> 3;
                if (nib & 1) diff += step >> 2;
                if (nib & 2) diff += step >> 1;
                if (nib & 4) diff += step;
                pred += (nib & 8) ? -diff : diff;
                if (pred > 32767) pred = 32767;
                if (pred < -32768) pred = -32768;
                index += k_ima_index[nib];
                if (index < 0) index = 0;
                if (index > 88) index = 88;
                out[c][n++] = (int16_t)pred;
            }
        }
    }
}

/* Dirección lineal del búfer de la voz -> memoria del host, por la tabla SGE (páginas de 4 KB). */
static const uint8_t *sge_ptr(mix_ctx_t *m, uint32_t linear, uint32_t len) {
    const uint8_t *entry;
    uint32_t page;
    if (((linear & 0xFFFu) + len) > 0x1000u) /* no cruza página: el llamador lee por partes */
        return NULL;
    entry = m->phys(m->sge_phys + (linear >> 12) * 8u, 4);
    if (!entry)
        return NULL;
    page = rd32(entry) & ~0xFFFu;
    return m->phys(page + (linear & 0xFFFu), len);
}

static float pcm_value(const uint8_t *p, uint32_t sample_size) {
    switch (sample_size) {
    case 0: return ((int)p[0] - 128) / 128.0f;
    case 1: return (int16_t)(p[0] | p[1] << 8) / 32768.0f;
    case 2: return (float)((int32_t)(rd32(p) << 8) >> 8) / 8388608.0f;
    default: return (float)(int32_t)rd32(p) / 2147483648.0f;
    }
}

float mix_voice_sample(mix_ctx_t *m, uint32_t handle, const uint8_t *voice, uint32_t cbo, int ch) {
    static const uint32_t container_bytes[4] = {1, 2, 0, 4};
    uint32_t fmt = rd32(voice + VP_VOICE_CFG_FMT), ba = rd32(voice + VP_VOICE_CUR_PSL_START) & VP_OFFSET_MASK;
    uint32_t container = fmt >> 30, sample_size = (fmt >> 28) & 3u;
    int channels = (fmt & (1u << 27)) ? 2 : 1;
    if (fmt & (1u << 24)) /* voz de stream (SSL): todavía no */
        return 0.0f;
    if (ch >= channels)
        ch = channels - 1;
    if (container == 2) { /* ADPCM: bloques de 36 bytes por canal y 64 muestras */
        mix_voice_cache_t *c = &m->cache[handle % VP_MAX_VOICES];
        uint32_t block = cbo / MIX_ADPCM_SAMPLES, bytes = MIX_ADPCM_BLOCK * (uint32_t)channels;
        if (c->block != block + 1 || c->ba != ba) {
            uint8_t raw[MIX_ADPCM_BLOCK * 2];
            uint32_t i, linear = ba + block * bytes;
            for (i = 0; i < bytes; i += 4) { /* palabra a palabra: puede cruzar de página */
                const uint8_t *p = sge_ptr(m, linear + i, 4);
                if (!p)
                    return 0.0f;
                memcpy(raw + i, p, 4);
            }
            mix_adpcm_decode(raw, channels, c->pcm);
            c->block = block + 1;
            c->ba = ba;
        }
        return c->pcm[ch][cbo % MIX_ADPCM_SAMPLES] / 32768.0f;
    } else {
        uint32_t csize = container_bytes[container];
        uint32_t spb = 1u + ((fmt >> 16) & 0x1Fu); /* muestras por bloque: canales del frame */
        const uint8_t *p = sge_ptr(m, ba + cbo * csize * spb + (uint32_t)ch * csize, csize);
        return p ? pcm_value(p, sample_size) : 0.0f;
    }
}

static float attenuation(uint32_t vol) {
    return vol >= 0xFFFu ? 0.0f : powf(10.0f, (float)vol / -1280.0f); /* 1/64 dB por paso */
}

void mix_voice_gains(const uint8_t *voice, int is3d, float g[2][2]) {
    static const uint32_t k_submix3d[4] = {0, 1, 4, 5}; /* FL, FR, BL, BR */
    uint32_t vbin = rd32(voice + 0x00), fmt = rd32(voice + VP_VOICE_CFG_FMT);
    uint32_t va = rd32(voice + 0x60), vb = rd32(voice + 0x64), vc = rd32(voice + 0x68);
    uint32_t vol[8], bin[8];
    int b, channels = (fmt & (1u << 27)) ? 2 : 1;
    vol[0] = (va >> 4) & 0xFFF;
    vol[1] = va >> 20;
    vol[2] = (vb >> 4) & 0xFFF;
    vol[3] = vb >> 20;
    vol[4] = (vc >> 4) & 0xFFF;
    vol[5] = vc >> 20;
    vol[6] = (vc & 0xF) << 8 | (vb & 0xF) << 4 | (va & 0xF);
    vol[7] = ((vc >> 16) & 0xF) << 8 | ((vb >> 16) & 0xF) << 4 | ((va >> 16) & 0xF);
    bin[0] = vbin & 0x1F;
    bin[1] = (vbin >> 5) & 0x1F;
    bin[2] = (vbin >> 10) & 0x1F;
    bin[3] = (vbin >> 16) & 0x1F;
    bin[4] = (vbin >> 21) & 0x1F;
    bin[5] = (vbin >> 26) & 0x1F;
    bin[6] = fmt & 0x1F;
    bin[7] = (fmt >> 5) & 0x1F;
    if (is3d)
        for (b = 0; b < 4; b++)
            bin[b] = k_submix3d[b];
    memset(g, 0, sizeof(float) * 4);
    for (b = 0; b < 8; b++) {
        float a = attenuation(vol[b]);
        float *ch = g[b % channels];
        switch (bin[b]) {
        case 0: ch[0] += a; break;                                /* frontal izquierdo */
        case 1: ch[1] += a; break;                                /* frontal derecho */
        case 2: ch[0] += 0.707f * a; ch[1] += 0.707f * a; break;  /* central */
        case 4: ch[0] += 0.5f * a; break;                         /* trasero izquierdo */
        case 5: ch[1] += 0.5f * a; break;                         /* trasero derecho */
        default: break;                                           /* LFE y efectos */
        }
    }
}

int mix_voice_is_dsp_feed(const uint8_t *voice) {
    /* DirectSound arranca voces propias en bucle sobre 32 muestras que en la Xbox rellena el
       DSP (GP) con la mezcla de efectos. Sin DSP emulado esas muestras son basura a volumen
       máximo en todos los mixbins: un ruido constante. Ninguna voz de sonido real tiene un
       bucle tan corto (en NGB, las voces 64-67 con EBO 0x1F). */
    uint32_t fmt = rd32(voice + VP_VOICE_CFG_FMT);
    uint32_t ebo = rd32(voice + VP_VOICE_PAR_NEXT) & VP_OFFSET_MASK;
    return (fmt & VP_FMT_LOOP) && ebo < 256;
}

void mix_render(vp_state_t *s, mix_ctx_t *m, float *out, uint32_t frames) {
    uint32_t h;
    if (!s->voices || !frames)
        return;
    for (h = 0; h < VP_MAX_VOICES; h++) {
        uint8_t *v = vp_voice_begin(s, h);
        double rate, pos;
        float g[2][2];
        uint32_t f, cbo;
        int stereo, audible;
        if (!v)
            continue;
        rate = vp_voice_rate(v) / 48000.0;
        mix_voice_gains(v, h < MIX_3D_VOICES, g);
        audible = !mix_voice_is_dsp_feed(v) && /* avanza, pero no se oye */
                  (g[0][0] > 0.0f || g[0][1] > 0.0f || g[1][0] > 0.0f || g[1][1] > 0.0f);
        stereo = (rd32(v + VP_VOICE_CFG_FMT) & (1u << 27)) != 0;
        pos = s->frac[h];
        for (f = 0; f < frames; f++) {
            uint32_t step;
            cbo = rd32(v + VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK;
            if (audible) {
                float c0 = mix_voice_sample(m, h, v, cbo, 0);
                float c1 = stereo ? mix_voice_sample(m, h, v, cbo, 1) : 0.0f;
                out[f * 2] += c0 * g[0][0] + c1 * g[1][0];
                out[f * 2 + 1] += c0 * g[0][1] + c1 * g[1][1];
            }
            pos += rate;
            step = (uint32_t)pos;
            pos -= step;
            if (step && !vp_voice_move(s, h, v, step))
                break; /* la voz terminó */
        }
        s->frac[h] = pos;
    }
}
