/*
 * input.c: PC -> Xbox gamepad mapping and button-press script (pure: no Windows or loader).
 * See input.h.
 */
#include "input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VK_RETURN_ 0x0D
#define VK_ESCAPE_ 0x1B
#define VK_LEFT_ 0x25
#define VK_UP_ 0x26
#define VK_RIGHT_ 0x27
#define VK_DOWN_ 0x28
#define STICK_MAX 32767

static uint8_t digital(uint16_t buttons, uint16_t bit) { return (buttons & bit) ? 255 : 0; }

void xpad_from_pc(const pc_gamepad_t *pc, xbox_gamepad_t *out) {
    memset(out, 0, sizeof *out);
    out->buttons = (uint16_t)(pc->buttons & (PC_DPAD_UP | PC_DPAD_DOWN | PC_DPAD_LEFT |
                                             PC_DPAD_RIGHT | PC_START | PC_BACK |
                                             PC_LEFT_THUMB | PC_RIGHT_THUMB));
    out->analog[XPAD_A] = digital(pc->buttons, PC_A);
    out->analog[XPAD_B] = digital(pc->buttons, PC_B);
    out->analog[XPAD_X] = digital(pc->buttons, PC_X);
    out->analog[XPAD_Y] = digital(pc->buttons, PC_Y);
    out->analog[XPAD_BLACK] = digital(pc->buttons, PC_RIGHT_SHOULDER);
    out->analog[XPAD_WHITE] = digital(pc->buttons, PC_LEFT_SHOULDER);
    out->analog[XPAD_LEFT_TRIGGER] = pc->left_trigger;
    out->analog[XPAD_RIGHT_TRIGGER] = pc->right_trigger;
    out->thumb_lx = pc->thumb_lx;
    out->thumb_ly = pc->thumb_ly;
    out->thumb_rx = pc->thumb_rx;
    out->thumb_ry = pc->thumb_ry;
}

void xpad_from_keys(key_down_fn down, xbox_gamepad_t *out) {
    memset(out, 0, sizeof *out);
    if (down(VK_RETURN_)) {
        out->buttons |= XPAD_START;
        out->analog[XPAD_A] = 255;
    }
    if (down(VK_ESCAPE_)) {
        out->buttons |= XPAD_BACK;
        out->analog[XPAD_B] = 255;
    }
    if (down(VK_UP_))
        out->buttons |= XPAD_DPAD_UP;
    if (down(VK_DOWN_))
        out->buttons |= XPAD_DPAD_DOWN;
    if (down(VK_LEFT_))
        out->buttons |= XPAD_DPAD_LEFT;
    if (down(VK_RIGHT_))
        out->buttons |= XPAD_DPAD_RIGHT;
    out->thumb_ly = (int16_t)((down('W') ? STICK_MAX : 0) - (down('S') ? STICK_MAX : 0));
    out->thumb_lx = (int16_t)((down('D') ? STICK_MAX : 0) - (down('A') ? STICK_MAX : 0));
    if (down('J'))
        out->analog[XPAD_A] = 255;
    if (down('K'))
        out->analog[XPAD_B] = 255;
    if (down('L'))
        out->analog[XPAD_X] = 255;
    if (down('I'))
        out->analog[XPAD_Y] = 255;
    if (down('Q'))
        out->analog[XPAD_WHITE] = 255;
    if (down('E'))
        out->analog[XPAD_BLACK] = 255;
    if (down('U'))
        out->analog[XPAD_LEFT_TRIGGER] = 255;
    if (down('O'))
        out->analog[XPAD_RIGHT_TRIGGER] = 255;
}

static int16_t stronger(int16_t a, int16_t b) { return abs(b) > abs(a) ? b : a; }

void xpad_merge(xbox_gamepad_t *dst, const xbox_gamepad_t *src) {
    int i;
    dst->buttons |= src->buttons;
    for (i = 0; i < XPAD_ANALOG_COUNT; i++)
        if (src->analog[i] > dst->analog[i])
            dst->analog[i] = src->analog[i];
    dst->thumb_lx = stronger(dst->thumb_lx, src->thumb_lx);
    dst->thumb_ly = stronger(dst->thumb_ly, src->thumb_ly);
    dst->thumb_rx = stronger(dst->thumb_rx, src->thumb_rx);
    dst->thumb_ry = stronger(dst->thumb_ry, src->thumb_ry);
}

int xpad_equal(const xbox_gamepad_t *a, const xbox_gamepad_t *b) {
    return memcmp(a, b, sizeof *a) == 0;
}

/* ---------------------------------------------------------------- script ---- */

typedef struct button_name {
    const char *name;
    uint16_t buttons;
    int analog; /* -1 if digital */
} button_name_t;

static const button_name_t k_buttons[] = {
    {"START", XPAD_START, -1},       {"BACK", XPAD_BACK, -1},
    {"UP", XPAD_DPAD_UP, -1},        {"DOWN", XPAD_DPAD_DOWN, -1},
    {"LEFT", XPAD_DPAD_LEFT, -1},    {"RIGHT", XPAD_DPAD_RIGHT, -1},
    {"LTHUMB", XPAD_LEFT_THUMB, -1}, {"RTHUMB", XPAD_RIGHT_THUMB, -1},
    {"A", 0, XPAD_A},                {"B", 0, XPAD_B},
    {"X", 0, XPAD_X},                {"Y", 0, XPAD_Y},
    {"BLACK", 0, XPAD_BLACK},        {"WHITE", 0, XPAD_WHITE},
    {"LT", 0, XPAD_LEFT_TRIGGER},    {"RT", 0, XPAD_RIGHT_TRIGGER},
};

/* "<n>s" or "<n>ms" (n integer) to milliseconds, or "<n>p" to reads (*polls = 1). -1 if not
 * valid. */
static long parse_ms(const char *text, size_t len, int *polls) {
    char buf[24];
    char *end;
    long value;
    if (!len || len >= sizeof buf)
        return -1;
    memcpy(buf, text, len);
    buf[len] = 0;
    value = strtol(buf, &end, 10);
    *polls = 0;
    if (end == buf || value < 0 || value > 86400000L)
        return -1;
    if (!strcmp(end, "p")) {
        *polls = 1;
        return value;
    }
    if (!strcmp(end, "ms"))
        return value;
    if (!strcmp(end, "s") && value <= 86400L)
        return value * 1000;
    return -1;
}

static int parse_buttons(const char *text, size_t len, input_event_t *ev) {
    size_t start = 0;
    while (start < len) {
        size_t stop = start, i;
        int found = 0;
        while (stop < len && text[stop] != '+')
            stop++;
        if (stop == start || (stop + 1 == len && text[stop] == '+'))
            return -1; /* empty name: "A++B" or "A+" */
        for (i = 0; i < sizeof k_buttons / sizeof k_buttons[0]; i++) {
            const button_name_t *b = &k_buttons[i];
            if (strlen(b->name) == stop - start && !_strnicmp(b->name, text + start, stop - start)) {
                if (b->analog >= 0)
                    ev->analog_mask |= (uint8_t)(1u << b->analog);
                else
                    ev->buttons |= b->buttons;
                found = 1;
                break;
            }
        }
        if (!found)
            return -1;
        start = stop + 1;
    }
    return ev->buttons || ev->analog_mask ? 0 : -1;
}

int input_script_parse(const char *text, input_script_t *out, char *err, size_t errlen) {
    const char *p = text;
    memset(out, 0, sizeof *out);
    while (p && *p) {
        const char *item_end = strchr(p, ','), *colon, *at;
        size_t item_len = item_end ? (size_t)(item_end - p) : strlen(p);
        input_event_t ev;
        long when, hold;
        int when_polls = 0, hold_polls = 0;
        memset(&ev, 0, sizeof ev);
        colon = memchr(p, ':', item_len);
        if (item_len < 3 || _strnicmp(p, "t=", 2) || !colon) {
            snprintf(err, errlen, "entry '%.*s': expected t=<time>:<BUTTON>", (int)item_len, p);
            return -1;
        }
        when = parse_ms(p + 2, (size_t)(colon - p - 2), &when_polls);
        hold = when_polls ? (long)INPUT_SCRIPT_HOLD_POLLS : (long)INPUT_SCRIPT_HOLD_MS;
        hold_polls = when_polls;
        at = memchr(colon, '@', item_len - (size_t)(colon - p));
        if (at)
            hold = parse_ms(at + 1, item_len - (size_t)(at + 1 - p), &hold_polls);
        if (when < 0 || hold <= 0) {
            snprintf(err, errlen, "entry '%.*s': invalid time (use 3s, 3500ms or 200p)", (int)item_len, p);
            return -1;
        }
        if (hold_polls != when_polls) {
            snprintf(err, errlen, "entry '%.*s': the instant and the duration must use the same unit",
                     (int)item_len, p);
            return -1;
        }
        if (parse_buttons(colon + 1, (size_t)((at ? at : p + item_len) - colon - 1), &ev)) {
            snprintf(err, errlen, "entry '%.*s': unknown button", (int)item_len, p);
            return -1;
        }
        if (out->count == INPUT_SCRIPT_MAX) {
            snprintf(err, errlen, "more than %d presses in the script", INPUT_SCRIPT_MAX);
            return -1;
        }
        ev.at_ms = (uint32_t)when;
        ev.hold_ms = (uint32_t)hold;
        ev.polls = (uint8_t)when_polls;
        out->events[out->count++] = ev;
        p = item_end ? item_end + 1 : NULL;
        if (p && !*p) {
            snprintf(err, errlen, "empty entry at the end of the script");
            return -1;
        }
    }
    return 0;
}

void input_script_apply(const input_script_t *script, uint32_t now_ms, uint32_t now_polls,
                        xbox_gamepad_t *out) {
    int i, k;
    for (i = 0; i < script->count; i++) {
        const input_event_t *ev = &script->events[i];
        uint32_t now = ev->polls ? now_polls : now_ms;
        if (now < ev->at_ms || now - ev->at_ms >= ev->hold_ms)
            continue;
        out->buttons |= ev->buttons;
        for (k = 0; k < XPAD_ANALOG_COUNT; k++)
            if (ev->analog_mask & (1u << k))
                out->analog[k] = 255;
    }
}
