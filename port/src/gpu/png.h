/* png.h: minimal PNG writer (8-bit RGB per channel). */
#pragma once

#include <stdint.h>

/* `rgb`: width*height*3 bytes, rows top to bottom. Returns 0 on success. */
int png_write_rgb(const char *path, uint32_t width, uint32_t height, const uint8_t *rgb);
