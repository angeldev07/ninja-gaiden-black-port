/*
 * music_test.c: the launcher's ADX decoder (launcher\music.c) against src/ngb_tools/audio/adx.py.
 *
 * The vector below is a synthetic one-channel version 3 ADX (64 samples, cutoff 500 Hz, a loop
 * over the whole stream) and the PCM the Python decoder gives for it, generated with:
 *   PYTHONPATH=src python -c "from ngb_tools.audio import adx; ..."  (see docs/log.md 2026-10-31)
 * No game data.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include <stdio.h>
#include <string.h>

#include "../launcher/music.h"

static int g_fail;
#define CHECK(c)                                                                         \
    do {                                                                                 \
        if (!(c)) {                                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                          \
            g_fail++;                                                                    \
        }                                                                                \
    } while (0)

static const uint8_t ADX[] = {
    0x80, 0x00, 0x00, 0x34, 0x03, 0x12, 0x04, 0x01, 0x00, 0x00, 0xac, 0x44, 0x00, 0x00, 0x00, 0x40,
    0x01, 0xf4, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x5c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x28, 0x63, 0x29, 0x43, 0x52, 0x49, 0x00, 0x01, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
    0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x00, 0x07, 0xf0, 0xe1, 0xd2, 0xc3,
    0xb4, 0xa5, 0x96, 0x87, 0x78, 0x69, 0x5a, 0x4b, 0x3c, 0x2d, 0x1e, 0x0f};
static const int16_t PCM[] = {
    0,    2,    7,    16,   30,   50,   76,   109,  118,  109,  88,   59,   26,   -8,   -40,  -68,
    -90,  -106, -114, -115, -107, -90,  -65,  -31,  -20,  -26,  -43,  -67,  -94,  -122, -148, -170,
    -195, -214, -244, -258, -291, -300, -337, -340, -379, -375, -417, -407, -451, -434, -481, -459,
    -381, -380, -328, -340, -307, -326, -306, -327, -317, -338, -336, -356, -361, -378, -388, -401};

static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

/* A one-entry AFS holding ADX as "bgm_title.adx", in a fresh temp folder; returns the folder. */
static void make_bgm_afs(wchar_t *dir) {
    static uint8_t afs[0x2000];
    wchar_t path[MAX_PATH];
    FILE *f;
    memset(afs, 0, sizeof afs);
    memcpy(afs, "AFS\0", 4);
    put32(afs + 4, 1);
    put32(afs + 8, 0x800);
    put32(afs + 12, (uint32_t)sizeof ADX);
    put32(afs + 16, 0x1000);
    put32(afs + 20, 0x30);
    memcpy(afs + 0x800, ADX, sizeof ADX);
    memcpy(afs + 0x1000, "bgm_title.adx", 13);
    GetTempPathW(MAX_PATH, dir);
    wcscat(dir, L"ngb_music_test");
    CreateDirectoryW(dir, NULL);
    _snwprintf(path, MAX_PATH, L"%ls\\bgm.afs", dir);
    f = _wfopen(path, L"wb");
    fwrite(afs, 1, 0x1030, f);
    fclose(f);
}

int main(void) {
    adx_info_t h;
    adx_state_t st;
    int16_t out[64];
    uint8_t bad[sizeof ADX];

    CHECK(adx_parse(ADX, sizeof ADX, &h));
    CHECK(h.channels == 1 && h.rate == 44100 && h.samples == 64 && h.version == 3);
    CHECK(h.c1 == 7334 && h.c2 == -3283);
    CHECK(h.has_loop && h.loop_start == 0 && h.loop_end == 64);
    CHECK(h.data_offset == 0x38);

    memset(&st, 0, sizeof st);
    CHECK(adx_decode(&h, ADX, sizeof ADX, 0, 2, out, &st) == 2);
    CHECK(!memcmp(out, PCM, sizeof PCM));
    /* Frame by frame with the carried history gives the same samples. */
    memset(&st, 0, sizeof st);
    CHECK(adx_decode(&h, ADX, sizeof ADX, 0, 1, out, &st) == 1);
    CHECK(adx_decode(&h, ADX, sizeof ADX, 1, 1, out + 32, &st) == 1);
    CHECK(!memcmp(out, PCM, sizeof PCM));
    /* Past the end: nothing. */
    CHECK(adx_decode(&h, ADX, sizeof ADX, 2, 1, out, &st) == 0);

    /* Damaged or unsupported headers. */
    CHECK(!adx_parse(ADX, 10, &h));
    memcpy(bad, ADX, sizeof ADX);
    bad[0x13] = 0x08; /* encrypted */
    CHECK(!adx_parse(bad, sizeof bad, &h));
    memcpy(bad, ADX, sizeof ADX);
    bad[0x32] = 'x'; /* no (c)CRI before the audio */
    CHECK(!adx_parse(bad, sizeof bad, &h));
    memcpy(bad, ADX, sizeof ADX);
    bad[0x07] = 0; /* no channels */
    CHECK(!adx_parse(bad, sizeof bad, &h));
    memcpy(bad, ADX, sizeof ADX);
    bad[0x03] = 0xF0; /* data offset past the buffer */
    CHECK(!adx_parse(bad, sizeof bad, &h));

    memcpy(bad, ADX, sizeof ADX);
    bad[0x0C] = bad[0x0D] = bad[0x0E] = bad[0x0F] = 0; /* no samples */
    CHECK(!adx_parse(bad, sizeof bad, &h));
    CHECK(!adx_parse(ADX, 0x38 + 10, &h)); /* header only, no complete frame */

    /* The player really hands buffers to waveOut (skipped on machines without an output). */
    if (waveOutGetNumDevs() > 0) {
        wchar_t dir[MAX_PATH];
        int i;
        make_bgm_afs(dir);
        music_start(dir);
        music_fade_to(0.0f, 1); /* silent: this is a test */
        for (i = 0; i < 100 && music_buffers_written() == 0; i++)
            Sleep(10);
        CHECK(music_buffers_written() > 0);
        music_stop();
    } else {
        printf("music_test: no audio output, player check skipped\n");
    }

    printf("music_test: %s\n", g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}
