/*
 * impl_rtl.c: Rtl* (strings, critical sections, exceptions, conversions).
 *
 * Critical sections: the Xbox RTL_CRITICAL_SECTION (0x1C bytes: DISPATCHER_HEADER,
 * LockCount, RecursionCount, OwningThread) does not match the Win32 one. Each Xbox
 * address is associated (hash table) with a Windows CRITICAL_SECTION created on first use, and
 * LockCount/RecursionCount/OwningThread are mirrored into the Xbox structure in case the game
 * looks at them.
 */
#include "../ngb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel.h"

#define CS_SLOTS 16384

typedef struct cs_slot {
    uint32_t key;
    CRITICAL_SECTION *cs;
} cs_slot_t;

static cs_slot_t g_cs[CS_SLOTS];
static SRWLOCK g_cs_lock = SRWLOCK_INIT;

static CRITICAL_SECTION *cs_lookup(uint32_t addr, int create) {
    uint32_t h = (addr >> 2) * 2654435761u, i;
    CRITICAL_SECTION *found = NULL;
    AcquireSRWLockShared(&g_cs_lock);
    for (i = 0; i < CS_SLOTS; i++) {
        cs_slot_t *s = &g_cs[(h + i) % CS_SLOTS];
        if (s->key == addr) {
            found = s->cs;
            break;
        }
        if (!s->key)
            break;
    }
    ReleaseSRWLockShared(&g_cs_lock);
    if (found || !create)
        return found;
    AcquireSRWLockExclusive(&g_cs_lock);
    for (i = 0; i < CS_SLOTS; i++) {
        cs_slot_t *s = &g_cs[(h + i) % CS_SLOTS];
        if (s->key == addr) {
            found = s->cs;
            break;
        }
        if (!s->key) {
            found = (CRITICAL_SECTION *)malloc(sizeof *found);
            InitializeCriticalSection(found);
            s->cs = found;
            s->key = addr;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_cs_lock);
    if (!found)
        ngb_stop(NGB_EXIT_CRASH, "critical section table full");
    return found;
}

static uint32_t current_kthread(void) {
    ngb_thread_t *t = ngb_thread_current();
    return t ? (uint32_t)(uintptr_t)t->ethread : 0;
}

/* 291 VOID RtlInitializeCriticalSection(PRTL_CRITICAL_SECTION CriticalSection) */
static uint64_t RtlInitializeCriticalSection(uint32_t *args) {
    uint8_t *x = (uint8_t *)PTR(0);
    memset(x, 0, 0x1C);
    x[0] = 1; /* EventSynchronizationObject */
    x[2] = 4;
    *(int32_t *)(x + 0x10) = -1;
    cs_lookup(ARG(0), 1);
    return 0;
}

/* 277 VOID RtlEnterCriticalSection(PRTL_CRITICAL_SECTION CriticalSection) */
static uint64_t RtlEnterCriticalSection(uint32_t *args) {
    uint8_t *x = (uint8_t *)PTR(0);
    EnterCriticalSection(cs_lookup(ARG(0), 1));
    InterlockedIncrement((volatile LONG *)(x + 0x10));
    (*(int32_t *)(x + 0x14))++;
    *(uint32_t *)(x + 0x18) = current_kthread();
    return 0;
}

/* 294 VOID RtlLeaveCriticalSection(PRTL_CRITICAL_SECTION CriticalSection) */
static uint64_t RtlLeaveCriticalSection(uint32_t *args) {
    uint8_t *x = (uint8_t *)PTR(0);
    CRITICAL_SECTION *cs = cs_lookup(ARG(0), 0);
    if (!cs)
        return 0;
    if (--(*(int32_t *)(x + 0x14)) <= 0) {
        *(int32_t *)(x + 0x14) = 0;
        *(uint32_t *)(x + 0x18) = 0;
    }
    InterlockedDecrement((volatile LONG *)(x + 0x10));
    LeaveCriticalSection(cs);
    return 0;
}

/* 278 BOOLEAN RtlTryEnterCriticalSection(PRTL_CRITICAL_SECTION CriticalSection) */
static uint64_t RtlTryEnterCriticalSection(uint32_t *args) {
    uint8_t *x = (uint8_t *)PTR(0);
    if (!TryEnterCriticalSection(cs_lookup(ARG(0), 1)))
        return 0;
    InterlockedIncrement((volatile LONG *)(x + 0x10));
    (*(int32_t *)(x + 0x14))++;
    *(uint32_t *)(x + 0x18) = current_kthread();
    return 1;
}

/* 289 VOID RtlInitAnsiString(PANSI_STRING DestinationString, PCSZ SourceString) */
static uint64_t RtlInitAnsiString(uint32_t *args) {
    uint16_t *dst = (uint16_t *)PTR(0);
    const char *src = (const char *)PTR(1);
    size_t len = src ? strlen(src) : 0;
    dst[0] = (uint16_t)len;
    dst[1] = (uint16_t)(src ? len + 1 : 0);
    *(uint32_t *)(dst + 2) = ARG(1);
    return 0;
}

/* 260 NTSTATUS RtlAnsiStringToUnicodeString(PUNICODE_STRING Dest, PSTRING Src, BOOLEAN Alloc) */
static uint64_t RtlAnsiStringToUnicodeString(uint32_t *args) {
    uint16_t *dst = (uint16_t *)PTR(0), *src = (uint16_t *)PTR(1);
    const uint8_t *s = (const uint8_t *)(uintptr_t) * (uint32_t *)(src + 2);
    uint16_t len = src[0], i;
    uint16_t *buf;
    if (ARG(2) & 0xFF) {
        buf = (uint16_t *)calloc((size_t)len + 1, 2);
        if (!buf)
            return RET32(STATUS_NO_MEMORY);
        *(uint32_t *)(dst + 2) = (uint32_t)(uintptr_t)buf;
        dst[1] = (uint16_t)((len + 1) * 2);
    } else {
        buf = (uint16_t *)(uintptr_t) * (uint32_t *)(dst + 2);
        if ((uint32_t)len * 2 > dst[1])
            return RET32(0x80000005u); /* STATUS_BUFFER_OVERFLOW */
    }
    for (i = 0; i < len; i++)
        buf[i] = s[i];
    if ((uint32_t)len * 2 + 2 <= dst[1])
        buf[len] = 0;
    dst[0] = (uint16_t)(len * 2);
    return STATUS_SUCCESS;
}

/* 279 BOOLEAN RtlEqualString(PSTRING String1, PSTRING String2, BOOLEAN CaseInSensitive) */
static uint64_t RtlEqualString(uint32_t *args) {
    uint16_t *a = (uint16_t *)PTR(0), *b = (uint16_t *)PTR(1);
    const char *sa = (const char *)(uintptr_t) * (uint32_t *)(a + 2);
    const char *sb = (const char *)(uintptr_t) * (uint32_t *)(b + 2);
    if (a[0] != b[0])
        return 0;
    if (ARG(2) & 0xFF)
        return _strnicmp(sa, sb, a[0]) == 0;
    return memcmp(sa, sb, a[0]) == 0;
}

/* 269 SIZE_T RtlCompareMemoryUlong(PVOID Source, SIZE_T Length, ULONG Pattern) */
static uint64_t RtlCompareMemoryUlong(uint32_t *args) {
    const uint32_t *p = (const uint32_t *)PTR(0);
    uint32_t n = ARG(1) / 4, i;
    for (i = 0; i < n && p[i] == ARG(2); i++)
        ;
    return RET32(i * 4);
}

typedef ULONG(WINAPI *ntstatus_to_dos_fn)(LONG);
typedef VOID(WINAPI *rtl_unwind_fn)(PVOID, PVOID, PEXCEPTION_RECORD, PVOID);
typedef VOID(WINAPI *rtl_raise_fn)(PEXCEPTION_RECORD);

static FARPROC ntdll_proc(const char *name) {
    return GetProcAddress(GetModuleHandleA("ntdll.dll"), name);
}

/* 301 ULONG RtlNtStatusToDosError(NTSTATUS Status) */
static uint64_t RtlNtStatusToDosError(uint32_t *args) {
    static ntstatus_to_dos_fn fn;
    if (!fn)
        fn = (ntstatus_to_dos_fn)(void *)ntdll_proc("RtlNtStatusToDosError");
    return RET32(fn((LONG)ARG(0)));
}

/* 312 VOID RtlUnwind(PVOID TargetFrame, PVOID TargetIp, PEXCEPTION_RECORD, PVOID ReturnValue):
 * forwarded to ntdll; the SEH chain (fs:[0]) and the records have the same format. */
static uint64_t RtlUnwind_(uint32_t *args) {
    static rtl_unwind_fn fn;
    if (!fn)
        fn = (rtl_unwind_fn)(void *)ntdll_proc("RtlUnwind");
    fn(PTR(0), PTR(1), (PEXCEPTION_RECORD)PTR(2), PTR(3));
    return 0;
}

/* 302 VOID RtlRaiseException(PEXCEPTION_RECORD ExceptionRecord) */
static uint64_t RtlRaiseException_(uint32_t *args) {
    EXCEPTION_RECORD *r = (EXCEPTION_RECORD *)PTR(0);
    ngb_log("    · RtlRaiseException code 0x%08lX at 0x%p", r->ExceptionCode, r->ExceptionAddress);
    RaiseException(r->ExceptionCode, r->ExceptionFlags, r->NumberParameters,
                   r->ExceptionInformation);
    return 0;
}

/* 305 VOID RtlTimeToTimeFields(PLARGE_INTEGER Time, PTIME_FIELDS TimeFields) */
static uint64_t RtlTimeToTimeFields(uint32_t *args) {
    SYSTEMTIME st;
    int16_t *tf = (int16_t *)PTR(1);
    FileTimeToSystemTime((const FILETIME *)PTR(0), &st);
    /* TIME_FIELDS: Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday. */
    tf[0] = (int16_t)st.wYear;
    tf[1] = (int16_t)st.wMonth;
    tf[2] = (int16_t)st.wDay;
    tf[3] = (int16_t)st.wHour;
    tf[4] = (int16_t)st.wMinute;
    tf[5] = (int16_t)st.wSecond;
    tf[6] = (int16_t)st.wMilliseconds;
    tf[7] = (int16_t)st.wDayOfWeek;
    return 0;
}

/* 304 BOOLEAN RtlTimeFieldsToTime(PTIME_FIELDS TimeFields, PLARGE_INTEGER Time) */
static uint64_t RtlTimeFieldsToTime(uint32_t *args) {
    SYSTEMTIME st;
    const int16_t *tf = (const int16_t *)PTR(0);
    memset(&st, 0, sizeof st);
    st.wYear = (WORD)tf[0];
    st.wMonth = (WORD)tf[1];
    st.wDay = (WORD)tf[2];
    st.wHour = (WORD)tf[3];
    st.wMinute = (WORD)tf[4];
    st.wSecond = (WORD)tf[5];
    st.wMilliseconds = (WORD)tf[6];
    return SystemTimeToFileTime(&st, (FILETIME *)PTR(1)) ? 1 : 0;
}

static const ngb_kimpl_t k_impls[] = {
    {260, RtlAnsiStringToUnicodeString},
    {269, RtlCompareMemoryUlong},
    {277, RtlEnterCriticalSection},
    {278, RtlTryEnterCriticalSection},
    {279, RtlEqualString},
    {289, RtlInitAnsiString},
    {291, RtlInitializeCriticalSection},
    {294, RtlLeaveCriticalSection},
    {301, RtlNtStatusToDosError},
    {302, RtlRaiseException_},
    {304, RtlTimeFieldsToTime},
    {305, RtlTimeToTimeFields},
    {312, RtlUnwind_},
};

void ngb_impl_register_rtl(void) {
    ngb_kernel_register(k_impls, sizeof k_impls / sizeof k_impls[0]);
}
