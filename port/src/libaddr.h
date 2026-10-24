/*
 * libaddr.h: addresses of the XDK library code that the port patches or reads, in any build of
 * the game.
 *
 * The port's hooks name library functions and globals by their VA in the reference build (the PAL
 * default.xbe, where they were found with Ghidra, H5). The XDK libraries are linked into their own
 * sections (D3D, XPP, DSOUND...), and every release of the game carries the same library build:
 * the sections have the same virtual size and the same code, only at another base (the NTSC-U XBE
 * puts XPP 0x26C0 bytes lower, for example). So a reference VA is moved to the loaded XBE as
 * "the same offset in the section of the same name". The game's own code (.text, .rdata, .data)
 * differs between releases and is never located this way.
 */
#pragma once

#include <stdint.h>

#include "xbe.h"

/* VA in `xbe` of the reference VA `ref_va`, or 0 if it is not inside a library section of the
 * reference build or `xbe` has no section of that name and size. */
uint32_t libaddr_va(const xbe_t *xbe, uint32_t ref_va);

/* Name of the reference library section that holds `ref_va` (NULL if none). */
const char *libaddr_section(uint32_t ref_va);
