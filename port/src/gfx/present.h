/*
 * present.h: presentador de frames del port (H7). Ventana Win32 + GDI en su propio hilo.
 *
 * El juego (o el HLE de D3D) entrega cada frame con gfx_present(). La copia va a un back
 * buffer BGRA protegido con una sección crítica y la ventana la pinta con StretchDIBits:
 * escalado entero en vertical y aspecto 4:3 o 16:9, con bandas negras. Nada bloquea al
 * llamante más que la conversión del frame.
 *
 *   gfx_init("ngb", 720, 480, 2);                      // ventana de 1280x960 (4:3)
 *   gfx_present(fb, 720, 480, 3072, GFX_FMT_A8R8G8B8);
 *   if (gfx_should_close()) ...                        // el usuario cerró la ventana
 *
 * Teclas: F12 guarda el frame actual en %NGB_DATA%\runs\shots\shot-<fecha>.png (PNG de C
 * sin compresión; RGB, el alfa se ignora). El título muestra los FPS de gfx_present.
 *
 * Formatos (los nombres son los D3DFORMAT de Xbox; píxeles little endian):
 *   GFX_FMT_A8R8G8B8, GFX_FMT_X8R8G8B8: u32 0xAARRGGBB (en memoria: B, G, R, A)
 *   GFX_FMT_R5G6B5: u16 RRRRRGGGGGGBBBBB
 * Con GFX_SWIZZLED (OR con el formato), los píxeles van en orden Morton de la NV2A (como
 * las superficies de color swizzled): se intercala un bit de x y otro de y empezando por el
 * menos significativo (xemu, generate_swizzle_masks). Las dimensiones deben ser potencias
 * de dos y `pitch` se ignora.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum gfx_format {
    GFX_FMT_A8R8G8B8 = 1,
    GFX_FMT_X8R8G8B8 = 2,
    GFX_FMT_R5G6B5 = 3,
};
#define GFX_FMT_MASK 0xFFu
#define GFX_SWIZZLED 0x100u /* flag: superficie en orden Morton (NV2A) */

enum gfx_aspect {
    GFX_ASPECT_4_3 = 0,
    GFX_ASPECT_16_9 = 1,
};

#define GFX_MAX_DIM 4096

typedef struct gfx_config {
    const char *title;  /* UTF-8 */
    int width, height;  /* tamaño lógico del frame (720x480 en NTSC) */
    int scale;          /* escala entera inicial de la ventana (1..8) */
    int aspect;         /* GFX_ASPECT_4_3 o GFX_ASPECT_16_9 */
    int hidden;         /* 1: la ventana existe pero no se muestra (tests sin pantalla) */
} gfx_config_t;

/* Crea la ventana en su propio hilo. 0 si todo fue bien. Solo una ventana por proceso. */
int gfx_init(const char *title, int width, int height, int scale);
int gfx_init_ex(const gfx_config_t *config);

/* Copia y convierte un frame. `format` = GFX_FMT_* [| GFX_SWIZZLED]. 0 si se aceptó; -1 si
 * el formato o las dimensiones no son válidos o la ventana no existe. */
int gfx_present(const void *pixels, int width, int height, int pitch, unsigned format);

/* Atiende peticiones pendientes (captura). Devuelve lo mismo que gfx_should_close(). */
int gfx_poll(void);
/* 1 cuando el usuario cerró la ventana: el loader debe parar limpio. */
int gfx_should_close(void);

/* Guarda el último frame como PNG. `path` NULL = %NGB_DATA%\runs\shots\shot-<fecha>.png.
 * La ruta usada va a `out_path` si no es NULL. 0 si se escribió. */
int gfx_screenshot(const char *path, char *out_path, size_t out_len);

/* Copia el último frame (BGRA, 4 bytes por píxel, fila a fila) a `dst` si caben
 * `dst_pixels` píxeles; devuelve sus dimensiones. 0 si había frame. Para los tests. */
int gfx_copy_frame(void *dst, size_t dst_pixels, int *width, int *height);

/* Render externo (OpenGL, H7c): otro hilo dibuja en la ventana. Con `on` = 1, WM_PAINT ya
 * no pinta con GDI y gfx_note_frame() cuenta los frames para los FPS del título. */
void *gfx_hwnd(void); /* HWND de la ventana, o NULL */
void gfx_set_external(int on);
void gfx_note_frame(void);

/* FPS medidos en el último segundo. */
double gfx_fps(void);

/* Cierra la ventana y espera a su hilo. */
void gfx_shutdown(void);

/* Utilidades públicas (las usan los tests). */
size_t gfx_swizzle_offset(unsigned x, unsigned y, unsigned width, unsigned height);
int gfx_write_png(const char *path, const void *bgra, int width, int height);

#ifdef __cplusplus
}
#endif
