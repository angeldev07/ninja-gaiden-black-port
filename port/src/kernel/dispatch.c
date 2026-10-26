/*
 * dispatch.c: ngb_kcall(), the common point of all the kernel stubs.
 *
 * Logs each call on one line: `[n] T<thread> @<caller> <ordinal> <name>(args) -> ret`
 * (the line is written on return, so the details the implementation adds
 * appear right above it). If the ordinal has no implementation, it stops the run
 * (or returns STATUS_SUCCESS with --lenient).
 */
#include "../ngb.h"

#include <stdio.h>
#include <string.h>

#include "kernel.h"

static const ngb_kexport_t *g_exports[NGB_MAX_ORDINAL + 1];
static ngb_kimpl_fn g_impls[NGB_MAX_ORDINAL + 1];
static volatile LONG g_counts[NGB_MAX_ORDINAL + 1];
static volatile LONG g_seq;
static volatile LONG g_unimpl_first;
volatile DWORD g_ngb_last_call_tick;

long ngb_kernel_calls(void) { return g_seq; }

/* Time spent inside kernel calls of the Xbox threads, by thread, ordinal and caller, for the
 * perf lines: where the game waits when frames run late. Entries are never removed; once the
 * table is full, new triples are not counted. */
#define KTIME_SLOTS 4096
static struct {
    volatile LONG key; /* thread << 26 ^ ordinal << 17 ^ caller; 0 = empty */
    int thread;
    uint32_t ordinal, caller, calls;
    LONGLONG ticks;
} g_ktime[KTIME_SLOTS];
static SRWLOCK g_ktime_lock = SRWLOCK_INIT;

static void ktime_add(int thread, uint32_t ordinal, uint32_t caller, LONGLONG ticks) {
    LONG key = (LONG)((uint32_t)thread << 26 ^ ordinal << 17 ^ caller);
    uint32_t h = ((uint32_t)key * 2654435761u) >> 20, i;
    AcquireSRWLockExclusive(&g_ktime_lock);
    for (i = 0; i < KTIME_SLOTS; i++, h = (h + 1) & (KTIME_SLOTS - 1)) {
        if (g_ktime[h].key == 0) {
            g_ktime[h].thread = thread;
            g_ktime[h].ordinal = ordinal;
            g_ktime[h].caller = caller;
            g_ktime[h].key = key;
        }
        if (g_ktime[h].key == key) {
            g_ktime[h].calls++;
            g_ktime[h].ticks += ticks;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_ktime_lock);
}

void ngb_kernel_time_take(char *out, size_t outlen, uint32_t frames) {
    struct { int thread; uint32_t ordinal, caller, calls; LONGLONG ticks; } top[5] = {{0, 0, 0, 0, 0}};
    LARGE_INTEGER f;
    LONGLONG total = 0;
    size_t len = 0;
    int i, j;
    QueryPerformanceFrequency(&f);
    AcquireSRWLockExclusive(&g_ktime_lock);
    for (i = 0; i < KTIME_SLOTS; i++) {
        LONGLONG t = g_ktime[i].ticks;
        if (!g_ktime[i].key)
            continue;
        total += t;
        for (j = 0; j < 5 && top[j].ticks >= t; j++)
            ;
        if (j < 5) {
            memmove(&top[j + 1], &top[j], (size_t)(4 - j) * sizeof top[0]);
            top[j].thread = g_ktime[i].thread;
            top[j].ordinal = g_ktime[i].ordinal;
            top[j].caller = g_ktime[i].caller;
            top[j].calls = g_ktime[i].calls;
            top[j].ticks = t;
        }
        g_ktime[i].ticks = 0;
        g_ktime[i].calls = 0;
    }
    ReleaseSRWLockExclusive(&g_ktime_lock);
    if (!out || !outlen)
        return;
    out[0] = 0;
    if (!frames)
        return;
    len = (size_t)snprintf(out, outlen, "Xbox threads inside kernel calls %.2f ms per frame:",
                           (double)total * 1000.0 / f.QuadPart / frames);
    for (j = 0; j < 5 && top[j].ticks && len < outlen; j++) {
        const ngb_kexport_t *e = ngb_kexport(top[j].ordinal);
        len += (size_t)snprintf(out + len, outlen - len, " T%d %s@%08X %.2f (%.1f calls)",
                                top[j].thread, e ? e->name : "?", top[j].caller,
                                (double)top[j].ticks * 1000.0 / f.QuadPart / frames,
                                (double)top[j].calls / frames);
    }
}

const ngb_kexport_t *ngb_kexport(uint32_t ordinal) {
    return ordinal <= NGB_MAX_ORDINAL ? g_exports[ordinal] : NULL;
}

void ngb_kernel_register(const ngb_kimpl_t *impls, size_t count) {
    size_t i;
    for (i = 0; i < count; i++)
        if (impls[i].ordinal <= NGB_MAX_ORDINAL)
            g_impls[impls[i].ordinal] = impls[i].fn;
}

void ngb_kernel_init(void) {
    size_t i;
    for (i = 0; i < ngb_kexport_count; i++)
        if (ngb_kexports[i].ordinal <= NGB_MAX_ORDINAL)
            g_exports[ngb_kexports[i].ordinal] = &ngb_kexports[i];
    ngb_impl_register_mm();
    ngb_impl_register_rtl();
    ngb_impl_register_ke();
    ngb_impl_register_ps();
    ngb_impl_register_io();
    ngb_impl_register_misc();
}

size_t ngb_kernel_impl_count(void) {
    size_t i, n = 0;
    for (i = 0; i <= NGB_MAX_ORDINAL; i++)
        n += g_impls[i] != NULL;
    return n;
}

int ngb_kernel_is_implemented(uint32_t ordinal) {
    return ordinal <= NGB_MAX_ORDINAL && g_impls[ordinal] != NULL;
}

static void format_args(char *out, size_t outlen, const ngb_kexport_t *e, const uint32_t *args) {
    size_t pos = 0;
    int i;
    out[0] = 0;
    for (i = 0; args && i < e->nargs && pos + 16 < outlen; i++) {
        uint32_t v = args[i];
        pos += (size_t)snprintf(out + pos, outlen - pos, v < 10 ? "%s%u" : "%s0x%08X",
                                i ? ", " : "", v);
    }
    if (e->conv == NGB_CONV_CDECL && pos + 6 < outlen)
        snprintf(out + pos, outlen - pos, ", ...");
}

static uint64_t kcall(uint32_t ordinal, uint32_t *args, uint32_t caller);

uint64_t ngb_kcall(uint32_t ordinal, uint32_t *args, uint32_t caller) {
    ngb_thread_t *t = ngb_thread_current();
    uint64_t ret;
    ngb_kernel_enter(t);
    ret = kcall(ordinal, args, caller);
    ngb_kernel_leave(t);
    return ret;
}

unsigned long ngb_kernel_call_seq(void) {
    return (unsigned long)g_seq;
}

static uint64_t kcall(uint32_t ordinal, uint32_t *args, uint32_t caller) {
    const ngb_kexport_t *e = ngb_kexport(ordinal);
    ngb_thread_t *t = ngb_thread_current();
    uint32_t prev_ord = 0, prev_caller = 0;
    LONG n = InterlockedIncrement(&g_seq);
    ngb_kimpl_fn fn = ordinal <= NGB_MAX_ORDINAL ? g_impls[ordinal] : NULL;
    char argtext[512], sym[96];
    uint64_t ret = STATUS_SUCCESS;
    LONG count = 0;

    g_ngb_last_call_tick = GetTickCount();
    if (ordinal <= NGB_MAX_ORDINAL)
        count = InterlockedIncrement(&g_counts[ordinal]);
    if (!e)
        ngb_stop(NGB_EXIT_UNIMPLEMENTED, "unknown ordinal %u (from 0x%08X)", ordinal, caller);
    /* The arguments are formatted before the call (it may overwrite them), and only when the
     * line will be written: a busy frame makes about 600 kernel calls. */
    argtext[0] = 0;
    if (!fn || (!g_ngb_opts.log_minimal &&
                !(g_ngb_opts.log_cap && (unsigned long)count > g_ngb_opts.log_cap && count % 100000 != 0)))
        format_args(argtext, sizeof argtext, e, args);
    if (!fn) {
        ngb_log("[%ld] T%d @%08X %u %s(%s) -> NOT IMPLEMENTED", n, t ? t->index : 0, caller,
                ordinal, e->name, argtext);
        InterlockedCompareExchange(&g_unimpl_first, (LONG)ordinal, 0);
        if (!g_ngb_opts.lenient)
            ngb_stop(NGB_EXIT_UNIMPLEMENTED, "first unimplemented call: %u %s(%s) from 0x%08X",
                     ordinal, e->name, argtext, caller);
        return STATUS_SUCCESS;
    }
    if (t) {
        prev_ord = t->inflight_ordinal;
        prev_caller = t->inflight_caller;
        t->inflight_ordinal = ordinal;
        t->inflight_caller = caller;
    }
    if (t && !t->internal) {
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        ret = fn(args);
        QueryPerformanceCounter(&t1);
        ktime_add(t->index, ordinal, caller, t1.QuadPart - t0.QuadPart);
    } else {
        ret = fn(args);
    }
    if (t) {
        t->inflight_ordinal = prev_ord;
        t->inflight_caller = prev_caller;
    }
    if (g_ngb_opts.log_minimal) {
        /* short log: the call is only counted */
    } else if (g_ngb_opts.log_cap && ordinal <= NGB_MAX_ORDINAL &&
        (unsigned long)count > g_ngb_opts.log_cap &&
        count % 100000 != 0) { /* after the cap, one sample every 100000 */
        if ((unsigned long)count == g_ngb_opts.log_cap + 1)
            ngb_log("    · %s: more than %lu calls; from here on they are only counted", e->name,
                    g_ngb_opts.log_cap);
    } else if (ret >> 32)
        ngb_log("[%ld] T%d @%08X %u %s(%s) -> 0x%08X%08X%s", n, t ? t->index : 0, caller, ordinal,
                e->name, argtext, (uint32_t)(ret >> 32), (uint32_t)ret,
                ngb_symbol(caller, sym, sizeof sym));
    else
        ngb_log("[%ld] T%d @%08X %u %s(%s) -> 0x%08X%s", n, t ? t->index : 0, caller, ordinal,
                e->name, argtext, (uint32_t)ret, ngb_symbol(caller, sym, sizeof sym));
    if (g_ngb_opts.max_calls && (unsigned long)n >= g_ngb_opts.max_calls)
        ngb_stop(NGB_EXIT_MAX_CALLS, "--max-calls %lu reached", g_ngb_opts.max_calls);
    return ret;
}

void ngb_kernel_summary(void) {
    uint32_t i;
    int distinct = 0, distinct_impl = 0;
    ngb_log("summary: %ld kernel calls; %u exports implemented in total",
            (long)g_seq, (unsigned)ngb_kernel_impl_count());
    for (i = 0; i <= NGB_MAX_ORDINAL; i++) {
        if (!g_counts[i])
            continue;
        distinct++;
        distinct_impl += g_impls[i] != NULL;
        ngb_log("  %8ld  %3u %-36s %s", (long)g_counts[i], i, g_exports[i] ? g_exports[i]->name : "?",
                g_impls[i] ? "" : "NOT IMPLEMENTED");
    }
    ngb_log("summary: %d distinct functions called (%d implemented)", distinct, distinct_impl);
    if (g_unimpl_first) {
        const ngb_kexport_t *e = ngb_kexport((uint32_t)g_unimpl_first);
        ngb_log("summary: first unimplemented: %ld %s", (long)g_unimpl_first, e ? e->name : "?");
    }
}
