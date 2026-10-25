/*
 * vp_test.c: APU voice processor (apu/vp.c) with a synthetic voice table.
 */
#include <stdio.h>
#include <string.h>

#include "apu/mix.h"
#include "apu/vp.h"

static int g_fail, g_checks;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        g_checks++;                                                                                \
        if (!(cond)) {                                                                             \
            printf("FAIL line %d: ", __LINE__);                                                    \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

static uint8_t g_voices[VP_MAX_VOICES * VP_VOICE_SIZE];

static uint32_t w(uint32_t handle, uint32_t offset) {
    uint32_t v;
    memcpy(&v, g_voices + handle * VP_VOICE_SIZE + offset, 4);
    return v;
}

static uint32_t w2(const uint8_t *voice, uint32_t offset) {
    uint32_t v;
    memcpy(&v, voice + offset, 4);
    return v;
}

/* --- Mixer (apu/mix.c) ----------------------------------------------------------------- */

/* "Physical RAM" of the test: 3 pages. Page 0: SGE table; pages 1 and 2: data. */
static uint8_t g_ram[3 * 0x1000];

static const uint8_t *test_phys(uint32_t phys, uint32_t len) {
    return phys + len <= sizeof g_ram ? g_ram + phys : NULL;
}

static void put32(uint8_t *p, uint32_t v) {
    memcpy(p, &v, 4);
}

static void test_mix(void) {
    static mix_ctx_t m;
    static uint8_t voices[VP_MAX_VOICES * VP_VOICE_SIZE];
    uint8_t block[MIX_ADPCM_BLOCK];
    int16_t pcm[2][MIX_ADPCM_SAMPLES + 1];
    uint8_t *v = voices + 3 * VP_VOICE_SIZE;
    vp_state_t s;
    float out[8] = {0}, g[2][2];
    int i;

    /* Hand-made IMA ADPCM: predictor 1000, index 0. Nibbles 0: diff = 7>>3 = 0, all 1000.
       First nibble 7: diff = 0 + 1 + 3 + 7 = 11 -> 1011, and the index goes up by 8. */
    memset(block, 0, sizeof block);
    block[0] = 1000 & 0xFF;
    block[1] = 1000 >> 8;
    mix_adpcm_decode(block, 1, pcm);
    CHECK(pcm[0][0] == 1000 && pcm[0][64] == 1000, "ADPCM with nibbles 0 (%d, %d)", pcm[0][0],
          pcm[0][64]);
    block[4] = 0x07;
    mix_adpcm_decode(block, 1, pcm);
    CHECK(pcm[0][1] == 1011, "ADPCM nibble 7 (%d)", pcm[0][1]);
    /* Second nibble 0 with index 8 (step 16): diff = 16>>3 = 2 -> 1013. */
    CHECK(pcm[0][2] == 1013, "ADPCM updated index (%d)", pcm[0][2]);

    /* Stereo S16 PCM voice: BA = 0x1FFC, so sample 1 falls in linear page 2.
       SGE: linear page 1 -> physical 0x1000, linear page 2 -> physical 0x2000. */
    memset(g_ram, 0, sizeof g_ram);
    put32(g_ram + 1 * 8, 0x1000);
    put32(g_ram + 2 * 8, 0x2000);
    g_ram[0x1FFD] = 0x40; /* L =  0x4000 =  0.5 */
    g_ram[0x1FFF] = 0xC0; /* R = -0x4000 = -0.5 */
    g_ram[0x2001] = 0x20; /* L =  0.25 */
    g_ram[0x2003] = 0x20; /* R =  0.25 */
    m.phys = test_phys;
    m.sge_phys = 0;
    memset(voices, 0, sizeof voices);
    /* S16 (1 << 28), B16 container (1 << 30), stereo (1 << 27), 2 samples per block. */
    put32(v + VP_VOICE_CFG_FMT, (1u << 30) | (1u << 28) | (1u << 27) | (1u << 16));
    put32(v + VP_VOICE_CUR_PSL_START, 0x1FFC);
    put32(v + VP_VOICE_PAR_NEXT, 1);
    CHECK(mix_voice_sample(&m, 3, v, 0, 0) == 0.5f, "PCM L");
    CHECK(mix_voice_sample(&m, 3, v, 0, 1) == -0.5f, "PCM R");
    CHECK(mix_voice_sample(&m, 3, v, 1, 0) == 0.25f, "PCM through the SGE on another page");

    /* Volumes: slot 0 to mixbin 0 (left) unattenuated and slot 1 to mixbin 1 (right)
       at -6 dB (384 steps of 1/64 dB). The rest go to mixbin 31 (not heard). */
    put32(v + 0x00, (0u << 0) | (1u << 5) | (31u << 10) | (31u << 16) | (31u << 21) | (31u << 26));
    put32(v + VP_VOICE_CFG_FMT, w2(v, VP_VOICE_CFG_FMT) | 31u | (31u << 5));
    put32(v + 0x60, (384u << 20) | (0u << 4));
    put32(v + 0x64, 0xFFFFFFFFu);
    put32(v + 0x68, 0xFFFFFFFFu);
    mix_voice_gains(v, 0, g);
    CHECK(g[0][0] > 0.999f && g[0][0] < 1.001f, "channel 0 to the left, 1 (%f)", g[0][0]);
    CHECK(g[1][1] > 0.49f && g[1][1] < 0.51f, "channel 1 to the right, -6 dB (%f)", g[1][1]);
    CHECK(g[0][1] == 0.0f && g[1][0] == 0.0f, "no channel crossover");
    /* Mono 3D voice: slot 0 (-6 dB) goes to the left even though CFG_VBIN says mixbin 31,
       and slot 2 (0 dB) to the rear left (x0.5). The rest is silent. */
    put32(v + VP_VOICE_CFG_FMT, (1u << 30) | (1u << 28) | 31u | (31u << 5));
    put32(v + 0x00, 31u | (31u << 5) | (31u << 10) | (31u << 16) | (31u << 21) | (31u << 26));
    put32(v + 0x60, (0xFFFu << 20) | (384u << 4));
    put32(v + 0x64, (0xFFFu << 20) | (0u << 4));
    put32(v + 0x68, 0xFFFFFFFFu);
    mix_voice_gains(v, 1, g);
    CHECK(g[0][0] > 0.99f && g[0][0] < 1.01f, "3D: -6 dB + 0.5 to the left (%f)", g[0][0]);
    CHECK(g[0][1] == 0.0f, "3D: nothing to the right (%f)", g[0][1]);
    put32(v + VP_VOICE_CFG_FMT, (1u << 30) | (1u << 28) | (1u << 27) | (1u << 16) | 31u | (31u << 5));
    put32(v + 0x00, (0u << 0) | (1u << 5) | (31u << 10) | (31u << 16) | (31u << 21) | (31u << 26));
    put32(v + 0x60, (384u << 20) | (0u << 4));
    put32(v + 0x64, 0xFFFFFFFFu);

    /* mix_render: 2 output samples at 48 kHz run through the whole voice and switch it off. */
    vp_init(&s);
    s.voices = voices;
    s.active[3] = 1;
    put32(v + VP_VOICE_PAR_STATE, VP_STATE_ACTIVE | VP_STATE_NEW_VOICE);
    mix_render(&s, &m, out, 4);
    CHECK(out[0] > 0.499f && out[0] < 0.501f, "render L0 (%f)", out[0]);
    CHECK(out[1] < -0.24f && out[1] > -0.26f, "render R0 (%f)", out[1]);
    CHECK(out[2] > 0.249f && out[2] < 0.251f, "render L1 (%f)", out[2]);
    CHECK(!(w2(v, VP_VOICE_PAR_STATE) & VP_STATE_ACTIVE), "the voice ends when it passes EBO");
    for (i = 4; i < 8; i++)
        CHECK(out[i] == 0.0f, "silence after the end (%d)", i);
}

int main(void) {
    vp_state_t s;
    vp_init(&s);
    s.voices = g_voices;

    /* Voice 5 looping over [100, 47999], pitch 0 (48 kHz). Configured through PIO. */
    vp_method(&s, VP_PIO_SET_CURRENT_VOICE, 5);
    vp_method(&s, VP_PIO_SET_VOICE_CFG_VBIN + 4, VP_FMT_LOOP); /* CFG_FMT */
    vp_method(&s, VP_PIO_SET_VOICE_CFG_BUF_LBO, 100);
    vp_method(&s, VP_PIO_SET_VOICE_CFG_BUF_EBO, 47999);
    vp_method(&s, VP_PIO_SET_VOICE_BUF_CBO, 0);
    vp_method(&s, VP_PIO_SET_VOICE_TAR_PITCH, 0x00000000u);
    CHECK(w(5, VP_VOICE_CFG_FMT) == VP_FMT_LOOP, "CFG_FMT through PIO");
    CHECK((w(5, VP_VOICE_PAR_NEXT) & VP_OFFSET_MASK) == 47999, "EBO through PIO");
    vp_method(&s, VP_PIO_VOICE_ON, 5);
    CHECK(w(5, VP_VOICE_PAR_STATE) & VP_STATE_NEW_VOICE, "VOICE_ON marks the voice as new");
    CHECK(w(5, VP_VOICE_PAR_STATE) & VP_STATE_ACTIVE, "VOICE_ON activates it");

    vp_advance(&s, 0.5);
    CHECK(!(w(5, VP_VOICE_PAR_STATE) & VP_STATE_NEW_VOICE), "the VP clears NEW_VOICE");
    CHECK((w(5, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK) == 24000, "0.5 s at 48 kHz = 24000 (%u)",
          w(5, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK);
    vp_advance(&s, 0.5); /* 48000 > EBO: back to LBO */
    CHECK((w(5, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK) == 100, "loop to LBO (%u)",
          w(5, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK);

    /* Pause: it does not advance. */
    vp_method(&s, VP_PIO_VOICE_PAUSE, 5 | VP_STATE_PAUSED);
    vp_advance(&s, 0.25);
    CHECK((w(5, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK) == 100, "paused, it does not advance");
    vp_method(&s, VP_PIO_VOICE_PAUSE, 5);
    vp_advance(&s, 100.0 / 48000.0); /* 100 samples */
    CHECK((w(5, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK) == 200, "resumed (%u)",
          w(5, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK);

    /* Pitch +4096 = one octave up: 96 kHz. The low 16 bits (link) are preserved. */
    memcpy(g_voices + 9 * VP_VOICE_SIZE + VP_VOICE_TAR_PITCH_LINK, "\x34\x12\x00\x00", 4);
    vp_method(&s, VP_PIO_SET_CURRENT_VOICE, 9);
    vp_method(&s, VP_PIO_SET_VOICE_TAR_PITCH, 0x10000000u);
    vp_method(&s, VP_PIO_SET_VOICE_TAR_VOLA + 4, 0x12345678u);
    CHECK(w(9, 0x64) == 0x12345678u, "TAR_VOLB through PIO");
    CHECK((w(9, VP_VOICE_TAR_PITCH_LINK) & 0xFFFF) == 0x1234, "the pitch does not overwrite the link");
    vp_method(&s, VP_PIO_SET_VOICE_CFG_BUF_EBO, 1000);
    vp_method(&s, VP_PIO_VOICE_ON, 9);
    vp_advance(&s, 0.01); /* 480 output samples = 960 of the voice */
    CHECK((w(9, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK) == 960, "pitch +1 octave (%u)",
          w(9, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK);

    /* Without LOOP, past EBO the voice switches itself off and the position stays at EBO. */
    vp_advance(&s, 0.01);
    CHECK((w(9, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK) == 1000, "stays at EBO");
    CHECK(!(w(9, VP_VOICE_PAR_STATE) & VP_STATE_ACTIVE), "voice ended");
    CHECK(s.voices_ended == 1, "ended-voices counter");

    /* VOICE_OFF and out-of-range handles. */
    vp_method(&s, VP_PIO_VOICE_OFF, 5);
    CHECK(!(w(5, VP_VOICE_PAR_STATE) & VP_STATE_ACTIVE), "VOICE_OFF");
    vp_method(&s, VP_PIO_VOICE_ON, 0xFFFF);
    vp_method(&s, VP_PIO_SET_CURRENT_VOICE, 0xFFFF);
    vp_method(&s, VP_PIO_SET_VOICE_BUF_CBO, 7); /* no current voice: ignored */
    vp_advance(&s, 1.0);
    CHECK(s.voices_on == 2, "only valid VOICE_ONs count (%u)", s.voices_on);

    /* Free voices to notify: 9 ended on its own and 5 got VOICE_OFF, in that order. */
    {
        uint32_t h = 0xFFFF;
        CHECK(vp_take_idle(&s, &h) && h == 9, "free voice: end of buffer (%u)", h);
        CHECK(vp_take_idle(&s, &h) && h == 5, "free voice: VOICE_OFF (%u)", h);
        CHECK(!vp_take_idle(&s, &h), "free-voice queue empty");
        vp_idle_delivered(&s, 9); /* the ISR returned for both */
        vp_idle_delivered(&s, 5);

        /* A voice that ends and gets VOICE_OFF before the notice is delivered is notified once:
           a second notice would reach DirectSound after it freed the voice. */
        vp_method(&s, VP_PIO_SET_CURRENT_VOICE, 9);
        vp_method(&s, VP_PIO_SET_VOICE_BUF_CBO, 0);
        vp_method(&s, VP_PIO_VOICE_ON, 9);
        vp_advance(&s, 0.1);
        vp_method(&s, VP_PIO_VOICE_OFF, 9);
        CHECK(vp_take_idle(&s, &h) && h == 9, "free voice: ended, then VOICE_OFF (%u)", h);
        CHECK(!vp_take_idle(&s, &h), "one notice for a voice already pending");
        vp_method(&s, VP_PIO_VOICE_OFF, 9); /* during the ISR: still pending */
        CHECK(!vp_take_idle(&s, &h), "no notice for a VOICE_OFF during the delivery");
        vp_idle_delivered(&s, 9);
        vp_method(&s, VP_PIO_VOICE_OFF, 9);
        CHECK(vp_take_idle(&s, &h) && h == 9, "VOICE_OFF after the delivery notifies again");
    }

    test_mix();
    if (g_fail)
        return 1;
    printf("vp_test: OK (%d checks)\n", g_checks);
    return 0;
}
