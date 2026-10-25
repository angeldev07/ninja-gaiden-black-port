/*
 * pgraph_gl.h: PGRAPH accelerated render with OpenGL 4.x (H7c, ADR-06).
 *
 * pgraph.c keeps interpreting the push buffer and stores the NV097 state in `pgl_regs`; with
 * `--renderer gl` the operations that touch pixels (CLEAR_SURFACE, the draw when closing a
 * BEGIN_END, and FLIP_STALL) go here instead of to the software rasterizer.
 *
 * Threads: all GL lives on its own render thread, which holds the current context. That thread
 * is also the pusher: pgraph_kick notifies it with pgl_post() and returns right away, and the
 * thread consumes the push buffer in parallel with the game, like the console's GPU.
 */
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stddef.h>
#include <stdint.h>

#define PGL_PROGRAM_SLOTS 136
#define PGL_CONSTANTS 192
#define PGL_BLOCK 0x40

/* PGRAPH state the render needs. Written by pgraph.c while decoding. */
typedef struct pgl_regs {
    uint32_t m[0x2000 / 4];                    /* last parameter of each 3D class method */
    uint32_t program[PGL_PROGRAM_SLOTS * 4];   /* vertex program memory (0x0B00) */
    uint32_t prog_load;                        /* in words (SET_TRANSFORM_PROGRAM_LOAD x 4) */
    float constants[PGL_CONSTANTS * 4];        /* c[0..191] (0x0B80) */
    uint32_t const_load;                       /* in words (SET_TRANSFORM_CONSTANT_LOAD x 4) */
    float vconst[16][4];                       /* attributes without an array: SET_VERTEX_DATA* */
    /* Change stamps to upload only the uniforms that change: `stamp` increases every time a
     * method or a constant changes value, and each block of 16 methods (PGL_BLOCK bytes) and
     * each constant keep the stamp of their last change. */
    uint64_t stamp; /* 64 bits: at thousands of changes per frame, 32 would wrap around in minutes */
    uint64_t blk_stamp[0x2000 / PGL_BLOCK];
    uint64_t const_stamp[PGL_CONSTANTS];
} pgl_regs;

/* A primitive closed with SET_BEGIN_END(0). */
typedef struct pgl_draw {
    uint32_t prim;                 /* 1 POINTS ... 10 POLYGON */
    const uint32_t *inline_words;  /* INLINE_ARRAY (NULL if it goes through arrays) */
    uint32_t n_inline;
    const uint32_t *indices;       /* ARRAY_ELEMENT16/32 and DRAW_ARRAYS, already as indices */
    uint32_t n_indices;
} pgl_draw_t;

/* pgl_track.c: stores one 3D class method in `r` (programs, constants, constant attributes and
 * the transform constants that alias fixed-function registers, like the viewport). */
void pgl_regs_track(pgl_regs *r, uint32_t method, uint32_t param);

/* Reset values of the state methods (those of nv2a_state_defaults). */
void pgl_regs_defaults(pgl_regs *r);

/* Starts the render thread with a context on `hwnd` (the presenter's window).
 * 0 if there is GL; otherwise a negative code and the reason in `why`. */
int pgl_start(HWND hwnd, char *why, size_t whylen);
int pgl_active(void);

/* Runs fn(arg) on the render thread and waits for it to return. */
void pgl_call(void (*fn)(void *), void *arg);
/* Asks the render thread for fn(arg) without waiting. If it is already running, it calls it again on
 * finishing (the event stays signaled): fn must be idempotent and look at its own queue. */
void pgl_post(void (*fn)(void *), void *arg);

/* From the render thread (inside pgl_call). */
/* At the start of each kick: collects the pages the CPU wrote since the previous one. */
void pgl_kick_begin(void);
void pgl_clear(const pgl_regs *r, uint32_t flags);
void pgl_draw(const pgl_regs *r, const pgl_draw_t *d);
/* Presents the 720-wide color target that is current when FLIP_STALL arrives (D3D switches to the
 * next back buffer afterwards) or, if it is not, the last 720-wide one drawn into; with
 * `shot_path`, saves it as PNG
 * by reading the GPU texture. 0 if the capture was saved. */
int pgl_flip(const pgl_regs *r, const char *shot_path);
/* Breakdown of the draw time since the last call (QPC), in ms per frame, and the work
 * counters per frame; zeroes the accumulators. From the render thread. */
void pgl_perf_take(char *out, size_t outlen, uint32_t frames);
/* Statistics to the log (no GL: can be called from any thread). */
void pgl_summary(void);

/* pgraph.c: base physical address of the DMA object set with SET_CONTEXT_DMA_* (method 0x180-0x1FC). */
int pgraph_dma_base(uint32_t method, uint32_t *phys);
