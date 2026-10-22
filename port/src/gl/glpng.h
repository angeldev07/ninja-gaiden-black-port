/*
 * glpng.h: minimal PNG writer (RGBA8, no dependencies) to save FBO captures.
 *
 * Uses "stored" (uncompressed) deflate blocks: the file is larger than a compressed
 * one, but any viewer opens it and zlib is not needed.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Writes `rgba` (width * height * 4, rows top to bottom) to `path`. 0 = ok, -1 = error. */
int glb_write_png(const char *path, int width, int height, const uint8_t *rgba);
