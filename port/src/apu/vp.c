/*
 * vp.c: procesador de voces mínimo de la APU. Ver vp.h.
 */
#include "apu/vp.h"

#include <math.h>
#include <string.h>

static uint32_t *word(uint8_t *voice, uint32_t offset) {
    return (uint32_t *)(voice + offset);
}

static uint8_t *voice_at(vp_state_t *s, uint32_t handle) {
    if (!s->voices || handle >= VP_MAX_VOICES)
        return NULL;
    return s->voices + (size_t)handle * VP_VOICE_SIZE;
}

static void set_low24(uint8_t *voice, uint32_t offset, uint32_t value) {
    uint32_t *w = word(voice, offset);
    *w = (*w & ~VP_OFFSET_MASK) | (value & VP_OFFSET_MASK);
}

void vp_init(vp_state_t *s) {
    memset(s, 0, sizeof *s);
    s->current = VP_NO_VOICE;
}

void vp_method(vp_state_t *s, uint32_t method, uint32_t value) {
    uint32_t handle = value & 0xFFFFu;
    uint8_t *v;
    switch (method) {
    case VP_PIO_VOICE_ON:
        if (handle < VP_MAX_VOICES) {
            s->active[handle] = 1;
            s->frac[handle] = 0.0;
            s->voices_on++;
            if ((v = voice_at(s, handle)) != NULL)
                *word(v, VP_VOICE_PAR_STATE) = (*word(v, VP_VOICE_PAR_STATE) | VP_STATE_ACTIVE |
                                                VP_STATE_NEW_VOICE) & ~VP_STATE_PAUSED;
        }
        return;
    case VP_PIO_VOICE_OFF:
    case VP_PIO_VOICE_RELEASE: /* sin envolventes: soltar es apagar */
        if (handle < VP_MAX_VOICES) {
            s->active[handle] = 0;
            if ((v = voice_at(s, handle)) != NULL)
                *word(v, VP_VOICE_PAR_STATE) &= ~(VP_STATE_ACTIVE | VP_STATE_NEW_VOICE);
        }
        return;
    case VP_PIO_VOICE_PAUSE:
        if ((v = voice_at(s, handle)) != NULL) {
            if (value & VP_STATE_PAUSED) /* el bit de acción coincide con el de estado */
                *word(v, VP_VOICE_PAR_STATE) |= VP_STATE_PAUSED;
            else
                *word(v, VP_VOICE_PAR_STATE) &= ~VP_STATE_PAUSED;
        }
        return;
    case VP_PIO_SET_CURRENT_VOICE:
        s->current = (uint16_t)handle;
        return;
    default:
        break;
    }
    v = voice_at(s, s->current);
    if (!v)
        return;
    if (method >= VP_PIO_SET_VOICE_CFG_VBIN && method <= VP_PIO_SET_VOICE_CFG_MISC && !(method & 3)) {
        *word(v, method - VP_PIO_SET_VOICE_CFG_VBIN) = value;
        return;
    }
    switch (method) {
    case VP_PIO_SET_VOICE_TAR_PITCH: /* el pitch va en los 16 bits altos; el enlace se queda */
        *word(v, VP_VOICE_TAR_PITCH_LINK) =
            (value & 0xFFFF0000u) | (*word(v, VP_VOICE_TAR_PITCH_LINK) & 0xFFFFu);
        break;
    case VP_PIO_SET_VOICE_CFG_BUF_BASE:
        set_low24(v, VP_VOICE_CUR_PSL_START, value);
        break;
    case VP_PIO_SET_VOICE_CFG_BUF_LBO:
        set_low24(v, VP_VOICE_CUR_PSH_SAMPLE, value);
        break;
    case VP_PIO_SET_VOICE_BUF_CBO:
        set_low24(v, VP_VOICE_PAR_OFFSET, value);
        s->frac[s->current < VP_MAX_VOICES ? s->current : 0] = 0.0;
        break;
    case VP_PIO_SET_VOICE_CFG_BUF_EBO:
        set_low24(v, VP_VOICE_PAR_NEXT, value);
        break;
    default:
        break; /* volúmenes, filtros, envolventes...: sin efecto en la posición */
    }
}

double vp_voice_rate(const uint8_t *voice) {
    int16_t pitch = (int16_t)(*(const uint32_t *)(voice + VP_VOICE_TAR_PITCH_LINK) >> 16);
    return 48000.0 * pow(2.0, pitch / 4096.0);
}

void vp_advance(vp_state_t *s, double seconds) {
    uint32_t h;
    if (!s->voices || seconds <= 0.0)
        return;
    for (h = 0; h < VP_MAX_VOICES; h++) {
        uint8_t *v;
        uint32_t state, cbo, ebo, lbo, step;
        double total;
        if (!s->active[h])
            continue;
        v = voice_at(s, h);
        state = *word(v, VP_VOICE_PAR_STATE);
        if (!(state & VP_STATE_ACTIVE)) { /* DirectSound la apagó escribiendo en la tabla */
            s->active[h] = 0;
            continue;
        }
        /* El hardware empieza a procesar la voz: deja de ser nueva y su posición es válida. */
        state &= ~VP_STATE_NEW_VOICE;
        *word(v, VP_VOICE_PAR_STATE) = state;
        if (state & VP_STATE_PAUSED)
            continue;
        total = s->frac[h] + seconds * vp_voice_rate(v);
        step = (uint32_t)floor(total + 1e-7); /* 99,9999999 muestras son 100 */
        s->frac[h] = total > step ? total - step : 0.0;
        cbo = *word(v, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK;
        ebo = *word(v, VP_VOICE_PAR_NEXT) & VP_OFFSET_MASK;
        lbo = *word(v, VP_VOICE_CUR_PSH_SAMPLE) & VP_OFFSET_MASK;
        cbo += step;
        if (cbo > ebo) {
            if ((*word(v, VP_VOICE_CFG_FMT) & VP_FMT_LOOP) && lbo <= ebo) {
                cbo = lbo + (cbo - ebo - 1) % (ebo - lbo + 1);
            } else { /* fin del búfer: la voz se apaga sola */
                cbo = ebo;
                *word(v, VP_VOICE_PAR_STATE) = state & ~VP_STATE_ACTIVE;
                s->active[h] = 0;
                s->voices_ended++;
            }
        }
        set_low24(v, VP_VOICE_PAR_OFFSET, cbo);
    }
}
