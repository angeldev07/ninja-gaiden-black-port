/*
 * rdoc.c: captura de un frame con RenderDoc (--rdoc-frame N), para depurar el render GL.
 *
 * RenderDoc (https://renderdoc.org, MIT) se engancha a OpenGL si su DLL está cargada antes de
 * crear el contexto. Con su API "in-application" se captura exactamente el frame pedido; la
 * captura (.rdc) queda junto al log, en NGB_DATA, y se analiza con qrenderdoc o con su API
 * de Python (draws, estado, texturas, pixel history). Es opcional: sin la DLL no pasa nada.
 * El loader es de 32 bits, así que se usa la DLL de x86 de la instalación.
 * Con --rdoc-key, cada pulsación de F11 captura el frame siguiente (para jugar a mano y
 * capturar justo cuando aparece un fallo).
 */
#include "rdoc.h"

#include <stdbool.h> /* renderdoc_app.h usa bool */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma warning(push)
#pragma warning(disable : 4201) /* la cabecera de RenderDoc usa uniones anónimas */
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
        ngb_log("rdoc: no se pudo cargar %s (¿RenderDoc instalado?)", dll);
        return -1;
    }
    get_api = (pRENDERDOC_GetAPI)(void (*)(void))GetProcAddress(h, "RENDERDOC_GetAPI");
    if (!get_api || !get_api(eRENDERDOC_API_Version_1_6_0, (void **)&g_rdoc) || !g_rdoc) {
        ngb_log("rdoc: %s no ofrece la API 1.6.0", dll);
        g_rdoc = NULL;
        return -1;
    }
    /* Capturas junto al log: <log sin extensión>-rdoc_frameN.rdc (lo numera RenderDoc). */
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
        ngb_log("rdoc: RenderDoc cargado; se capturará el frame %u en %s_*.rdc", frame, tmpl);
    if (hotkey)
        ngb_log("rdoc: RenderDoc cargado; F11 captura el frame siguiente en %s_*.rdc", tmpl);
    return 0;
}

void rdoc_after_present(uint32_t presented) {
    if (!g_rdoc)
        return;
    /* F11 (al pulsarla): captura el frame siguiente. */
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
            ngb_log("rdoc: frame %u capturado en %s", g_target, path);
        g_target = 0;
    } else if (!g_capturing && g_target && presented + 1 == g_target) {
        g_rdoc->StartFrameCapture(NULL, NULL);
        g_capturing = 1;
    }
}
