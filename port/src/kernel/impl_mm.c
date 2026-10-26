/*
 * impl_mm.c: memory (Mm*, Nt*VirtualMemory, Ex* pool).
 * Prototypes: nxdk lib/xboxkrnl/xboxkrnl.h (CC0).
 */
#include "../ngb.h"

#include <string.h>

#include "kernel.h"

#define XBOX_PAGE_NOCACHE 0x200u
#define XBOX_PAGE_WRITECOMBINE 0x400u
#define XBOX_MEM_NOZERO 0x800000u

static HANDLE g_pool;

static DWORD win_protect(uint32_t protect) {
    DWORD p = protect & ~(XBOX_PAGE_NOCACHE | XBOX_PAGE_WRITECOMBINE);
    return p ? p : PAGE_READWRITE;
}

/* 165 PVOID MmAllocateContiguousMemory(SIZE_T NumberOfBytes) */
static uint64_t MmAllocateContiguousMemory(uint32_t *args) {
    return RET32((uintptr_t)ngb_contig_alloc(ARG(0), 0x1000, 0, 0xFFFFFFFFu));
}

/* 166 PVOID MmAllocateContiguousMemoryEx(Size, Lowest, Highest, Alignment, Protect) */
static uint64_t MmAllocateContiguousMemoryEx(uint32_t *args) {
    uint32_t highest = ARG(2) ? NGB_CONTIG_BASE + ARG(2) : 0;
    return RET32((uintptr_t)ngb_contig_alloc(ARG(0), ARG(3), ARG(1), highest));
}

/* 171 VOID MmFreeContiguousMemory(PVOID BaseAddress) */
static uint64_t MmFreeContiguousMemory(uint32_t *args) {
    ngb_contig_free(PTR(0));
    return 0;
}

/* 178 VOID MmPersistContiguousMemory(PVOID BaseAddress, SIZE_T NumberOfBytes, BOOLEAN Persist) */
static uint64_t MmPersistContiguousMemory(uint32_t *args) {
    (void)args;
    return 0;
}

/* 173 ULONG_PTR MmGetPhysicalAddress(PVOID BaseAddress) */
static uint64_t MmGetPhysicalAddress(uint32_t *args) {
    /* Simplified "physical" (see gpu/nv2a.c, nv2a_phys): the contiguous arena is the RAM
     * 0..64 MB; the XBE image is seen in the NGB_PHYS_XBE window (96 MB + VA), so that the
     * GPU reads textures and buffers the game keeps in .data; the rest, identity. */
    if (ngb_contig_contains(PTR(0)))
        return RET32(ARG(0) - NGB_CONTIG_BASE);
    if (ARG(0) >= NGB_RESERVE_BASE && ARG(0) < NGB_RESERVE_END)
        return RET32(NGB_PHYS_XBE + ARG(0));
    return RET32(ARG(0) & 0x7FFFFFFFu);
}

/* 180 SIZE_T MmQueryAllocationSize(PVOID BaseAddress) */
static uint64_t MmQueryAllocationSize(uint32_t *args) {
    MEMORY_BASIC_INFORMATION m;
    uint32_t size = ngb_contig_size(PTR(0));
    if (size)
        return RET32(size);
    if (VirtualQuery(PTR(0), &m, sizeof m))
        return RET32(m.RegionSize);
    return 0;
}

/* 179 ULONG MmQueryAddressProtect(PVOID VirtualAddress) */
static uint64_t MmQueryAddressProtect(uint32_t *args) {
    MEMORY_BASIC_INFORMATION m;
    if (VirtualQuery(PTR(0), &m, sizeof m) && m.State == MEM_COMMIT)
        return RET32(m.Protect);
    return 0;
}

/* 182 VOID MmSetAddressProtect(PVOID BaseAddress, ULONG NumberOfBytes, ULONG NewProtect) */
static uint64_t MmSetAddressProtect(uint32_t *args) {
    DWORD old, p = win_protect(ARG(2));
    VirtualProtect(PTR(0), ARG(1), p, &old);
    /* D3D calls this several times per frame to make buffers write-combined (0x404), which
     * keeps them readable: only a change that takes access away empties the caches. */
    if (p & (PAGE_NOACCESS | PAGE_GUARD))
        ngb_mem_access_changed();
    return 0;
}

/* 184 NTSTATUS NtAllocateVirtualMemory(PVOID *Base, ULONG_PTR ZeroBits, PSIZE_T Size,
 *                                      ULONG AllocationType, ULONG Protect) */
static uint64_t NtAllocateVirtualMemory(uint32_t *args) {
    uint32_t *base = (uint32_t *)PTR(0), *size = (uint32_t *)PTR(2);
    DWORD type = ARG(3) & ~XBOX_MEM_NOZERO;
    void *p = VirtualAlloc((void *)(uintptr_t)*base, *size, type, win_protect(ARG(4)));
    if (!p) {
        ngb_log("    · VirtualAlloc(0x%08X, 0x%X, 0x%X, 0x%X) failed: %lu", *base, *size, type,
                ARG(4), GetLastError());
        return RET32(GetLastError() == ERROR_INVALID_ADDRESS ? 0xC0000018u : STATUS_NO_MEMORY);
    }
    /* VirtualAlloc already returns the rounded base (64 KB when reserving, page when committing). */
    if (*base)
        *size = ((*base + *size + 0xFFFu) & ~0xFFFu) - (uint32_t)(uintptr_t)p;
    else
        *size = (*size + 0xFFFu) & ~0xFFFu;
    *base = (uint32_t)(uintptr_t)p;
    return STATUS_SUCCESS;
}

/* 199 NTSTATUS NtFreeVirtualMemory(PVOID *BaseAddress, PSIZE_T FreeSize, ULONG FreeType) */
static uint64_t NtFreeVirtualMemory(uint32_t *args) {
    uint32_t *base = (uint32_t *)PTR(0), *size = (uint32_t *)PTR(1);
    DWORD type = ARG(2);
    ngb_mem_access_changed();
    if (!VirtualFree((void *)(uintptr_t)*base, type == MEM_RELEASE ? 0 : *size, type))
        return RET32(STATUS_INVALID_PARAMETER);
    return STATUS_SUCCESS;
}

/* 217 NTSTATUS NtQueryVirtualMemory(PVOID BaseAddress, PMEMORY_BASIC_INFORMATION Buffer) */
static uint64_t NtQueryVirtualMemory(uint32_t *args) {
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery(PTR(0), &m, sizeof m))
        return RET32(STATUS_INVALID_PARAMETER);
    memcpy(PTR(1), &m, sizeof m); /* same 28-byte layout on Xbox and Win32 */
    return STATUS_SUCCESS;
}

/* 181 NTSTATUS MmQueryStatistics(PMM_STATISTICS MemoryStatistics) */
static uint64_t MmQueryStatistics(uint32_t *args) {
    uint32_t *s = (uint32_t *)PTR(0);
    if (!s || s[0] != 36)
        return RET32(STATUS_INVALID_PARAMETER);
    s[1] = 16384; /* TotalPhysicalPages: 64 MB */
    s[2] = 12288; /* AvailablePages */
    s[3] = 4 << 20;
    s[4] = 8 << 20;
    s[5] = 0;
    s[6] = 256;
    s[7] = 64;
    s[8] = 1024;
    return STATUS_SUCCESS;
}

/* 169 PVOID MmCreateKernelStack(ULONG NumberOfBytes, BOOLEAN DebuggerThread) */
static uint64_t MmCreateKernelStack(uint32_t *args) {
    uint32_t size = (ARG(0) + 0xFFFu) & ~0xFFFu;
    uint8_t *p = (uint8_t *)VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    return p ? RET32((uintptr_t)(p + size)) : 0;
}

/* 170 VOID MmDeleteKernelStack(PVOID StackBase, PVOID StackLimit) */
static uint64_t MmDeleteKernelStack(uint32_t *args) {
    ngb_mem_access_changed();
    VirtualFree(PTR(1), 0, MEM_RELEASE);
    return 0;
}

/* 175 VOID MmLockUnlockBufferPages(PVOID BaseAddress, SIZE_T NumberOfBytes, BOOLEAN UnlockPages)
 * 176 VOID MmLockUnlockPhysicalPage(ULONG_PTR PhysicalAddress, BOOLEAN UnlockPage) */
static uint64_t MmLockUnlock(uint32_t *args) {
    (void)args;
    return 0;
}

/* 168 PVOID MmClaimGpuInstanceMemory(SIZE_T NumberOfBytes, PSIZE_T NumberOfPaddingBytes):
 * the NV2A instance memory is at the top of physical RAM. With 64 MB, the
 * last 64 KB are padding and the function returns the END of the area (0x83FF0000). */
static uint64_t MmClaimGpuInstanceMemory(uint32_t *args) {
    uint32_t padding = 0x10000u, top = NGB_CONTIG_BASE + NGB_CONTIG_SIZE - padding;
    uint32_t size = (ARG(0) + 0xFFFu) & ~0xFFFu;
    if (PTR(1))
        *(uint32_t *)PTR(1) = padding;
    MEMORY_BASIC_INFORMATION m;
    if (size &&
        !(VirtualQuery((void *)(uintptr_t)(top - size), &m, sizeof m) && m.State == MEM_COMMIT) &&
        !VirtualAlloc((void *)(uintptr_t)(top - size), size + padding, MEM_COMMIT, PAGE_READWRITE))
        ngb_log("    · MmClaimGpuInstanceMemory: could not commit [0x%08X, 0x%08X)",
                top - size, top + padding);
    return RET32(top);
}

/* 15 PVOID ExAllocatePoolWithTag(SIZE_T NumberOfBytes, ULONG Tag) */
static uint64_t ExAllocatePoolWithTag(uint32_t *args) {
    if (!g_pool)
        g_pool = HeapCreate(0, 0, 0);
    return RET32((uintptr_t)HeapAlloc(g_pool, HEAP_ZERO_MEMORY, ARG(0) ? ARG(0) : 1));
}

/* 14 PVOID ExAllocatePool(SIZE_T NumberOfBytes) */
static uint64_t ExAllocatePool(uint32_t *args) {
    uint32_t a[2];
    a[0] = ARG(0);
    a[1] = 0x656E6F4Eu; /* 'None' */
    return ExAllocatePoolWithTag(a);
}

/* 17 VOID ExFreePool(PVOID P) */
static uint64_t ExFreePool(uint32_t *args) {
    if (g_pool && PTR(0))
        HeapFree(g_pool, 0, PTR(0));
    return 0;
}

/* 23 ULONG ExQueryPoolBlockSize(PVOID PoolBlock) */
static uint64_t ExQueryPoolBlockSize(uint32_t *args) {
    return g_pool && PTR(0) ? RET32(HeapSize(g_pool, 0, PTR(0))) : 0;
}

static const ngb_kimpl_t k_impls[] = {
    {14, ExAllocatePool},
    {15, ExAllocatePoolWithTag},
    {17, ExFreePool},
    {23, ExQueryPoolBlockSize},
    {165, MmAllocateContiguousMemory},
    {166, MmAllocateContiguousMemoryEx},
    {168, MmClaimGpuInstanceMemory},
    {169, MmCreateKernelStack},
    {170, MmDeleteKernelStack},
    {171, MmFreeContiguousMemory},
    {173, MmGetPhysicalAddress},
    {175, MmLockUnlock},
    {176, MmLockUnlock},
    {178, MmPersistContiguousMemory},
    {179, MmQueryAddressProtect},
    {180, MmQueryAllocationSize},
    {181, MmQueryStatistics},
    {182, MmSetAddressProtect},
    {184, NtAllocateVirtualMemory},
    {199, NtFreeVirtualMemory},
    {217, NtQueryVirtualMemory},
};

void ngb_impl_register_mm(void) {
    ngb_kernel_register(k_impls, sizeof k_impls / sizeof k_impls[0]);
}
