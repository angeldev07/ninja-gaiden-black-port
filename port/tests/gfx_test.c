/*
 * gfx_test.c: prueba del presentador de frames (port/src/gfx/present.c). Sin datos del juego.
 *
 *   gfx_test.exe [--frames N] [--hidden] [--shot DIR] [--delay MS]
 *
 * 1. Comprobaciones exactas (código de salida 1 si fallan): copia del back buffer tras
 *    presentar A8R8G8B8 lineal con pitch de relleno (3072, como el framebuffer del juego),
 *    X8R8G8B8 (alfa forzado a 0xFF), R5G6B5 (expansión de 5/6 bits) y A8R8G8B8 swizzled
 *    (con un orden Morton propio de esta prueba, independiente del de present.c).
 * 2. Presenta N frames (por defecto 120): la primera mitad, un gradiente animado de
 *    720x480; la segunda, un patrón conocido de 256x256 entregado swizzled.
 * 3. Guarda dos capturas en DIR (por defecto %NGB_DATA%\runs\shots):
 *    gfx_test-gradient.png (último frame del gradiente) y gfx_test-swizzled.png (patrón
 *    final: cuadrantes rojo, verde, azul y amarillo con rejilla negra cada 32 px y la
 *    diagonal blanca; si el swizzle fallara, saldría revuelto).
 * Luego cierra la ventana sola. Código 0 si todo fue bien.
 */
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx/present.h"

#define FB_W 720
#define FB_H 480
#define FB_PITCH 3072 /* como la superficie de color del juego (768 px de pitch) */
#define SW_DIM 256

static int failures;

static void check(int ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "gfx_test: FALLO %s\n", what);
        failures++;
    }
}

/* Morton para potencias de dos cuadradas: bit i de x -> bit 2i, bit i de y -> bit 2i+1. */
static size_t morton(unsigned x, unsigned y) {
    size_t out = 0;
    unsigned i;
    for (i = 0; i < 16; i++) {
        out |= (size_t)((x >> i) & 1u) << (2 * i);
        out |= (size_t)((y >> i) & 1u) << (2 * i + 1);
    }
    return out;
}

static uint32_t pattern_pixel(unsigned x, unsigned y) {
    uint32_t color;
    if (x == y)
        return 0xFFFFFFFFu; /* diagonal blanca */
    if (x % 32 == 0 || y % 32 == 0)
        return 0xFF000000u; /* rejilla negra */
    if (y < SW_DIM / 2)
        color = x < SW_DIM / 2 ? 0xFFE03030u : 0xFF30C030u; /* rojo | verde */
    else
        color = x < SW_DIM / 2 ? 0xFF3050E0u : 0xFFE0D030u; /* azul | amarillo */
    return color;
}

static void gradient(uint8_t *fb, int frame) {
    int x, y;
    int bar = (frame * 6) % FB_W;
    for (y = 0; y < FB_H; y++) {
        uint32_t *row = (uint32_t *)(fb + (size_t)y * FB_PITCH);
        for (x = 0; x < FB_W; x++) {
            uint32_t r = (uint32_t)(x * 255 / (FB_W - 1)), g = (uint32_t)(y * 255 / (FB_H - 1));
            uint32_t b = (uint32_t)((frame * 4) & 255);
            row[x] = (x >= bar && x < bar + 12) ? 0xFFFFFFFFu : 0xFF000000u | r << 16 | g << 8 | b;
        }
        memset(row + FB_W, 0xCD, FB_PITCH - FB_W * 4); /* relleno de pitch: no debe verse */
    }
}

static void exact_checks(void) {
    static uint32_t back[SW_DIM * SW_DIM];
    uint8_t *fb = (uint8_t *)malloc((size_t)FB_PITCH * FB_H);
    uint32_t *sw = (uint32_t *)malloc(sizeof(uint32_t) * SW_DIM * SW_DIM);
    uint32_t x8[4] = {0x00123456u, 0x80FF0000u, 0x0000FF00u, 0x7F0000FFu};
    uint16_t rgb565[4] = {0xF800u, 0x07E0u, 0x001Fu, 0xFFFFu};
    uint32_t want565[4] = {0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0xFFFFFFFFu};
    static uint32_t big[FB_W * FB_H];
    int w = 0, h = 0, i, ok;
    unsigned x, y;
    if (!fb || !sw) {
        check(0, "memoria");
        free(fb);
        free(sw);
        return;
    }
    gradient(fb, 7);
    check(!gfx_present(fb, FB_W, FB_H, FB_PITCH, GFX_FMT_A8R8G8B8), "present A8R8G8B8");
    check(!gfx_copy_frame(big, FB_W * FB_H, &w, &h) && w == FB_W && h == FB_H, "copia 720x480");
    check(big[0] == ((const uint32_t *)fb)[0], "píxel (0,0)");
    check(big[479 * FB_W + 719] == ((const uint32_t *)(fb + 479 * FB_PITCH))[719],
          "píxel (719,479) con pitch 3072");

    check(!gfx_present(x8, 2, 2, 8, GFX_FMT_X8R8G8B8), "present X8R8G8B8");
    check(!gfx_copy_frame(back, SW_DIM * SW_DIM, &w, &h) && w == 2 && h == 2, "copia 2x2");
    for (i = 0; i < 4; i++)
        check(back[i] == (x8[i] | 0xFF000000u), "X8R8G8B8: alfa forzado a 0xFF");

    check(!gfx_present(rgb565, 2, 2, 4, GFX_FMT_R5G6B5), "present R5G6B5");
    check(!gfx_copy_frame(back, SW_DIM * SW_DIM, &w, &h), "copia R5G6B5");
    for (i = 0; i < 4; i++)
        check(back[i] == want565[i], "R5G6B5 expandido a 8 bits");

    for (y = 0; y < SW_DIM; y++)
        for (x = 0; x < SW_DIM; x++)
            sw[morton(x, y)] = pattern_pixel(x, y);
    check(!gfx_present(sw, SW_DIM, SW_DIM, 0, GFX_FMT_A8R8G8B8 | GFX_SWIZZLED), "present swizzled");
    check(!gfx_copy_frame(back, SW_DIM * SW_DIM, &w, &h) && w == SW_DIM, "copia swizzled");
    ok = 1;
    for (y = 0; y < SW_DIM && ok; y++)
        for (x = 0; x < SW_DIM && ok; x++)
            ok = back[y * SW_DIM + x] == pattern_pixel(x, y);
    check(ok, "des-swizzle: el patrón no coincide píxel a píxel");
    check(gfx_swizzle_offset(3, 5, 8, 8) == morton(3, 5), "gfx_swizzle_offset 8x8");
    check(gfx_swizzle_offset(7, 1, 8, 4) == 0x17, "gfx_swizzle_offset 8x4 (x2 y1 x1 y0 x0)");

    check(gfx_present(sw, 200, 100, 0, GFX_FMT_A8R8G8B8 | GFX_SWIZZLED) != 0,
          "rechaza swizzled sin potencias de dos");
    check(gfx_present(fb, FB_W, FB_H, 100, GFX_FMT_A8R8G8B8) != 0, "rechaza pitch corto");
    check(gfx_present(fb, FB_W, FB_H, FB_PITCH, 0x7F) != 0, "rechaza formato desconocido");
    free(fb);
    free(sw);
}

static void shot_dir(char *out, size_t len, const char *given) {
    const char *data = getenv("NGB_DATA"), *home = getenv("USERPROFILE");
    char runs[MAX_PATH];
    if (given) {
        snprintf(out, len, "%s", given);
        CreateDirectoryA(out, NULL);
        return;
    }
    if (data && *data)
        snprintf(runs, sizeof runs, "%s\\runs", data);
    else
        snprintf(runs, sizeof runs, "%s\\ngb-data\\runs", home ? home : ".");
    CreateDirectoryA(runs, NULL);
    snprintf(out, len, "%s\\shots", runs);
    CreateDirectoryA(out, NULL);
}

static int save(const char *dir, const char *name) {
    char path[MAX_PATH], used[MAX_PATH];
    snprintf(path, sizeof path, "%s\\%s", dir, name);
    if (gfx_screenshot(path, used, sizeof used)) {
        check(0, "captura");
        return -1;
    }
    printf("gfx_test: captura %s\n", used);
    return 0;
}

int main(int argc, char **argv) {
    gfx_config_t cfg;
    int frames = 120, delay = 16, f, i, half;
    const char *shot = NULL;
    char dir[MAX_PATH];
    uint8_t *fb;
    uint32_t *sw;
    unsigned x, y;
    memset(&cfg, 0, sizeof cfg);
    cfg.title = "ngb gfx_test";
    cfg.width = FB_W;
    cfg.height = FB_H;
    cfg.scale = 1;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--delay") && i + 1 < argc)
            delay = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc)
            shot = argv[++i];
        else if (!strcmp(argv[i], "--hidden"))
            cfg.hidden = 1;
        else {
            fprintf(stderr, "uso: gfx_test.exe [--frames N] [--hidden] [--shot DIR] [--delay MS]\n");
            return 2;
        }
    }
    if (frames < 2)
        frames = 2;
    if (gfx_init_ex(&cfg)) {
        fprintf(stderr, "gfx_test: no se pudo abrir la ventana\n");
        return 1;
    }
    exact_checks();
    shot_dir(dir, sizeof dir, shot);
    fb = (uint8_t *)malloc((size_t)FB_PITCH * FB_H);
    sw = (uint32_t *)malloc(sizeof(uint32_t) * SW_DIM * SW_DIM);
    if (!fb || !sw) {
        gfx_shutdown();
        return 1;
    }
    for (y = 0; y < SW_DIM; y++)
        for (x = 0; x < SW_DIM; x++)
            sw[morton(x, y)] = pattern_pixel(x, y);
    half = frames / 2;
    for (f = 0; f < frames && !gfx_poll(); f++) {
        if (f < half) {
            gradient(fb, f);
            gfx_present(fb, FB_W, FB_H, FB_PITCH, GFX_FMT_A8R8G8B8);
            if (f == half - 1)
                save(dir, "gfx_test-gradient.png");
        } else {
            gfx_present(sw, SW_DIM, SW_DIM, 0, GFX_FMT_A8R8G8B8 | GFX_SWIZZLED);
        }
        if (delay > 0)
            Sleep((DWORD)delay);
    }
    save(dir, "gfx_test-swizzled.png");
    /* Cerrar la ventana como lo haría el usuario: gfx_should_close() debe avisar. */
    {
        HWND window = FindWindowW(L"ngb-gfx-present", NULL); /* clase de present.c */
        check(window != NULL, "ventana del presentador");
        if (window)
            SendMessageW(window, WM_CLOSE, 0, 0);
        check(gfx_should_close() && gfx_poll(), "cerrar la ventana activa gfx_should_close");
    }
    printf("gfx_test: %d frames, %.1f fps, %d fallos\n", f, gfx_fps(), failures);
    gfx_shutdown();
    free(fb);
    free(sw);
    return failures ? 1 : 0;
}
