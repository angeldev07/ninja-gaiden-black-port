/*
 * vp.c: minimal APU voice processor. See vp.h.
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

static void push_idle(vp_state_t *s, uint32_t handle) {
    if (s->idle_tail - s->idle_head >= VP_IDLE_QUEUE) {
        s->idle_dropped++;
        return;
    }
    s->idle[s->idle_tail++ % VP_IDLE_QUEUE] = (uint16_t)handle;
}

int vp_take_idle(vp_state_t *s, uint32_t *handle) {
    if (s->idle_head == s->idle_tail)
        return 0;
    *handle = s->idle[s->idle_head++ % VP_IDLE_QUEUE];
    return 1;
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
    case VP_PIO_VOICE_RELEASE: /* no envelopes: releasing is turning off */
        if (handle < VP_MAX_VOICES) {
            s->active[handle] = 0;
            if ((v = voice_at(s, handle)) != NULL)
                *word(v, VP_VOICE_PAR_STATE) &= ~(VP_STATE_ACTIVE | VP_STATE_NEW_VOICE);
            push_idle(s, handle); /* like xemu: every VOICE_OFF notifies, whether it was active or not */
        }
        return;
    case VP_PIO_VOICE_PAUSE:
        if ((v = voice_at(s, handle)) != NULL) {
            if (value & VP_STATE_PAUSED) /* the action bit matches the state bit */
                *word(v, VP_VOICE_PAR_STATE) |= VP_STATE_PAUSED;
            else
                *word(v, VP_VOICE_PAR_STATE) &= ~VP_STATE_PAUSED;
        }
        return;
    case VP_PIO_SET_CURRENT_VOICE:
        s->current = (uint16_t)handle;
        return;
    case VP_PIO_SET_CURRENT_INBUF_SGE:
        s->inbuf_sge = value;
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
    if (method >= VP_PIO_SET_VOICE_TAR_VOLA && method <= VP_PIO_SET_VOICE_TAR_VOLC && !(method & 3)) {
        *word(v, 0x60 + (method - VP_PIO_SET_VOICE_TAR_VOLA)) = value;
        return;
    }
    switch (method) {
    case VP_PIO_SET_VOICE_TAR_PITCH: /* the pitch goes in the upper 16 bits; the link stays */
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
        break; /* volumes, filters, envelopes...: no effect on the position */
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
        double total;
        uint32_t step;
        if ((v = vp_voice_begin(s, h)) == NULL)
            continue;
        total = s->frac[h] + seconds * vp_voice_rate(v);
        step = (uint32_t)floor(total + 1e-7); /* 99.9999999 samples are 100 */
        s->frac[h] = total > step ? total - step : 0.0;
        vp_voice_move(s, h, v, step);
    }
}

uint8_t *vp_voice_begin(vp_state_t *s, uint32_t h) {
    uint8_t *v;
    uint32_t state;
    if (!s->voices || h >= VP_MAX_VOICES || !s->active[h])
        return NULL;
    v = voice_at(s, h);
    state = *word(v, VP_VOICE_PAR_STATE);
    if (!(state & VP_STATE_ACTIVE)) { /* DirectSound turned it off by writing to the table */
        s->active[h] = 0;
        return NULL;
    }
    /* The hardware starts processing the voice: it is no longer new and its position is valid. */
    state &= ~VP_STATE_NEW_VOICE;
    *word(v, VP_VOICE_PAR_STATE) = state;
    return (state & VP_STATE_PAUSED) ? NULL : v;
}

int vp_voice_move(vp_state_t *s, uint32_t h, uint8_t *v, uint32_t step) {
    uint32_t cbo = *word(v, VP_VOICE_PAR_OFFSET) & VP_OFFSET_MASK;
    uint32_t ebo = *word(v, VP_VOICE_PAR_NEXT) & VP_OFFSET_MASK;
    uint32_t lbo = *word(v, VP_VOICE_CUR_PSH_SAMPLE) & VP_OFFSET_MASK;
    int alive = 1;
    cbo += step;
    if (cbo > ebo) {
        if ((*word(v, VP_VOICE_CFG_FMT) & VP_FMT_LOOP) && lbo <= ebo) {
            cbo = lbo + (cbo - ebo - 1) % (ebo - lbo + 1);
        } else { /* end of buffer: the voice turns itself off */
            cbo = ebo;
            *word(v, VP_VOICE_PAR_STATE) &= ~VP_STATE_ACTIVE;
            s->active[h] = 0;
            s->voices_ended++;
            push_idle(s, h);
            alive = 0;
        }
    }
    set_low24(v, VP_VOICE_PAR_OFFSET, cbo);
    return alive;
}
