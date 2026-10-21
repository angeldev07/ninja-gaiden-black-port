/*
 * pushbuf.h: decodificador del push buffer del NV2A (el "pusher" de PFIFO en modo DMA).
 *
 * D3D escribe comandos en RAM y avanza DMA_PUT; la GPU los lee desde DMA_GET. Cada
 * comando es una cabecera seguida de N palabras de datos (formato de xemu
 * hw/xbox/nv2a/pfifo.c, pfifo_run_pusher, y de nxdk pbkit):
 *
 *   (w & 0xE0000003) == 0x20000000  salto antiguo: GET = w & 0x1FFFFFFF
 *   (w & 3) == 1                    salto:         GET = w & ~3
 *   (w & 3) == 2                    llamada:       retorno = GET; GET = w & ~3
 *   w == 0x00020000                 retorno de la llamada
 *   (w & 0xE0030003) == 0           métodos crecientes: método w & 0x1FFC,
 *                                   subcanal (w >> 13) & 7, cuenta (w >> 18) & 0x7FF
 *   (w & 0xE0030003) == 0x40000000  métodos no crecientes (todas las palabras al mismo)
 *
 * El decodificador es puro: no sabe nada de Windows ni de la GPU; se prueba con buffers
 * hechos a mano (port/tests/pushbuf_test.c).
 */
#pragma once

#include <stdint.h>

enum pb_event {
    PB_EVENT_JUMP = 1,
    PB_EVENT_OLD_JUMP = 2,
    PB_EVENT_CALL = 3,
    PB_EVENT_RETURN = 4,
    PB_EVENT_ERROR = 5, /* cabecera inválida, CALL anidada, RETURN sin CALL, fuera de límite */
};

typedef struct pb_state {
    uint32_t method;     /* método en curso (offset en bytes) */
    uint32_t subchannel; /* 0..7 */
    uint32_t count;      /* palabras de datos que faltan del comando en curso */
    int non_increasing;
    int in_subroutine;
    uint32_t return_get; /* GET al que vuelve el RETURN */
    uint32_t words;      /* palabras consumidas en total */
    uint32_t errors;
} pb_state_t;

typedef struct pb_callbacks {
    /* Una palabra de datos para `method` del subcanal. */
    void (*method)(void *user, uint32_t subchannel, uint32_t method, uint32_t param,
                   int non_increasing);
    /* Saltos, llamadas, retornos y errores (para el log); puede ser NULL. */
    void (*event)(void *user, int kind, uint32_t at, uint32_t word, uint32_t target);
} pb_callbacks_t;

/*
 * Consume comandos desde `get` hasta `put` (offsets dentro del objeto DMA, que empieza en
 * `base` y mide `limit` bytes). Devuelve el GET final: `put` si llegó, o el offset donde
 * paró (error de límite o `max_words` agotado). El estado persiste entre llamadas, así que
 * un comando puede quedar partido entre dos avances de PUT.
 */
uint32_t pb_run(pb_state_t *st, const uint8_t *base, uint32_t limit, uint32_t get, uint32_t put,
                uint32_t max_words, const pb_callbacks_t *cb, void *user);
