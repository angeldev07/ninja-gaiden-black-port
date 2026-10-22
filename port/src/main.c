/*
 * main.c: punto de entrada de ngb-core.dll (lo llama ngb-loader.exe, ver loader_stub.c).
 *
 *   ngb-loader.exe <ruta.xbe> [--log ARCHIVO] [--max-calls N] [--timeout S] [--idle S]
 *                  [--lenient] [--echo] [--window] [--symbols CSV]
 *                  [--input-script GUION] [--no-host-input]
 *
 * Carga el XBE en su dirección base, enlaza el kernel thunk con los stubs, prepara el
 * primer hilo Xbox (KPCR/KTHREAD falsos, pila con TLS) y salta al entry point. El hilo
 * principal hace de vigilante: tiempo máximo, inactividad y fin de todos los hilos.
 */
#include "ngb.h"

#include <shellapi.h>
#include <timeapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu/nv2a.h"
#include "gfx/present.h"
#include "input/input.h"
#include "kernel/kernel.h"

ngb_options_t g_ngb_opts;
xbe_t g_ngb_xbe;
extern volatile DWORD g_ngb_last_call_tick;

static void usage(void) {
    fprintf(stderr,
            "uso: ngb-loader.exe <ruta.xbe> [--log ARCHIVO] [--max-calls N] [--timeout S]\n"
            "                    [--idle S] [--lenient] [--echo] [--window] [--no-pacing]\n"
            "                    [--renderer gl|soft]\n"
            "                    [--mmio-strict] [--mmio-gpu-ram] [--pgraph-frames N]\n"
            "                    [--log-cap N] [--symbols CSV]\n"
            "                    [--input-script GUION] [--no-host-input]\n"
            "  --log        log de la ejecución (por defecto %%NGB_DATA%%\\runs\\h6-<fecha>.log)\n"
            "  --max-calls  para tras N llamadas al kernel (por defecto 200000; 0 = sin límite)\n"
            "  --timeout    segundos máximos de ejecución (por defecto 60)\n"
            "  --idle       para si pasan S segundos sin llamadas al kernel (por defecto 10)\n"
            "  --lenient    una llamada sin implementar devuelve STATUS_SUCCESS y sigue\n"
            "  --echo       copia el log a stderr\n"
            "  --window     abre la ventana del presentador de frames (H7; F12 = captura); --window-hidden: la crea sin mostrarla (pruebas)\n"
            "  --no-pacing  FLIP_STALL no espera al siguiente 1/60 s (frames tan rápido como pueda)\n"
            "  --renderer   gl (OpenGL 4.x, por defecto si hay contexto) o soft (rasterizador)\n"
            "  --mmio-strict   para en el primer MMIO o E/S de puertos que no sea el NV2A modelado\n"
            "  --mmio-gpu-ram  NV2A como RAM trazada en vez del modelo (exploración de H6)\n"
            "  --pgraph-frames N  frames con todos los métodos en el log de PGRAPH (3)\n"
            "  --log-cap N  líneas de log por función del kernel; luego solo se cuentan (2000)\n"
            "  --symbols CSV  nombres de funciones (CSV de Ghidra) en el log y los crashes\n"
            "  --mute       sin sonido (la APU emulada sigue avanzando)\n"
            "  --audio-wav  guarda lo que suena en <log>-audio.wav\n"
            "  --rdoc-frame N  captura el frame N con RenderDoc (render GL; .rdc junto al log)\n"
            "  --watch LISTA  vigila direcciones del juego (0xADDR[:1|2|4],...) y registra los "
            "cambios\n"
            "  --input-script  pulsaciones del mando, p. ej. \"t=3s:START,t=5s:A@500ms\"\n"
            "                  (\"t=2400p:A@10p\": en lecturas del mando, no en ms)\n"
            "  --no-host-input ni mando de PC ni teclado: solo el guion\n");
}

/* 1 si `path` (o alguna carpeta que lo contiene) está dentro de un repo git: algún ancestro
   tiene un `.git` (carpeta, o archivo en un worktree). */
static int inside_git_repo(const char *path) {
    char full[MAX_PATH], probe[MAX_PATH];
    char *cut;
    DWORD n = GetFullPathNameA(path, sizeof full, full, NULL);
    if (!n || n >= sizeof full)
        return 0;
    while ((cut = strrchr(full, '\\')) != NULL) {
        *cut = 0;
        if (!full[0])
            break;
        if ((size_t)snprintf(probe, sizeof probe, "%s\\.git", full) < sizeof probe &&
            GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES)
            return 1;
    }
    return 0;
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
    int argc = 0, i, explicit_limits = 0; /* bits: 1 max-calls, 2 timeout, 4 idle */
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    static char argbuf[32][2048]; /* un --input-script largo no cabe en MAX_PATH */
    char *argv[32];
    if (!wargv || argc < 2 || argc > 32)
        return -1;
    for (i = 0; i < argc; i++) {
        if (!WideCharToMultiByte(CP_ACP, 0, wargv[i], -1, argbuf[i], (int)sizeof argbuf[i], NULL,
                                 NULL))
            return -1; /* argumento demasiado largo: mejor fallar que truncarlo */
        argv[i] = argbuf[i];
    }
    g_ngb_opts.max_calls = 200000;
    g_ngb_opts.timeout_s = 60;
    g_ngb_opts.idle_s = 10;
    g_ngb_opts.pgraph_frames = 3;
    g_ngb_opts.log_cap = 2000;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;
        if (!strcmp(a, "--log") && more)
            snprintf(g_ngb_opts.log_path, MAX_PATH, "%s", argv[++i]);
        else if (!strcmp(a, "--max-calls") && more) {
            g_ngb_opts.max_calls = strtoul(argv[++i], NULL, 0);
            explicit_limits |= 1;
        } else if (!strcmp(a, "--timeout") && more) {
            g_ngb_opts.timeout_s = (unsigned)strtoul(argv[++i], NULL, 0);
            explicit_limits |= 2;
        } else if (!strcmp(a, "--idle") && more) {
            g_ngb_opts.idle_s = (unsigned)strtoul(argv[++i], NULL, 0);
            explicit_limits |= 4;
        }
        else if (!strcmp(a, "--lenient"))
            g_ngb_opts.lenient = 1;
        else if (!strcmp(a, "--echo"))
            g_ngb_opts.echo = 1;
        else if (!strcmp(a, "--mmio-strict"))
            g_ngb_opts.mmio_strict = 1;
        else if (!strcmp(a, "--mmio-gpu-ram"))
            g_ngb_opts.gpu_ram = 1;
        else if (!strcmp(a, "--pgraph-frames") && more)
            g_ngb_opts.pgraph_frames = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--pgraph-at") && more)
            g_ngb_opts.pgraph_at = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--symbols") && more)
            g_ngb_opts.symbols = _strdup(argv[++i]);
        else if (!strcmp(a, "--input-script") && more)
            g_ngb_opts.input_script = _strdup(argv[++i]);
        else if (!strcmp(a, "--no-host-input"))
            g_ngb_opts.no_host_input = 1;
        else if (!strcmp(a, "--watch") && more) {
            if (ngb_watch_parse(argv[++i]))
                return -1;
            g_ngb_opts.watch = argv[i];
        }
        else if (!strcmp(a, "--log-cap") && more)
            g_ngb_opts.log_cap = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--window"))
            g_ngb_opts.window = 1;
        else if (!strcmp(a, "--window-hidden"))
            g_ngb_opts.window = 2;
        else if (!strcmp(a, "--mute"))
            g_ngb_opts.mute = 1;
        else if (!strcmp(a, "--audio-wav"))
            g_ngb_opts.audio_wav = 1;
        else if (!strcmp(a, "--rdoc-frame") && more)
            g_ngb_opts.rdoc_frame = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--no-pacing"))
            g_ngb_opts.no_pacing = 1;
        else if (!strcmp(a, "--renderer") && more) {
            const char *v = argv[++i];
            if (!strcmp(v, "gl"))
                g_ngb_opts.renderer = NGB_RENDERER_GL;
            else if (!strcmp(v, "soft"))
                g_ngb_opts.renderer = NGB_RENDERER_SOFT;
            else
                return -1;
        }
        else if (a[0] == '-' || g_ngb_opts.xbe_path)
            return -1;
        else
            g_ngb_opts.xbe_path = _strdup(a);
    }
    /* Con ventana visible se juega: sin límites salvo los que se pidan expresamente (los
     * límites por defecto son para las ejecuciones de diagnóstico y los tests). */
    if (g_ngb_opts.window == 1) {
        if (!(explicit_limits & 1))
            g_ngb_opts.max_calls = 0;
        if (!(explicit_limits & 2))
            g_ngb_opts.timeout_s = 0;
        if (!(explicit_limits & 4))
            g_ngb_opts.idle_s = 0;
    }
    if (!g_ngb_opts.xbe_path)
        return -1;
    if (!g_ngb_opts.log_path[0])
        default_log_path(g_ngb_opts.log_path, MAX_PATH);
    if (!g_ngb_opts.log_path[0]) {
        fprintf(stderr, "ngb-loader: sin NGB_DATA ni USERPROFILE; indica el log con --log\n");
        return -1;
    }
    if (inside_git_repo(g_ngb_opts.log_path)) {
        /* Junto al log se vuelcan texturas y superficies del juego en PNG: nunca dentro de un repo. */
        fprintf(stderr, "ngb-loader: el log (%s) está dentro de un repo git; los volcados del juego "
                        "no pueden ir ahí. Usa una ruta bajo NGB_DATA.\n", g_ngb_opts.log_path);
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
    /* Resolución del reloj del sistema a 1 ms. Por defecto Windows redondea cada Sleep y cada
     * espera con timeout a 15,6 ms, y el código de la Xbox da por hechas esperas de 1 ms: el
     * hilo de ficheros de CRI (KeDelayExecutionThread en bucle) cargaba cada bloque 15 veces
     * más lento y la carga de la caché agotaba su timeout de 10 s una y otra vez (el bucle de
     * los avisos). Se restaura sola al salir del proceso. */
    timeBeginPeriod(1);
    if (xbe_load_file(g_ngb_opts.xbe_path, &g_ngb_xbe, err, sizeof err))
        ngb_stop(NGB_EXIT_USAGE, "XBE inválido: %s", err);
    log_header(&g_ngb_xbe);
    if (g_ngb_opts.symbols && ngb_symbols_load(g_ngb_opts.symbols))
        ngb_log("símbolos: no se pudo leer %s", g_ngb_opts.symbols);

    /* Ventana del presentador (H7). El enganche está en gpu/pgraph.c: en cada FLIP_STALL se
     * presenta el búfer de color de 720 de ancho que el juego limpió o dibujó (rasterizado
     * por software en gpu/raster.c) y se espera al siguiente 1/60 s (--no-pacing lo quita). */
    if (g_ngb_opts.window) {
        gfx_config_t cfg;
        memset(&cfg, 0, sizeof cfg);
        cfg.title = "Ninja Gaiden Black (ngb-loader)";
        cfg.width = 720;
        cfg.height = 480;
        cfg.scale = 2;
        cfg.aspect = GFX_ASPECT_4_3;
        cfg.hidden = g_ngb_opts.window == 2; /* render y capturas igual, sin ventana visible */
        if (gfx_init_ex(&cfg))
            ngb_log("gfx: no se pudo abrir la ventana; se sigue sin ella");
        else
            ngb_log("gfx: ventana 720x480 x2 (4:3), F12 guarda captura en runs\\shots");
    }

    /* Xbox: un solo núcleo y sin NX. Los hilos Xbox se fijan al núcleo 0 (ngb_thread_spawn);
     * los que emulan hardware, al resto (ngb_pin_hardware_thread). */
    SetThreadAffinityMask(GetCurrentThread(), 1);
    SetProcessDEPPolicy(0);

    ngb_kernel_init();
    ngb_kdata_init(g_ngb_opts.xbe_path);
    if (ngb_memory_init() || ngb_memory_map_xbe(&g_ngb_xbe))
        ngb_stop(NGB_EXIT_USAGE, "no se pudo preparar la memoria de la imagen");
    {
        /* Log de PGRAPH junto al principal: <log>-pgraph.log (y los PNG, <log>-pgraph-NNN.png). */
        char pg[MAX_PATH];
        char *dot;
        snprintf(pg, sizeof pg, "%s", g_ngb_opts.log_path);
        dot = strrchr(pg, '.');
        if (dot)
            *dot = 0;
        strncat_s(pg, sizeof pg, "-pgraph.log", _TRUNCATE);
        nv2a_init(pg);
    }
    ngb_memory_patch_irql(&g_ngb_xbe);
    if (ngb_memory_patch_thunk(&g_ngb_xbe))
        ngb_stop(NGB_EXIT_USAGE, "el thunk importa ordinales desconocidos");
    /* H8: el mando, por HLE de XAPI (el USB no tiene dispositivos). Antes del primer hilo. */
    if (ngb_input_install(g_ngb_opts.symbols, g_ngb_opts.input_script,
                          !g_ngb_opts.no_host_input) < 0)
        ngb_stop(NGB_EXIT_USAGE, "--input-script inválido");
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
        if (g_ngb_opts.window && gfx_poll())
            ngb_stop(NGB_EXIT_OK, "ventana cerrada por el usuario");
        if (g_ngb_opts.timeout_s && now - start > g_ngb_opts.timeout_s * 1000u)
            ngb_stop(NGB_EXIT_TIMEOUT, "--timeout de %u s agotado", g_ngb_opts.timeout_s);
        /* Con signo: si otro hilo apunta su llamada después de leer `now`, la resta sin signo
         * daba ~49 días y paraba la ejecución con un falso "sin llamadas". */
        if (g_ngb_opts.idle_s &&
            (LONG)(now - g_ngb_last_call_tick) > (LONG)(g_ngb_opts.idle_s * 1000u))
            ngb_stop(NGB_EXIT_TIMEOUT,
                     "%u s sin llamadas al kernel (bucle de espera, hilo bloqueado o cálculo largo)",
                     g_ngb_opts.idle_s);
        if ((now - start) % 1000 < 5)
            ngb_log_flush();
    }
}
