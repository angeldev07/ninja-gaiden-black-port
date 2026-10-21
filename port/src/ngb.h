/*
 * ngb.h: API común del loader nativo (ngb-core.dll).
 */
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "xbe.h"

/* Rango que reserva el exe (loader_stub.c): de la base del XBE hasta NGB_RESERVE_END. */
#define NGB_RESERVE_BASE 0x00010000u
#define NGB_RESERVE_END 0x01000000u
/* Cabecera PE sombra del exe (memory.c): primera página tras la reserva, dentro de su BSS. */
#define NGB_PE_SHADOW NGB_RESERVE_END

/* Arena de memoria contigua: en Xbox, MmAllocateContiguousMemory devuelve 0x8xxxxxxx. */
#define NGB_CONTIG_BASE 0x80000000u
#define NGB_CONTIG_SIZE 0x04000000u /* 64 MB, la RAM de una Xbox retail */

/* Códigos de salida del proceso (y motivo de parada en el log). */
enum ngb_exit {
    NGB_EXIT_OK = 0,           /* el entry point y todos los hilos terminaron */
    NGB_EXIT_USAGE = 1,        /* argumentos o XBE inválidos */
    NGB_EXIT_UNIMPLEMENTED = 2,/* llamada a un export sin implementar */
    NGB_EXIT_CRASH = 3,        /* excepción (incluye MMIO y E/S de puertos) */
    NGB_EXIT_MAX_CALLS = 4,    /* --max-calls alcanzado */
    NGB_EXIT_TIMEOUT = 5,      /* --timeout o --idle sin llamadas nuevas */
    NGB_EXIT_GUEST = 6,        /* el juego pidió parar (HalReturnToFirmware, KeBugCheck) */
};

typedef struct ngb_options {
    const char *xbe_path;
    char log_path[MAX_PATH];
    unsigned long max_calls; /* 0 = sin límite */
    unsigned timeout_s;      /* tiempo total máximo */
    unsigned idle_s;         /* segundos sin llamadas al kernel antes de parar */
    int lenient;             /* 1: una llamada sin implementar devuelve STATUS_SUCCESS */
    int echo;                /* 1: copia cada línea del log a stderr */
    int mmio_strict;         /* 1: cualquier MMIO o E/S de puertos para la ejecución */
    int gpu_ram;             /* 1: el NV2A también se traza con RAM a cero (más allá de H6) */
} ngb_options_t;

extern ngb_options_t g_ngb_opts;
extern xbe_t g_ngb_xbe;

/* log.c */
int ngb_log_open(const char *path);
void ngb_log(const char *fmt, ...);
void ngb_logv(const char *fmt, va_list ap);
void ngb_log_raw(const char *text); /* sin prefijo ni salto de línea añadido */
void ngb_log_flush(void);
/* Termina el proceso: escribe el motivo, el resumen y cierra el log. No vuelve. */
__declspec(noreturn) void ngb_stop(int code, const char *fmt, ...);
int ngb_stopping(void);
double ngb_elapsed(void);

/* memory.c */
int ngb_memory_init(void);
int ngb_memory_map_xbe(const xbe_t *xbe);
int ngb_memory_patch_thunk(const xbe_t *xbe);
void *ngb_contig_alloc(uint32_t size, uint32_t align, uint32_t lowest, uint32_t highest);
int ngb_contig_free(void *p);
uint32_t ngb_contig_size(const void *p);
int ngb_contig_contains(const void *p);

/* thread.c: hilos Xbox sobre hilos Win32 (KPCR/KTHREAD falsos, pila propia, TLS) */
#define NGB_KTHREAD_SIZE 0x200
#define NGB_KPCR_SIZE 0x400

typedef struct ngb_thread {
    struct ngb_thread *next;
    uint8_t *kpcr;    /* KPCR falso (0x28 = KPRCB, cuyo primer campo es CurrentThread) */
    uint8_t *ethread; /* ETHREAD falso (KTHREAD en 0, TlsData en 0x28, UniqueThread 0x12C) */
    HANDLE handle;
    DWORD tid;
    int index; /* T1, T2... en el log */
    uint32_t start_routine, start_context, system_routine;
    uint32_t stack_size, tls_size;
    int is_entry; /* hilo que ejecuta el entry point del XBE */
    int internal; /* hilo propio del loader (DPC): no cuenta para "todos terminaron" */
    volatile LONG exited;
    uint32_t inflight_ordinal; /* llamada al kernel en curso (0 = ninguna) */
    uint32_t inflight_caller;
    uint32_t irql;
    void *xbox_stack;
    uint32_t saved_stack_base, saved_stack_limit, saved_seh;
} ngb_thread_t;

ngb_thread_t *ngb_thread_spawn(uint32_t start_routine, uint32_t start_context,
                               uint32_t system_routine, uint32_t stack_size,
                               uint32_t tls_size, int suspended, int is_entry);
ngb_thread_t *ngb_thread_current(void); /* NULL si el hilo no es Xbox */
ngb_thread_t *ngb_thread_from_ethread(uint32_t ethread);
ngb_thread_t *ngb_thread_from_handle(HANDLE h);
__declspec(noreturn) void ngb_thread_exit(uint32_t status);
int ngb_thread_alive_count(void);
void ngb_thread_dump_all(void); /* EIP de cada hilo Xbox vivo, al log */
uint32_t ngb_tls_total_size(const xbe_t *xbe);

/* crash.c */
void ngb_crash_install(void);

/* Utilidades */
void ngb_hexdump_line(char *out, size_t outlen, uint32_t va, uint32_t len);
