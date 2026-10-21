/*
 * glpng.h: escritor PNG mínimo (RGBA8, sin dependencias) para guardar capturas del FBO.
 *
 * Usa bloques deflate "stored" (sin comprimir): el archivo es más grande que uno
 * comprimido, pero cualquier visor lo abre y no hace falta zlib.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Escribe `rgba` (width * height * 4, filas de arriba abajo) en `path`. 0 = bien, -1 = error. */
int glb_write_png(const char *path, int width, int height, const uint8_t *rgba);
