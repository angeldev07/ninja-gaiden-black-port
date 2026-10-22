/*
 * emit.c: bounded writing of GLSL text (see emit.h).
 */
#include "emit.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>

void glsl_buf_init(glsl_buf *b, char *out, size_t cap) {
    b->out = out;
    b->cap = out ? cap : 0;
    b->len = 0;
    b->overflow = b->cap == 0;
    if (b->cap) {
        b->out[0] = 0;
    }
}

void glsl_emit(glsl_buf *b, const char *fmt, ...) {
    va_list args;
    int n;
    size_t room;
    if (b->overflow) {
        return;
    }
    room = b->cap - b->len;
    va_start(args, fmt);
    n = vsnprintf(b->out + b->len, room, fmt, args);
    va_end(args);
    if (n < 0 || (size_t)n >= room) {
        b->overflow = 1;
        b->out[b->len] = 0; /* undoes the partial write */
        return;
    }
    b->len += (size_t)n;
}

int glsl_buf_result(const glsl_buf *b) {
    if (b->overflow || b->len > (size_t)INT_MAX) {
        return -1;
    }
    return (int)b->len;
}
