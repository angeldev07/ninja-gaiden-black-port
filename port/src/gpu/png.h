/* png.h: escritor PNG mínimo (RGB de 8 bits por canal). */
#pragma once

#include <stdint.h>

/* `rgb`: width*height*3 bytes, filas de arriba abajo. Devuelve 0 si va bien. */
int png_write_rgb(const char *path, uint32_t width, uint32_t height, const uint8_t *rgb);
