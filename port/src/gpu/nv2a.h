/*
 * nv2a.h: modelo mínimo de la GPU NV2A (H7a): registros MMIO, PRAMIN y el FIFO de comandos.
 *
 * No dibuja nada: atiende los registros que D3D lee y escribe durante su inicialización
 * con valores coherentes (identificación, timers, estados "ocioso", flushes que terminan
 * solos), decodifica el push buffer cada vez que avanza DMA_PUT (pgraph.c lo registra en
 * un log aparte) y sintetiza el vblank para que D3D no espere para siempre.
 */
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>

#define NV2A_MMIO_BASE 0xFD000000u
#define NV2A_MMIO_SIZE 0x01000000u
#define NV2A_PRAMIN 0x00700000u /* ventana a la RAM de instancias (último MB de la RAM) */
#define NV2A_PRAMIN_SIZE 0x00100000u
#define NV2A_USER 0x00800000u
#define NV2A_RAMIN_PHYS 0x03F00000u /* física: 64 MB - 1 MB */

/* Prepara las reservas (MMIO, alias de PRAMIN) y el log de PGRAPH. */
int nv2a_init(const char *pgraph_log_path);

/* VEH: acceso a [0xFD000000, 0xFE000000). Devuelve EXCEPTION_CONTINUE_EXECUTION si lo
 * atiende (deja TF puesto para nv2a_single_step) o EXCEPTION_CONTINUE_SEARCH si no. */
LONG nv2a_mmio_fault(EXCEPTION_RECORD *rec, CONTEXT *ctx);
/* VEH: EXCEPTION_SINGLE_STEP tras un acceso. 1 si era nuestro. */
int nv2a_single_step(CONTEXT *ctx);

/* Vblank sintético (lo llama el hilo de DPC a 60 Hz): 1 si hay que llamar a la ISR. */
int nv2a_vblank(void);
int nv2a_vblank_enabled(void);

/* Registro (sombra de lo escrito o valor modelado), offset desde 0xFD000000. */
uint32_t nv2a_reg_read(uint32_t off);
/* Accesos MMIO atendidos hasta ahora (medidas de H7c). */
uint32_t nv2a_mmio_count(void);

/* Memoria física de la Xbox (0..64 MB) a puntero del proceso, o NULL. */
uint8_t *nv2a_phys(uint32_t phys, uint32_t len);
uint32_t nv2a_phys_span(uint32_t phys);
/* RAM de instancias (RAMIN): offset -> puntero, o NULL. */
uint8_t *nv2a_ramin(uint32_t off, uint32_t len);

/* Resumen de registros y de PGRAPH al log principal. */
void nv2a_summary(void);

/* pgraph.c */
int pgraph_init(const char *log_path);
/* Decodifica el push buffer de GET a PUT; devuelve el GET final. */
uint32_t pgraph_kick(uint32_t dma_instance, uint32_t get, uint32_t put);
/* Pacing de 60 Hz pendiente del último FLIP_STALL; se llama ya sin locks. */
void pgraph_pace(void);
void pgraph_vblank(uint32_t count);
void pgraph_summary(void);
const char *nv097_method_name(uint32_t method, char *buf, size_t buflen);
