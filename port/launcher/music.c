/*
 * music.c: ADX decoder and the launcher's title-theme player. See music.h.
 *
 * Header layout (big endian; src/ngb_tools/audio/adx.py, cross-checked there against vgmstream and
 * FFmpeg): 0x00 u16 0x8000, 0x02 u16 copyright offset (audio at offset + 4, preceded by "(c)CRI"),
 * 0x04 encoding (3), 0x05 block size (18), 0x06 bits (4), 0x07 channels, 0x08 u32 rate,
 * 0x0C u32 samples, 0x10 u16 highpass cutoff, 0x12 version, 0x13 flags (0x08/0x09 encrypted).
 * Loop block {u16, u16, u32 flag, u32 start, u32 start offset, u32 end, u32 end offset} at 0x14 in
 * version 3, after the history (and an optional AINF block) in version 4.
 *
 * Player: a worker thread feeds three waveOut buffers (CALLBACK_EVENT), wraps from the loop end
 * to the loop start with the predictor history saved when the loop start was first decoded,
 * and ramps the volume per sample. While silent with a target of 0 the device is paused.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include "music.h"
#include "gamedata.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME 18
#define SPF 32 /* samples per frame */
#define NBUF 3
#define BUF_SAMPLES 8192 /* per channel */

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }

static int read_loop(const uint8_t *p, size_t n, size_t pos, adx_info_t *h) {
    if (pos + 24 > n || !be32(p + pos + 4))
        return 0;
    h->loop_start = be32(p + pos + 8);
    h->loop_end = be32(p + pos + 16);
    return h->loop_end > h->loop_start && h->loop_end <= h->samples;
}

int adx_parse(const uint8_t *p, size_t n, adx_info_t *h) {
    size_t data, room;
    uint32_t cutoff;
    double a, b, c;
    memset(h, 0, sizeof *h);
    if (n < 0x20 || be16(p) != 0x8000)
        return 0;
    data = be16(p + 2) + 4u;
    if (data < 0x14 + 6 || data > n || memcmp(p + data - 6, "(c)CRI", 6))
        return 0;
    if (p[4] != 3 || p[5] != FRAME || p[6] != 4 || p[0x13] == 0x08 || p[0x13] == 0x09)
        return 0;
    h->channels = p[7];
    h->rate = (int)be32(p + 8);
    h->samples = be32(p + 0x0C);
    cutoff = be16(p + 0x10);
    h->version = p[0x12];
    if (h->channels < 1 || h->channels > 8 || h->rate <= 0 || h->rate > 192000 || h->version < 3 ||
        h->version > 5 || cutoff * 2u >= (uint32_t)h->rate || h->samples == 0 ||
        data + (size_t)FRAME * (size_t)h->channels > n) /* at least one complete frame */
        return 0;
    h->data_offset = data;
    room = data - 6;
    if (h->version == 3 && room >= 0x14 + 24) {
        h->has_loop = read_loop(p, n, 0x14, h);
    } else if (h->version == 4) {
        size_t hist_end = 0x18 + (h->channels > 1 ? 4u * (size_t)h->channels : 8u), ainf = hist_end + 4;
        size_t ainf_size = ainf + 8 <= n && !memcmp(p + ainf, "AINF", 4) ? be32(p + ainf + 4) : 0;
        if (room >= ainf_size && room - ainf_size >= hist_end + 24)
            h->has_loop = read_loop(p, n, hist_end, h);
    }
    a = sqrt(2.0) - cos(2.0 * 3.14159265358979323846 * cutoff / h->rate);
    b = sqrt(2.0) - 1.0;
    c = (a - sqrt((a + b) * (a - b))) / b;
    h->c1 = (int)(c * 2.0 * 4096.0);
    h->c2 = (int)(-(c * c) * 4096.0);
    return 1;
}

/* Arithmetic shift right by 12 (floor division), well defined for negative values. */
static int asr12(int v) { return v >= 0 ? v >> 12 : -((-v + 4095) >> 12); }

uint32_t adx_decode(const adx_info_t *h, const uint8_t *p, size_t n, uint32_t frame0, uint32_t nframes,
                    int16_t *out, adx_state_t *st) {
    uint32_t total = (h->samples + SPF - 1) / SPF, f, done = 0;
    int ch, i;
    for (f = frame0; f < frame0 + nframes && f < total; f++, done++) {
        for (ch = 0; ch < h->channels; ch++) {
            size_t pos = h->data_offset + ((size_t)f * h->channels + ch) * FRAME;
            int scale, h1 = st->h1[ch], h2 = st->h2[ch];
            int16_t *o = out + (size_t)done * SPF * h->channels + ch;
            if (pos + FRAME > n)
                return done;
            scale = (int)be16(p + pos) + 1;
            for (i = 0; i < SPF; i++) {
                int byte = p[pos + 2 + i / 2], nib = i & 1 ? byte & 15 : byte >> 4, s;
                if (nib >= 8)
                    nib -= 16;
                if (h->version == 3)
                    s = nib * scale + asr12(h->c1 * h1) + asr12(h->c2 * h2);
                else
                    s = nib * scale + asr12(h->c1 * h1 + h->c2 * h2);
                s = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
                o[(size_t)i * h->channels] = (int16_t)s;
                h2 = h1;
                h1 = s;
            }
            st->h1[ch] = h1;
            st->h2[ch] = h2;
        }
    }
    return done;
}

/* --------------------------------------------------------------------- player ---- */

typedef struct player {
    wchar_t dir[MAX_PATH];
    uint8_t *data;
    size_t len;
    adx_info_t h;
    HANDLE thread, event;
    volatile LONG quit;
    CRITICAL_SECTION lock;
    float vol, target, step; /* step per output sample */
} player_t;

static player_t *g_p;
static volatile LONG g_written;

static DWORD WINAPI player_thread(LPVOID arg) {
    player_t *pl = (player_t *)arg;
    wchar_t afs[MAX_PATH];
    HWAVEOUT wo = NULL;
    WAVEFORMATEX wf;
    WAVEHDR hdr[NBUF];
    int16_t *buf[NBUF], frame[SPF * 8];
    adx_state_t st, loop_st;
    uint32_t frm = 0, loop_frame = 0, end_sample;
    int have_loop_st = 0, fpos = SPF, i, paused = 0;
    memset(&loop_st, 0, sizeof loop_st);
    memset(hdr, 0, sizeof hdr);
    memset(buf, 0, sizeof buf);
    memset(&st, 0, sizeof st);
    _snwprintf(afs, MAX_PATH, L"%ls\\bgm.afs", pl->dir);
    afs[MAX_PATH - 1] = 0;
    pl->data = gamedata_load(afs, "bgm_title.adx", &pl->len);
    if (!pl->data)
        pl->data = gamedata_load(afs, "bgm_startmenu.adx", &pl->len);
    if (!pl->data || !adx_parse(pl->data, pl->len, &pl->h) || pl->h.channels > 8)
        return 0;
    if (!pl->h.has_loop) {
        pl->h.loop_start = 0;
        pl->h.loop_end = pl->h.samples;
    }
    end_sample = pl->h.loop_end;
    loop_frame = pl->h.loop_start / SPF;
    memset(&wf, 0, sizeof wf);
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = (WORD)pl->h.channels;
    wf.nSamplesPerSec = (DWORD)pl->h.rate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = (WORD)(2 * pl->h.channels);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
    if (waveOutOpen(&wo, WAVE_MAPPER, &wf, (DWORD_PTR)pl->event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR)
        return 0;
    for (i = 0; i < NBUF; i++) {
        buf[i] = (int16_t *)malloc(sizeof(int16_t) * BUF_SAMPLES * pl->h.channels);
        if (!buf[i])
            goto out;
        hdr[i].lpData = (LPSTR)buf[i];
        hdr[i].dwBufferLength = (DWORD)(sizeof(int16_t) * BUF_SAMPLES * pl->h.channels);
        if (waveOutPrepareHeader(wo, &hdr[i], sizeof hdr[i]) != MMSYSERR_NOERROR)
            goto out;
        hdr[i].dwFlags |= WHDR_DONE; /* free to fill: preparing clears the other flags */
    }
    while (!pl->quit) {
        int filled = 0;
        for (i = 0; i < NBUF && !pl->quit; i++) {
            uint32_t s;
            int16_t *o = buf[i];
            if (!(hdr[i].dwFlags & WHDR_DONE))
                continue;
            for (s = 0; s < BUF_SAMPLES; s++) {
                int c;
                float v;
                /* `frm` is the frame after the one in `frame`; fpos indexes into it. */
                if (fpos >= SPF || (frm - 1) * SPF + (uint32_t)fpos >= end_sample) {
                    if (fpos >= SPF && frm * SPF < end_sample) {
                        if (frm == loop_frame && !have_loop_st) {
                            loop_st = st;
                            have_loop_st = 1;
                        }
                        if (adx_decode(&pl->h, pl->data, pl->len, frm, 1, frame, &st) == 1) {
                            frm++;
                            fpos = 0;
                        } else {
                            end_sample = frm * SPF; /* truncated data: loop from here on */
                        }
                    }
                    if (fpos >= SPF || (frm - 1) * SPF + (uint32_t)fpos >= end_sample) {
                        if (!have_loop_st) { /* loop start never reached: restart cleanly */
                            memset(&loop_st, 0, sizeof loop_st);
                            loop_frame = 0;
                        }
                        st = loop_st;
                        frm = loop_frame;
                        if (adx_decode(&pl->h, pl->data, pl->len, frm, 1, frame, &st) != 1)
                            goto out; /* nothing decodable: stop rather than play garbage */
                        frm++;
                        fpos = have_loop_st ? (int)(pl->h.loop_start % SPF) : 0;
                    }
                }
                EnterCriticalSection(&pl->lock);
                if (pl->vol != pl->target) {
                    pl->vol += pl->step;
                    if ((pl->step > 0 && pl->vol > pl->target) || (pl->step < 0 && pl->vol < pl->target))
                        pl->vol = pl->target;
                }
                v = pl->vol;
                LeaveCriticalSection(&pl->lock);
                v = v * v; /* perceptual curve */
                for (c = 0; c < pl->h.channels; c++)
                    o[(size_t)s * pl->h.channels + c] = (int16_t)((float)frame[fpos * pl->h.channels + c] * v);
                fpos++;
            }
            hdr[i].dwFlags &= ~WHDR_DONE;
            waveOutWrite(wo, &hdr[i], sizeof hdr[i]);
            InterlockedIncrement(&g_written);
            filled = 1;
        }
        EnterCriticalSection(&pl->lock);
        if (pl->vol <= 0.0f && pl->target <= 0.0f && !paused) {
            waveOutPause(wo);
            paused = 1;
        } else if (pl->target > 0.0f && paused) {
            waveOutRestart(wo);
            paused = 0;
        }
        LeaveCriticalSection(&pl->lock);
        if (!filled)
            WaitForSingleObject(pl->event, paused ? 50 : 200);
    }
out:
    waveOutReset(wo);
    for (i = 0; i < NBUF; i++) {
        if (hdr[i].dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(wo, &hdr[i], sizeof hdr[i]);
        free(buf[i]);
    }
    waveOutClose(wo);
    return 0;
}

void music_stop(void) {
    player_t *pl = g_p;
    if (!pl)
        return;
    g_p = NULL;
    InterlockedExchange(&pl->quit, 1);
    SetEvent(pl->event);
    if (pl->thread) {
        WaitForSingleObject(pl->thread, INFINITE);
        CloseHandle(pl->thread);
    }
    CloseHandle(pl->event);
    DeleteCriticalSection(&pl->lock);
    free(pl->data);
    free(pl);
}

void music_start(const wchar_t *game_dir) {
    player_t *pl;
    music_stop();
    InterlockedExchange(&g_written, 0);
    pl = (player_t *)calloc(1, sizeof *pl);
    if (!pl)
        return;
    _snwprintf(pl->dir, MAX_PATH, L"%ls", game_dir);
    pl->dir[MAX_PATH - 1] = 0;
    InitializeCriticalSection(&pl->lock);
    pl->event = CreateEventW(NULL, FALSE, FALSE, NULL);
    pl->thread = pl->event ? CreateThread(NULL, 0, player_thread, pl, 0, NULL) : NULL;
    g_p = pl;
    if (!pl->thread)
        music_stop();
}

void music_fade_to(float volume, int ms) {
    player_t *pl = g_p;
    float samples;
    if (!pl)
        return;
    volume = volume < 0.0f ? 0.0f : volume > 1.0f ? 1.0f : volume;
    EnterCriticalSection(&pl->lock);
    samples = (float)(ms > 0 ? ms : 1) * (float)(pl->h.rate > 0 ? pl->h.rate : 44100) / 1000.0f;
    pl->target = volume;
    pl->step = (volume - pl->vol) / samples;
    if (pl->step == 0.0f)
        pl->vol = volume;
    LeaveCriticalSection(&pl->lock);
    SetEvent(pl->event);
}

int music_playing(void) { return g_p != NULL; }

unsigned music_buffers_written(void) { return (unsigned)g_written; }
