/*
 * rdoc.c: single-frame capture with RenderDoc (--rdoc-frame N), to debug the GL render.
 *
 * RenderDoc (https://renderdoc.org, MIT) hooks into OpenGL if its DLL is loaded before the
 * context is created. With its in-application API exactly the requested frame is captured;
 * the capture (.rdc) is left next to the log, in NGB_DATA, and is analyzed with qrenderdoc or
 * with its Python API (draws, state, textures, pixel history). It is optional: without the
 * DLL nothing happens. The loader is 32-bit, so the installation's x86 DLL is used.
 * With --rdoc-key, each F11 press captures the next frame (to play by hand and capture
 * right when a glitch shows up).
 */
#include "rdoc.h"

#include <stdbool.h> /* renderdoc_app.h uses bool */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma warning(push)
#pragma warning(disable : 4201) /* the RenderDoc header uses anonymous unions */
#include "../../third_party/renderdoc/renderdoc_app.h"
#pragma warning(pop)
#include "../ngb.h"

static RENDERDOC_API_1_6_0 *g_rdoc;
static uint32_t g_target;
static int g_capturing, g_hotkey;

int rdoc_load(uint32_t frame, int hotkey) {
    pRENDERDOC_GetAPI get_api;
    char dll[MAX_PATH], tmpl[MAX_PATH + 16];
    const char *env = getenv("NGB_RENDERDOC_DLL");
    HMODULE h;
    char *dot;
    if (!frame && !hotkey)
        return 0;
    snprintf(dll, sizeof dll, "%s", env && *env ? env : "C:\\Program Files\\RenderDoc\\x86\\renderdoc.dll");
    h = GetModuleHandleA("renderdoc.dll");
    if (!h)
        h = LoadLibraryA(dll);
    if (!h) {
        ngb_log("rdoc: could not load %s (is RenderDoc installed?)", dll);
        return -1;
    }
    get_api = (pRENDERDOC_GetAPI)(void (*)(void))GetProcAddress(h, "RENDERDOC_GetAPI");
    if (!get_api || !get_api(eRENDERDOC_API_Version_1_6_0, (void **)&g_rdoc) || !g_rdoc) {
        ngb_log("rdoc: %s does not offer API 1.6.0", dll);
        g_rdoc = NULL;
        return -1;
    }
    /* Captures next to the log: <log without extension>-rdoc_frameN.rdc (numbered by RenderDoc). */
    snprintf(tmpl, sizeof tmpl, "%s", g_ngb_opts.log_path);
    dot = strrchr(tmpl, '.');
    if (dot && !strchr(dot, '\\'))
        *dot = 0;
    strncat(tmpl, "-rdoc", sizeof tmpl - strlen(tmpl) - 1);
    g_rdoc->SetCaptureFilePathTemplate(tmpl);
    g_rdoc->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
    g_target = frame;
    g_hotkey = hotkey;
    if (frame)
        ngb_log("rdoc: RenderDoc loaded; frame %u will be captured in %s_*.rdc", frame, tmpl);
    if (hotkey)
        ngb_log("rdoc: RenderDoc loaded; F11 captures the next frame in %s_*.rdc", tmpl);
    return 0;
}

void rdoc_after_present(uint32_t presented) {
    if (!g_rdoc)
        return;
    /* F11 (on press): captures the next frame. */
    if (g_hotkey) {
        static int was_down;
        int down = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
        if (down && !was_down && !g_capturing && !g_target)
            g_target = presented + 2;
        was_down = down;
    }
    if (g_capturing && presented >= g_target) {
        char path[MAX_PATH];
        uint32_t len = sizeof path;
        g_rdoc->EndFrameCapture(NULL, NULL);
        g_capturing = 0;
        if (g_rdoc->GetCapture(g_rdoc->GetNumCaptures() - 1, path, &len, NULL))
            ngb_log("rdoc: frame %u captured in %s", g_target, path);
        g_target = 0;
    } else if (!g_capturing && g_target && presented + 1 == g_target) {
        g_rdoc->StartFrameCapture(NULL, NULL);
        g_capturing = 1;
    }
}
