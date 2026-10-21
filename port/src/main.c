/*
 * main.c: punto de entrada de ngb-core.dll (lo llama ngb-loader.exe, ver loader_stub.c).
 *
 *   ngb-loader.exe <ruta.xbe> [--log ARCHIVO] [--max-calls N] [--timeout S] [--idle S]
 *                  [--lenient] [--echo]
 *
 * Carga el XBE en su dirección base, enlaza el kernel thunk con los stubs, prepara el
 * primer hilo Xbox (KPCR/KTHREAD falsos, pila con TLS) y salta al entry point. El hilo
 * principal hace de vigilante: tiempo máximo, inactividad y fin de todos los hilos.
 */
#include "ngb.h"

#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/kernel.h"

ngb_options_t g_ngb_opts;
xbe_t g_ngb_xbe;
extern volatile DWORD g_ngb_last_call_tick;

static void usage(void) {
    fprintf(stderr,
            "uso: ngb-loader.exe <ruta.xbe> [--log ARCHIVO] [--max-calls N] [--timeout S]\n"
            "                    [--idle S] [--lenient] [--echo]\n"
            "  --log        log de la ejecución (por defecto %%NGB_DATA%%\\runs\\h6-<fecha>.log)\n"
            "  --max-calls  para tras N llamadas al kernel (por defecto 200000; 0 = sin límite)\n"
            "  --timeout    segundos máximos de ejecución (por defecto 60)\n"
            "  --idle       para si pasan S segundos sin llamadas al kernel (por defecto 10)\n"
            "  --lenient    una llamada sin implementar devuelve STATUS_SUCCESS y sigue\n"
            "  --echo       copia el log a stderr\n");
}

static void default_log_path(char *out, size_t outlen) {
    char dir[MAX_PATH];
    const char *data = getenv("NGB_DATA"), *home = getenv("USERPROFILE");
    SYSTEMTIME st;
    if (data && *data)
        snprintf(dir, sizeof dir, "%s\\runs", data);
    else if (home && *home)
        snprintf(dir, sizeof dir, "%s\\ngb-data\\runs", home);
    else {
        out[0] = 0; /* sin NGB_DATA ni USERPROFILE: hay que pasar --log */
        return;
    }
    CreateDirectoryA(dir, NULL);
    GetLocalTime(&st);
    snprintf(out, outlen, "%s\\h6-%04u%02u%02u-%02u%02u%02u.log", dir, st.wYear, st.wMonth,
             st.wDay, st.wHour, st.wMinute, st.wSecond);
}

static int parse_args(void) {
    int argc = 0, i;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    static char argbuf[16][MAX_PATH];
    char *argv[16];
    if (!wargv || argc < 2 || argc > 16)
        return -1;
    for (i = 0; i < argc; i++) {
        WideCharToMultiByte(CP_ACP, 0, wargv[i], -1, argbuf[i], MAX_PATH, NULL, NULL);
        argv[i] = argbuf[i];
    }
    g_ngb_opts.max_calls = 200000;
    g_ngb_opts.timeout_s = 60;
    g_ngb_opts.idle_s = 10;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;
        if (!strcmp(a, "--log") && more)
            snprintf(g_ngb_opts.log_path, MAX_PATH, "%s", argv[++i]);
        else if (!strcmp(a, "--max-calls") && more)
            g_ngb_opts.max_calls = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--timeout") && more)
            g_ngb_opts.timeout_s = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--idle") && more)
            g_ngb_opts.idle_s = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--lenient"))
            g_ngb_opts.lenient = 1;
        else if (!strcmp(a, "--echo"))
            g_ngb_opts.echo = 1;
        else if (!strcmp(a, "--mmio-strict"))
            g_ngb_opts.mmio_strict = 1;
        else if (!strcmp(a, "--mmio-gpu-ram"))
            g_ngb_opts.gpu_ram = 1;
        else if (a[0] == '-' || g_ngb_opts.xbe_path)
            return -1;
        else
            g_ngb_opts.xbe_path = _strdup(a);
    }
    if (!g_ngb_opts.xbe_path)
        return -1;
    if (!g_ngb_opts.log_path[0])
        default_log_path(g_ngb_opts.log_path, MAX_PATH);
    if (!g_ngb_opts.log_path[0]) {
        fprintf(stderr, "ngb-loader: sin NGB_DATA ni USERPROFILE; indica el log con --log\n");
        return -1;
    }
    LocalFree(wargv);
    return 0;
}

static void log_header(const xbe_t *xbe) {
    uint32_t tls_total = ngb_tls_total_size(xbe);
    ngb_log("ngb-loader H6: arranque nativo de un XBE (proyecto pedagógico)");
    ngb_log("xbe: %s", g_ngb_opts.xbe_path);
    ngb_log("xbe: base 0x%08X, imagen 0x%X, cabeceras 0x%X, %u secciones, title id 0x%08X",
            xbe->base, xbe->size_of_image, xbe->size_of_headers, xbe->section_count,
            xbe->title_id);
    ngb_log("xbe: %s, entry 0x%08X, kernel thunk 0x%08X, TLS 0x%08X, pila 0x%X", xbe->build,
            xbe->entry, xbe->thunk, xbe->tls_addr, xbe->stack_size);
    if (xbe->has_tls)
        ngb_log("tls: datos [0x%08X, 0x%08X), ceros 0x%X, índice en 0x%08X -> bloque de %u B",
                xbe->tls.raw_start, xbe->tls.raw_end, xbe->tls.zero_fill, xbe->tls.index_addr,
                tls_total);
    ngb_log("opciones: max-calls %lu, timeout %u s, idle %u s%s", g_ngb_opts.max_calls,
            g_ngb_opts.timeout_s, g_ngb_opts.idle_s, g_ngb_opts.lenient ? ", lenient" : "");
}

__declspec(dllexport) int __cdecl ngb_main(void) {
    char err[256];
    uint32_t tls_total;
    ngb_thread_t *boot;
    DWORD start;
    if (parse_args()) {
        usage();
        ExitProcess(NGB_EXIT_USAGE);
    }
    if (ngb_log_open(g_ngb_opts.log_path)) {
        fprintf(stderr, "ngb-loader: no se puede crear el log %s\n", g_ngb_opts.log_path);
        ExitProcess(NGB_EXIT_USAGE);
    }
    fprintf(stderr, "ngb-loader: log en %s\n", g_ngb_opts.log_path);
    if (xbe_load_file(g_ngb_opts.xbe_path, &g_ngb_xbe, err, sizeof err))
        ngb_stop(NGB_EXIT_USAGE, "XBE inválido: %s", err);
    log_header(&g_ngb_xbe);

    /* Xbox: un solo núcleo y sin NX. */
    SetProcessAffinityMask(GetCurrentProcess(), 1);
    SetProcessDEPPolicy(0);

    ngb_kernel_init();
    ngb_kdata_init(g_ngb_opts.xbe_path);
    if (ngb_memory_init() || ngb_memory_map_xbe(&g_ngb_xbe))
        ngb_stop(NGB_EXIT_USAGE, "no se pudo preparar la memoria de la imagen");
    if (ngb_memory_patch_thunk(&g_ngb_xbe))
        ngb_stop(NGB_EXIT_USAGE, "el thunk importa ordinales desconocidos");
    tls_total = ngb_tls_total_size(&g_ngb_xbe);
    if (g_ngb_xbe.has_tls && g_ngb_xbe.tls.index_addr)
        *(int32_t *)(uintptr_t)g_ngb_xbe.tls.index_addr = -(int32_t)(tls_total / 4);
    ngb_log("kernel: %u exports en la tabla, %u implementados", (unsigned)ngb_kexport_count,
            (unsigned)ngb_kernel_impl_count());
    ngb_crash_install();
    ngb_log("---- saltando al entry point 0x%08X", g_ngb_xbe.entry);
    ngb_log_flush();

    g_ngb_last_call_tick = GetTickCount();
    boot = ngb_thread_spawn(g_ngb_xbe.entry, 0, 0, g_ngb_xbe.stack_size, tls_total, 0, 1);
    if (!boot)
        ngb_stop(NGB_EXIT_CRASH, "no se pudo crear el hilo de arranque");

    start = GetTickCount();
    for (;;) {
        DWORD now;
        Sleep(5);
        ngb_kdata_tick();
        now = GetTickCount();
        if (ngb_thread_alive_count() == 0)
            ngb_stop(NGB_EXIT_OK, "el entry point y todos los hilos Xbox terminaron");
        if (g_ngb_opts.timeout_s && now - start > g_ngb_opts.timeout_s * 1000u)
            ngb_stop(NGB_EXIT_TIMEOUT, "--timeout de %u s agotado", g_ngb_opts.timeout_s);
        if (g_ngb_opts.idle_s && now - g_ngb_last_call_tick > g_ngb_opts.idle_s * 1000u)
            ngb_stop(NGB_EXIT_TIMEOUT,
                     "%u s sin llamadas al kernel (bucle de espera, hilo bloqueado o cálculo largo)",
                     g_ngb_opts.idle_s);
        if ((now - start) % 1000 < 5)
            ngb_log_flush();
    }
}
