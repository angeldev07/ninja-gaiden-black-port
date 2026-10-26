/*
 * prof.c: sampling profiler (--profile), to find where the frame time goes.
 *
 * A sampler thread wakes about every millisecond, suspends each registered thread, reads its
 * EIP and resumes it. Samples are counted by (thread, EIP). The stack is also scanned for the
 * first return address that lands in ngb-core right after a call instruction. When the leaf is
 * outside ngb-core (the GL driver, Windows) that is the port function that called out; when it
 * is inside, it is usually the leaf's caller (memcpy called by whom). The scan is a heuristic
 * (a stale return address in a local can be picked), so those lines are a hint, not an exact
 * call graph.
 *
 * At the stop (ngb_stop) the counts are symbolized with dbghelp (ngb-core.pdb next to the DLL)
 * and written to <log>-profile.txt. XBE addresses use the --symbols names when they are loaded.
 * The sampler only reads registers and stack memory while the thread is suspended: it takes no
 * lock and allocates nothing, so it cannot deadlock with the thread it stopped.
 */
#include "ngb.h"

#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROF_THREADS 16
#define PROF_SLOTS (1u << 17) /* distinct (thread, address) pairs */
#define PROF_SCAN_BYTES 2048u /* stack scanned for the first ngb-core caller */

typedef struct {
    uint32_t key;    /* address; 0 = empty */
    uint16_t thread; /* index + 1 */
    uint16_t caller; /* 0 = leaf sample, 1 = first ngb-core return address on the stack */
    uint32_t count;
} prof_slot_t;

static struct {
    HANDLE thread[PROF_THREADS];
    char name[PROF_THREADS][24];
    uint32_t samples[PROF_THREADS];
    uintptr_t stack_base[PROF_THREADS]; /* NT_TIB.StackBase, read once from the thread's TEB */
    volatile LONG count;
    volatile LONG stop;
    HANDLE sampler;
    uintptr_t core_lo, core_hi; /* ngb-core.dll image */
    prof_slot_t *slots;
    uint32_t lost; /* table full */
    double started;
} P;

static void count(uint16_t thread, uint16_t caller, uint32_t addr) {
    uint32_t h = (addr * 2654435761u) ^ ((uint32_t)thread << 24) ^ caller, i;
    for (i = 0; i < 64; i++) {
        prof_slot_t *s = &P.slots[(h + i) & (PROF_SLOTS - 1)];
        if (s->key == addr && s->thread == thread && s->caller == caller) {
            s->count++;
            return;
        }
        if (!s->key) {
            s->key = addr;
            s->thread = thread;
            s->caller = caller;
            s->count = 1;
            return;
        }
    }
    P.lost++;
}

static int in_core(uintptr_t a) { return a >= P.core_lo && a < P.core_hi; }

/* A return address: the bytes before it are a call (E8 rel32, FF /2 with a ModRM of 2, 3 or 6
 * bytes). */
static int after_call(uintptr_t ret) {
    const uint8_t *p = (const uint8_t *)ret;
    return p[-5] == 0xE8 || (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) ||
           (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) || (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10);
}

/* No system call here: VirtualQuery takes the address space lock, and the stopped thread may
 * be inside VirtualQuery itself, which delayed the sampler and skewed the counts. */
static uint32_t first_core_caller(uintptr_t esp, uintptr_t stack_base) {
    uintptr_t end = esp + PROF_SCAN_BYTES, p;
    if (esp >= stack_base)
        return 0;
    if (end > stack_base)
        end = stack_base;
    for (p = esp & ~(uintptr_t)3; p + 4 <= end; p += 4) {
        uintptr_t v = *(const uintptr_t *)p;
        if (in_core(v) && v - P.core_lo >= 0x1000 && after_call(v))
            return (uint32_t)v;
    }
    return 0;
}

static DWORD WINAPI sampler_main(LPVOID unused) {
    (void)unused;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    while (!P.stop) {
        LONG n = P.count, i;
        for (i = 0; i < n; i++) {
            CONTEXT c;
            uint32_t caller;
            if (!P.stack_base[i]) {
                LDT_ENTRY e;
                c.ContextFlags = CONTEXT_SEGMENTS;
                if (GetThreadContext(P.thread[i], &c) && GetThreadSelectorEntry(P.thread[i], c.SegFs, &e)) {
                    uintptr_t teb = e.BaseLow | (uintptr_t)e.HighWord.Bytes.BaseMid << 16 |
                                    (uintptr_t)e.HighWord.Bytes.BaseHi << 24;
                    P.stack_base[i] = ((NT_TIB *)teb)->StackBase ? (uintptr_t)((NT_TIB *)teb)->StackBase : 1;
                }
            }
            if (SuspendThread(P.thread[i]) == (DWORD)-1)
                continue;
            c.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(P.thread[i], &c)) {
                caller = first_core_caller(c.Esp, P.stack_base[i]);
                count((uint16_t)(i + 1), 0, c.Eip);
                if (caller)
                    count((uint16_t)(i + 1), 1, caller);
                P.samples[i]++;
            }
            ResumeThread(P.thread[i]);
        }
        Sleep(1);
    }
    return 0;
}

void ngb_prof_thread(HANDLE h, const char *name) {
    HANDLE dup;
    LONG i;
    if (!g_ngb_opts.profile || !h)
        return;
    if (!DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup,
                         THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0))
        return;
    i = InterlockedIncrement(&P.count) - 1;
    if (i >= PROF_THREADS) {
        InterlockedDecrement(&P.count);
        CloseHandle(dup);
        return;
    }
    snprintf(P.name[i], sizeof P.name[i], "%s", name);
    P.thread[i] = dup;
}

void ngb_prof_start(void) {
    HMODULE core = NULL;
    IMAGE_NT_HEADERS *nt;
    if (!g_ngb_opts.profile)
        return;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(void *)ngb_prof_start, &core);
    if (!core)
        return;
    nt = (IMAGE_NT_HEADERS *)((uint8_t *)core + ((IMAGE_DOS_HEADER *)core)->e_lfanew);
    P.core_lo = (uintptr_t)core;
    P.core_hi = P.core_lo + nt->OptionalHeader.SizeOfImage;
    P.slots = (prof_slot_t *)calloc(PROF_SLOTS, sizeof *P.slots);
    if (!P.slots)
        return;
    P.started = ngb_elapsed();
    P.sampler = CreateThread(NULL, 0, sampler_main, NULL, 0, NULL);
    ngb_log("profile: sampling at about 1 kHz; report at the stop in <log>-profile.txt");
}

/* --- report --- */

typedef struct {
    char name[160];
    uint16_t thread, caller;
    uint32_t count;
} prof_row_t;

static int cmp_row(const void *a, const void *b) {
    const prof_row_t *x = (const prof_row_t *)a, *y = (const prof_row_t *)b;
    if (x->thread != y->thread)
        return x->thread - y->thread;
    if (x->caller != y->caller)
        return x->caller - y->caller;
    return x->count < y->count ? 1 : x->count > y->count ? -1 : 0;
}

static int cmp_name(const void *a, const void *b) {
    const prof_row_t *x = (const prof_row_t *)a, *y = (const prof_row_t *)b;
    if (x->thread != y->thread)
        return x->thread - y->thread;
    if (x->caller != y->caller)
        return x->caller - y->caller;
    return strcmp(x->name, y->name);
}

/* Function (or module) of an address: "module!function" or "module+0xOFF". */
static void describe(uint32_t addr, char *out, size_t outlen) {
    char modname[MAX_PATH] = "?", *base;
    HMODULE mod = NULL;
    union {
        SYMBOL_INFO s;
        char buf[sizeof(SYMBOL_INFO) + 128];
    } u;
    DWORD64 disp = 0;
    if (addr >= g_ngb_xbe.base && addr < g_ngb_xbe.base + g_ngb_xbe.size_of_image) {
        char sym[160];
        ngb_symbol(addr, sym, sizeof sym);
        if (sym[0]) { /* " <name+0xOFF>": keep the name */
            char *plus = strrchr(sym, '+');
            if (plus)
                *plus = 0;
            snprintf(out, outlen, "xbe!%s", sym + 2);
        } else {
            snprintf(out, outlen, "xbe!0x%06X", addr & ~0xFFu); /* 256-byte buckets */
        }
        return;
    }
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(uintptr_t)addr, &mod) &&
        mod)
        GetModuleFileNameA(mod, modname, sizeof modname);
    base = strrchr(modname, '\\');
    base = base ? base + 1 : modname;
    memset(&u, 0, sizeof u);
    u.s.SizeOfStruct = sizeof u.s;
    u.s.MaxNameLen = 127;
    if (SymFromAddr(GetCurrentProcess(), addr, &disp, &u.s))
        snprintf(out, outlen, "%s!%s", base, u.s.Name);
    else if (mod)
        snprintf(out, outlen, "%s+0x%X", base, (unsigned)(addr - (uintptr_t)mod));
    else
        snprintf(out, outlen, "0x%08X", addr);
}

void ngb_prof_report(void) {
    char path[MAX_PATH + 16], *dot;
    prof_row_t *rows;
    uint32_t i, n = 0, j;
    FILE *f;
    if (!P.sampler)
        return;
    InterlockedExchange(&P.stop, 1);
    WaitForSingleObject(P.sampler, 2000);
    snprintf(path, sizeof path, "%s", g_ngb_opts.log_path);
    dot = strrchr(path, '.');
    if (dot && !strchr(dot, '\\'))
        *dot = 0;
    strcat(path, "-profile.txt");
    rows = (prof_row_t *)malloc(PROF_SLOTS * sizeof *rows);
    f = rows ? fopen(path, "w") : NULL;
    if (!f) {
        free(rows);
        return;
    }
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    for (i = 0; i < PROF_SLOTS; i++)
        if (P.slots[i].key) {
            describe(P.slots[i].key, rows[n].name, sizeof rows[n].name);
            rows[n].thread = P.slots[i].thread;
            rows[n].caller = P.slots[i].caller;
            rows[n].count = P.slots[i].count;
            n++;
        }
    /* Merge the addresses of one function. */
    qsort(rows, n, sizeof *rows, cmp_name);
    for (i = 0, j = 0; i < n; i++) {
        if (j && rows[j - 1].thread == rows[i].thread && rows[j - 1].caller == rows[i].caller &&
            !strcmp(rows[j - 1].name, rows[i].name))
            rows[j - 1].count += rows[i].count;
        else
            rows[j++] = rows[i];
    }
    n = j;
    qsort(rows, n, sizeof *rows, cmp_row);
    fprintf(f, "ngb profile: %.1f s sampled, about 1 sample per ms per thread; %u lost\n",
            ngb_elapsed() - P.started, P.lost);
    fprintf(f, "Leaf: where the thread was. Caller: the first ngb-core return address on the stack\n"
               "(heuristic): the port function that called out, or the caller of a port leaf.\n");
    for (i = 0; i < n; i = j) {
        uint32_t shown = 0, total = P.samples[rows[i].thread - 1];
        for (j = i; j < n && rows[j].thread == rows[i].thread && rows[j].caller == rows[i].caller; j++)
            if (shown < 60 && rows[j].count * 1000u >= total) { /* >= 0.1 % */
                if (!shown++)
                    fprintf(f, "\n== thread %s, %s (%u samples)\n", P.name[rows[i].thread - 1],
                            rows[i].caller ? "caller" : "leaf", total);
                fprintf(f, "%6.2f%% %7u  %s\n", 100.0 * rows[j].count / (total ? total : 1), rows[j].count,
                        rows[j].name);
            }
    }
    fclose(f);
    SymCleanup(GetCurrentProcess());
    free(rows);
    ngb_log("profile: report in %s", path);
}
