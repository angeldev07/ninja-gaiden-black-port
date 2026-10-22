/*
 * present.h: port frame presenter (H7). Win32 window + GDI on its own thread.
 *
 * The game (or the D3D HLE) hands over each frame with gfx_present(). The copy goes to a BGRA
 * back buffer protected by a critical section and the window paints it with StretchDIBits:
 * integer vertical scaling and 4:3 or 16:9 aspect, with black bars. Nothing blocks the
 * caller except the frame conversion.
 *
 *   gfx_init("ngb", 720, 480, 2);                      // 1280x960 window (4:3)
 *   gfx_present(fb, 720, 480, 3072, GFX_FMT_A8R8G8B8);
 *   if (gfx_should_close()) ...                        // the user closed the window
 *
 * Keys: F12 saves the current frame to %NGB_DATA%\runs\shots\shot-<date>.png (C PNG
 * without compression; RGB, alpha is ignored). The title shows the FPS of gfx_present.
 *
 * Formats (the names are the Xbox D3DFORMATs; little-endian pixels):
 *   GFX_FMT_A8R8G8B8, GFX_FMT_X8R8G8B8: u32 0xAARRGGBB (in memory: B, G, R, A)
 *   GFX_FMT_R5G6B5: u16 RRRRRGGGGGGBBBBB
 * With GFX_SWIZZLED (ORed with the format), pixels are in NV2A Morton order (like
 * swizzled color surfaces): one bit of x and one of y are interleaved starting from the
 * least significant (xemu, generate_swizzle_masks). Dimensions must be powers
 * of two and `pitch` is ignored.
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
#define GFX_SWIZZLED 0x100u /* flag: surface in Morton order (NV2A) */

enum gfx_aspect {
    GFX_ASPECT_4_3 = 0,
    GFX_ASPECT_16_9 = 1,
};

#define GFX_MAX_DIM 4096

typedef struct gfx_config {
    const char *title;  /* UTF-8 */
    int width, height;  /* logical frame size (720x480 on NTSC) */
    int scale;          /* initial integer window scale (1..8) */
    int aspect;         /* GFX_ASPECT_4_3 or GFX_ASPECT_16_9 */
    int hidden;         /* 1: the window exists but is not shown (screenless tests) */
} gfx_config_t;

/* Creates the window on its own thread. 0 if all went well. Only one window per process. */
int gfx_init(const char *title, int width, int height, int scale);
int gfx_init_ex(const gfx_config_t *config);

/* Copies and converts a frame. `format` = GFX_FMT_* [| GFX_SWIZZLED]. 0 if accepted; -1 if
 * the format or the dimensions are not valid or the window does not exist. */
int gfx_present(const void *pixels, int width, int height, int pitch, unsigned format);

/* Serves pending requests (capture). Returns the same as gfx_should_close(). */
int gfx_poll(void);
/* 1 when the user closed the window: the loader must stop cleanly. */
int gfx_should_close(void);

/* Saves the last frame as PNG. `path` NULL = %NGB_DATA%\runs\shots\shot-<date>.png.
 * The path used goes to `out_path` if it is not NULL. 0 if it was written. */
int gfx_screenshot(const char *path, char *out_path, size_t out_len);

/* Copies the last frame (BGRA, 4 bytes per pixel, row by row) to `dst` if `dst_pixels`
 * pixels fit; returns its dimensions. 0 if there was a frame. For the tests. */
int gfx_copy_frame(void *dst, size_t dst_pixels, int *width, int *height);

/* External render (OpenGL, H7c): another thread draws in the window. With `on` = 1, WM_PAINT
 * no longer paints with GDI and gfx_note_frame() counts the frames for the title FPS. */
void *gfx_hwnd(void); /* HWND of the window, or NULL */
void gfx_set_external(int on);
void gfx_note_frame(void);

/* FPS measured over the last second. */
double gfx_fps(void);

/* Closes the window and waits for its thread. */
void gfx_shutdown(void);

/* Public utilities (used by the tests). */
size_t gfx_swizzle_offset(unsigned x, unsigned y, unsigned width, unsigned height);
int gfx_write_png(const char *path, const void *bgra, int width, int height);

#ifdef __cplusplus
}
#endif
