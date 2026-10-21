/*
 * crash.c: vectored exception handler. Ante la primera excepción fatal registra código,
 * dirección, registros, bytes alrededor de EIP, la pila y la llamada al kernel en curso,
 * la clasifica (MMIO de NV2A/APU..., E/S de puertos, instrucción privilegiada) y para.
 */
#include "ngb.h"

#include <stdio.h>
#include <string.h>

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
static __declspec(thread) uint32_t t_mmio_page;

static int mmio_traceable(uint32_t addr) {
    if (g_ngb_opts.mmio_strict)
        return 0;
    if (g_ngb_opts.gpu_ram && addr >= 0xFD000000u && addr < 0xFE000000u)
        return 1; /* --mmio-gpu-ram: exploración más allá de H6 */
    return addr >= 0xFE000000u; /* APU, AC97, USB, NIC, flash/BIOS; no el NV2A */
}

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
    n = InterlockedIncrement(&g_mmio_count);
    if (n <= MMIO_LOG_MAX) {
        ngb_thread_t *t = ngb_thread_current();
        ngb_log("    · MMIO %s 0x%08X (%s) desde EIP 0x%08lX, T%d%s",
                rec->ExceptionInformation[0] ? "escritura" : "lectura", addr, mmio_region(addr),
                c->Eip, t ? t->index : 0, n == MMIO_LOG_MAX ? " (último acceso MMIO registrado)" : "");
    }
    t_mmio_page = page;
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
    char bytes[256], before[128];
    char detail[256] = "";
    uint32_t stackw[12] = {0};
    SIZE_T got = 0;
    int i;
    if (!name || ngb_stopping())
        return EXCEPTION_CONTINUE_SEARCH;
    if (rec->ExceptionCode == EXCEPTION_SINGLE_STEP && t_mmio_page) {
        DWORD old;
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
    ngb_log("==== EXCEPCIÓN %s (0x%08lX) en EIP 0x%08lX, hilo T%d, %s", name, rec->ExceptionCode,
            c->Eip, t ? t->index : 0, where(c->Eip));
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
