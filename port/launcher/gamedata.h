/*
 * gamedata.h: minimal, bounds-checked readers for the user's own copy of the game, used by the
 * launcher's game art mode. Nothing is written; any malformed input is "not found".
 *
 *   - AFS (CRI container, docs/formats/afs.md): find an entry by the name in its attribute table.
 *   - XPR0 (docs/formats/xpr0.md): walk the resource headers and return the Nth texture with the
 *     palette that follows it, if any.
 *   - .spr sprite tables: resource 0 of a .spr XPR0 is USERDATA holding 24-byte records
 *     {f32 u0, v0, u1, v1; u32 texture ordinal; u16 w, h} (verified on demo.spr, cos_menu.spr and
 *     chapst*.spr; see docs/log.md 2026-10-31).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define GAMEDATA_MAX_ENTRY (16u << 20) /* 16 MB: far above any 2D asset the launcher reads */

typedef struct xpr_tex {
    uint32_t format;        /* the texture's Format word (D3DFORMAT in bits 8-15) */
    const uint8_t *data;    /* header_size + Data */
    size_t data_len;        /* bytes from data to the end of the XPR0 */
    const uint8_t *palette; /* D3DCOLOR entries of the palette header that follows, or NULL */
    size_t palette_len;     /* bytes */
} xpr_tex_t;

typedef struct spr_rect {
    int tex;            /* texture ordinal (counting textures only) */
    int x0, y0, x1, y1; /* pixels of that texture, x1/y1 exclusive */
} spr_rect_t;

/* 1 and the entry's offset and size if `name` (exact, case-sensitive) is in the AFS file. */
int afs_find(const wchar_t *afs_path, const char *name, uint64_t *off, uint32_t *size);

/* The entry read into a malloc'd buffer (caller frees), or NULL. */
uint8_t *gamedata_load(const wchar_t *afs_path, const char *name, size_t *len);

/* 1 and the `ordinal`-th texture (0-based, counting textures only) of an XPR0 buffer. */
int xpr_texture(const uint8_t *xpr, size_t len, int ordinal, xpr_tex_t *out);

/* 1 and sprite `index` of a .spr XPR0, in pixels of its texture (needs that texture's size). */
int spr_sprite(const uint8_t *xpr, size_t len, int index, spr_rect_t *out);
