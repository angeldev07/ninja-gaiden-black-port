/*
 * crash.c: vectored exception handler. Ante la primera excepción fatal registra código,
 * dirección, registros, bytes alrededor de EIP, la pila y la llamada al kernel en curso,
 * la clasifica (MMIO de NV2A/APU..., E/S de puertos, instrucción privilegiada) y para.
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
    if (addr >= 0x80000000u && addr < 0x84000000u) return "ventana física 0x80000000";
    if (addr >= 0xB0000000u && addr < 0xC0000000u) return "memoria física write-combined";
    if (addr < 0x10000u) return "puntero nulo (o casi)";
    return NULL;
}

static const char *priv_hint(uint32_t eip) {
    uint8_t b[2] = {0, 0};
    SIZE_T got = 0;
    ReadProcessMemory(GetCurrentProcess(), (const void *)(uintptr_t)eip, b, 2, &got);
    if (got < 1) return "";
    switch (b[0]) {
    case 0xE4: case 0xE5: case 0xEC: case 0xED: return " (IN: E/S de puertos)";
    case 0xE6: case 0xE7: case 0xEE: case 0xEF: return " (OUT: E/S de puertos)";
    case 0xFA: return " (CLI)";
    case 0xFB: return " (STI)";
    case 0xF4: return " (HLT)";
    case 0x0F:
        if (b[1] == 0x09) return " (WBINVD)";
        if (b[1] == 0x30) return " (WRMSR)";
        if (b[1] == 0x32) return " (RDMSR)";
        if (b[1] == 0x20 || b[1] == 0x22) return " (MOV CRx)";
        return " (0F xx privilegiada)";
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
        return "código del XBE";
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(uintptr_t)eip, &self);
    if (self && self == GetModuleHandleA("ngb-core.dll"))
        return "código del loader (ngb-core.dll): bug del kernel emulado o puntero del juego";
    return self ? "otro módulo de Windows" : "memoria sin módulo";
}

/*
 * MMIO "de RAM con traza". Los periféricos que no son la GPU (USB, APU, AC97, NIC, flash)
 * no se emulan, pero para seguir avanzando hasta la inicialización de D3D (el final de
 * H6) se les da memoria a cero: el primer acceso a una página falla (PAGE_NOACCESS), se
 * registra, se abre la página, se ejecuta la instrucción en modo paso a paso (TF) y se
 * vuelve a cerrar. Leer devuelve lo último escrito. El NV2A (0xFD000000) no entra aquí:
 * su primer acceso para la ejecución. Con --mmio-strict cualquier MMIO para.
 */
#define MMIO_LOG_MAX 400

static volatile LONG g_mmio_count;
static __declspec(thread) uint32_t t_mmio_page, t_mmio_addr, t_mmio_write;

static volatile uint32_t g_apu_gpsaddr; /* NV_PAPU_GPSADDR: lista SG de la memoria del GP DSP */

/* Procesador de voces (apu/vp.c): los métodos PIO llegan desde el hilo del juego que los
 * escribe y el avance desde el hilo de DPC. El tick usa TryEnter: un hilo del juego
 * suspendido con el lock tomado no puede colgar al hilo de DPC. */
static vp_state_t g_vp;
static CRITICAL_SECTION g_vp_lock;
static INIT_ONCE g_vp_once = INIT_ONCE_STATIC_INIT;
static LARGE_INTEGER g_vp_last, g_vp_freq;
static mix_ctx_t g_mix;      /* lectura de muestras y caché ADPCM (apu/mix.c) */
static int g_audio = -1;     /* -1 sin abrir, 0 sin dispositivo, 1 sonando */
static double g_audio_frac;  /* fracción de muestra de salida pendiente */
static float g_mixbuf[4800 * 2];
static double g_audio_sq;              /* suma de cuadrados, para el RMS del resumen */
static float g_audio_peak;
static uint64_t g_audio_frames, g_audio_loud; /* frames mezclados y frames con señal */
static FILE *g_audio_wav; /* --audio-wav: la mezcla en <log>-audio.wav (16 bits, 48 kHz) */

static void wav_header(FILE *f, uint32_t data_bytes) {
    uint32_t h[11] = {0x46464952u, 36 + data_bytes, 0x45564157u, 0x20746D66u, 16,
                      0x00020001u, 48000, 48000 * 4, 0x00100004u, 0x61746164u, data_bytes};
    fseek(f, 0, SEEK_SET);
    fwrite(h, 4, 11, f);
    fseek(f, 0, SEEK_END);
}

/* Física de la Xbox -> host. La RAM contigua (64 MB) está en NGB_CONTIG_BASE; el resto
 * de "direcciones físicas" son las que da MmGetPhysicalAddress para memoria normal del
 * proceso, que hoy es la identidad (impl_mm.c): así llegan, por ejemplo, los búferes de
 * audio de Sofdec a la tabla SGE. Esas solo se aceptan si están mapeadas y se pueden leer;
 * la última región comprobada se guarda para no llamar a VirtualQuery en cada muestra. */
static const uint8_t *apu_phys(uint32_t phys, uint32_t len) {
    static uint32_t ok_lo, ok_hi; /* región host legible ya comprobada [ok_lo, ok_hi) */
    MEMORY_BASIC_INFORMATION mbi;
    if (phys < NGB_CONTIG_SIZE)
        return len <= NGB_CONTIG_SIZE - phys ? (const uint8_t *)(uintptr_t)(NGB_CONTIG_BASE + phys)
                                             : NULL;
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
    if (addr == 0xFE80202Cu) { /* NV_PAPU_VPVADDR: dirección física de la tabla de voces */
        uint32_t phys = value & 0x07FFFF00u;
        g_vp.voices = phys + VP_MAX_VOICES * VP_VOICE_SIZE <= NGB_CONTIG_SIZE
                          ? (uint8_t *)(uintptr_t)(NGB_CONTIG_BASE + phys)
                          : NULL;
        ngb_log("    · APU: tabla de voces en 0x%08X (física 0x%08X)",
                (uint32_t)(uintptr_t)g_vp.voices, phys);
    } else if (addr == 0xFE802030u) { /* NV_PAPU_VPSGEADDR: tabla SGE de los búferes */
        g_mix.sge_phys = value;
    } else if (addr == 0xFE820000u + VP_PIO_SET_CURRENT_INBUF_SGE_OFFSET) {
        /* DirectSound programa la tabla SGE por PIO: entrada `inbuf_sge` = página física. */
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

void ngb_apu_summary(void) {
    ngb_log("apu: %u VOICE_ON, %u voces terminadas solas; audio %s, %u frames descartados",
            g_vp.voices_on, g_vp.voices_ended,
            g_audio == 1 ? "waveOut 48 kHz" : g_audio == 0 ? "sin dispositivo" : "sin usar",
            audio_out_dropped());
    if (g_audio_wav && TryEnterCriticalSection(&g_vp_lock)) {
        wav_header(g_audio_wav, (uint32_t)(g_audio_frames * 4));
        fclose(g_audio_wav);
        g_audio_wav = NULL;
        LeaveCriticalSection(&g_vp_lock);
    }
    if (g_audio_frames)
        ngb_log("apu: %.1f s mezclados, %.1f s con señal; pico %.3f, RMS %.4f",
                g_audio_frames / 48000.0, g_audio_loud / 48000.0, g_audio_peak,
                sqrt(g_audio_sq / (2.0 * (double)g_audio_frames)));
}

/*
 * DSP falso (H7a, atajo): DSOUND arranca el GP DSP de la APU con su microcódigo y espera a
 * que este ponga a 0 un buzón en su memoria de trabajo (offset 0x810 de la primera página
 * de la lista SG de GPSADDR) tras escribirle un comando. No hay DSP emulado, así que el
 * hilo de DPC lo hace por él. Ver port/README.md (H7a).
 */
void ngb_apu_tick(void) {
    static int logged;
    uint32_t sg = g_apu_gpsaddr & ~3u, page;
    volatile uint32_t *mbox;
    LARGE_INTEGER now;
    InitOnceExecuteOnce(&g_vp_once, vp_init_once, NULL, NULL);
    QueryPerformanceCounter(&now);
    if (TryEnterCriticalSection(&g_vp_lock)) {
        /* Tiempo real transcurrido; un salto grande (depurador, parada) se acota a 100 ms. */
        double dt = g_vp_last.QuadPart
                        ? (double)(now.QuadPart - g_vp_last.QuadPart) / (double)g_vp_freq.QuadPart
                        : 0.0;
        if (dt > 0.1)
            dt = 0.1;
        if (g_audio < 0 && g_vp.voices && !g_ngb_opts.mute) {
            g_audio = audio_out_open() == 0;
            if (g_audio == 1 && g_ngb_opts.audio_wav) {
                char path[MAX_PATH];
                snprintf(path, sizeof path, "%s-audio.wav", g_ngb_opts.log_path);
                if (fopen_s(&g_audio_wav, path, "wb") == 0 && g_audio_wav)
                    wav_header(g_audio_wav, 0);
            }
        }
        if (g_audio == 1) {
            /* Se mezcla justo lo que corresponde al tiempo real transcurrido. */
            double want = dt * 48000.0 + g_audio_frac;
            uint32_t frames = (uint32_t)want;
            if (frames > 4800)
                frames = 4800;
            g_audio_frac = want - (double)frames;
            if (g_audio_frac > 1.0)
                g_audio_frac = 0.0;
            memset(g_mixbuf, 0, (size_t)frames * 2 * sizeof g_mixbuf[0]);
            mix_render(&g_vp, &g_mix, g_mixbuf, frames);
            audio_out_write(g_mixbuf, frames);
            if (g_audio_wav) {
                int16_t pcm[4800 * 2];
                uint32_t k;
                for (k = 0; k < frames * 2; k++) {
                    float a = g_mixbuf[k] > 1.0f ? 1.0f : g_mixbuf[k] < -1.0f ? -1.0f : g_mixbuf[k];
                    pcm[k] = (int16_t)(a * 32767.0f);
                }
                fwrite(pcm, 4, frames, g_audio_wav);
            }
            {
                uint32_t i;
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
        } else {
            vp_advance(&g_vp, dt);
        }
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
            ngb_log("    · APU: DSP falso, buzón %p = 0x%08X -> 0", (void *)mbox, *mbox);
        *mbox = 0;
    }
}

/* AC97: el bit RR (1) de los registros de control x_CR se limpia solo tras el reset. */
static void mmio_after_step(uint32_t addr, int write) {
    uint32_t reg = addr & ~3u;
    if (reg == 0xFE802040u)
        g_apu_gpsaddr = *(volatile uint32_t *)(uintptr_t)0xFE802040u;
    /* Métodos PIO del procesador de voces y la base de su tabla. */
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
        return 1; /* --mmio-gpu-ram: exploración más allá de H6 */
    return addr >= 0xFE000000u; /* APU, AC97, USB, NIC, flash/BIOS; no el NV2A */
}

/*
 * Registros de periféricos (no GPU) que el arranque sondea hasta ver un valor: se leen fijos.
 *   APU VP NV1BA0_PIO_FREE (xemu mcpx/apu/vp/vp.c): 0x80 palabras libres en la cola PIO.
 *   AC97 GLOB_STA (NABMBAR 0x100 + 0x30): códecs primario y secundario listos (bits 8, 9).
 *   AC97 CAS (NABMBAR + 0x34): semáforo de acceso al códec libre.
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
        uint32_t i;
        for (i = 0; i < sizeof k_mmio_fixed / sizeof k_mmio_fixed[0]; i++)
            if ((addr & ~3u) == k_mmio_fixed[i].addr)
                *(volatile uint32_t *)(uintptr_t)k_mmio_fixed[i].addr = k_mmio_fixed[i].value;
    }
    n = InterlockedIncrement(&g_mmio_count);
    if (n <= MMIO_LOG_MAX) {
        ngb_thread_t *t = ngb_thread_current();
        ngb_log("    · MMIO %s 0x%08X (%s) desde EIP 0x%08lX, T%d%s",
                rec->ExceptionInformation[0] ? "escritura" : "lectura", addr, mmio_region(addr),
                c->Eip, t ? t->index : 0, n == MMIO_LOG_MAX ? " (último acceso MMIO registrado)" : "");
    }
    t_mmio_page = page;
    t_mmio_addr = addr;
    t_mmio_write = rec->ExceptionInformation[0] != 0;
    c->EFlags |= 0x100; /* TF: una instrucción y vuelta aquí para cerrar la página */
    return EXCEPTION_CONTINUE_EXECUTION;
}

/* IN/OUT, CLI/STI y WBINVD: se registran y se saltan; IN devuelve 0. */
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
        /* CLI/STI y WBINVD (vaciado de caché): sin efecto en un proceso de usuario. */
        if (b[0] == 0x0F && b[1] != 0x09)
            return EXCEPTION_CONTINUE_SEARCH;
        len = b[0] == 0x0F ? 2 : 1;
        if (InterlockedIncrement(&g_mmio_count) <= MMIO_LOG_MAX)
            ngb_log("    · instrucción privilegiada %s desde EIP 0x%08lX: se salta",
                    b[0] == 0xFA ? "CLI" : b[0] == 0xFB ? "STI" : "WBINVD", c->Eip);
        c->Eip += len + pre;
        return EXCEPTION_CONTINUE_EXECUTION;
    default: return EXCEPTION_CONTINUE_SEARCH;
    }
    if (InterlockedIncrement(&g_mmio_count) <= MMIO_LOG_MAX)
        ngb_log("    · E/S de puerto: %s 0x%04X desde EIP 0x%08lX (se salta%s)", in ? "IN" : "OUT",
                port, c->Eip, in ? ", lee 0" : "");
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
    /* El MMIO se sigue atendiendo durante la parada: los hilos Xbox siguen vivos mientras
     * se escribe el resumen, y un acceso sin atender tiraría el proceso sin informe. */
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
        const char *op = rec->ExceptionInformation[0] == 0   ? "lectura"
                         : rec->ExceptionInformation[0] == 1 ? "escritura"
                                                             : "ejecución";
        const char *region = mmio_region(addr);
        snprintf(detail, sizeof detail, "%s de 0x%08X%s%s", op, addr, region ? ": " : "",
                 region ? region : "");
    } else if (rec->ExceptionCode == EXCEPTION_PRIV_INSTRUCTION) {
        snprintf(detail, sizeof detail, "instrucción privilegiada%s", priv_hint(c->Eip));
    }
    ngb_log("");
    ngb_log("==== EXCEPCIÓN %s (0x%08lX) en EIP 0x%08lX%s, hilo T%d, %s", name, rec->ExceptionCode,
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
        ngb_log("  dentro de la llamada al kernel %u %s (desde 0x%08X)", t->inflight_ordinal,
                e ? e->name : "?", t->inflight_caller);
    }
    ngb_stop(NGB_EXIT_CRASH, "%s en 0x%08lX%s%s", name, c->Eip, detail[0] ? ": " : "", detail);
}

void ngb_crash_install(void) {
    AddVectoredExceptionHandler(1, on_exception);
}
