/*
 * pushbuf_test.c: pruebas del decodificador del push buffer con buffers hechos a mano.
 * Lo compila y ejecuta port\build.cmd; termina con código 1 si algo falla.
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
            fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
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

/* Cabecera de métodos crecientes / no crecientes. */
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
    /* 0x00: CALL 0x10 | 0x04: método | 0x08: dato | 0x0C: JMP 0x20
     * 0x10: método | 0x14: dato | 0x18: RETURN | 0x1C: (relleno) | 0x20: fin */
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
    CHECK(s.r[0].method == 0x0100 && s.r[0].param == 7); /* primero la subrutina */
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
    buf[0] = 0x20000000u | 0x08u; /* salto antiguo a 0x08 */
    buf[1] = 0xBAD0BAD0u;          /* se salta */
    buf[2] = inc(1, 0x0200, 1);
    buf[3] = 0x02800000u; /* clip horizontal: ancho 640 */
    memset(&st, 0, sizeof st);
    memset(&s, 0, sizeof s);
    /* PUT a mitad del comando: la cabecera llega en un avance y el dato en otro. */
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
    /* RETURN sin CALL y cabecera inválida: se registran y se sigue. */
    get = pb_run(&st, (const uint8_t *)buf, sizeof buf, 0, 8, 1000, &k_cb, &s);
    CHECK(get == 8 && st.errors == 2);
    /* CALL dentro de CALL: error y parada en la segunda. */
    get = pb_run(&st, (const uint8_t *)buf, sizeof buf, 8, 0x10, 1000, &k_cb, &s);
    CHECK(st.in_subroutine && st.errors == 3 && get == 0x0C);
    /* GET fuera del objeto DMA. */
    memset(&st, 0, sizeof st);
    get = pb_run(&st, (const uint8_t *)buf, sizeof buf, 0x100, 0x104, 1000, &k_cb, &s);
    CHECK(get == 0x100 && st.errors == 1);
}

static void test_max_words(void) {
    uint32_t buf[] = {0x0u | 1u}; /* JMP 0: bucle infinito */
    pb_state_t st;
    sink_t s;
    uint32_t get;
    memset(&st, 0, sizeof st);
    memset(&s, 0, sizeof s);
    get = pb_run(&st, (const uint8_t *)buf, sizeof buf, 0, 4, 50, &k_cb, &s);
    CHECK(get == 0 && st.words == 50);
}

int main(void) {
    test_increasing_methods();
    test_non_increasing_and_subchannel();
    test_jump_call_return();
    test_old_jump_and_split_command();
    test_errors();
    test_max_words();
    if (g_failures) {
        fprintf(stderr, "pushbuf_test: %d fallos\n", g_failures);
        return 1;
    }
    printf("pushbuf_test: OK (6 pruebas)\n");
    return 0;
}
