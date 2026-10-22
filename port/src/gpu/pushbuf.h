/*
 * pushbuf.h: NV2A push buffer decoder (the PFIFO "pusher" in DMA mode).
 *
 * D3D writes commands to RAM and advances DMA_PUT; the GPU reads them from DMA_GET. Each
 * command is a header followed by N data words (format from xemu
 * hw/xbox/nv2a/pfifo.c, pfifo_run_pusher, and from nxdk pbkit):
 *
 *   (w & 0xE0000003) == 0x20000000  old jump:     GET = w & 0x1FFFFFFF
 *   (w & 3) == 1                    jump:          GET = w & ~3
 *   (w & 3) == 2                    call:          return = GET; GET = w & ~3
 *   w == 0x00020000                 return from the call
 *   (w & 0xE0030003) == 0           increasing methods: method w & 0x1FFC,
 *                                   subchannel (w >> 13) & 7, count (w >> 18) & 0x7FF
 *   (w & 0xE0030003) == 0x40000000  non-increasing methods (all words to the same one)
 *
 * The decoder is pure: it knows nothing about Windows or the GPU; it is tested with
 * hand-made buffers (port/tests/pushbuf_test.c).
 */
#pragma once

#include <stdint.h>

enum pb_event {
    PB_EVENT_JUMP = 1,
    PB_EVENT_OLD_JUMP = 2,
    PB_EVENT_CALL = 3,
    PB_EVENT_RETURN = 4,
    PB_EVENT_ERROR = 5, /* invalid header, nested CALL, RETURN without CALL, out of bounds */
};

typedef struct pb_state {
    uint32_t method;     /* current method (byte offset) */
    uint32_t subchannel; /* 0..7 */
    uint32_t count;      /* data words left in the current command */
    int non_increasing;
    int in_subroutine;
    uint32_t return_get; /* GET the RETURN goes back to */
    uint32_t words;      /* total words consumed */
    uint32_t errors;
    volatile uint32_t cur; /* current GET (next word to read), for other threads */
} pb_state_t;

typedef struct pb_callbacks {
    /* One data word for `method` of the subchannel. */
    void (*method)(void *user, uint32_t subchannel, uint32_t method, uint32_t param,
                   int non_increasing);
    /* Jumps, calls, returns and errors (for the log); may be NULL. */
    void (*event)(void *user, int kind, uint32_t at, uint32_t word, uint32_t target);
    /* Optional: 0 if the 4 KB page at `offset` cannot be read (unreserved host memory).
     * Asked on entering each page; if it says 0, error and stop. */
    int (*readable)(void *user, uint32_t offset);
    /* Optional: `n` consecutive words of a non-increasing method (vertex data), all at
     * once and without crossing a page. Returns 0 if it does not serve them: then they go
     * word by word through `method`. */
    int (*bulk)(void *user, uint32_t subchannel, uint32_t method, const uint32_t *words, uint32_t n);
} pb_callbacks_t;

/*
 * Consumes commands from `get` to `put` (offsets inside the DMA object, which starts at
 * `base` and is `limit` bytes long). Returns the final GET: `put` if it got there, or the
 * offset where it stopped (limit error or `max_words` exhausted). State persists between
 * calls, so a command may be split across two PUT advances.
 */
uint32_t pb_run(pb_state_t *st, const uint8_t *base, uint32_t limit, uint32_t get, uint32_t put,
                uint32_t max_words, const pb_callbacks_t *cb, void *user);
