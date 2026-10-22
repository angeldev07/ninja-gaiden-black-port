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

    g_ngb_last_call_tick = GetTickCount();
    if (ordinal <= NGB_MAX_ORDINAL)
        InterlockedIncrement(&g_counts[ordinal]);
    if (!e)
        ngb_stop(NGB_EXIT_UNIMPLEMENTED, "unknown ordinal %u (from 0x%08X)", ordinal, caller);
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
    ret = fn(args);
    if (t) {
        t->inflight_ordinal = prev_ord;
        t->inflight_caller = prev_caller;
    }
    if (g_ngb_opts.log_cap && ordinal <= NGB_MAX_ORDINAL &&
        (unsigned long)g_counts[ordinal] > g_ngb_opts.log_cap &&
        g_counts[ordinal] % 100000 != 0) { /* after the cap, one sample every 100000 */
        if ((unsigned long)g_counts[ordinal] == g_ngb_opts.log_cap + 1)
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
