/*
 * vp_test.c: procesador de voces de la APU (apu/vp.c) con una tabla de voces sintética.
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
            printf("FALLO línea %d: ", __LINE__);                                                  \
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

/* --- Mezclador (apu/mix.c) ----------------------------------------------------------- */

/* "RAM física" de la prueba: 3 páginas. Página 0: tabla SGE; páginas 1 y 2: datos. */
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

    /* ADPCM IMA a mano: predictor 1000, índice 0. Nibbles 0: diff = 7>>3 = 0, todo 1000.
       Primer nibble 7: diff = 0 + 1 + 3 + 7 = 11 -> 1011, y el índice sube 8. */
    memset(block, 0, sizeof block);
    block[0] = 1000 & 0xFF;
    block[1] = 1000 >> 8;
    mix_adpcm_decode(block, 1, pcm);
    CHECK(pcm[0][0] == 1000 && pcm[0][64] == 1000, "ADPCM con nibbles 0 (%d, %d)", pcm[0][0],
          pcm[0][64]);
    block[4] = 0x07;
    mix_adpcm_decode(block, 1, pcm);
    CHECK(pcm[0][1] == 1011, "ADPCM nibble 7 (%d)", pcm[0][1]);
    /* Segundo nibble 0 con índice 8 (paso 16): diff = 16>>3 = 2 -> 1013. */
    CHECK(pcm[0][2] == 1013, "ADPCM índice actualizado (%d)", pcm[0][2]);

    /* Voz PCM S16 estéreo: BA = 0x1FFC, así que la muestra 1 cae en la página lineal 2.
       SGE: página lineal 1 -> física 0x1000, página lineal 2 -> física 0x2000. */
    memset(g_ram, 0, sizeof g_ram);
    put32(g_ram + 1 * 8, 0x1000);
    put32(g_ram + 2 * 8, 0x2000);
    g_ram[0x1FFD] = 0x40; /* L =  0x4000 =  0,5 */
    g_ram[0x1FFF] = 0xC0; /* R = -0x4000 = -0,5 */
    g_ram[0x2001] = 0x20; /* L =  0,25 */
    g_ram[0x2003] = 0x20; /* R =  0,25 */
    m.phys = test_phys;
    m.sge_phys = 0;
    memset(voices, 0, sizeof voices);
    /* S16 (1 << 28), contenedor B16 (1 << 30), estéreo (1 << 27), 2 muestras por bloque. */
    put32(v + VP_VOICE_CFG_FMT, (1u << 30) | (1u << 28) | (1u << 27) | (1u << 16));
    put32(v + VP_VOICE_CUR_PSL_START, 0x1FFC);
    put32(v + VP_VOICE_PAR_NEXT, 1);
    CHECK(mix_voice_sample(&m, 3, v, 0, 0) == 0.5f, "PCM L");
    CHECK(mix_voice_sample(&m, 3, v, 0, 1) == -0.5f, "PCM R");
    CHECK(mix_voice_sample(&m, 3, v, 1, 0) == 0.25f, "PCM por la SGE en otra página");

    /* Volúmenes: slot 0 al mixbin 0 (izquierdo) sin atenuar y slot 1 al mixbin 1 (derecho)
       a -6 dB (384 pasos de 1/64 dB). El resto, al mixbin 31 (no se oye). */
    put32(v + 0x00, (0u << 0) | (1u << 5) | (31u << 10) | (31u << 16) | (31u << 21) | (31u << 26));
    put32(v + VP_VOICE_CFG_FMT, w2(v, VP_VOICE_CFG_FMT) | 31u | (31u << 5));
    put32(v + 0x60, (384u << 20) | (0u << 4));
    put32(v + 0x64, 0xFFFFFFFFu);
    put32(v + 0x68, 0xFFFFFFFFu);
    mix_voice_gains(v, 0, g);
    CHECK(g[0][0] > 0.999f && g[0][0] < 1.001f, "canal 0 a la izquierda, 1 (%f)", g[0][0]);
    CHECK(g[1][1] > 0.49f && g[1][1] < 0.51f, "canal 1 a la derecha, -6 dB (%f)", g[1][1]);
    CHECK(g[0][1] == 0.0f && g[1][0] == 0.0f, "sin cruces de canal");
    /* Voz 3D mono: el slot 0 (-6 dB) va a la izquierda aunque CFG_VBIN diga el mixbin 31,
       y el slot 2 (0 dB) al trasero izquierdo (x0,5). El resto en silencio. */
    put32(v + VP_VOICE_CFG_FMT, (1u << 30) | (1u << 28) | 31u | (31u << 5));
    put32(v + 0x00, 31u | (31u << 5) | (31u << 10) | (31u << 16) | (31u << 21) | (31u << 26));
    put32(v + 0x60, (0xFFFu << 20) | (384u << 4));
    put32(v + 0x64, (0xFFFu << 20) | (0u << 4));
    put32(v + 0x68, 0xFFFFFFFFu);
    mix_voice_gains(v, 1, g);
    CHECK(g[0][0] > 0.99f && g[0][0] < 1.01f, "3D: -6 dB + 0,5 a la izquierda (%f)", g[0][0]);
    CHECK(g[0][1] == 0.0f, "3D: nada a la derecha (%f)", g[0][1]);
    put32(v + VP_VOICE_CFG_FMT, (1u << 30) | (1u << 28) | (1u << 27) | (1u << 16) | 31u | (31u << 5));
    put32(v + 0x00, (0u << 0) | (1u << 5) | (31u << 10) | (31u << 16) | (31u << 21) | (31u << 26));
    put32(v + 0x60, (384u << 20) | (0u << 4));
    put32(v + 0x64, 0xFFFFFFFFu);

    /* mix_render: 2 muestras de salida a 48 kHz recorren la voz entera y la apagan. */
    vp_init(&s);
    s.voices = voices;
    s.active[3] = 1;
    put32(v + VP_VOICE_PAR_STATE, VP_STATE_ACTIVE | VP_STATE_NEW_VOICE);
    mix_render(&s, &m, out, 4);
    CHECK(out[0] > 0.499f && out[0] < 0.501f, "render L0 (%f)", out[0]);
    CHECK(out[1] < -0.24f && out[1] > -0.26f, "render R0 (%f)", out[1]);
    CHECK(out[2] > 0.249f && out[2] < 0.251f, "render L1 (%f)", out[2]);
    CHECK(!(w2(v, VP_VOICE_PAR_STATE) & VP_STATE_ACTIVE), "la voz termina al pasar de EBO");
    for (i = 4; i < 8; i++)
        CHECK(out[i] == 0.0f, "tras el fin, silencio (%d)", i);
}

int main(void) {
    vp_state_t s;
    vp_init(&s);
    s.voices = g_voices;

    /* Voz 5 en bucle sobre [100, 47999], pitch 0 (48 kHz). Se configura por PIO. */
    vp_method(&s, VP_PIO_SET_CURRENT_VOICE, 5);
    vp_method(&s, VP_PIO_SET_VOICE_CFG_VBIN + 4, VP_FMT_LOOP); /* CFG_FMT */
    vp_method(&s, VP_PIO_SET_VOICE_CFG_BUF_LBO, 100);
    vp_method(&s, VP_PIO_SET_VOICE_CFG_BUF_EBO, 47999);
    vp_method(&s, VP_PIO_SET_VOICE_BUF_CBO, 0);
    vp_method(&s, VP_PIO_SET_VOICE_TAR_PITCH, 0x00000000u);
    CHECK(w(5, VP_VOICE_CFG_FMT) == VP_FMT_LOOP, "CFG_FMT por PIO");
    CHECK((w(5, VP_VOICE_PAR_NEXT) & VP_OFFSET_MASK) == 47999, "EBO por PIO");
    vp_method(&s, VP_PIO_VOICE_ON, 5);
    CHECK(w(5, VP_VOICE_PAR_STATE) & VP_STATE_NEW_VOICE, "VOICE_ON marca la voz como nueva");
    CHECK(w(5, VP_VOICE_PAR_STATE) & VP_STATE_ACTIVE, "VOICE_ON la activa");

    vp_advance(&s, 0.5);
    CHECK(!(w(5, VP_VOICE_PAR_STATE) & VP_STATE_NEW_VOICE), "el VP limpia NEW_VOICE");
    CHECK((w(5, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK) == 24000, "0,5 s a 48 kHz = 24000 (%u)",
          w(5, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK);
    vp_advance(&s, 0.5); /* 48000 > EBO: vuelve a LBO */
    CHECK((w(5, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK) == 100, "bucle a LBO (%u)",
          w(5, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK);

    /* Pausa: no avanza. */
    vp_method(&s, VP_PIO_VOICE_PAUSE, 5 | VP_STATE_PAUSED);
    vp_advance(&s, 0.25);
    CHECK((w(5, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK) == 100, "en pausa no avanza");
    vp_method(&s, VP_PIO_VOICE_PAUSE, 5);
    vp_advance(&s, 100.0 / 48000.0); /* 100 muestras */
    CHECK((w(5, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK) == 200, "reanudada (%u)",
          w(5, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK);

    /* Pitch +4096 = una octava arriba: 96 kHz. Los 16 bits bajos (enlace) se conservan. */
    memcpy(g_voices + 9 * VP_VOICE_SIZE + VP_VOICE_TAR_PITCH_LINK, "\x34\x12\x00\x00", 4);
    vp_method(&s, VP_PIO_SET_CURRENT_VOICE, 9);
    vp_method(&s, VP_PIO_SET_VOICE_TAR_PITCH, 0x10000000u);
    vp_method(&s, VP_PIO_SET_VOICE_TAR_VOLA + 4, 0x12345678u);
    CHECK(w(9, 0x64) == 0x12345678u, "TAR_VOLB por PIO");
    CHECK((w(9, VP_VOICE_TAR_PITCH_LINK) & 0xFFFF) == 0x1234, "el pitch no pisa el enlace");
    vp_method(&s, VP_PIO_SET_VOICE_CFG_BUF_EBO, 1000);
    vp_method(&s, VP_PIO_VOICE_ON, 9);
    vp_advance(&s, 0.01); /* 480 muestras de salida = 960 de la voz */
    CHECK((w(9, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK) == 960, "pitch +1 octava (%u)",
          w(9, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK);

    /* Sin LOOP, al pasar de EBO la voz se apaga sola y la posición queda en EBO. */
    vp_advance(&s, 0.01);
    CHECK((w(9, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK) == 1000, "se queda en EBO");
    CHECK(!(w(9, VP_VOICE_PAR_STATE) & VP_STATE_ACTIVE), "voz terminada");
    CHECK(s.voices_ended == 1, "contador de voces terminadas");

    /* VOICE_OFF y handles fuera de rango. */
    vp_method(&s, VP_PIO_VOICE_OFF, 5);
    CHECK(!(w(5, VP_VOICE_PAR_STATE) & VP_STATE_ACTIVE), "VOICE_OFF");
    vp_method(&s, VP_PIO_VOICE_ON, 0xFFFF);
    vp_method(&s, VP_PIO_SET_CURRENT_VOICE, 0xFFFF);
    vp_method(&s, VP_PIO_SET_VOICE_BUF_CBO, 7); /* sin voz actual: se ignora */
    vp_advance(&s, 1.0);
    CHECK(s.voices_on == 2, "solo cuentan los VOICE_ON válidos (%u)", s.voices_on);

    /* Voces libres a avisar: la 9 terminó sola y la 5 recibió VOICE_OFF, en ese orden. */
    {
        uint32_t h = 0xFFFF;
        CHECK(vp_take_idle(&s, &h) && h == 9, "voz libre: fin del búfer (%u)", h);
        CHECK(vp_take_idle(&s, &h) && h == 5, "voz libre: VOICE_OFF (%u)", h);
        CHECK(!vp_take_idle(&s, &h), "cola de voces libres vacía");
    }

    test_mix();
    if (g_fail)
        return 1;
    printf("vp_test: OK (%d comprobaciones)\n", g_checks);
    return 0;
}
