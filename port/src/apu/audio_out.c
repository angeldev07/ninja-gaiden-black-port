/*
 * audio_out.c: salida de audio del port por waveOut (48 kHz, estéreo, 16 bits).
 *
 * El hilo de DPC mezcla en cada tick lo que corresponde al tiempo transcurrido
 * (crash.c, ngb_apu_tick) y lo entrega aquí. Hay un anillo de cabeceras WAVEHDR:
 *   - arranca en pausa y empieza a sonar con 80 ms en cola, para que un tick tardío del
 *     hilo de DPC no deje al dispositivo sin datos (un corte audible);
 *   - si se queda sin datos, vuelve a esperar a tener 80 ms;
 *   - si la cola pasa de 250 ms (el dispositivo va más lento que el reloj), el bloque se
 *     descarta para no acumular latencia.
 */
#include "apu/audio_out.h"

#include <mmsystem.h>
#include <string.h>

#define AO_HEADERS 64
#define AO_MAX_FRAMES 4800 /* 100 ms por bloque como máximo */
#define AO_MAX_QUEUED 12000 /* 250 ms en cola */
#define AO_PREBUFFER 3840  /* 80 ms antes de sonar */

static HWAVEOUT g_wo;
static WAVEHDR g_hdr[AO_HEADERS];
static int16_t g_buf[AO_HEADERS][AO_MAX_FRAMES * 2];
static int g_next;
static uint32_t g_dropped, g_underruns;
static int g_playing;

int audio_out_open(void) {
    WAVEFORMATEX f;
    int i;
    memset(&f, 0, sizeof f);
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = 2;
    f.nSamplesPerSec = 48000;
    f.wBitsPerSample = 16;
    f.nBlockAlign = 4;
    f.nAvgBytesPerSec = 48000 * 4;
    if (waveOutOpen(&g_wo, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        g_wo = NULL;
        return -1;
    }
    for (i = 0; i < AO_HEADERS; i++) {
        memset(&g_hdr[i], 0, sizeof g_hdr[i]);
        g_hdr[i].dwFlags = WHDR_DONE; /* libre */
    }
    waveOutPause(g_wo); /* hasta tener AO_PREBUFFER en cola */
    return 0;
}

static uint32_t queued_frames(void) {
    uint32_t n = 0;
    int i;
    for (i = 0; i < AO_HEADERS; i++)
        if (!(g_hdr[i].dwFlags & WHDR_DONE))
            n += g_hdr[i].dwBufferLength / 4;
    return n;
}

void audio_out_write(const float *stereo, uint32_t frames) {
    WAVEHDR *h;
    uint32_t i, queued;
    if (!g_wo || !frames)
        return;
    if (frames > AO_MAX_FRAMES)
        frames = AO_MAX_FRAMES;
    h = &g_hdr[g_next];
    queued = queued_frames();
    if (g_playing && queued == 0) { /* el dispositivo se quedó sin datos */
        waveOutPause(g_wo);
        g_playing = 0;
        g_underruns++;
    }
    if (!(h->dwFlags & WHDR_DONE) || queued + frames > AO_MAX_QUEUED) {
        g_dropped += frames;
        return;
    }
    if (h->dwFlags & WHDR_PREPARED)
        waveOutUnprepareHeader(g_wo, h, sizeof *h);
    for (i = 0; i < frames * 2; i++) {
        float v = stereo[i];
        v = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
        g_buf[g_next][i] = (int16_t)(v * 32767.0f);
    }
    memset(h, 0, sizeof *h);
    h->lpData = (LPSTR)g_buf[g_next];
    h->dwBufferLength = frames * 4;
    if (waveOutPrepareHeader(g_wo, h, sizeof *h) == MMSYSERR_NOERROR)
        waveOutWrite(g_wo, h, sizeof *h);
    else
        h->dwFlags = WHDR_DONE;
    g_next = (g_next + 1) % AO_HEADERS;
    if (!g_playing && queued + frames >= AO_PREBUFFER) {
        waveOutRestart(g_wo);
        g_playing = 1;
    }
}

uint32_t audio_out_dropped(void) {
    return g_dropped;
}

uint32_t audio_out_queued(void) {
    return g_wo ? queued_frames() : 0;
}

uint32_t audio_out_underruns(void) {
    return g_underruns;
}
