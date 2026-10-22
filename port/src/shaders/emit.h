/*
 * emit.h: bounded writing of GLSL text into a caller-owned buffer.
 *
 * Shared by vsh.c and psh.c. `glsl_emit` appends printf-formatted text; if it does not fit,
 * it sets `overflow` and stops writing (the buffer always stays NUL-terminated).
 */
#pragma once

#include <stddef.h>

typedef struct glsl_buf {
    char *out;
    size_t cap;   /* bytes of `out`, including the final NUL */
    size_t len;   /* characters written, without the NUL */
    int overflow; /* 1 if something did not fit */
} glsl_buf;

/* Prepares the buffer; `out` may be NULL only if `cap` is 0 (everything will overflow). */
void glsl_buf_init(glsl_buf *b, char *out, size_t cap);

/* Appends printf-formatted text. */
void glsl_emit(glsl_buf *b, const char *fmt, ...);

/* Length written, or -1 if there was an overflow. */
int glsl_buf_result(const glsl_buf *b);
