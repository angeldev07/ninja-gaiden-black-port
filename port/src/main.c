/*
 * main.c: entry point of ngb-core.dll (called by ngb-loader.exe, see loader_stub.c).
 *
 *   ngb-loader.exe <path.xbe> [--log FILE] [--max-calls N] [--timeout S] [--idle S]
 *                  [--lenient] [--echo] [--window] [--symbols CSV]
 *                  [--input-script SCRIPT] [--no-host-input]
 *
 * Loads the XBE at its base address, links the kernel thunk to the stubs, sets up the first
 * Xbox thread (fake KPCR/KTHREAD, stack with TLS) and jumps to the entry point. The main
 * thread acts as a watchdog: maximum time, inactivity and the end of all threads.
 */
#include "ngb.h"

#include <shellapi.h>
#include <timeapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../res/version.h"
#include "ach/ach.h"
#include "gpu/nv2a.h"
#include "gfx/present.h"
#include "input/input.h"
#include "kernel/eeprom.h"
#include "kernel/kernel.h"

ngb_options_t g_ngb_opts;
xbe_t g_ngb_xbe;
extern volatile DWORD g_ngb_last_call_tick;

static void usage(void) {
    fprintf(stderr,
            "usage: ngb-loader.exe <path.xbe> [--log FILE] [--max-calls N] [--timeout S]\n"
            "                      [--idle S] [--lenient] [--echo] [--window] [--no-pacing]\n"
            "                      [--renderer gl|soft] [--scale N|auto] [--fullscreen] [--widescreen]\n"
            "                      [--mmio-strict] [--mmio-gpu-ram] [--pgraph-frames N]\n"
            "                      [--log-cap N] [--log-detail minimal|full] [--symbols CSV]\n"
            "                      [--input-script SCRIPT] [--no-host-input]\n"
            "  --log        execution log (default %%NGB_DATA%%\\runs\\h6-<date>.log)\n"
            "  --max-calls  stop after N kernel calls (default 200000; 0 = unlimited)\n"
            "  --timeout    maximum run time in seconds (default 60)\n"
            "  --idle       stop after S seconds without kernel calls (default 10)\n"
            "  --lenient    an unimplemented call returns STATUS_SUCCESS and execution continues\n"
            "  --echo       copy the log to stderr\n"
            "  --window     open the frame presenter window (H7; F12 = screenshot); --window-hidden: create it without showing it (tests)\n"
            "  --no-pacing  FLIP_STALL does not wait for the next 1/60 s (frames as fast as possible)\n"
            "  --renderer   gl (OpenGL 4.x, default when a context is available) or soft (rasterizer)\n"
            "  --scale N    internal resolution of the GL render: N times 720x480, 1-4, may be fractional (1);\n"
            "               auto = match the monitor's height (2.25 on a 1080p screen: 1620x1080)\n"
            "  --fullscreen borderless fullscreen on the monitor (Alt+Enter toggles it)\n"
            "  --widescreen tell the game the TV is 16:9: it widens its field of view and the picture\n"
            "               is shown at 16:9, with no bars on a 16:9 screen\n"
            "  --mmio-strict   stop at the first MMIO or port I/O that is not the modeled NV2A\n"
            "  --mmio-gpu-ram  NV2A as traced RAM instead of the model (H6 exploration)\n"
            "  --pgraph-frames N  frames with all methods in the PGRAPH log (3)\n"
            "  --log-cap N  log lines per kernel function; after that they are only counted (2000)\n"
            "  --log-detail minimal: only the header, warnings, errors, the stop summary and the crash\n"
            "               report (a few KB; the launcher default); full: every kernel call (default)\n"
            "  --symbols CSV  function names (Ghidra CSV) in the log and in crashes\n"
            "  --mute       no sound (the emulated APU keeps advancing)\n"
            "  --audio-wav  save what plays to <log>-audio.wav\n"
            "  --rdoc-frame N  capture frame N with RenderDoc (GL render; .rdc next to the log)\n"
            "  --rdoc-key     load RenderDoc and F11 captures the next frame (play and capture)\n"
            "  --debug-shots N  PNG of every Nth frame and of the first textures, next to the log (off)\n"
            "  --dump-ram FRAMES  guest RAM snapshots at those frames (\"2400,3000\"); F10 too\n"
            "  --watch LIST  watch game addresses (0xADDR[:1|2|4],...) and log the "
            "changes\n"
            "  --input-script  gamepad presses, e.g. \"t=3s:START,t=5s:A@500ms\"\n"
            "                  (\"t=2400p:A@10p\": in gamepad reads, not in ms)\n"
            "  --no-host-input neither PC gamepad nor keyboard: only the script\n"
            "  --ach-demo   a test achievement 5 s after start (toast and achievements.json)\n"
            "  --profile    sample the render and audio threads; hot functions in <log>-profile.txt\n");
}

/* Per-monitor DPI awareness before the window exists. Without it, at 125 % or 150 % Windows
 * renders the game window at a lower size and stretches the bitmap, which blurs the picture
 * whatever the internal resolution. Looked up at run time: Windows 10 1703 or later. */
static void set_dpi_aware(void) {
    typedef BOOL(WINAPI * set_ctx_fn)(HANDLE);
    HMODULE user = GetModuleHandleA("user32.dll");
    set_ctx_fn set_ctx = user ? (set_ctx_fn)(void *)GetProcAddress(user, "SetProcessDpiAwarenessContext") : NULL;
    if (!set_ctx || !set_ctx((HANDLE)(LONG_PTR)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */))
        SetProcessDPIAware();
}

/* Windows 11 stops honoring timeBeginPeriod for a process whose window is not visible (hidden,
 * minimized or covered) and may slow it down as a background process (EcoQoS). The synthetic
 * vblank of the DPC thread then came in 15.6 ms steps: one vblank in about 15 was 32 ms late and
 * the next one came right after, so the game stuttered at a steady average of 60 fps. Opting out
 * keeps 1 ms waits and full speed. Looked up at run time: it needs Windows 8 for the call and
 * Windows 11 for the timer flag; older systems ignore it. */
static void keep_timer_resolution(void) {
    typedef BOOL(WINAPI * set_info_fn)(HANDLE, int, LPVOID, DWORD);
    struct {
        ULONG version, control_mask, state_mask;
    } state = {1 /* PROCESS_POWER_THROTTLING_CURRENT_VERSION */,
               0x1 | 0x4 /* EXECUTION_SPEED | IGNORE_TIMER_RESOLUTION */, 0};
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    set_info_fn set_info = k32 ? (set_info_fn)(void *)GetProcAddress(k32, "SetProcessInformation") : NULL;
    if (set_info && !set_info(GetCurrentProcess(), 4 /* ProcessPowerThrottling */, &state, sizeof state))
        ngb_log("timer: could not opt out of power throttling (%lu)", GetLastError());
}

/* 1 if `path` (or any folder containing it) is inside a git repo: some ancestor has a
   `.git` (folder, or file in a worktree). */
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

/* Keeps runs\ from growing without bound: only the most recent KEEP_SESSIONS sessions with
 * the default log name survive. A session is every file starting with its "h6-<date>"
 * stem (log, PGRAPH log, debug PNGs, audio WAV); RenderDoc captures (.rdc) are kept. */
#define KEEP_SESSIONS 10

static int cmp_names(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

static void prune_old_sessions(const char *dir) {
    static char stems[512][32];
    char pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int n = 0, i;
    snprintf(pattern, sizeof pattern, "%s\\h6-????????-??????.log", dir);
    if ((h = FindFirstFileA(pattern, &fd)) == INVALID_HANDLE_VALUE)
        return;
    do {
        if (n < 512 && strlen(fd.cFileName) == 22) { /* "h6-YYYYMMDD-HHMMSS.log" */
            memcpy(stems[n], fd.cFileName, 18);
            stems[n++][18] = 0;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    qsort(stems, (size_t)n, sizeof stems[0], cmp_names); /* the timestamp sorts by name */
    for (i = 0; i < n - (KEEP_SESSIONS - 1); i++) { /* room for the session starting now */
        snprintf(pattern, sizeof pattern, "%s\\%s*", dir, stems[i]);
        if ((h = FindFirstFileA(pattern, &fd)) == INVALID_HANDLE_VALUE)
            continue;
        do {
            const char *ext = strrchr(fd.cFileName, '.');
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (ext && !_stricmp(ext, ".rdc")))
                continue;
            snprintf(path, sizeof path, "%s\\%s", dir, fd.cFileName);
            DeleteFileA(path);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
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
        out[0] = 0; /* no NGB_DATA or USERPROFILE: --log must be passed */
        return;
    }
    CreateDirectoryA(dir, NULL);
    prune_old_sessions(dir);
    GetLocalTime(&st);
    snprintf(out, outlen, "%s\\h6-%04u%02u%02u-%02u%02u%02u.log", dir, st.wYear, st.wMonth,
             st.wDay, st.wHour, st.wMinute, st.wSecond);
}

static int parse_args(void) {
    int argc = 0, i, explicit_limits = 0; /* bits: 1 max-calls, 2 timeout, 4 idle */
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    static char argbuf[32][8192]; /* a long --input-script does not fit in MAX_PATH */
    char *argv[32];
    if (!wargv || argc < 2 || argc > 32)
        return -1;
    for (i = 0; i < argc; i++) {
        if (!WideCharToMultiByte(CP_ACP, 0, wargv[i], -1, argbuf[i], (int)sizeof argbuf[i], NULL,
                                 NULL))
            return -1; /* argument too long: better to fail than to truncate it */
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
        else if (!strcmp(a, "--ach-demo"))
            g_ngb_opts.ach_demo = 1;
        else if (!strcmp(a, "--no-host-input"))
            g_ngb_opts.no_host_input = 1;
        else if (!strcmp(a, "--watch") && more) {
            if (ngb_watch_parse(argv[++i]))
                return -1;
            g_ngb_opts.watch = argv[i];
        }
        else if (!strcmp(a, "--log-detail") && more) {
            const char *v = argv[++i];
            if (!strcmp(v, "minimal"))
                g_ngb_opts.log_minimal = 1;
            else if (strcmp(v, "full"))
                return -1;
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
        else if (!strcmp(a, "--profile"))
            g_ngb_opts.profile = 1;
        else if (!strcmp(a, "--rdoc-key"))
            g_ngb_opts.rdoc_key = 1;
        else if (!strcmp(a, "--debug-shots") && more)
            g_ngb_opts.debug_shots = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--dump-ram") && more)
            g_ngb_opts.dump_ram = _strdup(argv[++i]);
        else if (!strcmp(a, "--scale") && more) {
            const char *v = argv[++i];
            if (!strcmp(v, "auto"))
                g_ngb_opts.scale_auto = 1;
            else {
                g_ngb_opts.scale = (float)strtod(v, NULL);
                if (!(g_ngb_opts.scale >= 1.0f && g_ngb_opts.scale <= 4.0f))
                    return -1;
            }
        }
        else if (!strcmp(a, "--fullscreen"))
            g_ngb_opts.fullscreen = 1;
        else if (!strcmp(a, "--widescreen"))
            g_ngb_opts.widescreen = 1;
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
    /* With a visible window the game is played: no limits except the ones explicitly requested
     * (the default limits are for diagnostic runs and tests). */
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
        fprintf(stderr, "ngb-loader: no NGB_DATA or USERPROFILE; give the log with --log\n");
        return -1;
    }
    if (inside_git_repo(g_ngb_opts.log_path)) {
        /* Game textures and surfaces are dumped as PNG next to the log: never inside a repo. */
        fprintf(stderr, "ngb-loader: the log (%s) is inside a git repo; game dumps "
                        "must not go there. Use a path under NGB_DATA.\n", g_ngb_opts.log_path);
        return -1;
    }
    if (g_ngb_opts.log_minimal)
        g_ngb_opts.pgraph_frames = g_ngb_opts.pgraph_at = 0; /* no method dumps in the short log */
    LocalFree(wargv);
    return 0;
}

static void log_header(const xbe_t *xbe) {
    uint32_t tls_total = ngb_tls_total_size(xbe);
    ngb_log("ngb-loader %s: native boot of an XBE (log: %s)", NGB_VERSION_STR,
            g_ngb_opts.log_minimal ? "minimal" : "full");
    ngb_log("xbe: %s", g_ngb_opts.xbe_path);
    ngb_log("xbe: base 0x%08X, image 0x%X, headers 0x%X, %u sections, title id 0x%08X",
            xbe->base, xbe->size_of_image, xbe->size_of_headers, xbe->section_count,
            xbe->title_id);
    ngb_log("xbe: %s, entry 0x%08X, kernel thunk 0x%08X, TLS 0x%08X, stack 0x%X", xbe->build,
            xbe->entry, xbe->thunk, xbe->tls_addr, xbe->stack_size);
    if (xbe->has_tls)
        ngb_log("tls: data [0x%08X, 0x%08X), zeros 0x%X, index at 0x%08X -> block of %u B",
                xbe->tls.raw_start, xbe->tls.raw_end, xbe->tls.zero_fill, xbe->tls.index_addr,
                tls_total);
    ngb_log("options: max-calls %lu, timeout %u s, idle %u s%s", g_ngb_opts.max_calls,
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
        fprintf(stderr, "ngb-loader: cannot create the log %s\n", g_ngb_opts.log_path);
        ExitProcess(NGB_EXIT_USAGE);
    }
    fprintf(stderr, "ngb-loader: log at %s\n", g_ngb_opts.log_path);
    /* System clock resolution at 1 ms. By default Windows rounds every Sleep and every
     * timed wait to 15.6 ms, and the Xbox code assumes 1 ms waits: the CRI file thread
     * (KeDelayExecutionThread in a loop) loaded each block 15 times slower and the cache
     * load hit its 10 s timeout over and over (the warning loop). It is restored on its own
     * when the process exits. */
    timeBeginPeriod(1);
    keep_timer_resolution();
    if (xbe_load_file(g_ngb_opts.xbe_path, &g_ngb_xbe, err, sizeof err))
        ngb_stop(NGB_EXIT_USAGE, "invalid XBE: %s", err);
    log_header(&g_ngb_xbe);
    {
        /* The console's region follows the XBE: one that may run in North America (game region
         * bit 0, e.g. the NTSC-U release) gets an NTSC console; any other (the PAL release),
         * a PAL one, as before. */
        const uint8_t *r = xbe_va_ptr(&g_ngb_xbe, g_ngb_xbe.cert_addr + 0xA0u, 4);
        uint32_t region = r ? (uint32_t)(r[0] | r[1] << 8 | r[2] << 16 | (uint32_t)r[3] << 24) : 0;
        eeprom_set_ntsc((region & 1u) != 0);
        ngb_log("xbe: game region 0x%X: %s console", region, region & 1u ? "NTSC (North America)" : "PAL");
    }
    if (g_ngb_opts.symbols && ngb_symbols_load(g_ngb_opts.symbols))
        ngb_log("symbols: could not read %s", g_ngb_opts.symbols);
    ngb_prof_start();

    /* Presenter window (H7). The hook is in gpu/pgraph.c: on every FLIP_STALL the 720-wide
     * color buffer that the game cleared or drew is presented (software-rasterized in
     * gpu/raster.c) and it waits for the next 1/60 s (--no-pacing removes that). */
    if (g_ngb_opts.window) {
        gfx_config_t cfg;
        set_dpi_aware();
        memset(&cfg, 0, sizeof cfg);
        cfg.title = "Ninja Gaiden Black · native port (unofficial)"; /* UTF-8: gfx_init_ex converts it to UTF-16 */
        cfg.width = 720;
        cfg.height = 480;
        cfg.scale = 2;
        cfg.aspect = g_ngb_opts.widescreen ? GFX_ASPECT_16_9 : GFX_ASPECT_4_3;
        cfg.hidden = g_ngb_opts.window == 2; /* render and captures all the same, no visible window */
        cfg.fullscreen = g_ngb_opts.fullscreen;
        if (gfx_init_ex(&cfg))
            ngb_log("gfx: could not open the window; continuing without it");
        else
            ngb_log("gfx: window 720x480 x2 (%s), F12 saves a screenshot in runs\\shots",
                    g_ngb_opts.widescreen ? "16:9" : "4:3");
    }

    /* Xbox: a single core and no NX. Xbox threads are pinned to core 0 (ngb_thread_spawn);
     * the ones that emulate hardware, to the rest (ngb_pin_hardware_thread). */
    SetThreadAffinityMask(GetCurrentThread(), 1);
    SetProcessDEPPolicy(0);

    ngb_kernel_init();
    ngb_kdata_init(g_ngb_opts.xbe_path);
    if (ngb_memory_init() || ngb_memory_map_xbe(&g_ngb_xbe))
        ngb_stop(NGB_EXIT_USAGE, "could not prepare the image memory");
    {
        /* PGRAPH log next to the main one: <log>-pgraph.log (and the PNGs, <log>-pgraph-NNN.png). */
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
    ngb_ach_init(&g_ngb_xbe, g_ngb_opts.ach_demo);
    if (ngb_memory_patch_thunk(&g_ngb_xbe))
        ngb_stop(NGB_EXIT_USAGE, "the thunk imports unknown ordinals");
    /* H8: the gamepad, through XAPI HLE (USB has no devices). Before the first thread. */
    if (ngb_input_install(g_ngb_opts.symbols, g_ngb_opts.input_script,
                          !g_ngb_opts.no_host_input) < 0)
        ngb_stop(NGB_EXIT_USAGE, "invalid --input-script");
    tls_total = ngb_tls_total_size(&g_ngb_xbe);
    if (g_ngb_xbe.has_tls && g_ngb_xbe.tls.index_addr)
        *(int32_t *)(uintptr_t)g_ngb_xbe.tls.index_addr = -(int32_t)(tls_total / 4);
    ngb_log("kernel: %u exports in the table, %u implemented", (unsigned)ngb_kexport_count,
            (unsigned)ngb_kernel_impl_count());
    ngb_crash_install();
    ngb_log("---- jumping to the entry point 0x%08X", g_ngb_xbe.entry);
    ngb_log_flush();

    g_ngb_last_call_tick = GetTickCount();
    boot = ngb_thread_spawn(g_ngb_xbe.entry, 0, 0, g_ngb_xbe.stack_size, tls_total, 0, 1);
    if (!boot)
        ngb_stop(NGB_EXIT_CRASH, "could not create the boot thread");

    start = GetTickCount();
    for (;;) {
        DWORD now;
        Sleep(5);
        ngb_kdata_tick();
        ngb_ach_tick();
        now = GetTickCount();
        if (ngb_thread_alive_count() == 0)
            ngb_stop(NGB_EXIT_OK, "the entry point and all Xbox threads finished");
        if (g_ngb_opts.window && gfx_poll())
            ngb_stop(NGB_EXIT_OK, "window closed by the user");
        if (g_ngb_opts.timeout_s && now - start > g_ngb_opts.timeout_s * 1000u)
            ngb_stop(NGB_EXIT_TIMEOUT, "--timeout of %u s expired", g_ngb_opts.timeout_s);
        /* Signed: if another thread stamps its call after `now` was read, the unsigned
         * subtraction gave ~49 days and stopped the run with a false "no calls". */
        if (g_ngb_opts.idle_s &&
            (LONG)(now - g_ngb_last_call_tick) > (LONG)(g_ngb_opts.idle_s * 1000u))
            ngb_stop(NGB_EXIT_TIMEOUT,
                     "%u s without kernel calls (wait loop, blocked thread or long computation)",
                     g_ngb_opts.idle_s);
        if ((now - start) % 1000 < 5)
            ngb_log_flush();
    }
}
