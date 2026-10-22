/*
 * pushbuf_test.c: push buffer decoder tests with hand-made buffers.
 * Built and run by port\build.cmd; exits with code 1 if anything fails.
 */
#include <stdio.h>
#include <string.h>

#include "../src/gpu/pushbuf.h"

#define MAXREC 64

typedef struct rec {
    uint32_t sub, method, param;
    int non_inc;
} rec_t;

typedef struct sink {
    rec_t r[MAXREC];
    int n;
    int events[MAXREC];
    int nevents;
} sink_t;

static int g_failures;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

static void on_method(void *user, uint32_t sub, uint32_t method, uint32_t param, int non_inc) {
    sink_t *s = (sink_t *)user;
    if (s->n < MAXREC) {
        s->r[s->n].sub = sub;
        s->r[s->n].method = method;
        s->r[s->n].param = param;
        s->r[s->n].non_inc = non_inc;
        s->n++;
    }
}

static void on_event(void *user, int kind, uint32_t at, uint32_t word, uint32_t target) {
    sink_t *s = (sink_t *)user;
    (void)at;
    (void)word;
    (void)target;
    if (s->nevents < MAXREC)
        s->events[s->nevents++] = kind;
}

static const pb_callbacks_t k_cb = {on_method, on_event};

/* Header of increasing / non-increasing methods. */
static uint32_t inc(uint32_t sub, uint32_t method, uint32_t count) {
    return (count << 18) | (sub << 13) | method;
}

static uint32_t noninc(uint32_t sub, uint32_t method, uint32_t count) {
    return 0x40000000u | inc(sub, method, count);
}

static void test_increasing_methods(void) {
    uint32_t buf[] = {inc(0, 0x1D90, 2), 0xFF336699u, 0x000000F0u};
    pb_state_t st;
    sink_t s;
    uint32_t get;
    memset(&st, 0, sizeof st);
    memset(&s, 0, sizeof s);
    get = pb_run(&st, (const uint8_t *)buf, sizeof buf, 0, sizeof buf, 1000, &k_cb, &s);
    CHECK(get == sizeof buf);
    CHECK(s.n == 2);
    CHECK(s.r[0].method == 0x1D90 && s.r[0].param == 0xFF336699u && !s.r[0].non_inc);
    CHECK(s.r[1].method == 0x1D94 && s.r[1].param == 0xF0); /* CLEAR_SURFACE */
    CHECK(st.count == 0 && st.errors == 0 && st.words == 3);
}

static void test_non_increasing_and_subchannel(void) {
    uint32_t buf[] = {noninc(3, 0x1818, 3), 1, 2, 3};
    pb_state_t st;
    sink_t s;
    memset(&st, 0, sizeof st);
    memset(&s, 0, sizeof s);
    pb_run(&st, (const uint8_t *)buf, sizeof buf, 0, sizeof buf, 1000, &k_cb, &s);
    CHECK(s.n == 3);
    CHECK(s.r[2].method == 0x1818 && s.r[2].param == 3 && s.r[2].non_inc && s.r[2].sub == 3);
}

static void test_jump_call_return(void) {
    /* 0x00: CALL 0x10 | 0x04: method | 0x08: data | 0x0C: JMP 0x20
     * 0x10: method | 0x14: data | 0x18: RETURN | 0x1C: (padding) | 0x20: end */
    uint32_t buf[9];
    pb_state_t st;
    sink_t s;
    uint32_t get;
    buf[0] = 0x10u | 2u;
    buf[1] = inc(0, 0x0130, 1); /* FLIP_STALL */
    buf[2] = 0;
    buf[3] = 0x20u | 1u;
    buf[4] = inc(0, 0x0100, 1); /* NO_OPERATION */
    buf[5] = 7;
    buf[6] = 0x00020000u;
    buf[7] = 0xDEADBEEFu;
    buf[8] = 0;
    memset(&st, 0, sizeof st);
    memset(&s, 0, sizeof s);
    get = pb_run(&st, (const uint8_t *)buf, sizeof buf, 0, 0x20, 1000, &k_cb, &s);
    CHECK(get == 0x20);
    CHECK(s.n == 2);
    CHECK(s.r[0].method == 0x0100 && s.r[0].param == 7); /* the subroutine first */
    CHECK(s.r[1].method == 0x0130);
    CHECK(s.nevents == 3);
    CHECK(s.events[0] == PB_EVENT_CALL && s.events[1] == PB_EVENT_RETURN &&
          s.events[2] == PB_EVENT_JUMP);
    CHECK(st.errors == 0 && !st.in_subroutine);
}

static void test_old_jump_and_split_command(void) {
    uint32_t buf[4];
    pb_state_t st;
    sink_t s;
    uint32_t get;
    buf[0] = 0x20000000u | 0x08u; /* old jump to 0x08 */
    buf[1] = 0xBAD0BAD0u;          /* skipped */
    buf[2] = inc(1, 0x0200, 1);
    buf[3] = 0x02800000u; /* horizontal clip: width 640 */
    memset(&st, 0, sizeof st);
    memset(&s, 0, sizeof s);
    /* PUT in the middle of the command: the header arrives in one advance and the data in another. */
    get = pb_run(&st, (const uint8_t *)buf, sizeof buf, 0, 0x0C, 1000, &k_cb, &s);
    CHECK(get == 0x0C && st.count == 1 && s.n == 0);
    get = pb_run(&st, (const uint8_t *)buf, sizeof buf, get, 0x10, 1000, &k_cb, &s);
    CHECK(get == 0x10 && s.n == 1 && s.r[0].method == 0x0200 && s.r[0].sub == 1);
    CHECK(s.events[0] == PB_EVENT_OLD_JUMP);
}

static void test_errors(void) {
    uint32_t buf[] = {0x00020000u, 0x80000000u, 0xCu | 2u, 0x0u | 2u};
    pb_state_t st;
    sink_t s;
    uint32_t get;
    memset(&st, 0, sizeof st);
    memset(&s, 0, sizeof s);
    /* RETURN without CALL and invalid header: they are logged and execution continues. */
    get = pb_run(&st, (const uint8_t *)buf, sizeof buf, 0, 8, 1000, &k_cb, &s);
    CHECK(get == 8 && st.errors == 2);
    /* CALL inside CALL: error and stop at the second. */
    get = pb_run(&st, (const uint8_t *)buf, sizeof buf, 8, 0x10, 1000, &k_cb, &s);
    CHECK(st.in_subroutine && st.errors == 3 && get == 0x0C);
    /* GET outside the DMA object. */
    memset(&st, 0, sizeof st);
    get = pb_run(&st, (const uint8_t *)buf, sizeof buf, 0x100, 0x104, 1000, &k_cb, &s);
    CHECK(get == 0x100 && st.errors == 1);
}

static void test_max_words(void) {
    uint32_t buf[] = {0x0u | 1u}; /* JMP 0: infinite loop */
    pb_state_t st;
    sink_t s;
    uint32_t get;
    memset(&st, 0, sizeof st);
    memset(&s, 0, sizeof s);
    get = pb_run(&st, (const uint8_t *)buf, sizeof buf, 0, 4, 50, &k_cb, &s);
    CHECK(get == 0 && st.words == 50);
}

/* Page 1 unreadable: a jump there stops the decoder without reading it. */
static int readable_page0(void *user, uint32_t offset) {
    (void)user;
    return offset < 0x1000u;
}

static void test_unreadable_page(void) {
    static uint32_t buf[0x2000 / 4];
    static const pb_callbacks_t cb = {on_method, on_event, readable_page0};
    pb_state_t st;
    sink_t s;
    uint32_t get;
    memset(&st, 0, sizeof st);
    memset(&s, 0, sizeof s);
    buf[0] = inc(0, 0x1D90, 1);
    buf[1] = 0x11223344u;
    buf[2] = 0x1000u | 1u; /* JUMP to page 1 */
    get = pb_run(&st, (const uint8_t *)buf, sizeof buf, 0, 0x1800u, 1000, &cb, &s);
    CHECK(get == 0x1000u);
    CHECK(s.n == 1 && st.errors == 1);
    CHECK(s.nevents == 2 && s.events[0] == PB_EVENT_JUMP && s.events[1] == PB_EVENT_ERROR);
}

/* Bulk runs: the callback receives the data without going past PUT or the page. */
static uint32_t g_bulk_sum, g_bulk_words, g_bulk_calls;
static int g_bulk_accept;

static int on_bulk(void *user, uint32_t sub, uint32_t method, const uint32_t *words, uint32_t n) {
    uint32_t i;
    (void)user;
    (void)sub;
    if (!g_bulk_accept || method != 0x1818)
        return 0;
    for (i = 0; i < n; i++)
        g_bulk_sum += words[i];
    g_bulk_words += n;
    g_bulk_calls++;
    return 1;
}

static void test_bulk(void) {
    static uint32_t buf[0x2000 / 4];
    static const pb_callbacks_t cb = {on_method, on_event, NULL, on_bulk};
    pb_state_t st;
    sink_t s;
    uint32_t get, i;
    /* Header at 0xFF8: 1 word in page 0 and 999 in page 1 */
    buf[0xFF8 / 4] = noninc(0, 0x1818, 1000);
    for (i = 0; i < 1000; i++)
        buf[0xFFC / 4 + i] = i + 1;
    memset(&st, 0, sizeof st);
    memset(&s, 0, sizeof s);
    g_bulk_accept = 1;
    /* PUT 100 data words in: the rest arrives in the second advance. */
    get = pb_run(&st, (const uint8_t *)buf, sizeof buf, 0xFF8, 0xFFC + 100 * 4, 1u << 20, &cb, &s);
    CHECK(get == 0xFFC + 100 * 4 && st.count == 900);
    get = pb_run(&st, (const uint8_t *)buf, sizeof buf, get, 0xFFC + 1000 * 4, 1u << 20, &cb, &s);
    CHECK(get == 0xFFC + 1000 * 4 && st.count == 0 && st.errors == 0);
    /* The first word goes alone (end of page 0, n = 1 does not use the block). */
    CHECK(s.n == 1 && s.r[0].param == 1);
    CHECK(g_bulk_words == 999 && g_bulk_sum == 1000u * 1001u / 2u - 1u && g_bulk_calls == 2);
    CHECK(st.words == 1001);
    /* If the callback does not serve it, everything goes word by word. */
    memset(&st, 0, sizeof st);
    memset(&s, 0, sizeof s);
    g_bulk_accept = 0;
    get = pb_run(&st, (const uint8_t *)buf, sizeof buf, 0xFF8, 0xFFC + 20 * 4, 1u << 20, &cb, &s);
    CHECK(get == 0xFFC + 20 * 4 && s.n == 20 && s.r[19].param == 20);
}

int main(void) {
    test_bulk();
    test_unreadable_page();
    test_increasing_methods();
    test_non_increasing_and_subchannel();
    test_jump_call_return();
    test_old_jump_and_split_command();
    test_errors();
    test_max_words();
    if (g_failures) {
        fprintf(stderr, "pushbuf_test: %d failures\n", g_failures);
        return 1;
    }
    printf("pushbuf_test: OK (8 tests)\n");
    return 0;
}
