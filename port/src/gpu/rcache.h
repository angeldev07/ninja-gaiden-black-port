/*
 * rcache.h: pure caches of the PGRAPH OpenGL render (H7c). No GL: they keep GL object names
 * as opaque numbers and decide what to create, reuse or delete, so the logic can be tested
 * without a context (tests/rcache_test.c).
 *
 * - Surfaces (render targets): one entry per (physical address, color/zeta kind). The game
 *   draws into a surface and then uses it as a texture (render-to-texture): the texture is
 *   resolved to the surface by its address, without going through the CPU. A new surface
 *   that overlaps another one's memory invalidates it.
 * - Textures: one entry per (address, format, control1, image_rect, palette), with the
 *   content hash. The first time it is used in a frame the memory is hashed again; only if
 *   it changed is it converted and uploaded again (per-frame invalidation).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define RC_MAX_SURFACES 64
#define RC_MAX_TEXTURES 512

enum rc_kind { RC_COLOR = 0, RC_ZETA = 1 };

typedef struct rc_surface {
    int used;
    int kind;               /* rc_kind */
    uint32_t addr;          /* physical address of the first pixel */
    uint32_t bytes;         /* memory it takes on the Xbox: pitch x height */
    uint32_t width, height; /* in pixels */
    uint32_t format;        /* SET_SURFACE_FORMAT: color or zeta code */
    unsigned gl_tex;        /* GL texture (RGBA8 or DEPTH24_STENCIL8); 0 = to be created */
    uint32_t last_use;      /* cache clock, for the LRU */
    uint64_t mem_sig;       /* rc_sparse_hash of the RAM when it was used as a target: if the
                             * CPU writes something else there (a new texture), it stops matching */
    uint32_t drawn_frame;   /* last frame in which it was a target (for the write watch) */
    int mips_stale;         /* drawn into since its mipmaps were last built (pgraph_gl.c) */
} rc_surface;

typedef struct rc_surfaces {
    rc_surface s[RC_MAX_SURFACES];
    uint32_t clock;
} rc_surfaces;

/* Result of rc_surface_get. */
enum rc_surface_result {
    RC_SURF_HIT = 0,     /* existed with the same size and format */
    RC_SURF_NEW = 1,     /* new entry: the GL texture must be created */
    RC_SURF_RESIZED = 2, /* existed with another size or format: delete the old `gl_tex` and create */
};

/*
 * Finds or creates the surface (addr, kind). The GL textures that must be deleted (entries
 * evicted by LRU, overwritten by the new one or recreated) are left in `dead` (up to
 * `dead_cap`, the rest is lost) and their count in `*n_dead`. NULL if the arguments are not
 * valid (size 0 or larger than 4096).
 */
rc_surface *rc_surface_get(rc_surfaces *c, int kind, uint32_t addr, uint32_t pitch,
                           uint32_t width, uint32_t height, uint32_t format, int *result,
                           unsigned *dead, int dead_cap, int *n_dead);

/* Color surface that starts exactly at `addr`, or NULL (to resolve textures). */
rc_surface *rc_surface_find(rc_surfaces *c, int kind, uint32_t addr);

typedef struct rc_texkey {
    uint32_t addr, format, control1, rect, palette;
} rc_texkey;

typedef struct rc_texture {
    int used;
    int16_t next;    /* next entry of the same bucket, as index + 1 (0 = none) */
    uint16_t bucket; /* bucket of the key in rc_textures.head */
    rc_texkey key;
    uint64_t hash;          /* of the content (and the palette) at the last upload */
    uint32_t checked_frame; /* frame in which the hash was last checked */
    unsigned gl_tex;
    uint32_t last_use;
} rc_texture;

/* The entries are found through a hash of the key (buckets of entries chained by `next`):
 * a linear search of the 512 entries cost about 1.5 us per texture bind once the cache was
 * full, a millisecond or more per frame after an hour of play. */
#define RC_TEX_BUCKETS 1024
typedef struct rc_textures {
    rc_texture t[RC_MAX_TEXTURES];
    int16_t head[RC_TEX_BUCKETS]; /* first entry of each bucket, as index + 1 (0 = empty) */
    uint32_t clock;
    uint32_t hits, uploads, checks; /* statistics */
} rc_textures;

/* What the caller must do with the entry returned by rc_texture_get. */
enum rc_texture_action {
    RC_TEX_USE = 0,    /* already checked this frame: use gl_tex as is */
    RC_TEX_CHECK = 1,  /* hash the content and call rc_texture_checked */
    RC_TEX_UPLOAD = 2, /* new entry: create gl_tex, upload and call rc_texture_uploaded */
};

/* Finds or creates the entry for `key`. If it evicts one by LRU, its GL texture goes to
 * `*dead` (0 if none). */
rc_texture *rc_texture_get(rc_textures *c, const rc_texkey *key, uint32_t frame, int *action,
                           unsigned *dead);

/* After hashing (RC_TEX_CHECK): 1 if the content changed and must be uploaded again. */
int rc_texture_checked(rc_textures *c, rc_texture *t, uint64_t hash, uint32_t frame);

/* After uploading: stores the hash and the frame. */
void rc_texture_uploaded(rc_texture *t, uint64_t hash, uint32_t frame);

/* Fast content hash (32-bit words; the tail, byte by byte). */
uint64_t rc_hash(const void *data, size_t len, uint64_t seed);

/* Cheap signature of a large area: rc_hash of RC_SPARSE_SAMPLES 16-byte chunks spread
 * evenly (reads about 4 KB even if the area is megabytes). */
#define RC_SPARSE_SAMPLES 256
uint64_t rc_sparse_hash(const void *data, size_t len);

/* Removes the surface from the cache (the CPU has reused its memory); returns its GL texture
 * for the caller to delete. */
unsigned rc_surface_drop(rc_surface *s);

/* ---- pure drawing utilities ---- */

/* One attribute according to SET_VERTEX_DATA_ARRAY_FORMAT (type in bits 0-3, size in 4-7,
 * stride in 8-31). NV2A types: 0 UB_D3D, 1 S1, 2 F, 4 UB_OGL, 5 S32K, 6 CMP. */
typedef struct rc_attr {
    int enabled;
    int type;        /* the NV2A one */
    int comps;       /* components the shader sees (CMP = 3) */
    int src_bytes;   /* bytes an element takes in memory */
    int dst_bytes;   /* bytes it takes in the VBO (CMP expands to 3 floats) */
    int inline_words;/* 32-bit words in INLINE_ARRAY */
    uint32_t stride; /* bytes between vertices in an array */
} rc_attr;

/* 0 if the format is valid (also if it is disabled: enabled = 0), -1 otherwise. */
int rc_attr_layout(uint32_t array_format, rc_attr *out);

/* QUADS -> TRIANGLES: (a b c d) -> (a b c, a c d). `out` needs n / 4 * 6 entries.
 * Returns the number of indices written. */
uint32_t rc_quads_to_triangles(const uint32_t *in, uint32_t n, uint32_t *out);

/* ---- pages written by the CPU (write watch of the contiguous arena) ---- */

/* Frame of the last write to each 4 KB page of the physical RAM (64 MB). The render fills it
 * once per frame with GetWriteWatch, and a texture is only hashed again if any of its pages
 * was written after its last check. */
#define RC_DIRTY_PAGE 4096u
#define RC_DIRTY_PAGES (0x04000000u / RC_DIRTY_PAGE)

typedef struct rc_dirty {
    uint32_t page_frame[RC_DIRTY_PAGES]; /* 0 = no writes seen */
} rc_dirty;

/* Marks with `frame` the pages of the physical addresses `phys[0..n)`. */
void rc_dirty_mark(rc_dirty *d, const uint32_t *phys, size_t n, uint32_t frame);
/* Most recent frame in which any page of [phys, phys + len) was written. Outside the physical
 * RAM it returns UINT32_MAX (no information: must hash). */
uint32_t rc_dirty_last(const rc_dirty *d, uint32_t phys, uint32_t len);

/* Compacts the indices of a draw whose range [min, max] is much larger than the vertices it
 * uses (issue #43): leaves in `unique` (capacity n) the distinct indices sorted and rewrites
 * `idx` as positions in that list. Returns how many distinct indices there are. */
uint32_t rc_compact_indices(uint32_t *idx, uint32_t n, uint32_t *unique);
