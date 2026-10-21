/*
 * xapi_hle.c: HLE de la entrada de XAPI (H8). El juego trae XAPI enlazada estáticamente y
 * su driver USB (OHCI) no encuentra ningún dispositivo: aquí se sustituyen las funciones
 * de consulta y lectura del mando por las nuestras, con un JMP rel32 en su entrada.
 *
 * Seguridad del parche: cada función se parchea solo si los 8 primeros bytes del XBE
 * cargado son los esperados y si en su epílogo está el `ret N` esperado (C2 N 00): N es lo
 * que limpia una función __stdcall, así que confirma el número de argumentos con el propio
 * XBE (cada entrada de la tabla se desarmó con capstone sobre default.xbe). Con --symbols,
 * además, la dirección tiene que coincidir con la del CSV de Ghidra (H5). Si algo no
 * cuadra, esa función no se toca (otro XBE, otra versión) y se avisa en el log.
 *
 *   función                 VA         ret   prototipo (XDK público)
 *   XGetDevices             0x3DCC99   4     DWORD (PXPP_DEVICE_TYPE)
 *   XGetDeviceChanges       0x3DCCBB   0xC   BOOL (PXPP_DEVICE_TYPE, PDWORD ins, PDWORD rem)
 *   XPeekDevices            0x3DCC58   0xC   DWORD (PXPP_DEVICE_TYPE, PDWORD ins, PDWORD rem)
 *   XInputOpen              0x3DDFF1   0x10  HANDLE (PXPP_DEVICE_TYPE, port, slot, params)
 *   XInputClose             0x3DE047   4     VOID (HANDLE)
 *   XInputGetCapabilities   0x3DE053   8     DWORD (HANDLE, PXINPUT_CAPABILITIES)
 *   XInputGetState          0x3DE22B   8     DWORD (HANDLE, PXINPUT_STATE)
 *   XInputSetState          0x3DE29E   8     DWORD (HANDLE, PXINPUT_FEEDBACK)
 *
 * XInitDevices (0x3DC02F, ret 8) no se parchea: prepara el estado interno que consulta
 * XGetDeviceEnumerationStatus (sin parchear) y ya funcionaba antes de H8.
 *
 * Semántica: hay un mando en el puerto 0 (el mando de PC, el teclado o el guion: siempre
 * está conectado). Como la XAPI real, XGetDevices da por conocida la inserción y
 * XGetDeviceChanges solo la notifica si nadie la ha visto aún (una sola vez). XInputOpen
 * devuelve como handle un bloque de memoria nuestro. XINPUT_CAPABILITIES es empaquetada
 * (verificado en el XBE: SubType +0, Reserved +1, In.Gamepad +3, Out.Rumble +21; 25 bytes).
 * XInputSetState (vibración) se completa al momento sin vibrar: devuelve ERROR_IO_PENDING,
 * como la real, con Header.dwStatus = ERROR_SUCCESS.
 */
#include "../ngb.h"

#include <intrin.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input.h"

#define XDEVICE_TYPE_GAMEPAD_TABLE 0x003DBD54u
#define GAMEPAD_PORT_MASK 0x1u
#define XERROR_SUCCESS 0u
#define XERROR_IO_PENDING 0x3E5u
#define XERROR_DEVICE_NOT_CONNECTED 0x48Fu
#define XINPUT_DEVSUBTYPE_GC_GAMEPAD 0x01u
#define CAPS_SIZE 25u
#define CAPS_GAMEPAD 3u
#define CAPS_RUMBLE 21u
#define LOG_FIRST_CALLS 6
#define LOG_STATE_CHANGES 400
#define HANDLE_BYTES 0x100u

enum {
    H_GET_DEVICES,
    H_GET_CHANGES,
    H_PEEK,
    H_OPEN,
    H_CLOSE,
    H_CAPS,
    H_GET_STATE,
    H_SET_STATE,
    H_COUNT
};

static CRITICAL_SECTION g_lock;
static int g_ready, g_host_input;
static input_script_t g_script;
static DWORD g_t0;
static volatile LONG g_polls; /* lecturas del puerto 0: el reloj de los guiones en "p" */
static uint32_t g_packet = 1;
static xbox_gamepad_t g_last;
static volatile LONG g_calls[H_COUNT], g_state_logs, g_insertion_known, g_event_warned;
static uint8_t g_handles[4][HANDLE_BYTES];

typedef DWORD(WINAPI *xinput_get_state_fn)(DWORD, void *);
static xinput_get_state_fn g_xinput;
static const char *g_xinput_dll = "ninguna";

/* ---------------------------------------------------------------- entrada de PC ---- */

static int key_down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

static void load_xinput(void) {
    static const char *const dlls[] = {"xinput1_4.dll", "xinput9_1_0.dll"};
    int i;
    for (i = 0; i < 2 && !g_xinput; i++) {
        HMODULE h = LoadLibraryA(dlls[i]);
        if (h) {
            g_xinput = (xinput_get_state_fn)(void (*)(void))GetProcAddress(h, "XInputGetState");
            if (g_xinput)
                g_xinput_dll = dlls[i];
        }
    }
}

/* Estado actual del mando del puerto 0: mando de PC + teclado + guion. */
static void read_gamepad(xbox_gamepad_t *out) {
    struct {
        DWORD packet;
        pc_gamepad_t pad;
    } pc;
    xbox_gamepad_t part;
    memset(out, 0, sizeof *out);
    if (g_host_input) {
        if (g_xinput && g_xinput(0, &pc) == ERROR_SUCCESS) {
            xpad_from_pc(&pc.pad, &part);
            xpad_merge(out, &part);
        }
        xpad_from_keys(key_down, &part);
        xpad_merge(out, &part);
    }
    input_script_apply(&g_script, GetTickCount() - g_t0, (uint32_t)InterlockedIncrement(&g_polls), out);
}

/* ---------------------------------------------------------------- funciones HLE ---- */

/* Cuenta la llamada y registra las primeras: hilo Xbox y dirección de retorno en el juego. */
static void count_call(int which, uint32_t caller, const char *fmt, ...) {
    LONG n = InterlockedIncrement(&g_calls[which]);
    if (n <= LOG_FIRST_CALLS) {
        char line[192];
        ngb_thread_t *t = ngb_thread_current();
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(line, sizeof line, fmt, ap);
        va_end(ap);
        ngb_log("    · input: T%d @%08X %s%s", t ? t->index : 0, caller, line,
                n == LOG_FIRST_CALLS ? " (no se registran más)" : "");
    }
}

#define CALLER ((uint32_t)(uintptr_t)_ReturnAddress())

static int is_gamepad(uint32_t type) { return type == XDEVICE_TYPE_GAMEPAD_TABLE; }

static int handle_port(uint32_t handle) {
    int port;
    for (port = 0; port < 4; port++)
        if (handle && handle == (uint32_t)(uintptr_t)g_handles[port])
            return port;
    return -1;
}

static DWORD __stdcall hle_XGetDevices(uint32_t type) {
    DWORD mask = is_gamepad(type) ? GAMEPAD_PORT_MASK : 0;
    if (mask)
        InterlockedExchange(&g_insertion_known, 1);
    count_call(H_GET_DEVICES, CALLER, "XGetDevices(0x%08X) -> 0x%lX", type, mask);
    return mask;
}

static BOOL __stdcall hle_XGetDeviceChanges(uint32_t type, DWORD *insertions, DWORD *removals) {
    DWORD ins = 0;
    if (is_gamepad(type) && !InterlockedExchange(&g_insertion_known, 1))
        ins = GAMEPAD_PORT_MASK;
    if (insertions)
        *insertions = ins;
    if (removals)
        *removals = 0;
    count_call(H_GET_CHANGES, CALLER, "XGetDeviceChanges(0x%08X) -> inserciones 0x%lX", type, ins);
    return ins != 0;
}

static DWORD __stdcall hle_XPeekDevices(uint32_t type, DWORD *insertions, DWORD *removals) {
    DWORD mask = is_gamepad(type) ? GAMEPAD_PORT_MASK : 0;
    if (insertions)
        *insertions = mask && !g_insertion_known ? GAMEPAD_PORT_MASK : 0;
    if (removals)
        *removals = 0;
    count_call(H_PEEK, CALLER, "XPeekDevices(0x%08X) -> 0x%lX", type, mask);
    return mask;
}

static uint32_t __stdcall hle_XInputOpen(uint32_t type, DWORD port, DWORD slot, void *params) {
    uint32_t handle = 0;
    (void)params;
    if (is_gamepad(type) && port == 0)
        handle = (uint32_t)(uintptr_t)g_handles[0];
    count_call(H_OPEN, CALLER, "XInputOpen(0x%08X, puerto %lu, slot %lu) -> 0x%08X", type, port, slot,
               handle);
    return handle;
}

static void __stdcall hle_XInputClose(uint32_t handle) {
    count_call(H_CLOSE, CALLER, "XInputClose(0x%08X)", handle);
}

static DWORD __stdcall hle_XInputGetCapabilities(uint32_t handle, uint8_t *caps) {
    DWORD result = XERROR_DEVICE_NOT_CONNECTED;
    if (handle_port(handle) == 0 && caps) {
        memset(caps, 0, CAPS_SIZE);
        caps[0] = XINPUT_DEVSUBTYPE_GC_GAMEPAD;
        memset(caps + CAPS_GAMEPAD, 0xFF, sizeof(xbox_gamepad_t)); /* todo el mando existe */
        memset(caps + CAPS_RUMBLE, 0xFF, 4);
        result = XERROR_SUCCESS;
    }
    count_call(H_CAPS, CALLER, "XInputGetCapabilities(0x%08X) -> 0x%lX", handle, result);
    return result;
}

static DWORD __stdcall hle_XInputGetState(uint32_t handle, uint8_t *state) {
    xbox_input_state_t s;
    if (handle_port(handle) != 0 || !state) {
        count_call(H_GET_STATE, CALLER, "XInputGetState(0x%08X) -> no conectado", handle);
        return XERROR_DEVICE_NOT_CONNECTED;
    }
    read_gamepad(&s.gamepad);
    EnterCriticalSection(&g_lock);
    if (!xpad_equal(&s.gamepad, &g_last)) {
        g_packet++;
        g_last = s.gamepad;
        if (InterlockedIncrement(&g_state_logs) <= LOG_STATE_CHANGES)
            ngb_log("    · input: paquete %lu a los %lu ms (lectura %ld): botones 0x%04X, analógicos %02X %02X "
                    "%02X %02X %02X %02X %02X %02X, stick L (%d, %d) R (%d, %d)",
                    (unsigned long)g_packet, (unsigned long)(GetTickCount() - g_t0), (long)g_polls,
                    s.gamepad.buttons, s.gamepad.analog[0], s.gamepad.analog[1],
                    s.gamepad.analog[2], s.gamepad.analog[3], s.gamepad.analog[4],
                    s.gamepad.analog[5], s.gamepad.analog[6], s.gamepad.analog[7],
                    s.gamepad.thumb_lx, s.gamepad.thumb_ly, s.gamepad.thumb_rx,
                    s.gamepad.thumb_ry);
    }
    s.packet = g_packet;
    LeaveCriticalSection(&g_lock);
    memcpy(state, &s, sizeof s); /* 22 bytes: dwPacketNumber + XINPUT_GAMEPAD */
    count_call(H_GET_STATE, CALLER, "XInputGetState(0x%08X) -> paquete %lu, botones 0x%04X", handle,
               (unsigned long)s.packet, s.gamepad.buttons);
    return XERROR_SUCCESS;
}

static DWORD __stdcall hle_XInputSetState(uint32_t handle, uint8_t *feedback) {
    DWORD result = XERROR_DEVICE_NOT_CONNECTED;
    if (handle_port(handle) == 0 && feedback) {
        DWORD done = XERROR_SUCCESS, event; /* Header: dwStatus +0, hEvent +4 */
        memcpy(feedback, &done, sizeof done);
        memcpy(&event, feedback + 4, sizeof event);
        if (event && !InterlockedExchange(&g_event_warned, 1))
            ngb_log("    · input: aviso: XInputSetState con hEvent 0x%08lX, que no se señala",
                    event);
        result = XERROR_IO_PENDING;
    }
    count_call(H_SET_STATE, CALLER, "XInputSetState(0x%08X) -> 0x%lX (vibración ignorada)", handle,
               result);
    return result;
}

/* ---------------------------------------------------------------- tabla de hooks ---- */

typedef struct xapi_hook {
    const char *name;
    uint32_t va;
    uint8_t expect[8];
    uint32_t ret_va; /* dirección de `ret N` (C2 N 00) en el epílogo */
    uint8_t ret_n;
    const void *fn;
    int patched;
} xapi_hook_t;

static xapi_hook_t g_hooks[H_COUNT] = {
    {"XGetDevices", 0x003DCC99u, {0x56, 0xFF, 0x15, 0x84, 0x3B, 0x3E, 0x00, 0x8B}, 0x003DCCB8u,
     4, (const void *)hle_XGetDevices, 0},
    {"XGetDeviceChanges", 0x003DCCBBu, {0x55, 0x8B, 0xEC, 0x56, 0x8B, 0x75, 0x08, 0x33},
     0x003DCD25u, 12, (const void *)hle_XGetDeviceChanges, 0},
    {"XPeekDevices", 0x003DCC58u, {0x56, 0x57, 0xFF, 0x15, 0x84, 0x3B, 0x3E, 0x00}, 0x003DCC91u,
     12, (const void *)hle_XPeekDevices, 0},
    {"XInputOpen", 0x003DDFF1u, {0x55, 0x8B, 0xEC, 0x51, 0x8B, 0x4D, 0x08, 0x83}, 0x003DE044u,
     16, (const void *)hle_XInputOpen, 0},
    {"XInputClose", 0x003DE047u, {0x8B, 0x4C, 0x24, 0x04, 0xE8, 0xE9, 0x38, 0x00}, 0x003DE050u,
     4, (const void *)hle_XInputClose, 0},
    {"XInputGetCapabilities", 0x003DE053u, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x48, 0x83, 0x65},
     0x003DE228u, 8, (const void *)hle_XInputGetCapabilities, 0},
    {"XInputGetState", 0x003DE22Bu, {0x53, 0x56, 0x33, 0xDB, 0xFF, 0x15, 0x84, 0x3B},
     0x003DE29Bu, 8, (const void *)hle_XInputGetState, 0},
    {"XInputSetState", 0x003DE29Eu, {0x8B, 0x4C, 0x24, 0x04, 0x8D, 0x81, 0xA3, 0x00},
     0x003DE2CEu, 8, (const void *)hle_XInputSetState, 0},
};

/* ---------------------------------------------------------------- instalación ---- */

static int in_image(uint32_t va, uint32_t len) {
    return va >= NGB_RESERVE_BASE && va + len > va && va + len <= NGB_RESERVE_END;
}

/* Dirección del CSV de Ghidra para `name` (cualquier tipo de fila). 0 si no está. */
static uint32_t csv_address(const char *csv, const char *name) {
    FILE *f = NULL;
    char line[512];
    size_t len = strlen(name);
    uint32_t found = 0;
    if (!csv || fopen_s(&f, csv, "r") || !f)
        return 0;
    while (!found && fgets(line, sizeof line, f)) {
        char *comma = strchr(line, ','), *end;
        if (!comma || strncmp(line, "0x", 2))
            continue;
        end = strchr(comma + 1, ',');
        if (end && (size_t)(end - comma - 1) == len && !strncmp(comma + 1, name, len))
            found = (uint32_t)strtoul(line, NULL, 16);
    }
    fclose(f);
    return found;
}

/* 0 si `h` se puede parchear; si no, el motivo en `why`. */
static int verify(const xapi_hook_t *h, const char *csv, char *why, size_t whylen) {
    const uint8_t *code = (const uint8_t *)(uintptr_t)h->va;
    const uint8_t *ret = (const uint8_t *)(uintptr_t)h->ret_va;
    uint32_t addr;
    if (!in_image(h->va, 8) || !in_image(h->ret_va, 3)) {
        snprintf(why, whylen, "fuera de la imagen");
        return -1;
    }
    if (memcmp(code, h->expect, sizeof h->expect)) {
        snprintf(why, whylen, "bytes distintos (%02X %02X %02X %02X ...)", code[0], code[1],
                 code[2], code[3]);
        return -1;
    }
    if (ret[0] != 0xC2 || ret[1] != h->ret_n || ret[2] != 0) {
        snprintf(why, whylen, "no hay ret %u en 0x%08X", h->ret_n, h->ret_va);
        return -1;
    }
    addr = csv ? csv_address(csv, h->name) : h->va;
    if (addr != h->va) {
        snprintf(why, whylen, "el CSV la sitúa en 0x%08X", addr);
        return -1;
    }
    return 0;
}

int ngb_input_install(const char *symbols_csv, const char *script, int host_input) {
    char err[160], why[H_COUNT][96];
    int ok[H_COUNT], i, patched = 0, matched = 0;
    uint32_t table;
    if (!g_ready) {
        InitializeCriticalSection(&g_lock);
        g_ready = 1;
    }
    if (script && input_script_parse(script, &g_script, err, sizeof err)) {
        ngb_log("input: --input-script inválido: %s", err);
        return -1;
    }
    g_t0 = GetTickCount();
    g_host_input = host_input;
    if (host_input)
        load_xinput();
    for (i = 0; i < H_COUNT; i++)
        matched += (ok[i] = !verify(&g_hooks[i], symbols_csv, why[i], sizeof why[i]));
    if (!matched) {
        ngb_log("input: XAPI de Ninja Gaiden Black no reconocida en este XBE (%s en %s): sin HLE "
                "de entrada",
                why[0], g_hooks[0].name);
        return 0;
    }
    table = csv_address(symbols_csv, "XDEVICE_TYPE_GAMEPAD_TABLE");
    if (table && table != XDEVICE_TYPE_GAMEPAD_TABLE)
        ngb_log("input: aviso: XDEVICE_TYPE_GAMEPAD_TABLE en el CSV es 0x%08X, se esperaba 0x%08X",
                table, XDEVICE_TYPE_GAMEPAD_TABLE);
    for (i = 0; i < H_COUNT; i++) {
        xapi_hook_t *h = &g_hooks[i];
        uint8_t *code = (uint8_t *)(uintptr_t)h->va;
        int32_t rel;
        if (!ok[i]) {
            ngb_log("input: %s (0x%08X) no se parchea: %s", h->name, h->va, why[i]);
            continue;
        }
        rel = (int32_t)((uint32_t)(uintptr_t)h->fn - (h->va + 5));
        code[0] = 0xE9; /* jmp rel32 */
        memcpy(code + 1, &rel, sizeof rel);
        h->patched = 1;
        patched++;
        ngb_log("input: %s 0x%08X (ret %u en 0x%08X; bytes %02X %02X %02X %02X %02X %02X %02X "
                "%02X) -> jmp %p",
                h->name, h->va, h->ret_n, h->ret_va, h->expect[0], h->expect[1], h->expect[2],
                h->expect[3], h->expect[4], h->expect[5], h->expect[6], h->expect[7], h->fn);
    }
    FlushInstructionCache(GetCurrentProcess(), NULL, 0);
    ngb_log("input: %d de %d funciones de XAPI parcheadas%s; mando de PC: %s; teclado: %s; "
            "guion: %d pulsaciones",
            patched, H_COUNT, symbols_csv ? " (cotejadas con el CSV)" : "",
            host_input ? g_xinput_dll : "desactivado", host_input ? "sí" : "desactivado",
            g_script.count);
    return patched;
}

void ngb_input_summary(void) {
    char line[512];
    int i, pos = 0;
    for (i = 0; i < H_COUNT && pos < (int)sizeof line - 64; i++)
        if (g_hooks[i].patched)
            pos += snprintf(line + pos, sizeof line - (size_t)pos, "%s%s %ld", pos ? ", " : "",
                            g_hooks[i].name, (long)g_calls[i]);
    if (pos)
        ngb_log("input: llamadas: %s; último paquete %lu", line, (unsigned long)g_packet);
}
