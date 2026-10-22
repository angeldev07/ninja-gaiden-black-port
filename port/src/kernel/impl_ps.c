/*
 * impl_ps.c: threads (Ps*, Nt*Thread) and object references (Ob*).
 */
#include "../ngb.h"

#include <string.h>

#include "kernel.h"

/* 255 NTSTATUS PsCreateSystemThreadEx(PHANDLE ThreadHandle, SIZE_T ThreadExtensionSize,
 *     SIZE_T KernelStackSize, SIZE_T TlsDataSize, PHANDLE ThreadId, PKSTART_ROUTINE StartRoutine,
 *     PVOID StartContext, BOOLEAN CreateSuspended, BOOLEAN DebuggerThread,
 *     PKSYSTEM_ROUTINE SystemRoutine) */
static uint64_t PsCreateSystemThreadEx(uint32_t *args) {
    HANDLE dup = NULL;
    ngb_thread_t *t = ngb_thread_spawn(ARG(5), ARG(6), ARG(9), ARG(2), ARG(3), ARG(7) & 0xFF, 0);
    if (!t)
        return RET32(STATUS_NO_MEMORY);
    DuplicateHandle(GetCurrentProcess(), t->handle, GetCurrentProcess(), &dup, 0, FALSE,
                    DUPLICATE_SAME_ACCESS);
    if (PTR(0))
        *(uint32_t *)PTR(0) = (uint32_t)(uintptr_t)dup;
    if (PTR(4))
        *(uint32_t *)PTR(4) = t->tid;
    ngb_log("    · PsCreateSystemThreadEx: T%d tid %lu, start 0x%08X ctx 0x%08X system 0x%08X, "
            "stack 0x%X, TLS %u, extension %u",
            t->index, t->tid, ARG(5), ARG(6), ARG(9), ARG(2), ARG(3), ARG(1));
    return STATUS_SUCCESS;
}

/* 254 NTSTATUS PsCreateSystemThread(PHANDLE, PHANDLE ThreadId, PKSTART_ROUTINE, PVOID, BOOLEAN) */
static uint64_t PsCreateSystemThread(uint32_t *args) {
    uint32_t a[10];
    memset(a, 0, sizeof a);
    a[0] = ARG(0);
    a[2] = 0x10000;
    a[4] = ARG(1);
    a[5] = ARG(2);
    a[6] = ARG(3);
    a[8] = ARG(4); /* DebuggerThread (nxdk: ThreadHandle, ThreadId, StartRoutine,
                    * StartContext, DebuggerThread); a[7] is CreateSuspended, which stays 0 */
    return PsCreateSystemThreadEx(a);
}

/* 258 VOID PsTerminateSystemThread(NTSTATUS ExitStatus) */
static uint64_t PsTerminateSystemThread(uint32_t *args) {
    ngb_thread_exit(ARG(0));
}

/* 246 NTSTATUS ObReferenceObjectByHandle(HANDLE, POBJECT_TYPE, PVOID *ReturnedObject) */
static uint64_t ObReferenceObjectByHandle(uint32_t *args) {
    ngb_thread_t *t = NULL;
    HANDLE h = (HANDLE)PTR(0);
    if (h == (HANDLE)(intptr_t)-2)
        t = ngb_thread_current();
    else
        t = ngb_thread_from_handle(h);
    if (t) {
        *(uint32_t *)PTR(2) = (uint32_t)(uintptr_t)t->ethread;
        return STATUS_SUCCESS;
    }
    ngb_log("    · ObReferenceObjectByHandle: handle 0x%08X (type 0x%08X) is not a thread", ARG(0),
            ARG(1));
    return RET32(STATUS_INVALID_HANDLE);
}

/* 250 VOID FASTCALL ObfDereferenceObject(PVOID) / 251 VOID FASTCALL ObfReferenceObject(PVOID) */
static uint64_t ObfReference(uint32_t *args) {
    (void)args;
    return 0;
}

/* 231 NTSTATUS NtSuspendThread(HANDLE, PULONG PreviousSuspendCount) */
static uint64_t NtSuspendThread(uint32_t *args) {
    /* An Xbox thread inside the emulated kernel or the VEH may hold loader locks: it is never
     * frozen there; it is parked on exit (ngb_thread_suspend, thread.c). */
    ngb_thread_t *t = ngb_thread_from_handle((HANDLE)PTR(0));
    LONG r = t ? ngb_thread_suspend(t) : (LONG)SuspendThread((HANDLE)PTR(0));
    if (r == -1)
        return RET32(STATUS_INVALID_HANDLE);
    if (PTR(1))
        *(uint32_t *)PTR(1) = r;
    return STATUS_SUCCESS;
}

/* 224 NTSTATUS NtResumeThread(HANDLE, PULONG PreviousSuspendCount) */
static uint64_t NtResumeThread(uint32_t *args) {
    ngb_thread_t *t = ngb_thread_from_handle((HANDLE)PTR(0));
    LONG r = t ? ngb_thread_resume(t) : (LONG)ResumeThread((HANDLE)PTR(0));
    if (r == -1)
        return RET32(STATUS_INVALID_HANDLE);
    if (PTR(1))
        *(uint32_t *)PTR(1) = r;
    return STATUS_SUCCESS;
}

static const ngb_kimpl_t k_impls[] = {
    {224, NtResumeThread},
    {231, NtSuspendThread},
    {246, ObReferenceObjectByHandle},
    {250, ObfReference},
    {251, ObfReference},
    {254, PsCreateSystemThread},
    {255, PsCreateSystemThreadEx},
    {258, PsTerminateSystemThread},
};

void ngb_impl_register_ps(void) {
    ngb_kernel_register(k_impls, sizeof k_impls / sizeof k_impls[0]);
}
