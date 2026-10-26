/*
 * nv2a.h: minimal model of the NV2A GPU (H7a): MMIO registers, PRAMIN and the command FIFO.
 *
 * It draws nothing: it serves the registers D3D reads and writes during initialization
 * with consistent values (identification, timers, "idle" states, flushes that finish
 * on their own), decodes the push buffer every time DMA_PUT advances (pgraph.c logs it to
 * a separate log) and synthesizes the vblank so D3D does not wait forever.
 */
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>

#define NV2A_MMIO_BASE 0xFD000000u
#define NV2A_MMIO_SIZE 0x01000000u
#define NV2A_PRAMIN 0x00700000u /* window onto the instance RAM (last MB of RAM) */
#define NV2A_PRAMIN_SIZE 0x00100000u
#define NV2A_USER 0x00800000u
#define NV2A_RAMIN_PHYS 0x03F00000u /* physical: 64 MB - 1 MB */

/* Sets up the reservations (MMIO, PRAMIN alias) and the PGRAPH log. */
int nv2a_init(const char *pgraph_log_path);

/* VEH: access to [0xFD000000, 0xFE000000). Returns EXCEPTION_CONTINUE_EXECUTION if it
 * serves it (leaves TF set for nv2a_single_step) or EXCEPTION_CONTINUE_SEARCH if not. */
LONG nv2a_mmio_fault(EXCEPTION_RECORD *rec, CONTEXT *ctx);
/* VEH: EXCEPTION_SINGLE_STEP after an access. 1 if it was ours. */
int nv2a_single_step(CONTEXT *ctx);

/* Synthetic vblank (called by the DPC thread at 60 Hz): 1 if the ISR must be called. */
int nv2a_vblank(void);
int nv2a_vblank_enabled(void);
uint32_t nv2a_vblank_count(void); /* vblanks so far: its parity is the interlaced field */

/* Register (shadow of what was written, or modeled value), offset from 0xFD000000. */
uint32_t nv2a_reg_read(uint32_t off);
/* MMIO accesses served so far (H7c measurements). */
uint32_t nv2a_mmio_count(void);
LONGLONG nv2a_mmio_ticks(void); /* QPC ticks inside MMIO accesses since boot */
LONG nv2a_mmio_fast(void);       /* accesses emulated in one exception (fast_access) */

/* Xbox physical memory (0..64 MB) to process pointer, or NULL. */
uint8_t *nv2a_phys(uint32_t phys, uint32_t len);
uint32_t nv2a_phys_span(uint32_t phys);
/* Instance RAM (RAMIN): offset -> pointer, or NULL. */
uint8_t *nv2a_ramin(uint32_t off, uint32_t len);

/* Register and PGRAPH summary to the main log. */
void nv2a_summary(void);

/* pgraph.c */
int pgraph_init(const char *log_path);
/* Decodes the push buffer from GET to PUT; returns the final GET. */
uint32_t pgraph_kick(uint32_t dma_instance, uint32_t get, uint32_t put);
/* FIFO GET: with the asynchronous pusher (GL render), how far it has really got; otherwise
 * `fallback` (the synchronous model in nv2a.c). Writing GET is only valid with the pusher idle. */
uint32_t pgraph_fifo_get(uint32_t fallback);
void pgraph_fifo_set_get(uint32_t get);
/* PGRAPH_PATT_COLOR0: the last SET_MONOCHROME_COLOR0 of the pattern object (D3D marker). */
uint32_t pgraph_patt_color0(void);
/* 60 Hz pacing pending from the last FLIP_STALL; called with no locks held. */
void pgraph_pace(void);
void pgraph_vblank(uint32_t count);
void pgraph_summary(void);
const char *nv097_method_name(uint32_t method, char *buf, size_t buflen);
