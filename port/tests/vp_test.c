/*
 * vp_test.c: procesador de voces de la APU (apu/vp.c) con una tabla de voces sintética.
 */
#include <stdio.h>
#include <string.h>

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

    if (g_fail)
        return 1;
    printf("vp_test: OK (%d comprobaciones)\n", g_checks);
    return 0;
}
