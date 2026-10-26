/*
 * log.c: execution log (one line per kernel call) and orderly stop.
 *
 * All writes go through a CRITICAL_SECTION. ngb_stop() writes the reason and the summary
 * and ends the process with TerminateProcess: that way DLL_PROCESS_DETACH does not run while
 * other Xbox threads are still alive (or blocked) halfway through something.
 */
#include "ngb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu/nv2a.h"
#include "input/input.h"
#include "kernel/kernel.h"
#include "logfilter.h"

static CRITICAL_SECTION g_log_lock;
static FILE *g_log_file;
static volatile LONG g_stopping;
static LARGE_INTEGER g_t0, g_freq;
static volatile LONG64 g_log_ticks; /* time inside ngb_log_raw (lock included) */
static char g_home[MAX_PATH];       /* %USERPROFILE%: scrubbed from every line */

long long ngb_log_ticks(void) { return (long long)g_log_ticks; }

int ngb_log_open(const char *path) {
    InitializeCriticalSection(&g_log_lock);
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_t0);
    if (!GetEnvironmentVariableA("USERPROFILE", g_home, sizeof g_home) || strlen(g_home) < 4)
        g_home[0] = 0; /* too short to be a profile folder ("C:\\" would match everything) */
    if (fopen_s(&g_log_file, path, "wb") || !g_log_file)
        return -1;
    /* NGB_LOG_UNBUFFERED=1: every line reaches the disk (to debug deaths without a report). */
    if (getenv("NGB_LOG_UNBUFFERED"))
        setvbuf(g_log_file, NULL, _IONBF, 0);
    else
        setvbuf(g_log_file, NULL, _IOFBF, 1 << 16);
    return 0;
}

double ngb_elapsed(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - g_t0.QuadPart) / (double)g_freq.QuadPart;
}

/* If the thread holding the lock died halfway (crash inside the log), do not wait. */
static int log_lock(void) {
    int tries;
    for (tries = 0; tries < 2000; tries++) {
        if (TryEnterCriticalSection(&g_log_lock))
            return 1;
        Sleep(1);
    }
    return 0;
}

void ngb_log_raw(const char *text) {
    int locked;
    LARGE_INTEGER a, b;
    if (!g_log_file)
        return;
    QueryPerformanceCounter(&a);
    locked = log_lock();
    fputs(text, g_log_file);
    if (g_ngb_opts.echo)
        fputs(text, stderr);
    if (locked)
        LeaveCriticalSection(&g_log_lock);
    QueryPerformanceCounter(&b);
    InterlockedAdd64(&g_log_ticks, b.QuadPart - a.QuadPart);
}

void ngb_logv(const char *fmt, va_list ap) {
    char buf[2048];
    int n = vsnprintf(buf, sizeof buf - 2, fmt, ap);
    if (n < 0)
        n = 0;
    if (n > (int)sizeof buf - 2)
        n = (int)sizeof buf - 2;
    buf[n] = 0;
    if (g_ngb_opts.log_minimal && ngb_logline_is_detail(buf))
        return;
    n = (int)ngb_log_scrub(buf, sizeof buf - 1, g_home);
    buf[n] = '\n';
    buf[n + 1] = 0;
    ngb_log_raw(buf);
}

void ngb_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ngb_logv(fmt, ap);
    va_end(ap);
}

void ngb_log_flush(void) {
    int locked;
    if (!g_log_file)
        return;
    locked = log_lock();
    fflush(g_log_file);
    if (locked)
        LeaveCriticalSection(&g_log_lock);
}

int ngb_stopping(void) { return g_stopping != 0; }

static const char *exit_name(int code) {
    switch (code) {
    case NGB_EXIT_OK: return "OK";
    case NGB_EXIT_USAGE: return "USAGE";
    case NGB_EXIT_UNIMPLEMENTED: return "UNIMPLEMENTED";
    case NGB_EXIT_CRASH: return "CRASH";
    case NGB_EXIT_MAX_CALLS: return "MAX_CALLS";
    case NGB_EXIT_TIMEOUT: return "TIMEOUT";
    case NGB_EXIT_GUEST: return "GUEST_EXIT";
    default: return "?";
    }
}

__declspec(noreturn) void ngb_stop(int code, const char *fmt, ...) {
    char reason[1024];
    va_list ap;
    if (InterlockedExchange(&g_stopping, 1)) {
        /* Another thread is already stopping: wait for the process to end. */
        for (;;)
            Sleep(1000);
    }
    va_start(ap, fmt);
    vsnprintf(reason, sizeof reason, fmt, ap);
    va_end(ap);
    ngb_log("");
    ngb_log("==== STOP %s (%d) at %.3f s: %s", exit_name(code), code, ngb_elapsed(), reason);
    ngb_prof_report(); /* first: it stops the sampler, which suspends threads */
    /* The NV2A summary takes its lock: it goes BEFORE suspending the threads for the dump. */
    nv2a_summary();
    ngb_thread_dump_all();
    ngb_kernel_summary();
    ngb_input_summary();
    ngb_apu_summary();
    ngb_log("==== END");
    ngb_log_flush();
    fprintf(stderr, "ngb-loader: STOP %s: %s\nlog: %s\n", exit_name(code), reason,
            g_ngb_opts.log_path);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), (UINT)code);
    for (;;)
        Sleep(1000);
}
