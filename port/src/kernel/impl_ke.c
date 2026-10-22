/*
 * impl_ke.c: funciones Ke, Kf y Hal del planificador: tiempo, IRQL, eventos, esperas, timers, DPC
 * e interrupciones.
 *
 * - IRQL: se lleva por hilo. Subir a DISPATCH_LEVEL (2) o más toma un lock global
 *   recursivo ("el procesador"): en Xbox, un hilo a DPC level no puede ser expulsado, y
 *   el código confía en ello para exclusión mutua. Bajar de 2 lo suelta.
 * - Objetos dispatcher (KEVENT, KTIMER...) que viven en memoria del juego: cada dirección
 *   se asocia a un objeto de Windows creado al primer uso según DISPATCHER_HEADER.Type.
 * - Timers y DPC: un hilo Xbox propio ("DPC") ejecuta las rutinas diferidas a DPC level.
 * - Interrupciones: se aceptan todas (KeConnectInterrupt devuelve TRUE). El hilo de DPC
 *   dispara dos: la del NV2A (nivel 3, vblank sintético) y la de la APU (nivel 5, avisos
 *   de voz libre); las demás nunca se disparan.
 */
#include "../ngb.h"

#include <stdio.h>
#include <string.h>

#include "../gpu/nv2a.h"
#include "kernel.h"

#define OBJ_SLOTS 8192
#define MAX_TIMERS 256
#define MAX_DPCS 1024

/* ------------------------------------------------------------------ IRQL */

static CRITICAL_SECTION g_dispatch_lock;
static volatile LONG g_ke_ready;

static void ke_init_once(void) {
    if (!InterlockedCompareExchange(&g_ke_ready, 1, 0))
        InitializeCriticalSection(&g_dispatch_lock);
}

static uint32_t raise_irql(uint32_t new_irql) {
    ngb_thread_t *t = ngb_thread_current();
    uint32_t old = t ? t->irql : 0;
    ke_init_once();
    if (t) {
        if (new_irql >= 2 && old < 2) {
            /* A DISPATCH_LEVEL un hilo Xbox no se suspende (la APC de suspensión llega al
             * bajar el IRQL): mientras tenga el lock, NtSuspendThread se aplaza. */
            ngb_kernel_enter(t);
            EnterCriticalSection(&g_dispatch_lock);
        }
        t->irql = new_irql;
    }
    return old;
}

static void lower_irql(uint32_t new_irql) {
    ngb_thread_t *t = ngb_thread_current();
    if (!t)
        return;
    if (new_irql < 2 && t->irql >= 2) {
        t->irql = new_irql;
        LeaveCriticalSection(&g_dispatch_lock);
        ngb_kernel_leave(t);
        return;
    }
    t->irql = new_irql;
}

/* 129 KIRQL KeRaiseIrqlToDpcLevel(void) */
static uint64_t KeRaiseIrqlToDpcLevel(uint32_t *args) {
    (void)args;
    return RET32(raise_irql(2));
}

/* 130 KIRQL KeRaiseIrqlToSynchLevel(void) */
static uint64_t KeRaiseIrqlToSynchLevel(uint32_t *args) {
    (void)args;
    return RET32(raise_irql(0x1C));
}

/* 160 KIRQL FASTCALL KfRaiseIrql(KIRQL NewIrql) */
static uint64_t KfRaiseIrql(uint32_t *args) {
    return RET32(raise_irql(ARG(0) & 0xFF));
}

/* 161 VOID FASTCALL KfLowerIrql(KIRQL NewIrql) */
static uint64_t KfLowerIrql(uint32_t *args) {
    lower_irql(ARG(0) & 0xFF);
    return 0;
}

/* 103 KIRQL KeGetCurrentIrql(void) */
static uint64_t KeGetCurrentIrql(uint32_t *args) {
    ngb_thread_t *t = ngb_thread_current();
    (void)args;
    return t ? RET32(t->irql) : 0;
}

/* ------------------------------------------------------------------ tiempo */

#define XBOX_PERF_FREQUENCY 733333333ull /* KeQueryPerformanceFrequency de Xbox: el TSC */

/* 126 ULONGLONG KeQueryPerformanceCounter(void) */
static uint64_t KeQueryPerformanceCounter(uint32_t *args) {
    LARGE_INTEGER c, f;
    (void)args;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return (uint64_t)(c.QuadPart / f.QuadPart) * XBOX_PERF_FREQUENCY +
           (uint64_t)(c.QuadPart % f.QuadPart) * XBOX_PERF_FREQUENCY / (uint64_t)f.QuadPart;
}

/* 127 ULONGLONG KeQueryPerformanceFrequency(void) */
static uint64_t KeQueryPerformanceFrequency(uint32_t *args) {
    (void)args;
    return XBOX_PERF_FREQUENCY;
}

/* 128 VOID KeQuerySystemTime(PLARGE_INTEGER CurrentTime) */
static uint64_t KeQuerySystemTime(uint32_t *args) {
    GetSystemTimeAsFileTime((FILETIME *)PTR(0));
    return 0;
}

/* 125 ULONGLONG KeQueryInterruptTime(void) */
static uint64_t KeQueryInterruptTime(uint32_t *args) {
    (void)args;
    return (uint64_t)GetTickCount64() * 10000ull;
}

/* 228 NTSTATUS NtSetSystemTime(PLARGE_INTEGER SystemTime, PLARGE_INTEGER PreviousTime) */
static uint64_t NtSetSystemTime(uint32_t *args) {
    if (PTR(1))
        GetSystemTimeAsFileTime((FILETIME *)PTR(1));
    return STATUS_SUCCESS; /* no se toca el reloj del PC */
}

/* Timeout NT (100 ns; negativo = relativo, positivo = absoluto) a milisegundos. */
static DWORD timeout_ms(const int64_t *timeout) {
    int64_t v;
    if (!timeout)
        return INFINITE;
    v = *timeout;
    if (v < 0)
        return (DWORD)((-v + 9999) / 10000);
    if (v == 0)
        return 0;
    {
        FILETIME now;
        int64_t n;
        GetSystemTimeAsFileTime(&now);
        n = ((int64_t)now.dwHighDateTime << 32) | now.dwLowDateTime;
        return v > n ? (DWORD)((v - n + 9999) / 10000) : 0;
    }
}

/* 151 VOID KeStallExecutionProcessor(ULONG MicroSeconds) */
static uint64_t KeStallExecutionProcessor(uint32_t *args) {
    LARGE_INTEGER f, s, n;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&s);
    do {
        YieldProcessor();
        QueryPerformanceCounter(&n);
    } while ((n.QuadPart - s.QuadPart) * 1000000 / f.QuadPart < (int64_t)ARG(0));
    return 0;
}

/* 99 NTSTATUS KeDelayExecutionThread(KPROCESSOR_MODE, BOOLEAN Alertable, PLARGE_INTEGER) */
static uint64_t KeDelayExecutionThread(uint32_t *args) {
    DWORD ms = timeout_ms((const int64_t *)PTR(2));
    DWORD r = SleepEx(ms, (ARG(1) & 0xFF) != 0);
    return r == WAIT_IO_COMPLETION ? 0xC0u : STATUS_SUCCESS; /* STATUS_USER_APC */
}

/* 238 NTSTATUS NtYieldExecution(void) */
static uint64_t NtYieldExecution(uint32_t *args) {
    (void)args;
    SwitchToThread();
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ objetos */

typedef struct obj_slot {
    uint32_t key;
    HANDLE h;
} obj_slot_t;

static obj_slot_t g_objs[OBJ_SLOTS];
static SRWLOCK g_obj_lock = SRWLOCK_INIT;

/* Objeto Windows para un objeto dispatcher Xbox en `addr` (lo crea si hace falta). */
static HANDLE obj_handle(uint32_t addr) {
    uint32_t h = (addr >> 2) * 2654435761u, i;
    HANDLE found = NULL;
    const uint8_t *hdr = (const uint8_t *)(uintptr_t)addr;
    ngb_thread_t *t;
    AcquireSRWLockExclusive(&g_obj_lock);
    for (i = 0; i < OBJ_SLOTS; i++) {
        obj_slot_t *s = &g_objs[(h + i) % OBJ_SLOTS];
        if (s->key == addr) {
            found = s->h;
            break;
        }
        if (!s->key) {
            int32_t signal = *(const int32_t *)(hdr + 4);
            switch (hdr[0]) {
            case 0: /* EventNotificationObject */
                found = CreateEventA(NULL, TRUE, signal != 0, NULL);
                break;
            case 1: /* EventSynchronizationObject */
                found = CreateEventA(NULL, FALSE, signal != 0, NULL);
                break;
            case 2: /* MutantObject */
                found = CreateMutexA(NULL, FALSE, NULL);
                break;
            case 5: /* SemaphoreObject: Limit en 0x10 */
                found = CreateSemaphoreA(NULL, signal, *(const int32_t *)(hdr + 0x10), NULL);
                break;
            case 6: /* ThreadObject */
                t = ngb_thread_from_ethread(addr);
                if (t)
                    DuplicateHandle(GetCurrentProcess(), t->handle, GetCurrentProcess(), &found,
                                    0, FALSE, DUPLICATE_SAME_ACCESS);
                break;
            case 8: /* TimerNotificationObject */
                found = CreateEventA(NULL, TRUE, signal != 0, NULL);
                break;
            case 9: /* TimerSynchronizationObject */
                found = CreateEventA(NULL, FALSE, signal != 0, NULL);
                break;
            default:
                break;
            }
            if (found) {
                s->key = addr;
                s->h = found;
            }
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_obj_lock);
    if (!found)
        ngb_log("    · objeto dispatcher 0x%08X de tipo %u no soportado", addr, hdr[0]);
    return found;
}

static void obj_forget(uint32_t addr) {
    uint32_t h = (addr >> 2) * 2654435761u, i;
    AcquireSRWLockExclusive(&g_obj_lock);
    for (i = 0; i < OBJ_SLOTS; i++) {
        obj_slot_t *s = &g_objs[(h + i) % OBJ_SLOTS];
        if (s->key == addr) {
            /* Se deja la clave (borrar rompería el sondeo lineal); se reinicia el objeto. */
            ResetEvent(s->h);
            break;
        }
        if (!s->key)
            break;
    }
    ReleaseSRWLockExclusive(&g_obj_lock);
}

/* El juego también limpia eventos escribiendo SignalState = 0 en memoria (D3D con su evento
 * de vblank), sin llamar al kernel: antes de esperar, el objeto de Windows se alinea con la
 * cabecera para no despertar con una señal ya consumida. */
static void sync_event(uint32_t addr, HANDLE h) {
    const uint8_t *hdr = (const uint8_t *)(uintptr_t)addr;
    const volatile LONG *signal = (const volatile LONG *)(hdr + 4);
    if ((hdr[0] <= 1 || hdr[0] == 8 || hdr[0] == 9) && *signal == 0) {
        ResetEvent(h);
        /* Si KeSetEvent (o el timer) escribió 1 y llamó a SetEvent entre la lectura y el
         * ResetEvent, la señal se habría borrado: se relee la cabecera y se restaura. */
        MemoryBarrier();
        if (*signal != 0)
            SetEvent(h);
    }
}

static void after_wait(uint32_t addr) {
    uint8_t *hdr = (uint8_t *)(uintptr_t)addr;
    if (hdr[0] == 1 || hdr[0] == 9)
        *(int32_t *)(hdr + 4) = 0; /* sincronización: se consume la señal */
}

static uint32_t wait_status(DWORD r, DWORD base) {
    if (r == WAIT_TIMEOUT)
        return STATUS_TIMEOUT;
    if (r == WAIT_IO_COMPLETION)
        return 0xC0u; /* STATUS_USER_APC */
    if (r == WAIT_FAILED)
        return STATUS_INVALID_HANDLE;
    if (r >= WAIT_ABANDONED_0 && r < WAIT_ABANDONED_0 + MAXIMUM_WAIT_OBJECTS)
        return 0x80u + (r - WAIT_ABANDONED_0);
    return r - base;
}

/* 108 VOID KeInitializeEvent(PRKEVENT Event, EVENT_TYPE Type, BOOLEAN State) */
static uint64_t KeInitializeEvent(uint32_t *args) {
    uint8_t *e = (uint8_t *)PTR(0);
    memset(e, 0, 16);
    e[0] = (uint8_t)ARG(1);
    e[2] = 4;
    *(int32_t *)(e + 4) = (int32_t)(ARG(2) & 0xFF);
    *(uint32_t *)(e + 8) = ARG(0) + 8; /* WaitListHead vacía */
    *(uint32_t *)(e + 12) = ARG(0) + 8;
    obj_forget(ARG(0));
    if (ARG(2) & 0xFF)
        SetEvent(obj_handle(ARG(0)));
    return 0;
}

/* 145 LONG KeSetEvent(PRKEVENT Event, KPRIORITY Increment, BOOLEAN Wait) */
static uint64_t KeSetEvent(uint32_t *args) {
    int32_t *signal = (int32_t *)((uint8_t *)PTR(0) + 4);
    int32_t old = *signal;
    HANDLE h = obj_handle(ARG(0));
    *signal = 1;
    if (h)
        SetEvent(h);
    return RET32(old);
}

/* 123 LONG KeResetEvent(PRKEVENT Event) / 92 VOID KeClearEvent(PRKEVENT Event) */
static uint64_t KeResetEvent(uint32_t *args) {
    int32_t *signal = (int32_t *)((uint8_t *)PTR(0) + 4);
    int32_t old = *signal;
    HANDLE h = obj_handle(ARG(0));
    *signal = 0;
    if (h)
        ResetEvent(h);
    return RET32(old);
}

/* 159 NTSTATUS KeWaitForSingleObject(PVOID Object, KWAIT_REASON, KPROCESSOR_MODE,
 *                                    BOOLEAN Alertable, PLARGE_INTEGER Timeout) */
static uint64_t KeWaitForSingleObject(uint32_t *args) {
    HANDLE h = obj_handle(ARG(0));
    DWORD r;
    if (!h)
        return RET32(STATUS_INVALID_HANDLE);
    sync_event(ARG(0), h);
    r = WaitForSingleObjectEx(h, timeout_ms((const int64_t *)PTR(4)), (ARG(3) & 0xFF) != 0);
    if (r == WAIT_OBJECT_0)
        after_wait(ARG(0));
    return RET32(wait_status(r, WAIT_OBJECT_0));
}

/* 158 NTSTATUS KeWaitForMultipleObjects(ULONG Count, PVOID Object[], WAIT_TYPE WaitType,
 *        KWAIT_REASON, KPROCESSOR_MODE, BOOLEAN Alertable, PLARGE_INTEGER, PKWAIT_BLOCK) */
static uint64_t KeWaitForMultipleObjects(uint32_t *args) {
    HANDLE hs[MAXIMUM_WAIT_OBJECTS];
    const uint32_t *objs = (const uint32_t *)PTR(1);
    uint32_t n = ARG(0), i;
    DWORD r;
    if (n > MAXIMUM_WAIT_OBJECTS)
        return RET32(STATUS_INVALID_PARAMETER);
    for (i = 0; i < n; i++) {
        if (!(hs[i] = obj_handle(objs[i])))
            return RET32(STATUS_INVALID_HANDLE);
        sync_event(objs[i], hs[i]);
    }
    r = WaitForMultipleObjectsEx(n, hs, ARG(2) == 0 /* WaitAll */,
                                 timeout_ms((const int64_t *)PTR(6)), (ARG(5) & 0xFF) != 0);
    if (r < WAIT_OBJECT_0 + n) {
        if (ARG(2) == 0) { /* WaitAll: Windows consumió la señal de todos */
            for (i = 0; i < n; i++)
                after_wait(objs[i]);
        } else {
            after_wait(objs[r - WAIT_OBJECT_0]);
        }
    }
    return RET32(wait_status(r, WAIT_OBJECT_0));
}

/* 189 NTSTATUS NtCreateEvent(PHANDLE, POBJECT_ATTRIBUTES, EVENT_TYPE, BOOLEAN InitialState) */
static uint64_t NtCreateEvent(uint32_t *args) {
    HANDLE h = CreateEventA(NULL, ARG(2) == 0, (ARG(3) & 0xFF) != 0, NULL);
    if (!h)
        return RET32(STATUS_NO_MEMORY);
    *(uint32_t *)PTR(0) = (uint32_t)(uintptr_t)h;
    return STATUS_SUCCESS;
}

/* 225 NTSTATUS NtSetEvent(HANDLE EventHandle, PLONG PreviousState) */
static uint64_t NtSetEvent(uint32_t *args) {
    if (PTR(1))
        *(int32_t *)PTR(1) = 0;
    return SetEvent((HANDLE)PTR(0)) ? STATUS_SUCCESS : RET32(STATUS_INVALID_HANDLE);
}

/* 233 NTSTATUS NtWaitForSingleObject(HANDLE Handle, BOOLEAN Alertable, PLARGE_INTEGER) */
static uint64_t NtWaitForSingleObject(uint32_t *args) {
    DWORD r = WaitForSingleObjectEx((HANDLE)PTR(0), timeout_ms((const int64_t *)PTR(2)),
                                    (ARG(1) & 0xFF) != 0);
    return RET32(wait_status(r, WAIT_OBJECT_0));
}

/* 234 NTSTATUS NtWaitForSingleObjectEx(HANDLE, KPROCESSOR_MODE, BOOLEAN Alertable, PLARGE_INTEGER) */
static uint64_t NtWaitForSingleObjectEx(uint32_t *args) {
    DWORD r = WaitForSingleObjectEx((HANDLE)PTR(0), timeout_ms((const int64_t *)PTR(3)),
                                    (ARG(2) & 0xFF) != 0);
    return RET32(wait_status(r, WAIT_OBJECT_0));
}

/* 235 NTSTATUS NtWaitForMultipleObjectsEx(ULONG Count, HANDLE Handles[], WAIT_TYPE,
 *                                         KPROCESSOR_MODE, BOOLEAN Alertable, PLARGE_INTEGER) */
static uint64_t NtWaitForMultipleObjectsEx(uint32_t *args) {
    DWORD r = WaitForMultipleObjectsEx(ARG(0), (const HANDLE *)PTR(1), ARG(2) == 0,
                                       timeout_ms((const int64_t *)PTR(5)), (ARG(4) & 0xFF) != 0);
    return RET32(wait_status(r, WAIT_OBJECT_0));
}

/* 187 NTSTATUS NtClose(HANDLE Handle) */
static uint64_t NtClose(uint32_t *args) {
    if (ngb_io_close(PTR(0)))
        return STATUS_SUCCESS;
    return CloseHandle((HANDLE)PTR(0)) ? STATUS_SUCCESS : RET32(STATUS_INVALID_HANDLE);
}

/* ------------------------------------------------------------------ timers y DPC */

typedef struct ktimer_slot {
    uint32_t timer;
    ULONGLONG due_ms;
    uint32_t period_ms;
    int active;
} ktimer_slot_t;

static ktimer_slot_t g_timers[MAX_TIMERS];
static uint32_t g_dpc_queue[MAX_DPCS];
static int g_dpc_count;
static CRITICAL_SECTION g_timer_lock;
static HANDLE g_dpc_wake;
static volatile LONG g_dpc_started;
static INIT_ONCE g_dpc_once = INIT_ONCE_STATIC_INIT;

typedef void(__stdcall *dpc_routine_fn)(uint32_t dpc, uint32_t ctx, uint32_t a1, uint32_t a2);

static uint8_t *g_gpu_interrupt; /* KINTERRUPT conectado al nivel 3 (NV2A) */
static uint8_t *g_apu_interrupt; /* KINTERRUPT conectado al nivel 5 (APU, DirectSound) */
static ULONGLONG g_next_vblank;

typedef uint8_t(__stdcall *isr_fn)(uint32_t interrupt, uint32_t context);

/* Vblank sintético a ~60 Hz: marca la interrupción en el modelo y llama a la ISR de D3D. */
static void vblank_tick(ULONGLONG now, ULONGLONG *next) {
    if (!g_gpu_interrupt || !nv2a_vblank_enabled())
        return;
    if (now >= g_next_vblank) {
        g_next_vblank = now + 16;
        if (nv2a_vblank() && g_gpu_interrupt[0x10]) {
            uint32_t old = raise_irql(*(uint32_t *)(g_gpu_interrupt + 0xC) & 0xFF);
            ((isr_fn)(uintptr_t) * (uint32_t *)g_gpu_interrupt)(
                (uint32_t)(uintptr_t)g_gpu_interrupt, *(uint32_t *)(g_gpu_interrupt + 4));
            lower_irql(old);
        }
    }
    if (g_next_vblank < *next)
        *next = g_next_vblank;
}

/* Llama a la ISR de un KINTERRUPT conectado, a su IRQL. */
static void call_isr(void *k) {
    uint8_t *ki = (uint8_t *)k;
    uint32_t old;
    if (!ki[0x10])
        return;
    old = raise_irql(*(uint32_t *)(ki + 0xC) & 0xFF);
    ((isr_fn)(uintptr_t) * (uint32_t *)ki)((uint32_t)(uintptr_t)ki, *(uint32_t *)(ki + 4));
    lower_irql(old);
}

static void run_dpc(uint32_t dpc) {
    uint8_t *d = (uint8_t *)(uintptr_t)dpc;
    uint32_t routine = *(uint32_t *)(d + 0x0C);
    d[2] = 0; /* Inserted = FALSE */
    if (!routine)
        return;
    raise_irql(2);
    ((dpc_routine_fn)(uintptr_t)routine)(dpc, *(uint32_t *)(d + 0x10), *(uint32_t *)(d + 0x14),
                                         *(uint32_t *)(d + 0x18));
    lower_irql(0);
}

/* Rutina del hilo Xbox "DPC" (stdcall, corre sobre una pila Xbox con su KPCR falso). */
static void __stdcall dpc_thread(uint32_t unused) {
    (void)unused;
    for (;;) {
        ULONGLONG now = GetTickCount64(), next = now + 50;
        uint32_t fire[MAX_TIMERS + MAX_DPCS];
        int nfire = 0, i;
        EnterCriticalSection(&g_timer_lock);
        for (i = 0; i < g_dpc_count; i++)
            fire[nfire++] = g_dpc_queue[i];
        g_dpc_count = 0;
        for (i = 0; i < MAX_TIMERS; i++) {
            ktimer_slot_t *s = &g_timers[i];
            uint8_t *tm;
            if (!s->active)
                continue;
            if (s->due_ms > now) {
                if (s->due_ms < next)
                    next = s->due_ms;
                continue;
            }
            tm = (uint8_t *)(uintptr_t)s->timer;
            *(int32_t *)(tm + 4) = 1; /* SignalState */
            SetEvent(obj_handle(s->timer));
            if (*(uint32_t *)(tm + 0x20))
                fire[nfire++] = *(uint32_t *)(tm + 0x20);
            if (s->period_ms)
                s->due_ms = now + s->period_ms;
            else
                s->active = 0;
        }
        LeaveCriticalSection(&g_timer_lock);
        for (i = 0; i < nfire; i++)
            run_dpc(fire[i]);
        vblank_tick(now, &next);
        ngb_apu_tick();
        if (g_apu_interrupt)
            ngb_apu_interrupt(call_isr, g_apu_interrupt);
        ngb_watch_tick();
        if (next > now + 16)
            next = now + 16;
        WaitForSingleObject(g_dpc_wake, (DWORD)(next > now ? next - now : 0));
    }
}

static BOOL CALLBACK dpc_init(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    ngb_thread_t *t;
    (void)once;
    (void)param;
    (void)ctx;
    InitializeCriticalSection(&g_timer_lock);
    g_dpc_wake = CreateEventA(NULL, FALSE, FALSE, NULL);
    t = ngb_thread_spawn((uint32_t)(uintptr_t)dpc_thread, 0, 0, 0x10000,
                         ngb_tls_total_size(&g_ngb_xbe), 1, 0);
    if (!t)
        ngb_stop(NGB_EXIT_CRASH, "no se pudo crear el hilo de DPC");
    t->internal = 1;
    InterlockedExchange(&g_dpc_started, 1);
    ResumeThread(t->handle);
    return TRUE;
}

/* Arranca el hilo de DPC una sola vez; los demás hilos esperan a que esté listo. */
static void dpc_init_once(void) {
    InitOnceExecuteOnce(&g_dpc_once, dpc_init, NULL, NULL);
}

/* 113 VOID KeInitializeTimerEx(PKTIMER Timer, TIMER_TYPE Type) */
static uint64_t KeInitializeTimerEx(uint32_t *args) {
    uint8_t *tm = (uint8_t *)PTR(0);
    memset(tm, 0, 0x28);
    tm[0] = (uint8_t)(8 + (ARG(1) & 1)); /* TimerNotificationObject / Synchronization */
    tm[2] = 10;
    obj_forget(ARG(0));
    return 0;
}

static int timer_set(uint32_t timer, int64_t due, uint32_t period, uint32_t dpc) {
    int i, was = 0, free_slot = -1;
    uint8_t *tm = (uint8_t *)(uintptr_t)timer;
    DWORD ms = timeout_ms(&due);
    dpc_init_once();
    EnterCriticalSection(&g_timer_lock);
    for (i = 0; i < MAX_TIMERS; i++) {
        if (g_timers[i].active && g_timers[i].timer == timer) {
            was = 1;
            free_slot = i;
            break;
        }
        if (!g_timers[i].active && free_slot < 0)
            free_slot = i;
    }
    *(int32_t *)(tm + 4) = 0;
    *(uint32_t *)(tm + 0x20) = dpc;
    *(int32_t *)(tm + 0x24) = (int32_t)period;
    ResetEvent(obj_handle(timer));
    if (free_slot >= 0) {
        g_timers[free_slot].timer = timer;
        g_timers[free_slot].due_ms = GetTickCount64() + ms;
        g_timers[free_slot].period_ms = period;
        g_timers[free_slot].active = 1;
    }
    LeaveCriticalSection(&g_timer_lock);
    SetEvent(g_dpc_wake);
    return was;
}

/* 149 BOOLEAN KeSetTimer(PKTIMER Timer, LARGE_INTEGER DueTime, PKDPC Dpc) */
static uint64_t KeSetTimer(uint32_t *args) {
    int64_t due = (int64_t)(((uint64_t)ARG(2) << 32) | ARG(1));
    return RET32(timer_set(ARG(0), due, 0, ARG(3)));
}

/* 150 BOOLEAN KeSetTimerEx(PKTIMER Timer, LARGE_INTEGER DueTime, LONG Period, PKDPC Dpc) */
static uint64_t KeSetTimerEx(uint32_t *args) {
    int64_t due = (int64_t)(((uint64_t)ARG(2) << 32) | ARG(1));
    return RET32(timer_set(ARG(0), due, ARG(3), ARG(4)));
}

/* 97 BOOLEAN KeCancelTimer(PKTIMER Timer) */
static uint64_t KeCancelTimer(uint32_t *args) {
    int i, was = 0;
    if (!g_dpc_started)
        return 0;
    EnterCriticalSection(&g_timer_lock);
    for (i = 0; i < MAX_TIMERS; i++)
        if (g_timers[i].active && g_timers[i].timer == ARG(0)) {
            g_timers[i].active = 0;
            was = 1;
        }
    LeaveCriticalSection(&g_timer_lock);
    return RET32(was);
}

/* 107 VOID KeInitializeDpc(PKDPC Dpc, PKDEFERRED_ROUTINE DeferredRoutine, PVOID Context) */
static uint64_t KeInitializeDpc(uint32_t *args) {
    uint8_t *d = (uint8_t *)PTR(0);
    memset(d, 0, 0x1C);
    *(int16_t *)d = 0x13; /* DpcObject */
    *(uint32_t *)(d + 0x0C) = ARG(1);
    *(uint32_t *)(d + 0x10) = ARG(2);
    return 0;
}

/* 119 BOOLEAN KeInsertQueueDpc(PRKDPC Dpc, PVOID SystemArgument1, PVOID SystemArgument2) */
static uint64_t KeInsertQueueDpc(uint32_t *args) {
    uint8_t *d = (uint8_t *)PTR(0);
    int ok = 0;
    dpc_init_once();
    EnterCriticalSection(&g_timer_lock);
    if (!d[2] && g_dpc_count < MAX_DPCS) {
        d[2] = 1;
        *(uint32_t *)(d + 0x14) = ARG(1);
        *(uint32_t *)(d + 0x18) = ARG(2);
        g_dpc_queue[g_dpc_count++] = ARG(0);
        ok = 1;
    }
    LeaveCriticalSection(&g_timer_lock);
    SetEvent(g_dpc_wake);
    return RET32(ok);
}

/* 137 BOOLEAN KeRemoveQueueDpc(PKDPC Dpc) */
static uint64_t KeRemoveQueueDpc(uint32_t *args) {
    int i, was = 0;
    if (!g_dpc_started)
        return 0;
    EnterCriticalSection(&g_timer_lock);
    for (i = 0; i < g_dpc_count; i++)
        if (g_dpc_queue[i] == ARG(0)) {
            g_dpc_queue[i] = g_dpc_queue[--g_dpc_count];
            ((uint8_t *)PTR(0))[2] = 0;
            was = 1;
            break;
        }
    LeaveCriticalSection(&g_timer_lock);
    return RET32(was);
}

/* ------------------------------------------------------------------ interrupciones */

/* 109 VOID KeInitializeInterrupt(PKINTERRUPT, PKSERVICE_ROUTINE, PVOID ServiceContext,
 *                                ULONG Vector, KIRQL Irql, KINTERRUPT_MODE, BOOLEAN Share) */
static uint64_t KeInitializeInterrupt(uint32_t *args) {
    uint8_t *k = (uint8_t *)PTR(0);
    *(uint32_t *)(k + 0) = ARG(1);
    *(uint32_t *)(k + 4) = ARG(2);
    *(uint32_t *)(k + 8) = ARG(3) - 0x30;
    *(uint32_t *)(k + 0xC) = ARG(4) & 0xFF;
    k[0x10] = 0;
    k[0x11] = (uint8_t)ARG(6);
    k[0x12] = (uint8_t)ARG(5);
    return 0;
}

/* 98 BOOLEAN KeConnectInterrupt(PKINTERRUPT) / 100 BOOLEAN KeDisconnectInterrupt(PKINTERRUPT) */
static uint64_t KeConnectInterrupt(uint32_t *args) {
    uint8_t *k = (uint8_t *)PTR(0);
    uint32_t level = *(uint32_t *)(k + 8);
    ngb_log("    · interrupción conectada: nivel %u, ISR 0x%08X%s", level, *(uint32_t *)k,
            level == 3 || level == 5 ? "" : " (nunca se disparará)");
    k[0x10] = 1;
    if (level == 5) { /* la APU: avisos de voz libre (crash.c, ngb_apu_interrupt) */
        g_apu_interrupt = k;
        dpc_init_once();
    }
    if (*(uint32_t *)(k + 8) == 3) { /* el NV2A: el vblank sintético llama a esta ISR */
        g_gpu_interrupt = k;
        dpc_init_once();
    }
    return 1;
}

static uint64_t KeDisconnectInterrupt(uint32_t *args) {
    ((uint8_t *)PTR(0))[0x10] = 0;
    return 1;
}

/* 44 ULONG HalGetInterruptVector(ULONG BusInterruptLevel, PKIRQL Irql) */
static uint64_t HalGetInterruptVector(uint32_t *args) {
    if (PTR(1))
        *(uint8_t *)PTR(1) = (uint8_t)(0x1A - ARG(0));
    return RET32(ARG(0) + 0x30);
}

typedef uint8_t(__stdcall *sync_routine_fn)(uint32_t ctx);

/* 153 BOOLEAN KeSynchronizeExecution(PKINTERRUPT, PKSYNCHRONIZE_ROUTINE, PVOID Context) */
static uint64_t KeSynchronizeExecution(uint32_t *args) {
    uint32_t old = raise_irql(0x1C);
    uint8_t r = ((sync_routine_fn)PTR(1))(ARG(2));
    lower_irql(old);
    return RET32(r);
}

/* ------------------------------------------------------------------ prioridades y FPU */

/* 143 KeSetBasePriorityThread, 144 KeSetDisableBoostThread, 124 KeQueryBasePriorityThread */
static uint64_t KePriorityStub(uint32_t *args) {
    (void)args;
    return 0;
}

/* 142 KeSaveFloatingPointState(PKFLOATING_SAVE) / 139 KeRestoreFloatingPointState */
static uint64_t KeFloatingPointState(uint32_t *args) {
    (void)args;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ apagado */

/* 95 VOID KeBugCheck(ULONG BugCheckCode) */
static uint64_t KeBugCheck(uint32_t *args) {
    ngb_stop(NGB_EXIT_GUEST, "KeBugCheck(0x%08X)", ARG(0));
}

/* 96 VOID KeBugCheckEx(ULONG Code, ULONG_PTR P1..P4) */
static uint64_t KeBugCheckEx(uint32_t *args) {
    ngb_stop(NGB_EXIT_GUEST, "KeBugCheckEx(0x%08X, 0x%08X, 0x%08X, 0x%08X, 0x%08X)", ARG(0),
             ARG(1), ARG(2), ARG(3), ARG(4));
}

/* 49 VOID HalReturnToFirmware(FIRMWARE_REENTRY Routine) */
static uint64_t HalReturnToFirmware(uint32_t *args) {
    ngb_stop(NGB_EXIT_GUEST, "HalReturnToFirmware(%u)", ARG(0));
}

/* 360 VOID HalInitiateShutdown(void) */
static uint64_t HalInitiateShutdown(uint32_t *args) {
    (void)args;
    ngb_stop(NGB_EXIT_GUEST, "HalInitiateShutdown");
}

/* 358 BOOLEAN HalIsResetOrShutdownPending(void) */
static uint64_t HalIsResetOrShutdownPending(uint32_t *args) {
    (void)args;
    return 0;
}

/* 47 VOID HalRegisterShutdownNotification(PHAL_SHUTDOWN_REGISTRATION, BOOLEAN Register) */
static uint64_t HalRegisterShutdownNotification(uint32_t *args) {
    (void)args;
    return 0;
}

static const ngb_kimpl_t k_impls[] = {
    {44, HalGetInterruptVector},
    {47, HalRegisterShutdownNotification},
    {49, HalReturnToFirmware},
    {92, KeResetEvent},
    {95, KeBugCheck},
    {96, KeBugCheckEx},
    {97, KeCancelTimer},
    {98, KeConnectInterrupt},
    {99, KeDelayExecutionThread},
    {100, KeDisconnectInterrupt},
    {103, KeGetCurrentIrql},
    {107, KeInitializeDpc},
    {108, KeInitializeEvent},
    {109, KeInitializeInterrupt},
    {113, KeInitializeTimerEx},
    {119, KeInsertQueueDpc},
    {123, KeResetEvent},
    {124, KePriorityStub},
    {125, KeQueryInterruptTime},
    {126, KeQueryPerformanceCounter},
    {127, KeQueryPerformanceFrequency},
    {128, KeQuerySystemTime},
    {129, KeRaiseIrqlToDpcLevel},
    {130, KeRaiseIrqlToSynchLevel},
    {137, KeRemoveQueueDpc},
    {139, KeFloatingPointState},
    {142, KeFloatingPointState},
    {143, KePriorityStub},
    {144, KePriorityStub},
    {145, KeSetEvent},
    {149, KeSetTimer},
    {150, KeSetTimerEx},
    {151, KeStallExecutionProcessor},
    {153, KeSynchronizeExecution},
    {158, KeWaitForMultipleObjects},
    {159, KeWaitForSingleObject},
    {160, KfRaiseIrql},
    {161, KfLowerIrql},
    {187, NtClose},
    {189, NtCreateEvent},
    {225, NtSetEvent},
    {228, NtSetSystemTime},
    {233, NtWaitForSingleObject},
    {234, NtWaitForSingleObjectEx},
    {235, NtWaitForMultipleObjectsEx},
    {238, NtYieldExecution},
    {358, HalIsResetOrShutdownPending},
    {360, HalInitiateShutdown},
};

void ngb_impl_register_ke(void) {
    ke_init_once();
    ngb_kernel_register(k_impls, sizeof k_impls / sizeof k_impls[0]);
}
