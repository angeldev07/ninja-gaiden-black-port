/*
 * pgraph_gl.h: render acelerado de PGRAPH con OpenGL 4.x (H7c, ADR-06).
 *
 * pgraph.c sigue interpretando el push buffer y guarda el estado NV097 en `pgl_regs`; con
 * `--renderer gl` las operaciones que tocan píxeles (CLEAR_SURFACE, el dibujo al cerrar un
 * BEGIN_END y FLIP_STALL) van aquí en vez de al rasterizador por software.
 *
 * Hilos: todo GL vive en un hilo de render propio que tiene el contexto actual. pgraph_kick
 * le pasa el kick entero con pgl_call() y espera a que termine (el kick sigue siendo
 * síncrono, como espera D3D). Ver el porqué en pgraph_gl.c.
 */
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stddef.h>
#include <stdint.h>

#define PGL_PROGRAM_SLOTS 136
#define PGL_CONSTANTS 192

/* Estado de PGRAPH que necesita el render. Lo escribe pgraph.c al decodificar. */
typedef struct pgl_regs {
    uint32_t m[0x2000 / 4];                    /* último parámetro de cada método de la clase 3D */
    uint32_t program[PGL_PROGRAM_SLOTS * 4];   /* memoria de vertex programs (0x0B00) */
    uint32_t prog_load;                        /* en palabras (SET_TRANSFORM_PROGRAM_LOAD x 4) */
    float constants[PGL_CONSTANTS * 4];        /* c[0..191] (0x0B80) */
    uint32_t const_load;                       /* en palabras (SET_TRANSFORM_CONSTANT_LOAD x 4) */
    uint32_t const_serial;                     /* cambia con cada escritura de constantes */
    float vconst[16][4];                       /* atributos sin array: SET_VERTEX_DATA* */
} pgl_regs;

/* Una primitiva cerrada con SET_BEGIN_END(0). */
typedef struct pgl_draw {
    uint32_t prim;                 /* 1 POINTS ... 10 POLYGON */
    const uint32_t *inline_words;  /* INLINE_ARRAY (NULL si va por arrays) */
    uint32_t n_inline;
    const uint32_t *indices;       /* ARRAY_ELEMENT16/32 y DRAW_ARRAYS, ya como índices */
    uint32_t n_indices;
} pgl_draw_t;

/* Valores de reset de los métodos de estado (los de nv2a_state_defaults). */
void pgl_regs_defaults(pgl_regs *r);

/* Arranca el hilo de render con un contexto sobre `hwnd` (la ventana del presentador).
 * 0 si hay GL; si no, un código negativo y el motivo en `why`. */
int pgl_start(HWND hwnd, char *why, size_t whylen);
int pgl_active(void);

/* Ejecuta fn(arg) en el hilo de render y espera a que vuelva. */
void pgl_call(void (*fn)(void *), void *arg);

/* Desde el hilo de render (dentro de pgl_call). */
void pgl_clear(const pgl_regs *r, uint32_t flags);
void pgl_draw(const pgl_regs *r, const pgl_draw_t *d);
/* Presenta el destino de color de 720 de ancho que hay al llegar FLIP_STALL (D3D cambia al
 * siguiente back buffer después) o, si no lo es, el último de 720 en el que se dibujó; con
 * `shot_path`, la guarda en PNG
 * leyendo la textura de la GPU. 0 si guardó la captura. */
int pgl_flip(const pgl_regs *r, const char *shot_path);
/* Estadísticas al log (sin GL: se puede llamar desde cualquier hilo). */
void pgl_summary(void);

/* pgraph.c: física base del objeto DMA puesto con SET_CONTEXT_DMA_* (método 0x180-0x1FC). */
int pgraph_dma_base(uint32_t method, uint32_t *phys);
