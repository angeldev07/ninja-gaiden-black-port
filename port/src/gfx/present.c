/*
 * present.c: presentador de frames (H7). Ver present.h para la API.
 *
 * Hilos:
 *   - El del llamante (el juego o el HLE de D3D) convierte el frame a BGRA en el back
 *     buffer, con la sección crítica, e invalida la ventana. No espera a la pantalla.
 *   - El de la ventana: message loop, WM_PAINT (StretchDIBits del back buffer), F12
 *     (captura PNG) y el título con los FPS.
 *
 * Escalado: el frame se pinta con un alto entero (1x, 2x...) y el ancho que pida el
 * aspecto (4:3 o 16:9), centrado y con bandas negras. Los 720x480 de la Xbox no son
 * píxeles cuadrados: con 4:3 se ven a 640x480 por cada 1x de alto. Si la ventana es más
 * pequeña que 1x, se encoge para que quepa.
 *
 * PNG: firma, IHDR (RGB de 8 bits), IDAT con zlib en bloques "stored" (sin compresión,
 * con Adler-32) e IEND, cada chunk con su CRC-32. Se escribe en `<ruta>.part` y se
 * renombra, así que una captura a medias nunca tiene el nombre final.
 */
#define WIN32_LEAN_AND_MEAN
#include "present.h"

#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WM_GFX_TITLE (WM_APP + 1)
#define WM_GFX_QUIT (WM_APP + 2)
#define GFX_CLASS L"ngb-gfx-present"
#define GFX_INIT_TIMEOUT_MS 5000
#define GFX_JOIN_TIMEOUT_MS 3000
#define ZLIB_STORED_MAX 65535u

static struct {
    CRITICAL_SECTION lock;
    int lock_ready;
    HANDLE thread;
    HANDLE ready;
    HWND hwnd;
    gfx_config_t cfg;
    wchar_t title[128];
    uint32_t *frame; /* BGRA, frame_w x frame_h */
    int frame_w, frame_h;
    size_t frame_cap; /* píxeles reservados */
    volatile LONG should_close;
    volatile LONG external; /* 1: otro hilo dibuja con OpenGL (H7c); GDI no pinta */
    int init_ok;
    DWORD fps_tick;
    unsigned fps_frames;
    double fps;
} g;

/* ---------------------------------------------------------------- swizzle (NV2A) ---- */

static int is_pow2(unsigned v) { return v && !(v & (v - 1)); }

static void swizzle_masks(unsigned width, unsigned height, uint32_t *mask_x, uint32_t *mask_y) {
    uint32_t mx = 0, my = 0, mask_bit = 1;
    unsigned bit = 1;
    while (bit < width || bit < height) {
        if (bit < width) {
            mx |= mask_bit;
            mask_bit <<= 1;
        }
        if (bit < height) {
            my |= mask_bit;
            mask_bit <<= 1;
        }
        bit <<= 1;
    }
    *mask_x = mx;
    *mask_y = my;
}

/* Los valores 0..count-1 con sus bits depositados en `mask` (truco de xemu). */
static void spread(uint32_t *out, unsigned count, uint32_t mask) {
    unsigned i;
    out[0] = 0;
    for (i = 1; i < count; i++)
        out[i] = (out[i - 1] - mask) & mask;
}

size_t gfx_swizzle_offset(unsigned x, unsigned y, unsigned width, unsigned height) {
    uint32_t mx, my, ox = 0, oy = 0, bit;
    swizzle_masks(width, height, &mx, &my);
    for (bit = 1; mx; bit <<= 1, mx &= mx - 1)
        if (x & bit)
            ox |= mx & (~mx + 1);
    for (bit = 1; my; bit <<= 1, my &= my - 1)
        if (y & bit)
            oy |= my & (~my + 1);
    return (size_t)(ox | oy);
}

/* ---------------------------------------------------------------- conversión ---- */

static uint32_t rgb565_to_bgra(uint16_t v) {
    uint32_t r = (v >> 11) & 31u, gg = (v >> 5) & 63u, b = v & 31u;
    r = (r << 3) | (r >> 2);
    gg = (gg << 2) | (gg >> 4);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (gg << 8) | b;
}

static int format_bpp(unsigned format) {
    switch (format & GFX_FMT_MASK) {
    case GFX_FMT_A8R8G8B8:
    case GFX_FMT_X8R8G8B8:
        return 4;
    case GFX_FMT_R5G6B5:
        return 2;
    default:
        return 0;
    }
}

/* Convierte a `dst` (BGRA, w*h). Devuelve -1 si faltan recursos. */
static int convert(uint32_t *dst, const uint8_t *src, int w, int h, int pitch, unsigned format) {
    int x, y, bpp = format_bpp(format);
    unsigned kind = format & GFX_FMT_MASK;
    uint32_t alpha = kind == GFX_FMT_X8R8G8B8 ? 0xFF000000u : 0;
    uint32_t *xs = NULL, *ys = NULL;
    if (format & GFX_SWIZZLED) {
        uint32_t mx, my;
        xs = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)w);
        ys = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)h);
        if (!xs || !ys) {
            free(xs);
            free(ys);
            return -1;
        }
        swizzle_masks((unsigned)w, (unsigned)h, &mx, &my);
        spread(xs, (unsigned)w, mx);
        spread(ys, (unsigned)h, my);
    }
    for (y = 0; y < h; y++) {
        uint32_t *out = dst + (size_t)y * (size_t)w;
        for (x = 0; x < w; x++) {
            size_t index = xs ? (size_t)(xs[x] | ys[y]) * (size_t)bpp
                              : (size_t)y * (size_t)pitch + (size_t)x * (size_t)bpp;
            const uint8_t *p = src + index;
            if (bpp == 4) {
                uint32_t v;
                memcpy(&v, p, 4);
                out[x] = v | alpha;
            } else {
                uint16_t v;
                memcpy(&v, p, 2);
                out[x] = rgb565_to_bgra(v);
            }
        }
    }
    free(xs);
    free(ys);
    return 0;
}

/* ---------------------------------------------------------------- PNG mínimo ---- */

static uint32_t crc_table[256];

static void crc_init(void) {
    uint32_t n, k, c;
    if (crc_table[1])
        return;
    for (n = 0; n < 256; n++) {
        c = n;
        for (k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        crc = crc_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return crc;
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int write_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t head[8], tail[4];
    uint32_t crc;
    put_be32(head, len);
    memcpy(head + 4, type, 4);
    crc = crc_update(0xFFFFFFFFu, head + 4, 4);
    crc = crc_update(crc, data, len) ^ 0xFFFFFFFFu;
    put_be32(tail, crc);
    return fwrite(head, 1, 8, f) != 8 || (len && fwrite(data, 1, len, f) != len) ||
           fwrite(tail, 1, 4, f) != 4;
}

int gfx_write_png(const char *path, const void *bgra, int width, int height) {
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    const uint8_t *px = (const uint8_t *)bgra;
    size_t row = 1 + 3 * (size_t)width, raw_len = row * (size_t)height, blocks, i, pos;
    size_t z_len;
    uint8_t *raw, *z, ihdr[13];
    uint32_t a = 1, b = 0;
    char part[MAX_PATH + 8];
    FILE *f;
    int x, y, failed;
    if (width <= 0 || height <= 0 || width > GFX_MAX_DIM || height > GFX_MAX_DIM)
        return -1;
    blocks = (raw_len + ZLIB_STORED_MAX - 1) / ZLIB_STORED_MAX;
    z_len = 2 + blocks * 5 + raw_len + 4;
    raw = (uint8_t *)malloc(raw_len);
    z = (uint8_t *)malloc(z_len);
    if (!raw || !z) {
        free(raw);
        free(z);
        return -1;
    }
    for (y = 0; y < height; y++) {
        uint8_t *out = raw + (size_t)y * row;
        const uint8_t *in = px + (size_t)y * (size_t)width * 4;
        out[0] = 0; /* filtro None */
        for (x = 0; x < width; x++) {
            out[1 + 3 * x] = in[4 * x + 2]; /* R */
            out[2 + 3 * x] = in[4 * x + 1]; /* G */
            out[3 + 3 * x] = in[4 * x];     /* B */
        }
    }
    z[0] = 0x78; /* zlib: deflate, ventana de 32 KB */
    z[1] = 0x01; /* sin diccionario, nivel mínimo; (0x78 << 8 | 0x01) % 31 == 0 */
    pos = 2;
    for (i = 0; i < raw_len; i += ZLIB_STORED_MAX) {
        size_t n = raw_len - i < ZLIB_STORED_MAX ? raw_len - i : ZLIB_STORED_MAX;
        z[pos++] = (uint8_t)(i + n == raw_len); /* BFINAL; BTYPE = 00 (stored) */
        z[pos++] = (uint8_t)(n & 0xFF);
        z[pos++] = (uint8_t)(n >> 8);
        z[pos++] = (uint8_t)(~n & 0xFF);
        z[pos++] = (uint8_t)((~n >> 8) & 0xFF);
        memcpy(z + pos, raw + i, n);
        pos += n;
    }
    for (i = 0; i < raw_len; i++) { /* Adler-32 de los datos sin comprimir */
        a = (a + raw[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    put_be32(z + pos, (b << 16) | a);
    pos += 4;
    put_be32(ihdr, (uint32_t)width);
    put_be32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8;  /* bits por canal */
    ihdr[9] = 2;  /* RGB */
    ihdr[10] = 0; /* deflate */
    ihdr[11] = 0; /* filtro estándar */
    ihdr[12] = 0; /* sin entrelazado */
    crc_init();
    snprintf(part, sizeof part, "%s.part", path);
    f = fopen(part, "wb");
    failed = !f;
    if (f) {
        failed = fwrite(signature, 1, 8, f) != 8 || write_chunk(f, "IHDR", ihdr, 13) ||
                 write_chunk(f, "IDAT", z, (uint32_t)pos) || write_chunk(f, "IEND", NULL, 0);
        failed |= fclose(f) != 0;
    }
    free(raw);
    free(z);
    if (!failed && !MoveFileExA(part, path, MOVEFILE_REPLACE_EXISTING))
        failed = 1;
    if (failed)
        DeleteFileA(part);
    return failed ? -1 : 0;
}

/* ---------------------------------------------------------------- capturas ---- */

static int default_shot_path(char *out, size_t len) {
    char dir[MAX_PATH];
    const char *data = getenv("NGB_DATA"), *home = getenv("USERPROFILE");
    SYSTEMTIME st;
    if (data && *data)
        snprintf(dir, sizeof dir, "%s\\runs", data);
    else if (home && *home)
        snprintf(dir, sizeof dir, "%s\\ngb-data\\runs", home);
    else
        return -1;
    CreateDirectoryA(dir, NULL);
    strncat(dir, "\\shots", sizeof dir - strlen(dir) - 1);
    CreateDirectoryA(dir, NULL);
    GetLocalTime(&st);
    snprintf(out, len, "%s\\shot-%04u%02u%02u-%02u%02u%02u-%03u.png", dir, st.wYear, st.wMonth,
             st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return 0;
}

int gfx_copy_frame(void *dst, size_t dst_pixels, int *width, int *height) {
    int ok = -1;
    if (!g.lock_ready)
        return -1;
    EnterCriticalSection(&g.lock);
    if (g.frame && (size_t)g.frame_w * (size_t)g.frame_h <= dst_pixels) {
        memcpy(dst, g.frame, (size_t)g.frame_w * (size_t)g.frame_h * 4);
        *width = g.frame_w;
        *height = g.frame_h;
        ok = 0;
    }
    LeaveCriticalSection(&g.lock);
    return ok;
}

int gfx_screenshot(const char *path, char *out_path, size_t out_len) {
    char chosen[MAX_PATH];
    uint32_t *copy;
    int w = 0, h = 0, result;
    size_t pixels;
    if (!g.lock_ready)
        return -1;
    if (path)
        snprintf(chosen, sizeof chosen, "%s", path);
    else if (default_shot_path(chosen, sizeof chosen))
        return -1;
    EnterCriticalSection(&g.lock);
    pixels = (size_t)g.frame_w * (size_t)g.frame_h;
    LeaveCriticalSection(&g.lock);
    copy = (uint32_t *)malloc(pixels * 4 + 4);
    if (!copy)
        return -1;
    result = gfx_copy_frame(copy, pixels, &w, &h) || gfx_write_png(chosen, copy, w, h);
    free(copy);
    if (!result && out_path && out_len)
        snprintf(out_path, out_len, "%s", chosen);
    return result ? -1 : 0;
}

/* ---------------------------------------------------------------- ventana ---- */

static double aspect_ratio(void) { return g.cfg.aspect == GFX_ASPECT_16_9 ? 16.0 / 9.0 : 4.0 / 3.0; }

/* Rectángulo de destino: alto entero (k x alto del frame) y ancho según el aspecto. */
static RECT display_rect(int cw, int ch, int fh) {
    RECT r;
    double a = aspect_ratio();
    int scale = fh > 0 ? ch / fh : 1, dw, dh;
    if (scale < 1)
        scale = 1;
    while (scale > 1 && (int)(fh * scale * a + 0.5) > cw)
        scale--;
    dh = fh * scale;
    dw = (int)(dh * a + 0.5);
    if (dw > cw || dh > ch) { /* ventana menor que 1x: encoger para que quepa */
        dh = ch;
        dw = (int)(dh * a + 0.5);
        if (dw > cw) {
            dw = cw;
            dh = (int)(dw / a + 0.5);
        }
    }
    r.left = (cw - dw) / 2;
    r.top = (ch - dh) / 2;
    r.right = r.left + dw;
    r.bottom = r.top + dh;
    return r;
}

static void paint(HWND hwnd) {
    PAINTSTRUCT ps;
    RECT client, dst, bar;
    HDC hdc = BeginPaint(hwnd, &ps);
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    GetClientRect(hwnd, &client);
    EnterCriticalSection(&g.lock);
    if (g.frame && g.frame_w > 0 && g.frame_h > 0) {
        BITMAPINFO bmi;
        dst = display_rect(client.right, client.bottom, g.frame_h);
        memset(&bmi, 0, sizeof bmi);
        bmi.bmiHeader.biSize = sizeof bmi.bmiHeader;
        bmi.bmiHeader.biWidth = g.frame_w;
        bmi.bmiHeader.biHeight = -g.frame_h; /* de arriba abajo */
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(hdc, COLORONCOLOR);
        StretchDIBits(hdc, dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top, 0, 0,
                      g.frame_w, g.frame_h, g.frame, &bmi, DIB_RGB_COLORS, SRCCOPY);
        SetRect(&bar, 0, 0, client.right, dst.top);
        FillRect(hdc, &bar, black);
        SetRect(&bar, 0, dst.bottom, client.right, client.bottom);
        FillRect(hdc, &bar, black);
        SetRect(&bar, 0, dst.top, dst.left, dst.bottom);
        FillRect(hdc, &bar, black);
        SetRect(&bar, dst.right, dst.top, client.right, dst.bottom);
        FillRect(hdc, &bar, black);
    } else {
        FillRect(hdc, &client, black);
    }
    LeaveCriticalSection(&g.lock);
    EndPaint(hwnd, &ps);
}

static void update_title(HWND hwnd) {
    wchar_t text[192];
    double fps;
    EnterCriticalSection(&g.lock);
    fps = g.fps;
    LeaveCriticalSection(&g.lock);
    swprintf(text, sizeof text / sizeof text[0], L"%ls  ·  %.1f fps  ·  F12: captura", g.title, fps);
    SetWindowTextW(hwnd, text);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    char shot[MAX_PATH];
    switch (msg) {
    case WM_PAINT:
        if (g.external) {
            ValidateRect(hwnd, NULL); /* el render GL repinta en el siguiente frame */
            return 0;
        }
        paint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1; /* paint() rellena las bandas: sin parpadeo */
    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_F12) {
            if (!gfx_screenshot(NULL, shot, sizeof shot))
                fprintf(stderr, "gfx: captura en %s\n", shot);
            else
                fprintf(stderr, "gfx: no se pudo guardar la captura\n");
            return 0;
        }
        break;
    case WM_GFX_TITLE:
        update_title(hwnd);
        return 0;
    case WM_GFX_QUIT:
        DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE:
        InterlockedExchange(&g.should_close, 1);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* Icono de la ventana: el recurso 1 de ngb-core.dll (res\ngb.rc), en el tamaño que pide el
 * sistema (grande para la barra de tareas y Alt+Tab, pequeño para el título). La DLL, y no
 * el exe: cuando el XBE ocupa 0x10000 el exe pierde su cabecera PE. Sin recurso (el
 * presentador enlazado a mano en los tests) devuelve NULL y la ventana usa el icono por
 * defecto. */
static HICON load_app_icon(int big) {
    HMODULE mod = NULL;
    int cx = GetSystemMetrics(big ? SM_CXICON : SM_CXSMICON);
    int cy = GetSystemMetrics(big ? SM_CYICON : SM_CYSMICON);
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)&g, &mod) ||
        !mod)
        return NULL;
    return (HICON)LoadImageW(mod, MAKEINTRESOURCEW(1), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
}

/* Esquina superior izquierda para que una ventana de `w` x `h` (borde incluido) quede
 * centrada en el área de trabajo del monitor donde está el cursor (el del usuario que acaba
 * de lanzar el juego). Si no se puede saber, deja que Windows elija (CW_USEDEFAULT). */
static void centered_origin(int w, int h, int *x, int *y) {
    POINT cursor;
    MONITORINFO mi;
    HMONITOR mon;
    *x = CW_USEDEFAULT;
    *y = CW_USEDEFAULT;
    if (!GetCursorPos(&cursor))
        return;
    mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    if (!mon || !GetMonitorInfoW(mon, &mi))
        return;
    *x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2;
    *y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2;
    if (*x < mi.rcWork.left) /* ventana mayor que el monitor: que se vea al menos la esquina */
        *x = mi.rcWork.left;
    if (*y < mi.rcWork.top)
        *y = mi.rcWork.top;
}

static DWORD WINAPI window_thread(LPVOID param) {
    WNDCLASSEXW wc;
    RECT r;
    MSG msg;
    int ox, oy;
    HINSTANCE inst = GetModuleHandleW(NULL);
    int ch = g.cfg.height * g.cfg.scale, cw = (int)(ch * aspect_ratio() + 0.5);
    (void)param;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC; /* OWNDC: un DC estable para OpenGL */
    wc.lpfnWndProc = window_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = load_app_icon(1);
    wc.hIconSm = load_app_icon(0);
    wc.lpszClassName = GFX_CLASS;
    RegisterClassExW(&wc); /* si ya existía (segunda ventana tras un shutdown), sirve igual */
    SetRect(&r, 0, 0, cw, ch);
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    centered_origin(r.right - r.left, r.bottom - r.top, &ox, &oy);
    g.hwnd = CreateWindowExW(0, GFX_CLASS, g.title, WS_OVERLAPPEDWINDOW, ox, oy,
                             r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    g.init_ok = g.hwnd != NULL;
    if (g.hwnd)
        ShowWindow(g.hwnd, g.cfg.hidden ? SW_HIDE : SW_SHOWNORMAL);
    SetEvent(g.ready);
    if (!g.hwnd)
        return 1;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g.hwnd = NULL;
    return 0;
}

/* ---------------------------------------------------------------- API ---- */

int gfx_init(const char *title, int width, int height, int scale) {
    gfx_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.title = title;
    cfg.width = width;
    cfg.height = height;
    cfg.scale = scale;
    cfg.aspect = GFX_ASPECT_4_3;
    return gfx_init_ex(&cfg);
}

int gfx_init_ex(const gfx_config_t *config) {
    size_t pixels;
    if (g.thread || !config || config->width <= 0 || config->height <= 0 ||
        config->width > GFX_MAX_DIM || config->height > GFX_MAX_DIM)
        return -1;
    g.cfg = *config;
    if (g.cfg.scale < 1)
        g.cfg.scale = 1;
    if (g.cfg.scale > 8)
        g.cfg.scale = 8;
    if (!MultiByteToWideChar(CP_UTF8, 0, config->title ? config->title : "ngb", -1, g.title,
                             (int)(sizeof g.title / sizeof g.title[0])))
        wcscpy(g.title, L"ngb");
    if (!g.lock_ready) {
        InitializeCriticalSection(&g.lock);
        g.lock_ready = 1;
    }
    pixels = (size_t)config->width * (size_t)config->height;
    g.frame = (uint32_t *)calloc(pixels, 4); /* negro hasta el primer gfx_present */
    if (!g.frame)
        return -1;
    g.frame_cap = pixels;
    g.frame_w = config->width;
    g.frame_h = config->height;
    g.should_close = 0;
    g.fps_tick = GetTickCount();
    g.fps_frames = 0;
    g.fps = 0;
    g.ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g.ready)
        return -1;
    g.thread = CreateThread(NULL, 0, window_thread, NULL, 0, NULL);
    if (!g.thread || WaitForSingleObject(g.ready, GFX_INIT_TIMEOUT_MS) != WAIT_OBJECT_0 ||
        !g.init_ok) {
        gfx_shutdown();
        return -1;
    }
    return 0;
}

int gfx_present(const void *pixels, int width, int height, int pitch, unsigned format) {
    int bpp = format_bpp(format), result;
    size_t needed;
    DWORD now;
    HWND hwnd;
    if (!g.thread || !pixels || !bpp || width <= 0 || height <= 0 || width > GFX_MAX_DIM ||
        height > GFX_MAX_DIM)
        return -1;
    if (format & GFX_SWIZZLED) {
        if (!is_pow2((unsigned)width) || !is_pow2((unsigned)height))
            return -1;
    } else if (pitch < width * bpp) {
        return -1;
    }
    needed = (size_t)width * (size_t)height;
    EnterCriticalSection(&g.lock);
    if (needed > g.frame_cap) {
        uint32_t *bigger = (uint32_t *)realloc(g.frame, needed * 4);
        if (!bigger) {
            LeaveCriticalSection(&g.lock);
            return -1;
        }
        g.frame = bigger;
        g.frame_cap = needed;
    }
    result = convert(g.frame, (const uint8_t *)pixels, width, height, pitch, format);
    if (!result) {
        g.frame_w = width;
        g.frame_h = height;
    }
    g.fps_frames++;
    now = GetTickCount();
    if (now - g.fps_tick >= 1000) {
        g.fps = g.fps_frames * 1000.0 / (double)(now - g.fps_tick);
        g.fps_frames = 0;
        g.fps_tick = now;
        hwnd = g.hwnd;
        if (hwnd)
            PostMessageW(hwnd, WM_GFX_TITLE, 0, 0);
    }
    hwnd = g.hwnd;
    LeaveCriticalSection(&g.lock);
    if (hwnd)
        InvalidateRect(hwnd, NULL, FALSE);
    return result;
}

int gfx_should_close(void) { return g.should_close != 0; }

void *gfx_hwnd(void) { return g.hwnd; }

void gfx_set_external(int on) { InterlockedExchange(&g.external, on ? 1 : 0); }

void gfx_note_frame(void) {
    DWORD now;
    HWND hwnd = NULL;
    if (!g.lock_ready)
        return;
    EnterCriticalSection(&g.lock);
    g.fps_frames++;
    now = GetTickCount();
    if (now - g.fps_tick >= 1000) {
        g.fps = g.fps_frames * 1000.0 / (double)(now - g.fps_tick);
        g.fps_frames = 0;
        g.fps_tick = now;
        hwnd = g.hwnd;
    }
    LeaveCriticalSection(&g.lock);
    if (hwnd)
        PostMessageW(hwnd, WM_GFX_TITLE, 0, 0);
}

int gfx_poll(void) { return gfx_should_close(); }

double gfx_fps(void) {
    double fps = 0;
    if (!g.lock_ready)
        return 0;
    EnterCriticalSection(&g.lock);
    fps = g.fps;
    LeaveCriticalSection(&g.lock);
    return fps;
}

void gfx_shutdown(void) {
    if (g.thread) {
        HWND hwnd = g.hwnd;
        if (hwnd)
            PostMessageW(hwnd, WM_GFX_QUIT, 0, 0);
        WaitForSingleObject(g.thread, GFX_JOIN_TIMEOUT_MS);
        CloseHandle(g.thread);
        g.thread = NULL;
    }
    if (g.ready) {
        CloseHandle(g.ready);
        g.ready = NULL;
    }
    if (g.lock_ready) {
        EnterCriticalSection(&g.lock);
        free(g.frame);
        g.frame = NULL;
        g.frame_cap = 0;
        g.frame_w = g.frame_h = 0;
        LeaveCriticalSection(&g.lock);
    }
    g.hwnd = NULL;
    g.init_ok = 0;
}
