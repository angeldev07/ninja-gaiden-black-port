/*
 * nv2a.c: NV2A MMIO registers served from the VEH.
 *
 * Mechanism (same as H6 for traced MMIO, now with a model):
 *   1. The region [0xFD000000, 0xFE000000) is reserved with no access. An access faults.
 *   2. The VEH writes the register's modeled value (nv2a_value) into the page, opens it,
 *      sets TF and lets that single instruction run.
 *   3. On SINGLE_STEP it reads what is left in the page: if it was a write, it applies its
 *      effects (nv2a_write: W1C of interrupts, DMA_PUT -> decode the FIFO...), stores the
 *      value in the g_regs shadow and closes the page again.
 * Cost: two exceptions per access. In the D3D init that is a few thousand; per frame D3D
 * only touches PUT/GET and a few registers, so mapping "RAM except registers with side
 * effects" does not pay off. A lock serializes accesses from several threads.
 *
 * PRAMIN (0xFD700000, 1 MB) does not go through here: it is the instance RAM (RAMHT, RAMFC,
 * DMA and class objects), the last MB of physical RAM. A 1 MB section is mapped at both
 * 0xFD700000 and 0x83F00000 (its address in the contiguous window), so D3D and we see the
 * same bytes through both paths.
 *
 * Offsets: xemu hw/xbox/nv2a/nv2a_regs.h and nv2a.c (block table); read values:
 * pmc.c, pbus.c, pfb.c, ptimer.c, pcrtc.c, pramdac.c, pfifo.c and user.c from xemu.
 */
#include "nv2a.h"
#include "mmio_insn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/kernel.h"
#include "../ngb.h"

/* Registers with their own behavior (offset from 0xFD000000). */
#define PMC_BOOT_0 0x000000u
#define PMC_INTR_0 0x000100u
#define PMC_INTR_EN_0 0x000140u
#define PMC_INTR_0_PCRTC (1u << 24)
#define PBUS_PCI_NV_0 0x001800u
#define PBUS_PCI_NV_2 0x001808u
#define PFIFO_INTR_0 0x002100u
#define PFIFO_RAMHT 0x002210u
#define PFIFO_RUNOUT_STATUS 0x002400u
#define PFIFO_CACHE1_PUSH0 0x003200u
#define PFIFO_CACHE1_PUSH1 0x003204u
#define PFIFO_CACHE1_STATUS 0x003214u
#define PFIFO_CACHE1_DMA_PUSH 0x003220u
#define PFIFO_CACHE1_DMA_INSTANCE 0x00322Cu
#define PFIFO_CACHE1_DMA_CTL 0x003230u
#define PFIFO_CACHE1_DMA_PUT 0x003240u
#define PFIFO_CACHE1_DMA_GET 0x003244u
#define PFIFO_CACHE1_REF 0x003248u
#define PTIMER_INTR_0 0x009100u
#define PTIMER_NUMERATOR 0x009200u
#define PTIMER_DENOMINATOR 0x009210u
#define PTIMER_TIME_0 0x009400u
#define PTIMER_TIME_1 0x009410u
#define PFB_CSTATUS 0x10020Cu
#define PFB_WBC 0x100410u
#define PGRAPH_INTR 0x400100u
#define PGRAPH_STATUS 0x400700u
#define PCRTC_INTR_0 0x600100u
#define PCRTC_INTR_EN_0 0x600140u
#define PCRTC_START 0x600800u
#define PCRTC_RASTER 0x600808u
#define PRAMDAC_PLL_TEST_COUNTER 0x680514u
#define USER_DMA_PUT 0x800040u
#define USER_DMA_GET 0x800044u
#define USER_REF 0x800048u

typedef struct block {
    uint32_t offset, size;
    const char *name;
} block_t;

/* xemu block table (nv2a.c, blocktable). */
static const block_t k_blocks[] = {
    {0x000000, 0x001000, "PMC"},     {0x001000, 0x001000, "PBUS"},
    {0x002000, 0x002000, "PFIFO"},   {0x007000, 0x001000, "PRMA"},
    {0x008000, 0x001000, "PVIDEO"},  {0x009000, 0x001000, "PTIMER"},
    {0x00A000, 0x001000, "PCOUNTER"}, {0x00B000, 0x001000, "PVPE"},
    {0x00D000, 0x001000, "PTV"},     {0x0A0000, 0x020000, "PRMFB"},
    {0x0C0000, 0x001000, "PRMVIO"},  {0x100000, 0x001000, "PFB"},
    {0x101000, 0x001000, "PSTRAPS"}, {0x400000, 0x002000, "PGRAPH"},
    {0x600000, 0x001000, "PCRTC"},   {0x601000, 0x001000, "PRMCIO"},
    {0x680000, 0x001000, "PRAMDAC"}, {0x681000, 0x001000, "PRMDIO"},
    {0x700000, 0x100000, "PRAMIN"},  {0x800000, 0x800000, "USER"},
};
#define NBLOCKS (sizeof k_blocks / sizeof k_blocks[0])

static uint32_t g_regs[NV2A_MMIO_SIZE / 4];     /* shadow of what was written (BSS, on demand) */
static uint8_t g_seen[NV2A_MMIO_SIZE / 4 / 8];  /* registers already noted in the log */
static uint32_t g_reads[NBLOCKS + 1], g_writes[NBLOCKS + 1];
static CRITICAL_SECTION g_lock;
static int g_ready;
static uint8_t *g_ramin;        /* RAMIN reachable by the loader */
static int g_pramin_mapped;     /* PRAMIN is real RAM (alias) and does not go through the VEH */
static uint32_t g_put, g_get;   /* FIFO channel 0 (offsets in the DMA object) */
static int g_push0_off, g_dmapush_off, g_fifo_off; /* pusher stopped (CACHE1_PUSH0/DMA_PUSH bit 0 = 0) */
static uint32_t g_pcrtc_pending, g_pmc_pending;
static uint32_t g_raster;
static LARGE_INTEGER g_t0, g_freq;
static volatile LONG g_vblanks;

/* Access in progress for the thread (between the fault and the single step). */
static __declspec(thread) uint32_t t_off, t_page, t_is_write, t_pending;
static __declspec(thread) LONGLONG t_fault_qpc; /* when the fault of the pending access began */
/* QPC ticks inside MMIO accesses (from the fault to the end of the access, without the
 * exception dispatch itself) and the accesses served by fast_access, for the perf lines. */
static volatile LONGLONG g_mmio_ticks;
static volatile LONG g_mmio_fast;
static int g_slow_only; /* NGB_MMIO_SLOW=1: every access single-steps (to compare) */

static int block_index(uint32_t off) {
    int i;
    for (i = 0; i < (int)NBLOCKS; i++)
        if (off >= k_blocks[i].offset && off - k_blocks[i].offset < k_blocks[i].size)
            return i;
    return -1;
}

static const char *block_name(uint32_t off, uint32_t *rel) {
    int i = block_index(off);
    if (i < 0) {
        *rel = off;
        return "NV2A";
    }
    *rel = off - k_blocks[i].offset;
    return k_blocks[i].name;
}

static int is_modelled(uint32_t off) {
    switch (off) {
    case PMC_BOOT_0: case PMC_INTR_0: case PMC_INTR_EN_0: case PBUS_PCI_NV_0:
    case PBUS_PCI_NV_2: case PFIFO_INTR_0: case PFIFO_RAMHT: case PFIFO_RUNOUT_STATUS:
    case PFIFO_CACHE1_PUSH0: case PFIFO_CACHE1_PUSH1: case PFIFO_CACHE1_STATUS: case PFIFO_CACHE1_DMA_PUSH:
    case PFIFO_CACHE1_DMA_INSTANCE: case PFIFO_CACHE1_DMA_CTL: case PFIFO_CACHE1_DMA_PUT:
    case PFIFO_CACHE1_DMA_GET: case PFIFO_CACHE1_REF: case PTIMER_INTR_0:
    case PTIMER_NUMERATOR: case PTIMER_DENOMINATOR: case PTIMER_TIME_0: case PTIMER_TIME_1:
    case PFB_CSTATUS: case PFB_WBC: case PGRAPH_INTR: case PGRAPH_STATUS: case PCRTC_INTR_0:
    case PCRTC_INTR_EN_0: case PCRTC_START: case PCRTC_RASTER: case PRAMDAC_PLL_TEST_COUNTER:
    case USER_DMA_PUT: case USER_DMA_GET: case USER_REF: case 0x400B10u:
        return 1;
    default:
        return 0;
    }
}

/* PTIMER clock: nanoseconds since boot (low bits of TIME_0 zero). */
static uint64_t ptimer_ns(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (uint64_t)(now.QuadPart - g_t0.QuadPart) * 1000000000ull / (uint64_t)g_freq.QuadPart;
}

/* Value seen by a read of register `off` (4-aligned). */
static uint32_t nv2a_value(uint32_t off) {
    switch (off) {
    case PMC_BOOT_0: return 0x02A000A3u; /* NV2A, A03 (xemu pmc.c) */
    case PMC_INTR_0: return g_pmc_pending | (g_pcrtc_pending ? PMC_INTR_0_PCRTC : 0);
    case PBUS_PCI_NV_0: return 0x02A010DEu; /* NVIDIA 10DE, device 02A0 */
    case PBUS_PCI_NV_2: return 0x030000A1u; /* VGA class, revision A1 */
    case PFIFO_RUNOUT_STATUS: return 1u << 4;              /* LOW_MARK: empty */
    case PFIFO_CACHE1_STATUS: return 1u << 4;              /* LOW_MARK: empty */
    case PFIFO_CACHE1_DMA_PUSH: return g_regs[off / 4] & ~(1u << 12); /* no stop STATUS */
    case PFIFO_CACHE1_DMA_CTL: return g_regs[off / 4] | 0x80000000u;  /* VALID */
    case PFIFO_CACHE1_DMA_PUT: case USER_DMA_PUT: return g_put;
    case PFIFO_CACHE1_DMA_GET: case USER_DMA_GET: return pgraph_fifo_get(g_get);
    case USER_REF: return g_regs[PFIFO_CACHE1_REF / 4];
    case 0x400B10u: return pgraph_patt_color0(); /* PGRAPH_PATT_COLOR0 */
    case PTIMER_TIME_0: return (uint32_t)(ptimer_ns() << 5);
    case PTIMER_TIME_1: return (uint32_t)(ptimer_ns() >> 27) & 0x1FFFFFFFu;
    case PFB_CSTATUS: return NGB_CONTIG_SIZE; /* 64 MB */
    case PFB_WBC: return g_regs[off / 4] & ~(1u << 16); /* the flush finishes instantly */
    case PGRAPH_INTR: return 0;
    case PGRAPH_STATUS: return 0; /* idle */
    case PCRTC_INTR_0: return g_pcrtc_pending;
    case PCRTC_RASTER: return (g_raster += 7) % 625; /* advancing scan */
    case PRAMDAC_PLL_TEST_COUNTER: return 0xE8000000u; /* VPLL2, NVPLL, MPLL and VPLL locked */
    default: return g_regs[off / 4];
    }
}

/* Effects of a write; returns the value left in the register. */
static uint32_t nv2a_write(uint32_t off, uint32_t val) {
    if (off == PMC_INTR_EN_0 || off == PCRTC_INTR_EN_0 || off == 0x400140u || off == 0x002140u)
        ngb_log("    · NV2A: interrupt enable %s = 0x%08X",
                off == PMC_INTR_EN_0 ? "PMC_INTR_EN_0" : off == PCRTC_INTR_EN_0 ? "PCRTC_INTR_EN_0"
                : off == 0x400140u ? "PGRAPH_INTR_EN" : "PFIFO_INTR_EN_0", val);
    /* FIFO trace: everything that is not a normal PUT (D3D resets, GET, instance). */
    if ((off >= 0x003200u && off < 0x003250u && off != PFIFO_CACHE1_DMA_PUT) || off == USER_DMA_GET ||
        ((off == PFIFO_CACHE1_DMA_PUT || off == USER_DMA_PUT) && val < g_put))
        ngb_log("    · NV2A: FIFO 0x%06X = 0x%08X (GET 0x%08X, PUT 0x%08X)", off, val, g_get, g_put);
    switch (off) {
    case PMC_INTR_0:
        g_pmc_pending &= ~val;
        return 0;
    case PCRTC_INTR_0:
        g_pcrtc_pending &= ~val; /* W1C: D3D acknowledges the vblank */
        return g_pcrtc_pending;
    case PFIFO_INTR_0: case PGRAPH_INTR: case PTIMER_INTR_0:
        return 0;
    case PFIFO_CACHE1_DMA_GET: case USER_DMA_GET:
        g_get = val;
        pgraph_fifo_set_get(val);
        return val;
    case PFIFO_CACHE1_DMA_PUT: case USER_DMA_PUT:
        g_put = val;
        if (!g_fifo_off) /* with the pusher stopped, PUT is only stored (D3D reset) */
            g_get = pgraph_kick(g_regs[PFIFO_CACHE1_DMA_INSTANCE / 4] & 0xFFFFu, g_get, g_put);
        return val;
    case PFIFO_CACHE1_PUSH0: case PFIFO_CACHE1_DMA_PUSH:
        /* Bit 0 = ACCESS. On reset D3D lowers it, moves GET/PUT to a new push buffer
         * (PUT = 0 before GET) and raises it again: executing that PUT with the pusher
         * stopped walked the old, already freed push buffer for millions of words. */
        if (off == PFIFO_CACHE1_PUSH0)
            g_push0_off = !(val & 1u);
        else
            g_dmapush_off = !(val & 1u);
        g_fifo_off = g_push0_off || g_dmapush_off;
        if (!g_fifo_off && g_get != g_put)
            g_get = pgraph_kick(g_regs[PFIFO_CACHE1_DMA_INSTANCE / 4] & 0xFFFFu, g_get, g_put);
        return val;
    case USER_REF:
        g_regs[PFIFO_CACHE1_REF / 4] = val;
        return val;
    case PCRTC_START:
        ngb_log("    · NV2A: PCRTC_START = 0x%08X (displayed buffer)", val & 0x07FFFFFFu);
        return val & 0x07FFFFFFu;
    default:
        return val;
    }
}

static void note_first_access(uint32_t off, int is_write, uint32_t value, uint32_t eip) {
    uint32_t idx = off / 4, rel;
    const char *name;
    if (g_seen[idx / 8] & (1u << (idx % 8)))
        return;
    g_seen[idx / 8] |= (uint8_t)(1u << (idx % 8));
    name = block_name(off, &rel);
    ngb_log("    · NV2A %s %s+0x%04X (0x%08X) = 0x%08X from EIP 0x%08X%s",
            is_write ? "write" : "read", name, rel, NV2A_MMIO_BASE + off, value, eip,
            is_modelled(off) ? "" : " (unknown: RAM, last written value is read)");
}

static int commit_page(uint32_t page) {
    MEMORY_BASIC_INFORMATION m;
    if (VirtualQuery((void *)(uintptr_t)page, &m, sizeof m) && m.State == MEM_COMMIT)
        return 1;
    return VirtualAlloc((void *)(uintptr_t)page, 0x1000, MEM_COMMIT, PAGE_NOACCESS) != NULL;
}

/* Fast path: the instructions D3D uses on the registers every frame (mmio_insn.h) are
 * emulated right here, with one exception and no system call. The single-step path opens the
 * page, runs the instruction and closes it again: two exceptions, a VirtualQuery and two
 * VirtualProtect. D3D's KickOff flushes the write-combine buffer (PFB_WBC: read, write, poll)
 * and writes PUT about 14 times per frame in busy fights, and at about 60 us per access that
 * took 2-5 ms of the game thread's frame. Anything else takes the single-step path. */
static uint32_t *ctx_reg(CONTEXT *ctx, int r) {
    switch (r & 7) {
    case 0: return (uint32_t *)&ctx->Eax;
    case 1: return (uint32_t *)&ctx->Ecx;
    case 2: return (uint32_t *)&ctx->Edx;
    case 3: return (uint32_t *)&ctx->Ebx;
    case 4: return (uint32_t *)&ctx->Esp;
    case 5: return (uint32_t *)&ctx->Ebp;
    case 6: return (uint32_t *)&ctx->Esi;
    default: return (uint32_t *)&ctx->Edi;
    }
}

static LONG fast_access(uint32_t off, CONTEXT *ctx) {
    mmio_insn_t in;
    uint32_t len, value, b;
    int write;
    if ((off & 3) || (len = mmio_insn_decode((const uint8_t *)(uintptr_t)ctx->Eip, &in)) == 0)
        return EXCEPTION_CONTINUE_SEARCH;
    write = in.op == MMIO_STORE_REG || in.op == MMIO_STORE_IMM;
    ngb_kernel_enter(ngb_thread_current());
    EnterCriticalSection(&g_lock);
    b = (uint32_t)block_index(off);
    if (write) {
        value = in.op == MMIO_STORE_REG ? *ctx_reg(ctx, in.reg) : in.imm;
        g_writes[b < NBLOCKS ? b : NBLOCKS]++;
        note_first_access(off, 1, value, ctx->Eip);
        g_regs[off / 4] = nv2a_write(off, value);
    } else {
        value = nv2a_value(off);
        g_reads[b < NBLOCKS ? b : NBLOCKS]++;
        note_first_access(off, 0, value, ctx->Eip);
        g_regs[off / 4] = value;
        if (in.op == MMIO_LOAD)
            *ctx_reg(ctx, in.reg) = value;
        else
            ctx->EFlags = mmio_insn_test_flags(ctx->EFlags, value & in.imm);
    }
    ctx->Eip += len;
    LeaveCriticalSection(&g_lock);
    ngb_kernel_leave(ngb_thread_current());
    if (write) /* PUT or pusher re-enable: a FLIP may be pending */
        pgraph_pace();
    return EXCEPTION_CONTINUE_EXECUTION;
}

LONG nv2a_mmio_fault(EXCEPTION_RECORD *rec, CONTEXT *ctx) {
    uint32_t addr = (uint32_t)rec->ExceptionInformation[1], off = addr - NV2A_MMIO_BASE;
    uint32_t page = addr & ~0xFFFu, aligned = off & ~3u, value;
    DWORD old;
    int b;
    if (!g_ready || addr < NV2A_MMIO_BASE || off >= NV2A_MMIO_SIZE || t_pending)
        return EXCEPTION_CONTINUE_SEARCH;
    if (g_pramin_mapped && off >= NV2A_PRAMIN && off - NV2A_PRAMIN < NV2A_PRAMIN_SIZE)
        return EXCEPTION_CONTINUE_SEARCH; /* PRAMIN is RAM: a fault there is a real bug */
    if (!g_slow_only) {
        LARGE_INTEGER a, z;
        LONG r;
        QueryPerformanceCounter(&a);
        r = fast_access(off, ctx);
        if (r == EXCEPTION_CONTINUE_EXECUTION) {
            QueryPerformanceCounter(&z);
            InterlockedExchangeAdd64(&g_mmio_ticks, z.QuadPart - a.QuadPart);
            InterlockedIncrement(&g_mmio_fast);
            return r;
        }
    }
    ngb_kernel_enter(ngb_thread_current()); /* until the single step: lock held */
    {
        LARGE_INTEGER a;
        QueryPerformanceCounter(&a);
        t_fault_qpc = a.QuadPart;
    }
    EnterCriticalSection(&g_lock);
    if (!commit_page(page) ||
        !VirtualProtect((void *)(uintptr_t)page, 0x1000, PAGE_READWRITE, &old)) {
        LeaveCriticalSection(&g_lock);
        ngb_kernel_leave(ngb_thread_current());
        return EXCEPTION_CONTINUE_SEARCH;
    }
    value = nv2a_value(aligned);
    *(volatile uint32_t *)(uintptr_t)(NV2A_MMIO_BASE + aligned) = value;
    t_off = aligned;
    t_page = page;
    t_is_write = rec->ExceptionInformation[0] == 1;
    t_pending = 1;
    b = block_index(off);
    if (t_is_write)
        g_writes[b < 0 ? NBLOCKS : (uint32_t)b]++;
    else
        g_reads[b < 0 ? NBLOCKS : (uint32_t)b]++;
    if (!t_is_write)
        note_first_access(aligned, 0, value, ctx->Eip);
    ctx->EFlags |= 0x100; /* TF: comes back in nv2a_single_step after this instruction */
    return EXCEPTION_CONTINUE_EXECUTION; /* the lock stays held until the single step */
}

int nv2a_single_step(CONTEXT *ctx) {
    DWORD old;
    volatile uint32_t *reg;
    uint32_t val;
    if (!t_pending)
        return 0;
    reg = (volatile uint32_t *)(uintptr_t)(NV2A_MMIO_BASE + t_off);
    val = *reg;
    if (t_is_write) {
        note_first_access(t_off, 1, val, ctx->Eip);
        val = nv2a_write(t_off, val);
    }
    g_regs[t_off / 4] = val;
    *reg = val;
    VirtualProtect((void *)(uintptr_t)t_page, 0x1000, PAGE_NOACCESS, &old);
    t_pending = 0;
    {
        LARGE_INTEGER z;
        QueryPerformanceCounter(&z);
        InterlockedExchangeAdd64(&g_mmio_ticks, z.QuadPart - t_fault_qpc);
    }
    LeaveCriticalSection(&g_lock);
    ngb_kernel_leave(ngb_thread_current());
    if (t_is_write) /* PUT or pusher re-enable: a FLIP may be pending */
        pgraph_pace(); /* outside the NV2A lock and g_pg_lock (issue #35) */
    return 1;
}

/* MMIO accesses served by the VEH (each one costs two exceptions). */
uint32_t nv2a_mmio_count(void) {
    uint32_t i, n = 0;
    for (i = 0; i <= NBLOCKS; i++)
        n += g_reads[i] + g_writes[i];
    return n;
}

LONG nv2a_mmio_fast(void) { return g_mmio_fast; }

LONGLONG nv2a_mmio_ticks(void) { return InterlockedCompareExchange64(&g_mmio_ticks, 0, 0); }

uint32_t nv2a_reg_read(uint32_t off) {
    return off < NV2A_MMIO_SIZE ? nv2a_value(off & ~3u) : 0;
}

/* Bytes reachable from the given physical address (same translation as nv2a_phys), 0 if none. */
uint32_t nv2a_phys_span(uint32_t phys) {
    MEMORY_BASIC_INFORMATION m;
    if (phys < NGB_CONTIG_SIZE)
        return NGB_CONTIG_SIZE - phys;
    if (phys >= NGB_PHYS_XBE + NGB_RESERVE_BASE && phys < NGB_PHYS_XBE + NGB_RESERVE_END)
        return NGB_PHYS_XBE + NGB_RESERVE_END - phys;
    if (VirtualQuery((void *)(uintptr_t)phys, &m, sizeof m) && m.State == MEM_COMMIT)
        return (uint32_t)((uintptr_t)m.BaseAddress + m.RegionSize - phys);
    return 0;
}

uint8_t *nv2a_phys(uint32_t phys, uint32_t len) {
    MEMORY_BASIC_INFORMATION m;
    if (phys < NGB_CONTIG_SIZE) /* physical RAM: the contiguous arena */
        return NGB_CONTIG_SIZE - phys < len ? NULL : (uint8_t *)(uintptr_t)(NGB_CONTIG_BASE + phys);
    if (phys >= NGB_PHYS_XBE + NGB_RESERVE_BASE && phys < NGB_PHYS_XBE + NGB_RESERVE_END) {
        uint32_t va = phys - NGB_PHYS_XBE; /* XBE image (see MmGetPhysicalAddress) */
        return NGB_RESERVE_END - va < len ? NULL : (uint8_t *)(uintptr_t)va;
    }
    /* NtAllocateVirtualMemory memory: MmGetPhysicalAddress returned the VA itself. */
    if (VirtualQuery((void *)(uintptr_t)phys, &m, sizeof m) && m.State == MEM_COMMIT &&
        (uintptr_t)m.BaseAddress + m.RegionSize - phys >= len)
        return (uint8_t *)(uintptr_t)phys;
    return NULL;
}

uint8_t *nv2a_ramin(uint32_t off, uint32_t len) {
    if (off >= NV2A_PRAMIN_SIZE || NV2A_PRAMIN_SIZE - off < len)
        return NULL;
    return g_ramin ? g_ramin + off : (uint8_t *)&g_regs[(NV2A_PRAMIN + off) / 4];
}

int nv2a_vblank_enabled(void) {
    return g_ready && (g_regs[PCRTC_INTR_EN_0 / 4] & 1u);
}

uint32_t nv2a_vblank_count(void) {
    return (uint32_t)g_vblanks;
}

int nv2a_vblank(void) {
    if (!nv2a_vblank_enabled())
        return 0;
    InterlockedIncrement(&g_vblanks);
    EnterCriticalSection(&g_lock);
    g_pcrtc_pending |= 1u; /* NV_PCRTC_INTR_0_VBLANK */
    LeaveCriticalSection(&g_lock);
    pgraph_vblank((uint32_t)g_vblanks);
    return (g_regs[PMC_INTR_EN_0 / 4] & 1u) != 0; /* INTR_EN_0_HARDWARE */
}

int nv2a_init(const char *pgraph_log_path) {
    HANDLE section;
    void *a, *b;
    InitializeCriticalSection(&g_lock);
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_t0);
    g_regs[PTIMER_NUMERATOR / 4] = 1;
    g_regs[PTIMER_DENOMINATOR / 4] = 1;
    /* PRAMIN: a 1 MB section seen at 0x83F00000 (contiguous window) and at 0xFD700000. */
    section = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, NV2A_PRAMIN_SIZE,
                                 NULL);
    a = section ? MapViewOfFileEx(section, FILE_MAP_ALL_ACCESS, 0, 0, NV2A_PRAMIN_SIZE,
                                  (void *)(uintptr_t)(NGB_CONTIG_BASE + NV2A_RAMIN_PHYS))
                : NULL;
    b = a ? MapViewOfFileEx(section, FILE_MAP_ALL_ACCESS, 0, 0, NV2A_PRAMIN_SIZE,
                            (void *)(uintptr_t)(NV2A_MMIO_BASE + NV2A_PRAMIN))
          : NULL;
    g_ramin = (uint8_t *)a;
    g_pramin_mapped = b != NULL;
    /* The rest of the MMIO: reserved with no access (each page is confirmed on first use). */
    VirtualAlloc((void *)(uintptr_t)NV2A_MMIO_BASE, NV2A_PRAMIN, MEM_RESERVE, PAGE_NOACCESS);
    VirtualAlloc((void *)(uintptr_t)(NV2A_MMIO_BASE + NV2A_USER),
                 NV2A_MMIO_SIZE - NV2A_USER, MEM_RESERVE, PAGE_NOACCESS);
    if (!g_pramin_mapped)
        VirtualAlloc((void *)(uintptr_t)(NV2A_MMIO_BASE + NV2A_PRAMIN), NV2A_PRAMIN_SIZE,
                     MEM_RESERVE, PAGE_NOACCESS);
    ngb_log("nv2a: MMIO at 0x%08X modeled; PRAMIN %s (physical RAMIN 0x%08X at %p)",
            NV2A_MMIO_BASE, g_pramin_mapped ? "RAM alias" : "via the VEH (no alias)",
            NV2A_RAMIN_PHYS, (void *)g_ramin);
    {
        const char *slow = getenv("NGB_MMIO_SLOW");
        g_slow_only = slow && *slow == '1';
    }
    g_ready = 1;
    return pgraph_init(pgraph_log_path);
}

void nv2a_summary(void) {
    uint32_t i;
    if (!g_ready)
        return;
    ngb_log("nv2a: accesses per block (reads/writes), %ld vblanks, GET 0x%08X PUT 0x%08X:",
            (long)g_vblanks, g_get, g_put);
    for (i = 0; i <= NBLOCKS; i++)
        if (g_reads[i] || g_writes[i])
            ngb_log("  %-8s %8u / %8u", i < NBLOCKS ? k_blocks[i].name : "other", g_reads[i],
                    g_writes[i]);
    pgraph_summary();
}
