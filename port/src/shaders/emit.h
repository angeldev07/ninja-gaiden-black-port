/*
 * emit.h: escritura acotada de texto GLSL en un buffer del llamante.
 *
 * Lo comparten vsh.c y psh.c. `glsl_emit` añade texto con formato printf; si no cabe,
 * marca `overflow` y deja de escribir (el buffer siempre queda terminado en NUL).
 */
#pragma once

#include <stddef.h>

typedef struct glsl_buf {
    char *out;
    size_t cap;   /* bytes de `out`, incluido el NUL final */
    size_t len;   /* caracteres escritos, sin el NUL */
    int overflow; /* 1 si algo no cupo */
} glsl_buf;

/* Prepara el buffer; `out` puede ser NULL solo si `cap` es 0 (todo será overflow). */
void glsl_buf_init(glsl_buf *b, char *out, size_t cap);

/* Añade texto con formato printf. */
void glsl_emit(glsl_buf *b, const char *fmt, ...);

/* Longitud escrita, o -1 si hubo overflow. */
int glsl_buf_result(const glsl_buf *b);
