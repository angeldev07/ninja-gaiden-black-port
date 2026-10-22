/*
 * input_test.c: tests of the input mapping (input/input.c): PC gamepad and keyboard to
 * Xbox XINPUT_GAMEPAD, source merging and the --input-script script.
 * Built and run by port\build.cmd; exits with code 1 if anything fails.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../src/input/input.h"

static int g_failures;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

/* Keys pressed for the fake keyboard (Windows virtual-key codes). */
static int g_keys[16];
static int g_nkeys;

static int fake_down(int vk) {
    int i;
    for (i = 0; i < g_nkeys; i++)
        if (g_keys[i] == vk)
            return 1;
    return 0;
}

static void press(const int *keys, int n) {
    memcpy(g_keys, keys, (size_t)n * sizeof *keys);
    g_nkeys = n;
}

static void test_layout(void) {
    /* Xbox layout: XINPUT_GAMEPAD 18 bytes, XINPUT_STATE 22, no padding. In tables
     * so the comparisons are not constant (C4127). */
    static const size_t got[] = {
        sizeof(xbox_gamepad_t),           sizeof(xbox_input_state_t),
        offsetof(xbox_gamepad_t, analog), offsetof(xbox_gamepad_t, thumb_lx),
        offsetof(xbox_gamepad_t, thumb_ry), offsetof(xbox_input_state_t, gamepad),
        sizeof(pc_gamepad_t),
    };
    static const size_t want[] = {18, 22, 2, 10, 16, 4, 12};
    size_t i;
    for (i = 0; i < sizeof want / sizeof want[0]; i++)
        CHECK(got[i] == want[i]);
}

static void test_pc_mapping(void) {
    pc_gamepad_t pc;
    xbox_gamepad_t x;
    memset(&pc, 0, sizeof pc);
    pc.buttons = PC_A | PC_Y | PC_LEFT_SHOULDER | PC_START | PC_DPAD_LEFT | PC_RIGHT_THUMB;
    pc.left_trigger = 17;
    pc.right_trigger = 250;
    pc.thumb_lx = -32768;
    pc.thumb_ly = 32767;
    pc.thumb_rx = 123;
    pc.thumb_ry = -456;
    xpad_from_pc(&pc, &x);
    /* The digital bits match; A/B/X/Y and the shoulders become analog. */
    CHECK(x.buttons == (XPAD_START | XPAD_DPAD_LEFT | XPAD_RIGHT_THUMB));
    CHECK(x.analog[XPAD_A] == 255 && x.analog[XPAD_B] == 0);
    CHECK(x.analog[XPAD_X] == 0 && x.analog[XPAD_Y] == 255);
    CHECK(x.analog[XPAD_WHITE] == 255 && x.analog[XPAD_BLACK] == 0); /* LB -> WHITE */
    CHECK(x.analog[XPAD_LEFT_TRIGGER] == 17 && x.analog[XPAD_RIGHT_TRIGGER] == 250);
    CHECK(x.thumb_lx == -32768 && x.thumb_ly == 32767);
    CHECK(x.thumb_rx == 123 && x.thumb_ry == -456);

    pc.buttons = PC_RIGHT_SHOULDER | PC_B | PC_X | PC_BACK | PC_LEFT_SHOULDER;
    xpad_from_pc(&pc, &x);
    CHECK(x.buttons == XPAD_BACK); /* the shoulders do not leak into wButtons */
    CHECK(x.analog[XPAD_BLACK] == 255 && x.analog[XPAD_WHITE] == 255); /* RB -> BLACK */
    CHECK(x.analog[XPAD_B] == 255 && x.analog[XPAD_X] == 255 && x.analog[XPAD_A] == 0);
}

static void test_keyboard(void) {
    xbox_gamepad_t x;
    static const int enter_esc[] = {0x0D, 0x1B};
    static const int arrows[] = {0x26, 0x27};
    static const int wasd[] = {'W', 'A'};
    static const int opposite[] = {'W', 'S', 'D'};
    static const int jkli[] = {'J', 'K', 'L', 'I', 'Q', 'E', 'U', 'O'};

    press(NULL, 0);
    xpad_from_keys(fake_down, &x);
    {
        xbox_gamepad_t zero;
        memset(&zero, 0, sizeof zero);
        CHECK(xpad_equal(&x, &zero));
    }
    press(enter_esc, 2);
    xpad_from_keys(fake_down, &x);
    CHECK(x.buttons == (XPAD_START | XPAD_BACK));
    CHECK(x.analog[XPAD_A] == 255 && x.analog[XPAD_B] == 255);

    press(arrows, 2);
    xpad_from_keys(fake_down, &x);
    CHECK(x.buttons == (XPAD_DPAD_UP | XPAD_DPAD_RIGHT));
    CHECK(x.analog[XPAD_A] == 0);

    press(wasd, 2);
    xpad_from_keys(fake_down, &x);
    CHECK(x.thumb_ly == 32767 && x.thumb_lx == -32767 && x.buttons == 0);

    press(opposite, 3); /* W and S cancel out */
    xpad_from_keys(fake_down, &x);
    CHECK(x.thumb_ly == 0 && x.thumb_lx == 32767);

    press(jkli, 8);
    xpad_from_keys(fake_down, &x);
    CHECK(x.analog[XPAD_A] == 255 && x.analog[XPAD_B] == 255 && x.analog[XPAD_X] == 255 &&
          x.analog[XPAD_Y] == 255);
    CHECK(x.analog[XPAD_WHITE] == 255 && x.analog[XPAD_BLACK] == 255);
    CHECK(x.analog[XPAD_LEFT_TRIGGER] == 255 && x.analog[XPAD_RIGHT_TRIGGER] == 255);
    CHECK(x.buttons == 0);
}

static void test_merge(void) {
    xbox_gamepad_t a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    a.buttons = XPAD_START;
    a.analog[XPAD_LEFT_TRIGGER] = 100;
    a.thumb_lx = 1000;
    a.thumb_ry = -20000;
    b.buttons = XPAD_DPAD_DOWN;
    b.analog[XPAD_LEFT_TRIGGER] = 40;
    b.analog[XPAD_A] = 255;
    b.thumb_lx = -32767;
    b.thumb_ry = 5;
    xpad_merge(&a, &b);
    CHECK(a.buttons == (XPAD_START | XPAD_DPAD_DOWN));
    CHECK(a.analog[XPAD_LEFT_TRIGGER] == 100 && a.analog[XPAD_A] == 255);
    CHECK(a.thumb_lx == -32767 && a.thumb_ry == -20000); /* the most deflected wins */
}

static void test_script(void) {
    input_script_t s;
    xbox_gamepad_t x;
    char err[160];
    CHECK(input_script_parse("t=3s:START,t=5s:A,t=6500ms:UP+b@1s", &s, err, sizeof err) == 0);
    CHECK(s.count == 3);
    CHECK(s.events[0].at_ms == 3000 && s.events[0].hold_ms == INPUT_SCRIPT_HOLD_MS);
    CHECK(s.events[0].buttons == XPAD_START && s.events[0].analog_mask == 0);
    CHECK(s.events[1].at_ms == 5000 && s.events[1].analog_mask == (1u << XPAD_A));
    CHECK(s.events[2].at_ms == 6500 && s.events[2].hold_ms == 1000);
    CHECK(s.events[2].buttons == XPAD_DPAD_UP && s.events[2].analog_mask == (1u << XPAD_B));

    memset(&x, 0, sizeof x);
    input_script_apply(&s, 2999, 0, &x);
    CHECK(x.buttons == 0);
    input_script_apply(&s, 3000, 0, &x);
    CHECK(x.buttons == XPAD_START);
    memset(&x, 0, sizeof x);
    input_script_apply(&s, 3000 + INPUT_SCRIPT_HOLD_MS, 0, &x); /* already released */
    CHECK(x.buttons == 0);
    input_script_apply(&s, 5100, 0, &x);
    CHECK(x.analog[XPAD_A] == 255 && x.buttons == 0);
    memset(&x, 0, sizeof x);
    input_script_apply(&s, 7499, 0, &x);
    CHECK(x.buttons == XPAD_DPAD_UP && x.analog[XPAD_B] == 255);
    memset(&x, 0, sizeof x);
    input_script_apply(&s, 7500, 0, &x);
    CHECK(x.buttons == 0 && x.analog[XPAD_B] == 0);

    CHECK(input_script_parse("", &s, err, sizeof err) == 0 && s.count == 0);
    CHECK(input_script_parse("t=1s:LT+RT+BLACK+WHITE+LTHUMB", &s, err, sizeof err) == 0);
    CHECK(s.events[0].analog_mask == ((1u << XPAD_LEFT_TRIGGER) | (1u << XPAD_RIGHT_TRIGGER) |
                                      (1u << XPAD_BLACK) | (1u << XPAD_WHITE)));
    CHECK(s.events[0].buttons == XPAD_LEFT_THUMB);

    /* "p" unit: gamepad reads, with their own default hold; the ms clock does not count. */
    CHECK(input_script_parse("t=100p:START,t=200p:A@3p,t=1s:B", &s, err, sizeof err) == 0);
    CHECK(s.count == 3 && s.events[0].polls && s.events[0].at_ms == 100);
    CHECK(s.events[0].hold_ms == INPUT_SCRIPT_HOLD_POLLS && s.events[1].hold_ms == 3 && !s.events[2].polls);
    memset(&x, 0, sizeof x);
    input_script_apply(&s, 999999, 99, &x);
    CHECK(x.buttons == 0 && x.analog[XPAD_A] == 0);
    input_script_apply(&s, 0, 100, &x);
    CHECK(x.buttons == XPAD_START);
    memset(&x, 0, sizeof x);
    input_script_apply(&s, 0, 100 + INPUT_SCRIPT_HOLD_POLLS, &x);
    CHECK(x.buttons == 0);
    input_script_apply(&s, 0, 202, &x);
    CHECK(x.analog[XPAD_A] == 255);
    memset(&x, 0, sizeof x);
    input_script_apply(&s, 0, 203, &x);
    CHECK(x.analog[XPAD_A] == 0);
    input_script_apply(&s, 1000, 0, &x);
    CHECK(x.analog[XPAD_B] == 255);
}

static void test_script_errors(void) {
    static const char *const bad[] = {
        "START",          /* no t= */
        "t=3:START",      /* no unit */
        "t=3s:",          /* no button */
        "t=3s:JUMP",      /* unknown button */
        "t=3s:A+",        /* empty button */
        "t=-1s:A",        /* negative time */
        "t=3s:A@0ms",     /* 0 ms press */
        "t=3s:A,",        /* empty entry at the end */
        "t=99999999s:A",  /* out of range */
        "t=10p:A@1s",     /* mixed units */
        "t=10s:A@5p",
        "t=10p:A@0p",     /* 0 reads press */
    };
    input_script_t s;
    char err[160];
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        err[0] = 0;
        if (input_script_parse(bad[i], &s, err, sizeof err) != -1 || !err[0]) {
            fprintf(stderr, "FAIL: script '%s' should be invalid\n", bad[i]);
            g_failures++;
        }
    }
}

int main(void) {
    test_layout();
    test_pc_mapping();
    test_keyboard();
    test_merge();
    test_script();
    test_script_errors();
    if (g_failures) {
        fprintf(stderr, "input_test: %d failures\n", g_failures);
        return 1;
    }
    printf("input_test: OK (6 tests)\n");
    return 0;
}
