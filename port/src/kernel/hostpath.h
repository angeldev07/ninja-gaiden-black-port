/*
 * hostpath.h: traducción segura de una ruta Xbox (lo que queda tras el prefijo del
 * dispositivo) a una ruta del PC dentro de una carpeta raíz.
 *
 * El juego controla las rutas que pide (NtCreateFile, enlaces simbólicos), así que el
 * resultado nunca puede salir de `root`: se rechazan los componentes "..", los que llevan
 * ':' (unidades, flujos alternativos de NTFS) o caracteres no válidos, y al final la ruta
 * completa (GetFullPathNameA) tiene que empezar por `root\`. Lo usa impl_io.c y lo prueba
 * port/tests/hostpath_test.c.
 */
#pragma once

#include <stddef.h>

/*
 * `rel` es "" o empieza por '\' (por ejemplo "\media\stage01.afs"). Escribe en `out` la
 * ruta completa `root` + `rel` normalizada. Devuelve 0 si es segura y cabe; -1 si no
 * (y `out` queda vacío).
 */
int hostpath_join(const char *root, const char *rel, char *out, size_t outlen);

/* 1 si `rel` (formato de arriba) solo tiene componentes válidos; no toca el disco. */
int hostpath_rel_ok(const char *rel);
