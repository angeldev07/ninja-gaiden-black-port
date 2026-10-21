/*
 * thread.c: hilos Xbox sobre hilos Win32.
 *
 * FS/KPCR (atajo de H6, ver port/README.md): en WOW64 no hay LDT (NtSetLdtEntries no
 * existe en x64), así que FS sigue apuntando al TEB de Windows. El código Xbox solo usa
 * unos pocos campos de fs: y se superponen en el TEB del hilo:
 *   fs:[0x00] ExceptionList, fs:[0x04] StackBase, fs:[0x08] StackLimit: misma semántica
 *             en NT_TIB de Windows y en el KPCR de Xbox; se usan tal cual.
 *   fs:[0x1C] SelfPcr     -> TEB.EnvironmentPointer := KPCR falso
 *   fs:[0x20] Prcb        -> TEB.ClientId.UniqueProcess := &KPCR falso.PrcbData
 *   fs:[0x24] Irql (byte) -> NO se toca: es TEB.ClientId.UniqueThread, y Windows lo usa
 *             para despertar a los hilos que esperan un CRITICAL_SECTION o un SRW (cambiarlo
 *             perdía despertares: deadlock en H7a). Las 7 lecturas `movzx r32, byte fs:[24h]`
 *             del juego se parchean al cargar a `mov r32, 0` (memory.c, ngb_memory_patch_irql).
 *   fs:[0x28] Prcb.CurrentThread -> TEB.ActiveRpcHandle := ETHREAD falso
 *
 * TLS Xbox: el compilador del XDK accede con [fs:[4] + _tls_index*4], con _tls_index
 * negativo: el bloque TLS vive justo debajo de StackBase. Por eso cada hilo Xbox corre en
 * una pila propia (VirtualAlloc) con el bloque TLS en la cima, y el TEB apunta a ella
 * (StackBase/StackLimit). La cadena SEH se reinicia con una copia del registro final de
 * ntdll dentro de la pila nueva, para que la validación de SEH/SEHOP la acepte.
 */
#include "ngb.h"

#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/kernel.h"

#define XBOX_MIN_STACK (1u << 20) /* las impl. del kernel y el log corren en esta pila */

static DWORD g_tls = TLS_OUT_OF_INDEXES;
static CRITICAL_SECTION g_threads_lock;
static ngb_thread_t *g_threads;
static volatile LONG g_thread_count;

typedef struct seh_record {
    struct seh_record *next;
    void *handler;
} seh_record_t;

static void threads_init_once(void) {
    if (g_tls == TLS_OUT_OF_INDEXES) {
        g_tls = TlsAlloc();
        InitializeCriticalSection(&g_threads_lock);
    }
}

void ngb_kernel_enter(ngb_thread_t *t) {
    if (t)
        InterlockedIncrement(&t->in_kernel);
}

void ngb_kernel_leave(ngb_thread_t *t) {
    if (!t || InterlockedDecrement(&t->in_kernel) != 0)
        return;
    /* Fuera ya de todo código propio: si el juego pidió suspender este hilo mientras estaba
     * dentro, se aparca aquí hasta el NtResumeThread. El evento se resetea bajo el lock y la
     * condición se vuelve a mirar tras despertar, así que un resume no se pierde. */
    EnterCriticalSection(&t->susp_lock);
    while (t->susp_park > 0 && t->in_kernel == 0) {
        ResetEvent(t->resume_evt);
        LeaveCriticalSection(&t->susp_lock);
        WaitForSingleObject(t->resume_evt, INFINITE);
        EnterCriticalSection(&t->susp_lock);
    }
    LeaveCriticalSection(&t->susp_lock);
}

LONG ngb_thread_suspend(ngb_thread_t *t) {
    LONG prev;
    CONTEXT ctx;
    EnterCriticalSection(&t->susp_lock);
    prev = t->susp_park + t->susp_host;
    if (t == ngb_thread_current() || t->in_kernel > 0) {
        t->susp_park++; /* dentro del kernel: se aparca al salir (ngb_kernel_leave) */
    } else if (SuspendThread(t->handle) == (DWORD)-1) {
        prev = -1;
    } else {
        /* SuspendThread es asíncrono: GetThreadContext espera a que el hilo pare de verdad.
         * Si entró al kernel justo antes, se deshace y se aparca. */
        ctx.ContextFlags = CONTEXT_CONTROL;
        GetThreadContext(t->handle, &ctx);
        if (t->in_kernel > 0) {
            ResumeThread(t->handle);
            t->susp_park++;
        } else {
            t->susp_host++;
        }
    }
    LeaveCriticalSection(&t->susp_lock);
    return prev;
}

LONG ngb_thread_resume(ngb_thread_t *t) {
    LONG prev;
    DWORD r;
    EnterCriticalSection(&t->susp_lock);
    prev = t->susp_park + t->susp_host;
    if (t->susp_park > 0) {
        if (--t->susp_park == 0)
            SetEvent(t->resume_evt);
    } else if (t->susp_host > 0) {
        ResumeThread(t->handle);
        t->susp_host--;
    } else {
        /* Sin suspensiones propias: el hilo se creó suspendido (PsCreateSystemThreadEx). */
        r = ResumeThread(t->handle);
        prev = r == (DWORD)-1 ? -1 : (LONG)r;
    }
    LeaveCriticalSection(&t->susp_lock);
    return prev;
}

ngb_thread_t *ngb_thread_current(void) {
    return g_tls == TLS_OUT_OF_INDEXES ? NULL : (ngb_thread_t *)TlsGetValue(g_tls);
}

uint32_t ngb_tls_total_size(const xbe_t *xbe) {
    uint32_t raw;
    if (!xbe->has_tls)
        return 0;
    /* Igual que el arranque de XAPI: align16(datos + ceros) + 4 del puntero al bloque. */
    raw = xbe->tls.raw_end - xbe->tls.raw_start + xbe->tls.zero_fill;
    return ((raw + 0xFu) & ~0xFu) + 4u;
}

static void init_tls_block(ngb_thread_t *t, uint8_t *tls) {
    const xbe_t *xbe = &g_ngb_xbe;
    uint32_t raw;
    if (!t->tls_size || !xbe->has_tls)
        return;
    /* [TlsData] = TlsData + 4 y detrás la plantilla .tls; lo mismo que XapiThreadStartup. */
    *(uint32_t *)tls = (uint32_t)(uintptr_t)(tls + 4);
    raw = xbe->tls.raw_end - xbe->tls.raw_start;
    if (raw && raw <= t->tls_size - 4)
        memcpy(tls + 4, (const void *)(uintptr_t)xbe->tls.raw_start, raw);
}

static void *final_seh_handler(void) {
    seh_record_t *r = (seh_record_t *)(uintptr_t)__readfsdword(0);
    void *handler = NULL;
    while (r && (uintptr_t)r != 0xFFFFFFFFu) {
        handler = r->handler;
        r = r->next;
    }
    return handler;
}

/* Cambia ESP a new_esp, llama fn(t) (cdecl) y vuelve a la pila original.
 * C4100 justificado: en una función naked los parámetros solo se leen desde el asm. */
#pragma warning(push)
#pragma warning(disable : 4100)
static __declspec(naked) void __cdecl call_on_stack(uint32_t new_esp,
                                                    void(__cdecl *fn)(ngb_thread_t *),
                                                    ngb_thread_t *t) {
    __asm {
        push ebp
        mov ebp, esp
        mov eax, [ebp + 8]
        mov ecx, [ebp + 12]
        mov edx, [ebp + 16]
        mov esp, eax
        push edx
        call ecx
        mov esp, ebp
        pop ebp
        ret
    }
}
#pragma warning(pop)

typedef void(__cdecl *entry_fn)(void);
typedef void(__stdcall *system_routine_fn)(uint32_t start_routine, uint32_t start_context);
typedef void(__stdcall *start_routine_fn)(uint32_t start_context);

static void __cdecl xbox_start(ngb_thread_t *t) {
    if (t->is_entry)
        ((entry_fn)(uintptr_t)t->start_routine)();
    else if (t->system_routine)
        ((system_routine_fn)(uintptr_t)t->system_routine)(t->start_routine, t->start_context);
    else
        ((start_routine_fn)(uintptr_t)t->start_routine)(t->start_context);
}

static void teb_overlay(ngb_thread_t *t) {
    __writefsdword(0x1C, (unsigned long)(uintptr_t)t->kpcr);
    __writefsdword(0x20, (unsigned long)(uintptr_t)(t->kpcr + 0x28));
    __writefsdword(0x28, (unsigned long)(uintptr_t)t->ethread);
}

/* C4733 justificado: se escribe fs:[0] a propósito para reiniciar la cadena SEH en la pila
 * Xbox con el manejador final de ntdll (no es un manejador nuestro sin registrar). */
#pragma warning(push)
#pragma warning(disable : 4733)
static DWORD WINAPI thread_proc(LPVOID param) {
    ngb_thread_t *t = (ngb_thread_t *)param;
    uint32_t size, top, tls_data, esp;
    uint8_t *stack;
    seh_record_t *seh;
    TlsSetValue(g_tls, t);

    size = (t->stack_size > XBOX_MIN_STACK ? t->stack_size : XBOX_MIN_STACK) + t->tls_size + 0x10000u;
    size = (size + 0xFFFFu) & ~0xFFFFu;
    stack = (uint8_t *)VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!stack)
        ngb_stop(NGB_EXIT_CRASH, "T%d: sin memoria para la pila Xbox (%u bytes)", t->index, size);
    t->xbox_stack = stack;
    top = (uint32_t)(uintptr_t)stack + size;
    tls_data = top - t->tls_size;
    init_tls_block(t, (uint8_t *)(uintptr_t)tls_data);

    /* KTHREAD: StackBase 0x1C, StackLimit 0x20, KernelStack 0x24, TlsData 0x28. */
    *(uint32_t *)(t->ethread + 0x1C) = top;
    *(uint32_t *)(t->ethread + 0x20) = (uint32_t)(uintptr_t)stack;
    *(uint32_t *)(t->ethread + 0x24) = tls_data;
    *(uint32_t *)(t->ethread + 0x28) = t->tls_size ? tls_data : 0;

    seh = (seh_record_t *)(uintptr_t)((tls_data - 0x20u) & ~0xFu);
    seh->next = (seh_record_t *)(uintptr_t)0xFFFFFFFFu;
    seh->handler = final_seh_handler();
    esp = ((uint32_t)(uintptr_t)seh - 0x10u) & ~0xFu;

    t->saved_stack_base = __readfsdword(0x04);
    t->saved_stack_limit = __readfsdword(0x08);
    t->saved_seh = __readfsdword(0x00);
    teb_overlay(t);
    ngb_log("hilo T%d (tid %lu): %s 0x%08X ctx 0x%08X, pila [0x%08X, 0x%08X), TlsData 0x%08X "
            "(%u B), ETHREAD %p",
            t->index, t->tid, t->is_entry ? "entry" : "start", t->start_routine, t->start_context,
            (uint32_t)(uintptr_t)stack, top, tls_data, t->tls_size, (void *)t->ethread);
    __writefsdword(0x04, top);
    __writefsdword(0x08, (unsigned long)(uintptr_t)stack);
    __writefsdword(0x00, (unsigned long)(uintptr_t)seh);

    call_on_stack(esp, xbox_start, t);

    __writefsdword(0x00, t->saved_seh);
    __writefsdword(0x04, t->saved_stack_base);
    __writefsdword(0x08, t->saved_stack_limit);
    InterlockedExchange(&t->exited, 1);
    ngb_log("hilo T%d: %s ha vuelto", t->index, t->is_entry ? "el entry point" : "la rutina");
    return 0;
}
#pragma warning(pop)

ngb_thread_t *ngb_thread_spawn(uint32_t start_routine, uint32_t start_context,
                               uint32_t system_routine, uint32_t stack_size, uint32_t tls_size,
                               int suspended, int is_entry) {
    ngb_thread_t *t;
    uint8_t *block;
    threads_init_once();
    t = (ngb_thread_t *)calloc(1, sizeof *t);
    block = (uint8_t *)VirtualAlloc(NULL, NGB_KPCR_SIZE + NGB_KTHREAD_SIZE * 2,
                                    MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (t && block) {
        t->resume_evt = CreateEventA(NULL, TRUE, FALSE, NULL);
        InitializeCriticalSection(&t->susp_lock);
    }
    if (!t || !block || !t->resume_evt) {
        if (t && block) {
            if (t->resume_evt)
                CloseHandle(t->resume_evt);
            DeleteCriticalSection(&t->susp_lock);
        }
        free(t);
        if (block)
            VirtualFree(block, 0, MEM_RELEASE);
        return NULL;
    }
    t->kpcr = block;
    t->ethread = block + NGB_KPCR_SIZE;
    t->start_routine = start_routine;
    t->start_context = start_context;
    t->system_routine = system_routine;
    t->stack_size = stack_size;
    t->tls_size = tls_size;
    t->is_entry = is_entry;
    t->index = (int)InterlockedIncrement(&g_thread_count);

    /* KPCR: NtTib.Self 0x18, SelfPcr 0x1C, Prcb 0x20, Irql 0x24, PrcbData 0x28. */
    *(uint32_t *)(t->kpcr + 0x18) = (uint32_t)(uintptr_t)t->kpcr;
    *(uint32_t *)(t->kpcr + 0x1C) = (uint32_t)(uintptr_t)t->kpcr;
    *(uint32_t *)(t->kpcr + 0x20) = (uint32_t)(uintptr_t)(t->kpcr + 0x28);
    *(uint32_t *)(t->kpcr + 0x28) = (uint32_t)(uintptr_t)t->ethread;
    /* KTHREAD.Header: Type = ThreadObject (6), Size en dwords. */
    t->ethread[0] = 6;
    t->ethread[2] = (uint8_t)(NGB_KTHREAD_SIZE / 4);
    *(uint32_t *)(t->ethread + 0x130) = start_routine; /* ETHREAD.StartAddress */

    t->handle = CreateThread(NULL, 0x40000, thread_proc, t, CREATE_SUSPENDED, &t->tid);
    if (!t->handle) {
        ngb_log("hilo: CreateThread falló (%lu)", GetLastError());
        VirtualFree(block, 0, MEM_RELEASE);
        free(t);
        return NULL;
    }
    *(uint32_t *)(t->ethread + 0x12C) = t->tid; /* ETHREAD.UniqueThread */
    EnterCriticalSection(&g_threads_lock);
    t->next = g_threads;
    g_threads = t;
    LeaveCriticalSection(&g_threads_lock);
    if (!suspended)
        ResumeThread(t->handle);
    return t;
}

ngb_thread_t *ngb_thread_from_ethread(uint32_t ethread) {
    ngb_thread_t *t;
    EnterCriticalSection(&g_threads_lock);
    for (t = g_threads; t; t = t->next)
        if ((uint32_t)(uintptr_t)t->ethread == ethread)
            break;
    LeaveCriticalSection(&g_threads_lock);
    return t;
}

ngb_thread_t *ngb_thread_from_handle(HANDLE h) {
    ngb_thread_t *t;
    DWORD tid = GetThreadId(h);
    if (!tid)
        return NULL;
    EnterCriticalSection(&g_threads_lock);
    for (t = g_threads; t; t = t->next)
        if (t->tid == tid)
            break;
    LeaveCriticalSection(&g_threads_lock);
    return t;
}

__declspec(noreturn) void ngb_thread_exit(uint32_t status) {
    ngb_thread_t *t = ngb_thread_current();
    if (t) {
        InterlockedExchange(&t->exited, 1);
        ngb_log("hilo T%d: PsTerminateSystemThread(0x%08X)", t->index, status);
    }
    /* Seguimos en la pila Xbox, que el TEB describe bien: ExitThread puede correr aquí. */
    ExitThread(status);
}

int ngb_thread_alive_count(void) {
    ngb_thread_t *t;
    int n = 0;
    if (g_tls == TLS_OUT_OF_INDEXES)
        return 0;
    EnterCriticalSection(&g_threads_lock);
    for (t = g_threads; t; t = t->next) {
        if (!t->exited && WaitForSingleObject(t->handle, 0) == WAIT_OBJECT_0)
            InterlockedExchange(&t->exited, 1);
        if (!t->exited && !t->internal)
            n++;
    }
    LeaveCriticalSection(&g_threads_lock);
    return n;
}

static void log_inflight(const ngb_thread_t *t) {
    if (t->inflight_ordinal) {
        const ngb_kexport_t *e = ngb_kexport(t->inflight_ordinal);
        ngb_log("  T%d dentro de %u %s (llamada desde 0x%08X)", t->index, t->inflight_ordinal,
                e ? e->name : "?", t->inflight_caller);
    }
}

void ngb_thread_dump_all(void) {
    ngb_thread_t *t, *self = ngb_thread_current();
    if (g_tls == TLS_OUT_OF_INDEXES)
        return;
    if (!TryEnterCriticalSection(&g_threads_lock))
        return;
    ngb_log("hilos Xbox:");
    for (t = g_threads; t; t = t->next) {
        CONTEXT ctx;
        char bytes[200];
        if (t->exited) {
            ngb_log("  T%d (tid %lu): terminado", t->index, t->tid);
            continue;
        }
        if (t->internal)
            ngb_log("  T%d (tid %lu): hilo interno del loader (DPC/timers)", t->index, t->tid);
        if (t == self) {
            ngb_log("  T%d (tid %lu): hilo actual (el que para)", t->index, t->tid);
            log_inflight(t);
            continue;
        }
        if (SuspendThread(t->handle) == (DWORD)-1) {
            ngb_log("  T%d (tid %lu): no se pudo suspender", t->index, t->tid);
            continue;
        }
        memset(&ctx, 0, sizeof ctx);
        ctx.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(t->handle, &ctx)) {
            uint32_t *sp = (uint32_t *)(uintptr_t)ctx.Esp;
            SIZE_T got = 0;
            uint32_t stackw[8] = {0};
            ReadProcessMemory(GetCurrentProcess(), sp, stackw, sizeof stackw, &got);
            ngb_hexdump_line(bytes, sizeof bytes, ctx.Eip, 16);
            ngb_log("  T%d (tid %lu): EIP 0x%08lX ESP 0x%08lX EAX 0x%08lX EBX 0x%08lX ECX 0x%08lX "
                    "EDX 0x%08lX ESI 0x%08lX EDI 0x%08lX | %s",
                    t->index, t->tid, ctx.Eip, ctx.Esp, ctx.Eax, ctx.Ebx, ctx.Ecx, ctx.Edx, ctx.Esi,
                    ctx.Edi, bytes);
            ngb_log("     pila: %08X %08X %08X %08X %08X %08X %08X %08X", stackw[0], stackw[1],
                    stackw[2], stackw[3], stackw[4], stackw[5], stackw[6], stackw[7]);
        }
        log_inflight(t);
    }
    LeaveCriticalSection(&g_threads_lock);
}
