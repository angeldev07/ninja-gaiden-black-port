/*
 * crash.c: vectored exception handler. On the first fatal exception it logs the code,
 * address, registers, bytes around EIP, the stack and the kernel call in progress,
 * classifies it (NV2A/APU... MMIO, port I/O, privileged instruction) and stops.
 */
#include "ngb.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "apu/audio_out.h"
#include "apu/mix.h"
#include "apu/vp.h"
#include "gpu/nv2a.h"
#include "kernel/kernel.h"

static const char *mmio_region(uint32_t addr) {
    if (addr >= 0xFD000000u && addr < 0xFE000000u) return "MMIO NV2A (GPU)";
    if (addr >= 0xFE800000u && addr < 0xFE880000u) return "MMIO APU (audio)";
    if (addr >= 0xFEC00000u && addr < 0xFEC01000u) return "MMIO AC97";
    if (addr >= 0xFED00000u && addr < 0xFED08000u) return "MMIO USB";
    if (addr >= 0xFEF00000u && addr < 0xFEF00400u) return "MMIO NIC";
    if (addr >= 0xFF000000u) return "flash/BIOS";
    if (addr >= 0xF0000000u && addr < 0xF8000000u) return "AGP / framebuffer (0xF0000000)";
    if (addr >= 0x80000000u && addr < 0x84000000u) return "physical window 0x80000000";
    if (addr >= 0xB0000000u && addr < 0xC0000000u) return "write-combined physical memory";
    if (addr < 0x10000u) return "null pointer (or close to it)";
    return NULL;
}

static const char *priv_hint(uint32_t eip) {
    uint8_t b[2] = {0, 0};
    SIZE_T got = 0;
    ReadProcessMemory(GetCurrentProcess(), (const void *)(uintptr_t)eip, b, 2, &got);
    if (got < 1) return "";
    switch (b[0]) {
    case 0xE4: case 0xE5: case 0xEC: case 0xED: return " (IN: port I/O)";
    case 0xE6: case 0xE7: case 0xEE: case 0xEF: return " (OUT: port I/O)";
    case 0xFA: return " (CLI)";
    case 0xFB: return " (STI)";
    case 0xF4: return " (HLT)";
    case 0x0F:
        if (b[1] == 0x09) return " (WBINVD)";
        if (b[1] == 0x30) return " (WRMSR)";
        if (b[1] == 0x32) return " (RDMSR)";
        if (b[1] == 0x20 || b[1] == 0x22) return " (MOV CRx)";
        return " (privileged 0F xx)";
    default: return "";
    }
}

static const char *code_name(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW: return "INT_OVERFLOW";
    case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
    case EXCEPTION_BREAKPOINT: return "BREAKPOINT (int3)";
    case EXCEPTION_SINGLE_STEP: return "SINGLE_STEP";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INVALID_OPERATION: return "FLT_INVALID_OPERATION";
    case 0xC0000409u: return "STACK_BUFFER_OVERRUN (fail fast)";
    default: return NULL;
    }
}

static const char *where(uint32_t eip) {
    HMODULE self = NULL;
    if (eip >= g_ngb_xbe.base && eip < g_ngb_xbe.base + g_ngb_xbe.size_of_image)
        return "XBE code";
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(uintptr_t)eip, &self);
    if (self && self == GetModuleHandleA("ngb-core.dll"))
        return "loader code (ngb-core.dll): emulated kernel bug or game pointer";
    return self ? "another Windows module" : "memory without a module";
}

/*
 * "RAM with trace" MMIO. The peripherals that are not the GPU (USB, APU, AC97, NIC, flash)
 * are not emulated, but to keep going until D3D initialization (the end of
 * H6) they are given zeroed memory: the first access to a page faults (PAGE_NOACCESS), it is
 * logged, the page is opened, the instruction is executed in single-step mode (TF) and the
 * page is closed again. A read returns the last value written. The NV2A (0xFD000000) does not
 * come in here: its first access stops the run. With --mmio-strict any MMIO stops it.
 */
#define MMIO_LOG_MAX 400

static volatile LONG g_mmio_count;
static __declspec(thread) uint32_t t_mmio_page, t_mmio_addr, t_mmio_write;

static volatile uint32_t g_apu_gpsaddr; /* NV_PAPU_GPSADDR: SG list of the GP DSP memory */

/* APU front end trap seen by the DirectSound ISR while a free voice is delivered to it
 * (ngb_apu_interrupt): ISTS (0x1000), FECTL (0x1100, reason in bits 8-11) and the decoded
 * method and parameter (FEDECMETH 0x1300, FEDECPARAM 0x1304). Its reads take them from here
 * (mmio_access); outside a delivery, ISTS reads 0. */
static volatile uint32_t g_apu_ists, g_apu_fe_meth, g_apu_fe_param, g_apu_trap;
static uint32_t g_apu_idle_irqs;

/* Voice processor (apu/vp.c): the PIO methods arrive from the game thread that writes
 * them and the advance from the DPC thread. The tick uses TryEnter: a game thread suspended
 * while holding the lock cannot hang the DPC thread. */
static vp_state_t g_vp;
static CRITICAL_SECTION g_vp_lock;
static INIT_ONCE g_vp_once = INIT_ONCE_STATIC_INIT;
static LARGE_INTEGER g_vp_last, g_vp_freq;
static mix_ctx_t g_mix;      /* sample reads and ADPCM cache (apu/mix.c) */
static int g_audio = -1;     /* -1 not opened, 0 no device, 1 playing */
static float g_mixbuf[4800 * 2];
static double g_audio_sq;              /* sum of squares, for the summary RMS */
static float g_audio_peak;
static uint64_t g_audio_frames, g_audio_loud; /* mixed frames and frames with signal */
static FILE *g_audio_wav; /* --audio-wav: the mix in <log>-audio.wav (16 bits, 48 kHz) */

static void wav_header(FILE *f, uint32_t data_bytes) {
    uint32_t h[11] = {0x46464952u, 36 + data_bytes, 0x45564157u, 0x20746D66u, 16,
                      0x00020001u, 48000, 48000 * 4, 0x00100004u, 0x61746164u, data_bytes};
    fseek(f, 0, SEEK_SET);
    fwrite(h, 4, 11, f);
    fseek(f, 0, SEEK_END);
}

/* Xbox physical -> host. The contiguous RAM (64 MB) is at NGB_CONTIG_BASE; the rest
 * of the "physical addresses" are the ones MmGetPhysicalAddress gives for normal process
 * memory, which today is the identity (impl_mm.c): that is how, for example, the Sofdec
 * audio buffers reach the SGE table. Those are only accepted if they are mapped and readable;
 * the last region checked is kept so VirtualQuery is not called for every sample. */
static const uint8_t *apu_phys(uint32_t phys, uint32_t len) {
    static uint32_t ok_lo, ok_hi; /* already-checked readable host region [ok_lo, ok_hi) */
    MEMORY_BASIC_INFORMATION mbi;
    if (phys < NGB_CONTIG_SIZE)
        return len <= NGB_CONTIG_SIZE - phys ? (const uint8_t *)(uintptr_t)(NGB_CONTIG_BASE + phys)
                                             : NULL;
    if (phys >= NGB_PHYS_XBE + NGB_RESERVE_BASE && phys < NGB_PHYS_XBE + NGB_RESERVE_END) {
        /* XBE image (MmGetPhysicalAddress window, as in nv2a_phys): the sounds the game
           has in .data, like the menu ones (SetBufferData on 0x00B05000). */
        uint32_t va = phys - NGB_PHYS_XBE;
        return NGB_RESERVE_END - va >= len ? (const uint8_t *)(uintptr_t)va : NULL;
    }
    if (len > 0x1000u || phys > 0xFFFFFFFFu - len)
        return NULL;
    if (phys >= ok_lo && phys + len <= ok_hi)
        return (const uint8_t *)(uintptr_t)phys;
    if (!VirtualQuery((const void *)(uintptr_t)phys, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !(mbi.Protect & 0xEEu))
        return NULL;
    ok_lo = (uint32_t)(uintptr_t)mbi.BaseAddress;
    ok_hi = ok_lo + (uint32_t)mbi.RegionSize;
    return phys + len <= ok_hi ? (const uint8_t *)(uintptr_t)phys : NULL;
}

static BOOL CALLBACK vp_init_once(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once;
    (void)param;
    (void)ctx;
    InitializeCriticalSection(&g_vp_lock);
    vp_init(&g_vp);
    g_mix.phys = apu_phys;
    QueryPerformanceFrequency(&g_vp_freq);
    return TRUE;
}

static void vp_write(uint32_t addr, uint32_t value) {
    InitOnceExecuteOnce(&g_vp_once, vp_init_once, NULL, NULL);
    EnterCriticalSection(&g_vp_lock);
    if (addr == 0xFE80202Cu) { /* NV_PAPU_VPVADDR: physical address of the voice table */
        uint32_t phys = value & 0x07FFFF00u;
        g_vp.voices = phys + VP_MAX_VOICES * VP_VOICE_SIZE <= NGB_CONTIG_SIZE
                          ? (uint8_t *)(uintptr_t)(NGB_CONTIG_BASE + phys)
                          : NULL;
        ngb_log("    · APU: voice table at 0x%08X (physical 0x%08X)",
                (uint32_t)(uintptr_t)g_vp.voices, phys);
    } else if (addr == 0xFE802030u) { /* NV_PAPU_VPSGEADDR: SGE table of the buffers */
        g_mix.sge_phys = value;
    } else if (addr == 0xFE820000u + VP_PIO_SET_CURRENT_INBUF_SGE_OFFSET) {
        /* DirectSound programs the SGE table through PIO: entry `inbuf_sge` = physical page. */
        uint32_t entry = g_mix.sge_phys + g_vp.inbuf_sge * 8u;
        if (g_vp.inbuf_sge < 0x10000u && entry + 8u <= NGB_CONTIG_SIZE)
            *(volatile uint32_t *)(uintptr_t)(NGB_CONTIG_BASE + entry) = value & 0xFFFFF000u;
    } else {
        vp_method(&g_vp, addr - 0xFE820000u, value);
        if (addr == 0xFE820124u && g_vp.voices && (value & 0xFFFFu) < VP_MAX_VOICES &&
            g_vp.voices_on <= 64) {
            const uint32_t *w = (const uint32_t *)(g_vp.voices + (value & 0xFFFFu) * VP_VOICE_SIZE);
            ngb_log("    · APU: VOICE_ON %u: vbin %08X fmt %08X ba %06X lbo %06X cbo %06X ebo %06X "
                    "pitch %04X vol %08X %08X %08X sge %08X", value & 0xFFFFu, w[0], w[1],
                    w[8] & 0xFFFFFF, w[9] & 0xFFFFFF, w[22] & 0xFFFFFF, w[23] & 0xFFFFFF,
                    w[31] >> 16, w[24], w[25], w[26], g_mix.sge_phys);
        }
    }
    LeaveCriticalSection(&g_vp_lock);
}

/* Audio thread ("pull" model): keeps ~100 ms in the waveOut queue. It mixes in 10 ms blocks
 * only when the card has consumed, so voices advance at the real pace of the
 * device and a late DPC thread no longer produces cuts or bursts. */
static void mix_block(uint32_t frames) {
    uint32_t i;
    memset(g_mixbuf, 0, (size_t)frames * 2 * sizeof g_mixbuf[0]);
    mix_render(&g_vp, &g_mix, g_mixbuf, frames);
    audio_out_write(g_mixbuf, frames);
    if (g_audio_wav) {
        int16_t pcm[480 * 2];
        for (i = 0; i < frames * 2; i++) {
            float a = g_mixbuf[i] > 1.0f ? 1.0f : g_mixbuf[i] < -1.0f ? -1.0f : g_mixbuf[i];
            pcm[i] = (int16_t)(a * 32767.0f);
        }
        fwrite(pcm, 4, frames, g_audio_wav);
    }
    for (i = 0; i < frames * 2; i++) {
        float a = g_mixbuf[i] < 0 ? -g_mixbuf[i] : g_mixbuf[i];
        g_audio_sq += (double)a * a;
        if (a > g_audio_peak)
            g_audio_peak = a;
        if (a > 0.001f && (i & 1) == 0)
            g_audio_loud++;
    }
    g_audio_frames += frames;
}

static DWORD WINAPI audio_pump(LPVOID unused) {
    (void)unused;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    for (;;) {
        /* TryEnter: if a game thread was left suspended holding the lock, retry. */
        if (TryEnterCriticalSection(&g_vp_lock)) {
            while (audio_out_queued() < 4800)
                mix_block(480);
            LeaveCriticalSection(&g_vp_lock);
        }
        Sleep(5);
    }
}

/*
 * Free voices (SE2FE_IDLE_VOICE): for each one the front end trap is prepared (method
 * 0x8000, parameter = voice, reason "requested" in FECTL, ISTS = GINTSTS | FETINTSTS) and the
 * APU ISR that DirectSound connected is called. The DPC thread does it (impl_ke.c), and
 * passes the call to the ISR in `isr`. With no ISR connected the voices wait in the queue.
 */
void ngb_apu_interrupt(void (*isr)(void *ctx), void *ctx) {
    uint32_t handle;
    int got;
    for (;;) {
        if (!TryEnterCriticalSection(&g_vp_lock))
            return; /* held by a game thread: next tick */
        got = vp_take_idle(&g_vp, &handle);
        LeaveCriticalSection(&g_vp_lock);
        if (!got)
            return;
        g_apu_fe_meth = 0x8000u;
        g_apu_fe_param = handle;
        g_apu_trap = 1;
        g_apu_ists = 0x11u;
        isr(ctx);
        g_apu_ists = 0;
        g_apu_trap = 0;
        /* Without the lock (TryEnter could fail): a VOICE_OFF during the ISR was not queued. */
        vp_idle_delivered(&g_vp, handle);
        g_apu_idle_irqs++;
    }
}

/* Read without the lock: a voice queued right after is delivered on the next tick. */
int ngb_apu_interrupt_pending(void) {
    return g_vp.idle_head != g_vp.idle_tail;
}

void ngb_apu_summary(void) {
    ngb_log("apu: %u VOICE_ON, %u voices ended on their own, %u free-voice notices (%u lost); "
            "audio %s, %u frames dropped, %u flushed",
            g_vp.voices_on, g_vp.voices_ended, g_apu_idle_irqs, g_vp.idle_dropped,
            g_audio == 1 ? "waveOut 48 kHz" : g_audio == 0 ? "no device" : "unused",
            audio_out_dropped(), audio_out_underruns());
    if (g_audio_wav && TryEnterCriticalSection(&g_vp_lock)) {
        wav_header(g_audio_wav, (uint32_t)(g_audio_frames * 4));
        fclose(g_audio_wav);
        g_audio_wav = NULL;
        LeaveCriticalSection(&g_vp_lock);
    }
    if (g_audio_frames)
        ngb_log("apu: %.1f s mixed, %.1f s with signal; peak %.3f, RMS %.4f",
                g_audio_frames / 48000.0, g_audio_loud / 48000.0, g_audio_peak,
                sqrt(g_audio_sq / (2.0 * (double)g_audio_frames)));
}

/*
 * Fake DSP (H7a, shortcut): DSOUND starts the APU GP DSP with its microcode and waits for
 * it to set to 0 a mailbox in its working memory (offset 0x810 of the first page
 * of the GPSADDR SG list) after writing a command to it. There is no emulated DSP, so the
 * DPC thread does it on its behalf. See port/README.md (H7a).
 */
void ngb_apu_tick(void) {
    static int logged;
    uint32_t sg = g_apu_gpsaddr & ~3u, page;
    volatile uint32_t *mbox;
    LARGE_INTEGER now;
    InitOnceExecuteOnce(&g_vp_once, vp_init_once, NULL, NULL);
    QueryPerformanceCounter(&now);
    if (TryEnterCriticalSection(&g_vp_lock)) {
        /* Real elapsed time; a big jump (debugger, pause) is capped to 100 ms. */
        double dt = g_vp_last.QuadPart
                        ? (double)(now.QuadPart - g_vp_last.QuadPart) / (double)g_vp_freq.QuadPart
                        : 0.0;
        if (dt > 0.1)
            dt = 0.1;
        if (g_audio < 0 && g_vp.voices && !g_ngb_opts.mute) {
            g_audio = audio_out_open() == 0;
            if (g_audio == 1) {
                HANDLE pump = CreateThread(NULL, 0, audio_pump, NULL, 0, NULL);
                ngb_pin_hardware_thread(pump);
                if (pump)
                    CloseHandle(pump);
            }
            if (g_audio == 1 && g_ngb_opts.audio_wav) {
                char path[MAX_PATH];
                snprintf(path, sizeof path, "%s-audio.wav", g_ngb_opts.log_path);
                if (fopen_s(&g_audio_wav, path, "wb") == 0 && g_audio_wav)
                    wav_header(g_audio_wav, 0);
            }
        }
        /* With audio, the audio thread (audio_pump) advances the voices at the card's pace;
           without audio, the VP advances in real time. */
        if (g_audio != 1)
            vp_advance(&g_vp, dt);
        g_vp_last = now;
        LeaveCriticalSection(&g_vp_lock);
    }
    if (!sg || sg >= NGB_CONTIG_SIZE)
        return;
    page = *(volatile uint32_t *)(uintptr_t)(NGB_CONTIG_BASE + sg) & 0x07FFF000u;
    if (page >= NGB_CONTIG_SIZE - 0x1000u)
        return;
    mbox = (volatile uint32_t *)(uintptr_t)(NGB_CONTIG_BASE + page + 0x810u);
    if (*mbox) {
        if (logged++ < 8)
            ngb_log("    · APU: fake DSP, mailbox %p = 0x%08X -> 0", (void *)mbox, *mbox);
        *mbox = 0;
    }
}

/* AC97: the RR bit (1) of the x_CR control registers clears itself after the reset. */
static void mmio_after_step(uint32_t addr, int write) {
    uint32_t reg = addr & ~3u;
    if (reg == 0xFE802040u)
        g_apu_gpsaddr = *(volatile uint32_t *)(uintptr_t)0xFE802040u;
    /* PIO methods of the voice processor and the base of its table. */
    if (write && (reg == 0xFE80202Cu || reg == 0xFE802030u ||
                  (reg >= 0xFE820100u && reg < 0xFE820400u) ||
                  reg == 0xFE820804u || reg == 0xFE820808u))
        vp_write(reg, *(volatile uint32_t *)(uintptr_t)reg);
    if (addr == 0xFEC0010Bu || addr == 0xFEC0011Bu || addr == 0xFEC0012Bu || addr == 0xFEC0017Bu)
        *(volatile uint8_t *)(uintptr_t)addr &= 0xFDu;
}

static int mmio_traceable(uint32_t addr) {
    if (g_ngb_opts.mmio_strict)
        return 0;
    if (g_ngb_opts.gpu_ram && addr >= 0xFD000000u && addr < 0xFE000000u)
        return 1; /* --mmio-gpu-ram: exploration beyond H6 */
    return addr >= 0xFE000000u; /* APU, AC97, USB, NIC, flash/BIOS; not the NV2A */
}

/*
/*
 * Peripheral (non-GPU) registers that boot polls until it sees a value: they are read as fixed.
 *   APU VP NV1BA0_PIO_FREE (xemu mcpx/apu/vp/vp.c): 0x80 free words in the PIO queue.
 *   AC97 GLOB_STA (NABMBAR 0x100 + 0x30): primary and secondary codecs ready (bits 8, 9).
 *   AC97 CAS (NABMBAR + 0x34): codec access semaphore free.
 */
static const struct {
    uint32_t addr, value;
} k_mmio_fixed[] = {
    {0xFE820010u, 0x80u},
    {0xFEC00130u, 0x300u},
    {0xFEC00134u, 0u},
};

static LONG mmio_access(EXCEPTION_RECORD *rec, CONTEXT *c) {
    uint32_t addr = (uint32_t)rec->ExceptionInformation[1], page = addr & ~0xFFFu;
    DWORD old;
    LONG n;
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery((void *)(uintptr_t)page, &m, sizeof m) || m.State != MEM_COMMIT) {
        if (!VirtualAlloc((void *)(uintptr_t)(page & ~0xFFFFu), 0x10000, MEM_RESERVE, PAGE_NOACCESS) &&
            GetLastError() != ERROR_INVALID_ADDRESS)
            return EXCEPTION_CONTINUE_SEARCH;
        if (!VirtualAlloc((void *)(uintptr_t)page, 0x1000, MEM_COMMIT, PAGE_NOACCESS))
            return EXCEPTION_CONTINUE_SEARCH;
    }
    if (!VirtualProtect((void *)(uintptr_t)page, 0x1000, PAGE_READWRITE, &old))
        return EXCEPTION_CONTINUE_SEARCH;
    if (rec->ExceptionInformation[0] == 0) {
        uint32_t i, reg = addr & ~3u;
        volatile uint32_t *r = (volatile uint32_t *)(uintptr_t)reg;
        for (i = 0; i < sizeof k_mmio_fixed / sizeof k_mmio_fixed[0]; i++)
            if (reg == k_mmio_fixed[i].addr)
                *r = k_mmio_fixed[i].value;
        if (reg == 0xFE801000u)
            *r = g_apu_ists;
        else if (reg == 0xFE801100u)
            *r = (*r & ~0xF00u) | (g_apu_trap ? 0xF00u : 0u);
        else if (reg == 0xFE801300u && g_apu_trap)
            *r = g_apu_fe_meth;
        else if (reg == 0xFE801304u && g_apu_trap)
            *r = g_apu_fe_param;
    }
    n = InterlockedIncrement(&g_mmio_count);
    if (n <= MMIO_LOG_MAX) {
        ngb_thread_t *t = ngb_thread_current();
        ngb_log("    · MMIO %s 0x%08X (%s) from EIP 0x%08lX, T%d%s",
                rec->ExceptionInformation[0] ? "write" : "read", addr, mmio_region(addr),
                c->Eip, t ? t->index : 0, n == MMIO_LOG_MAX ? " (last MMIO access logged)" : "");
    }
    t_mmio_page = page;
    t_mmio_addr = addr;
    t_mmio_write = rec->ExceptionInformation[0] != 0;
    c->EFlags |= 0x100; /* TF: one instruction and back here to close the page */
    return EXCEPTION_CONTINUE_EXECUTION;
}

/* MCPX I/O port 0x80C0: bit 5 is the field the TV encoder is sending (odd or even). D3D reads it
 * at each vblank of an interlaced mode; the game waits for a frame boundary with it, so a movie
 * shows each frame for two fields. The field changes at every vblank. */
#define PORT_FIELD 0x80C0u

/* IN/OUT, CLI/STI and WBINVD: they are logged and skipped; IN returns 0, except the field. */
static LONG port_io(CONTEXT *c) {
    uint8_t b[3] = {0, 0, 0};
    SIZE_T got = 0;
    uint32_t len = 1, port = 0, pre = 0;
    int in = 0;
    ReadProcessMemory(GetCurrentProcess(), (const void *)(uintptr_t)c->Eip, b, 3, &got);
    if (b[0] == 0x66) {
        pre = 1;
        b[0] = b[1];
        b[1] = b[2];
    }
    switch (b[0]) {
    case 0xE4: case 0xE5: in = 1; port = b[1]; len = 2; break;
    case 0xE6: case 0xE7: port = b[1]; len = 2; break;
    case 0xEC: case 0xED: in = 1; port = c->Edx & 0xFFFF; break;
    case 0xEE: case 0xEF: port = c->Edx & 0xFFFF; break;
    case 0xFA: case 0xFB: case 0x0F:
        /* CLI/STI and WBINVD (cache flush): no effect in a user process. */
        if (b[0] == 0x0F && b[1] != 0x09)
            return EXCEPTION_CONTINUE_SEARCH;
        len = b[0] == 0x0F ? 2 : 1;
        if (InterlockedIncrement(&g_mmio_count) <= MMIO_LOG_MAX)
            ngb_log("    · privileged instruction %s from EIP 0x%08lX: skipped",
                    b[0] == 0xFA ? "CLI" : b[0] == 0xFB ? "STI" : "WBINVD", c->Eip);
        c->Eip += len + pre;
        return EXCEPTION_CONTINUE_EXECUTION;
    default: return EXCEPTION_CONTINUE_SEARCH;
    }
    if (in && port == PORT_FIELD && !(b[0] & 1)) {
        c->Eax = (c->Eax & ~0xFFu) | ((nv2a_vblank_count() & 1u) << 5);
        c->Eip += len + pre;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (InterlockedIncrement(&g_mmio_count) <= MMIO_LOG_MAX)
        ngb_log("    · port I/O: %s 0x%04X from EIP 0x%08lX (skipped%s)", in ? "IN" : "OUT",
                port, c->Eip, in ? ", reads 0" : "");
    if (in)
        c->Eax = (b[0] & 1) ? 0 : (c->Eax & ~0xFFu);
    c->Eip += len + pre;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static LONG CALLBACK on_exception(EXCEPTION_POINTERS *info) {
    EXCEPTION_RECORD *rec = info->ExceptionRecord;
    CONTEXT *c = info->ContextRecord;
    const char *name = code_name(rec->ExceptionCode);
    ngb_thread_t *t = ngb_thread_current();
    char bytes[256], before[128], sym[96];
    char detail[256] = "";
    uint32_t stackw[12] = {0};
    SIZE_T got = 0;
    int i;
    if (!name)
        return EXCEPTION_CONTINUE_SEARCH;
    /* MMIO keeps being served during the stop: Xbox threads are still alive while
     * the summary is written, and an unserved access would take the process down without a report. */
    if (rec->ExceptionCode == EXCEPTION_SINGLE_STEP && nv2a_single_step(c)) {
        c->EFlags &= ~0x100u;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2 &&
        rec->ExceptionInformation[0] != 8 && !g_ngb_opts.mmio_strict && !g_ngb_opts.gpu_ram &&
        nv2a_mmio_fault(rec, c) == EXCEPTION_CONTINUE_EXECUTION)
        return EXCEPTION_CONTINUE_EXECUTION;
    if (rec->ExceptionCode == EXCEPTION_SINGLE_STEP && t_mmio_page) {
        c->EFlags &= ~0x100u;
        DWORD old;
        mmio_after_step(t_mmio_addr, (int)t_mmio_write);
        VirtualProtect((void *)(uintptr_t)t_mmio_page, 0x1000, PAGE_NOACCESS, &old);
        t_mmio_page = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2 &&
        rec->ExceptionInformation[0] != 8 && mmio_traceable((uint32_t)rec->ExceptionInformation[1]) &&
        mmio_access(rec, c) == EXCEPTION_CONTINUE_EXECUTION)
        return EXCEPTION_CONTINUE_EXECUTION;
    if (rec->ExceptionCode == EXCEPTION_PRIV_INSTRUCTION && !g_ngb_opts.mmio_strict &&
        port_io(c) == EXCEPTION_CONTINUE_EXECUTION)
        return EXCEPTION_CONTINUE_EXECUTION;
    if (ngb_stopping())
        return EXCEPTION_CONTINUE_SEARCH;
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
        uint32_t addr = (uint32_t)rec->ExceptionInformation[1];
        const char *op = rec->ExceptionInformation[0] == 0   ? "read"
                         : rec->ExceptionInformation[0] == 1 ? "write"
                                                             : "execution";
        const char *region = mmio_region(addr);
        snprintf(detail, sizeof detail, "%s of 0x%08X%s%s", op, addr, region ? ": " : "",
                 region ? region : "");
    } else if (rec->ExceptionCode == EXCEPTION_PRIV_INSTRUCTION) {
        snprintf(detail, sizeof detail, "privileged instruction%s", priv_hint(c->Eip));
    }
    ngb_log("");
    ngb_log("==== EXCEPTION %s (0x%08lX) at EIP 0x%08lX%s, thread T%d, %s", name, rec->ExceptionCode,
            c->Eip, ngb_symbol(c->Eip, sym, sizeof sym), t ? t->index : 0, where(c->Eip));
    if (detail[0])
        ngb_log("  %s", detail);
    ngb_log("  EAX %08lX EBX %08lX ECX %08lX EDX %08lX ESI %08lX EDI %08lX", c->Eax, c->Ebx,
            c->Ecx, c->Edx, c->Esi, c->Edi);
    ngb_log("  EBP %08lX ESP %08lX EIP %08lX EFL %08lX  fs:[0]=%08lX", c->Ebp, c->Esp, c->Eip,
            c->EFlags, __readfsdword(0));
    ngb_hexdump_line(before, sizeof before, c->Eip - 16, 16);
    ngb_hexdump_line(bytes, sizeof bytes, c->Eip, 32);
    ngb_log("  bytes EIP-16: %s", before);
    ngb_log("  bytes EIP   : %s", bytes);
    ReadProcessMemory(GetCurrentProcess(), (const void *)(uintptr_t)c->Esp, stackw, sizeof stackw,
                      &got);
    for (i = 0; i < 12; i += 4)
        ngb_log("  [esp+%02X] %08X %08X %08X %08X", i * 4, stackw[i], stackw[i + 1],
                stackw[i + 2], stackw[i + 3]);
    if (t && t->inflight_ordinal) {
        const ngb_kexport_t *e = ngb_kexport(t->inflight_ordinal);
        ngb_log("  inside kernel call %u %s (from 0x%08X)", t->inflight_ordinal,
                e ? e->name : "?", t->inflight_caller);
    }
    ngb_stop(NGB_EXIT_CRASH, "%s at 0x%08lX%s%s", name, c->Eip, detail[0] ? ": " : "", detail);
}

void ngb_crash_install(void) {
    AddVectoredExceptionHandler(1, on_exception);
}
