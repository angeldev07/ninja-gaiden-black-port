/*
 * ngb.h: common API of the native loader (ngb-core.dll).
 */
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "xbe.h"

/* Range reserved by the exe (loader_stub.c): from the XBE base up to NGB_RESERVE_END. */
#define NGB_RESERVE_BASE 0x00010000u
#define NGB_RESERVE_END 0x01000000u
/* Shadow PE header of the exe (memory.c): first page after the reservation, inside its BSS. */
#define NGB_PE_SHADOW NGB_RESERVE_END

/* Contiguous memory arena: on Xbox, MmAllocateContiguousMemory returns 0x8xxxxxxx. */
#define NGB_CONTIG_BASE 0x80000000u
#define NGB_CONTIG_SIZE 0x04000000u /* 64 MB, the RAM of a retail Xbox */
/* "Physical" window of the XBE image (MmGetPhysicalAddress and GPU reads). */
#define NGB_PHYS_XBE 0x06000000u

/* Process exit codes (and the stop reason in the log). */
enum ngb_exit {
    NGB_EXIT_OK = 0,           /* the entry point and all threads finished */
    NGB_EXIT_USAGE = 1,        /* invalid arguments or XBE */
    NGB_EXIT_UNIMPLEMENTED = 2,/* call to an unimplemented export */
    NGB_EXIT_CRASH = 3,        /* exception (includes MMIO and port I/O) */
    NGB_EXIT_MAX_CALLS = 4,    /* --max-calls reached */
    NGB_EXIT_TIMEOUT = 5,      /* --timeout or --idle without new calls */
    NGB_EXIT_GUEST = 6,        /* the game asked to stop (HalReturnToFirmware, KeBugCheck) */
};

typedef struct ngb_options {
    const char *xbe_path;
    char log_path[MAX_PATH];
    unsigned long max_calls; /* 0 = unlimited */
    unsigned timeout_s;      /* maximum total time */
    unsigned idle_s;         /* seconds without kernel calls before stopping */
    int lenient;             /* 1: an unimplemented call returns STATUS_SUCCESS */
    int echo;                /* 1: copy every log line to stderr */
    int mmio_strict;         /* 1: any MMIO or port I/O stops the run */
    int gpu_ram;             /* 1: the NV2A is also traced with zeroed RAM (beyond H6) */
    unsigned pgraph_frames;  /* frames with all methods in the PGRAPH log */
    unsigned pgraph_at;      /* plus, 3 full frames starting at this one (0 = no) */
    unsigned long log_cap;   /* log lines per kernel function (0 = unlimited) */
    int log_minimal;         /* --log-detail minimal: no per-call lines (logfilter.h) */
    const char *symbols;     /* symbols CSV (optional) */
    int window;              /* 1: opens the presenter window (gfx/present.c, H7); 2: hidden */
    int no_pacing;           /* 1: FLIP_STALL does not wait for the next 1/60 s (H7b) */
    int renderer;            /* NGB_RENDERER_*: --renderer gl|soft (H7c) */
    const char *input_script; /* automatic presses (input/input.h), optional */
    int no_host_input;        /* 1: neither PC gamepad nor keyboard, only the script */
    int ach_demo;             /* --ach-demo: a test achievement 5 s after start (ach/ach.c) */
    const char *watch;        /* --watch: game addresses to watch (watch.c) */
    int mute;                 /* --mute: no audio output (the VP keeps advancing) */
    int audio_wav;            /* --audio-wav: saves the mix to <log>-audio.wav */
    unsigned rdoc_frame;      /* --rdoc-frame N: captures that frame with RenderDoc (gpu/rdoc.c) */
    int rdoc_key;             /* --rdoc-key: F11 captures the next frame with RenderDoc */
    unsigned debug_shots;     /* --debug-shots N: PNG every N frames + texture dumps (0 = off) */
    float scale;              /* --scale N: GL render surfaces at N times 720x480 (1-4, may be fractional; 0 = 1) */
    int scale_auto;           /* --scale auto: N = height of the window's monitor / 480 */
    int fullscreen;           /* --fullscreen: borderless over the whole monitor (Alt+Enter toggles) */
    int widescreen;           /* --widescreen: the console reports a 16:9 TV, the game renders anamorphic 16:9 */
    const char *dump_ram;    /* --dump-ram FRAMES: guest RAM snapshots at those frames (F10 too) */
    int profile;              /* --profile: sampling profiler, report in <log>-profile.txt (prof.c) */
} ngb_options_t;

/* --renderer: AUTO = OpenGL if there is a window and a 4.x context; otherwise the rasterizer. */
#define NGB_RENDERER_AUTO 0
#define NGB_RENDERER_SOFT 1
#define NGB_RENDERER_GL 2

extern ngb_options_t g_ngb_opts;
extern xbe_t g_ngb_xbe;

/* log.c */
int ngb_log_open(const char *path);
void ngb_log(const char *fmt, ...);
void ngb_logv(const char *fmt, va_list ap);
void ngb_log_raw(const char *text); /* no prefix and no newline added */
void ngb_log_flush(void);
/* Ends the process: writes the reason and the summary and closes the log. Does not return. */
__declspec(noreturn) void ngb_stop(int code, const char *fmt, ...);
int ngb_stopping(void);
double ngb_elapsed(void);
/* watch.c: --watch ADDR[:1|2|4],... (changes go to the log from the DPC thread) */
int ngb_watch_parse(const char *spec);
void ngb_watch_tick(void);
/* prof.c: --profile. Threads are registered as they are created; the report is written at the stop. */
void ngb_prof_start(void);
void ngb_prof_thread(HANDLE h, const char *name);
void ngb_prof_report(void);

/* Cheap measurements (H7c): QueryPerformanceCounter ticks spent writing the log and number of
 * kernel calls so far. */
long long ngb_log_ticks(void);
long ngb_kernel_calls(void);
/* Time the Xbox threads spent inside kernel calls since the previous call: the total and the
 * five costliest thread, ordinal and caller triples, in ms per frame over `frames` frames. */
void ngb_kernel_time_take(char *out, size_t outlen, uint32_t frames);
/* Waits on the KEVENT at `event` since the previous call with the same event. */
long ngb_ke_waits_take(uint32_t event);
/* Threads blocked right now in KeWaitForSingleObject on the KEVENT given to ngb_ke_watch_waiters. */
void ngb_ke_watch_waiters(uint32_t event);
long ngb_ke_waiters(void);

/* memory.c */
int ngb_memory_init(void);
int ngb_memory_map_xbe(const xbe_t *xbe);
int ngb_memory_patch_thunk(const xbe_t *xbe);
int ngb_memory_patch_irql(const xbe_t *xbe);
void *ngb_contig_alloc(uint32_t size, uint32_t align, uint32_t lowest, uint32_t highest);
int ngb_contig_free(void *p);
uint32_t ngb_contig_size(const void *p);
int ngb_contig_contains(const void *p);
/* End of the committed part of the arena, as an offset from its base. */
uint32_t ngb_contig_committed(void);
size_t ngb_contig_written(uint32_t *phys, size_t cap);
/* Bumped when the game may have made memory unreadable (MmSetAddressProtect,
 * NtFreeVirtualMemory, MmDeleteKernelStack): caches of readable pages start over (pgraph.c,
 * on_readable). */
void ngb_mem_access_changed(void);
long ngb_mem_access_gen(void);
/* Writes every committed, writable private region below 2 GB plus the contiguous RAM to
 * `path` as {u32 va, u32 len, bytes} records. A development tool to diff game state. */
int ngb_dump_guest_ram(const char *path);

/* thread.c: Xbox threads on top of Win32 threads (fake KPCR/KTHREAD, own stack, TLS) */
#define NGB_KTHREAD_SIZE 0x200
#define NGB_KPCR_SIZE 0x400

typedef struct ngb_thread {
    struct ngb_thread *next;
    uint8_t *kpcr;    /* fake KPCR (0x28 = KPRCB, whose first field is CurrentThread) */
    uint8_t *ethread; /* fake ETHREAD (KTHREAD at 0, TlsData at 0x28, UniqueThread 0x12C) */
    HANDLE handle;
    DWORD tid;
    int index; /* T1, T2... in the log */
    uint32_t start_routine, start_context, system_routine;
    uint32_t stack_size, tls_size;
    int is_entry; /* thread that runs the XBE entry point */
    int internal; /* loader-owned thread (DPC): does not count for "all finished" */
    volatile LONG exited;
    volatile LONG in_kernel; /* >0: inside the emulated kernel or the VEH (own locks) */
    /* NtSuspendThread suspension (see ngb_thread_suspend). Protected by susp_lock:
     *   susp_park: suspensions applied by parking the thread on resume_evt when it leaves the
     *              kernel (a thread cannot be frozen while holding a loader lock);
     *   susp_host: host SuspendThread calls applied (the thread was in game code).
     * The thread runs only if both counts are 0. */
    CRITICAL_SECTION susp_lock;
    HANDLE resume_evt; /* manual: reset under susp_lock before waiting */
    LONG susp_park, susp_host;
    uint32_t inflight_ordinal; /* kernel call in progress (0 = none) */
    uint32_t inflight_caller;
    uint32_t irql;
    int32_t base_prio; /* KeSetBasePriorityThread increment (0 = normal) */
    void *xbox_stack;
    uint32_t saved_stack_base, saved_stack_limit, saved_seh;
    uint64_t cpu_mark; /* QueryThreadCycleTime at the last ngb_thread_cpu_take */
} ngb_thread_t;

ngb_thread_t *ngb_thread_spawn(uint32_t start_routine, uint32_t start_context,
                               uint32_t system_routine, uint32_t stack_size,
                               uint32_t tls_size, int suspended, int is_entry);
ngb_thread_t *ngb_thread_current(void); /* NULL if the thread is not an Xbox thread */
/* Entry and exit of loader code (kernel, VEH): the exit applies a deferred suspension. That
 * way NtSuspendThread never freezes a thread that holds a loader lock. */
void ngb_kernel_enter(ngb_thread_t *t);
void ngb_kernel_leave(ngb_thread_t *t);
void ngb_ke_set_event(uint32_t event); /* KeSetEvent on a game KEVENT */
/* NtSuspendThread/NtResumeThread on an Xbox thread. They return the previous count, or -1 if
 * the host fails. */
LONG ngb_thread_suspend(ngb_thread_t *t);
LONG ngb_thread_resume(ngb_thread_t *t);
ngb_thread_t *ngb_thread_from_ethread(uint32_t ethread);
/* Moves a loader-owned thread that emulates hardware (GPU, APU) off the Xbox threads' core. */
void ngb_pin_hardware_thread(HANDLE h);
ngb_thread_t *ngb_thread_from_handle(HANDLE h);
__declspec(noreturn) void ngb_thread_exit(uint32_t status);
int ngb_thread_alive_count(void);
void ngb_thread_dump_all(void); /* EIP of every live Xbox thread, to the log */
/* CPU time of the Xbox threads since the previous call, for the perf lines: the total and the
 * three busiest threads, in ms per frame over `frames` frames. Returns the total. */
double ngb_thread_cpu_take(char *out, size_t outlen, uint32_t frames);
uint32_t ngb_tls_total_size(const xbe_t *xbe);

/* crash.c */
void ngb_crash_install(void);
void ngb_apu_tick(void);    /* fake DSP and voice processor (called by the DPC thread) */
void ngb_apu_summary(void); /* voice processor counters, to the log */
/* Delivers the pending free voices: for each one it sets up the trap and calls isr(ctx). */
void ngb_apu_interrupt(void (*isr)(void *ctx), void *ctx);
int ngb_apu_interrupt_pending(void); /* 1 if there are free voices to deliver */

/* symbols.c: XBE function names (--symbols, Ghidra CSV in NGB_DATA) */
int ngb_symbols_load(const char *path);
/* " <Name+0xNN>" or "" if there are no symbols nearby. */
const char *ngb_symbol(uint32_t addr, char *buf, size_t buflen);

/* Utilities */
void ngb_hexdump_line(char *out, size_t outlen, uint32_t va, uint32_t len);
