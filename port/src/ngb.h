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
/* Ventana "física" de la imagen del XBE (MmGetPhysicalAddress y lecturas de la GPU). */
#define NGB_PHYS_XBE 0x06000000u

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
    unsigned pgraph_frames;  /* frames con todos los métodos en el log de PGRAPH */
    unsigned pgraph_at;      /* además, 3 frames completos a partir de este (0 = no) */
    unsigned long log_cap;   /* líneas de log por función del kernel (0 = sin límite) */
    const char *symbols;     /* CSV de símbolos (opcional) */
    int window;              /* 1: abre la ventana del presentador (gfx/present.c, H7) */
    int no_pacing;           /* 1: FLIP_STALL no espera al siguiente 1/60 s (H7b) */
    const char *input_script; /* pulsaciones automáticas (input/input.h), opcional */
    int no_host_input;        /* 1: ni mando de PC ni teclado, solo el guion */
    const char *watch;        /* --watch: direcciones del juego a vigilar (watch.c) */
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
/* watch.c: --watch ADDR[:1|2|4],... (los cambios van al log desde el hilo de DPC) */
int ngb_watch_parse(const char *spec);
void ngb_watch_tick(void);

/* memory.c */
int ngb_memory_init(void);
int ngb_memory_map_xbe(const xbe_t *xbe);
int ngb_memory_patch_thunk(const xbe_t *xbe);
int ngb_memory_patch_irql(const xbe_t *xbe);
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
    volatile LONG in_kernel; /* >0: dentro del kernel emulado o del VEH (locks propios) */
    /* Suspensión de NtSuspendThread (ver ngb_thread_suspend). Protegido por susp_lock:
     *   susp_park: suspensiones que se aplican aparcando el hilo en resume_evt al salir del
     *              kernel (no se puede congelar con un lock del loader tomado);
     *   susp_host: SuspendThread del host aplicados (el hilo estaba en código del juego).
     * El hilo corre solo si las dos cuentas son 0. */
    CRITICAL_SECTION susp_lock;
    HANDLE resume_evt; /* manual: se resetea bajo susp_lock antes de esperar */
    LONG susp_park, susp_host;
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
/* Entrada y salida del código propio (kernel, VEH): la salida aplica una suspensión
 * aplazada. Así NtSuspendThread nunca congela un hilo con un lock del loader tomado. */
void ngb_kernel_enter(ngb_thread_t *t);
void ngb_kernel_leave(ngb_thread_t *t);
/* NtSuspendThread/NtResumeThread sobre un hilo Xbox. Devuelven la cuenta previa, o -1 si
 * el host falla. */
LONG ngb_thread_suspend(ngb_thread_t *t);
LONG ngb_thread_resume(ngb_thread_t *t);
ngb_thread_t *ngb_thread_from_ethread(uint32_t ethread);
ngb_thread_t *ngb_thread_from_handle(HANDLE h);
__declspec(noreturn) void ngb_thread_exit(uint32_t status);
int ngb_thread_alive_count(void);
void ngb_thread_dump_all(void); /* EIP de cada hilo Xbox vivo, al log */
uint32_t ngb_tls_total_size(const xbe_t *xbe);

/* crash.c */
void ngb_crash_install(void);
void ngb_apu_tick(void);    /* DSP falso y procesador de voces (lo llama el hilo de DPC) */
void ngb_apu_summary(void); /* contadores del procesador de voces, al log */

/* symbols.c: nombres de funciones del XBE (--symbols, CSV de Ghidra en NGB_DATA) */
int ngb_symbols_load(const char *path);
/* " <Nombre+0xNN>" o "" si no hay símbolos cerca. */
const char *ngb_symbol(uint32_t addr, char *buf, size_t buflen);

/* Utilidades */
void ngb_hexdump_line(char *out, size_t outlen, uint32_t va, uint32_t len);
