/*
 * rcache.c: surface and texture caches of the GL render (see rcache.h). Pure code.
 *
 * They are small tables with linear search: a game frame uses a few surfaces and a few
 * dozen textures, so a hash does not pay off. The LRU uses a clock that advances on every
 * access.
 */
#include "rcache.h"

#include <stdlib.h>
#include <string.h>

#define RC_MAX_DIM 4096u

static void push_dead(unsigned tex, unsigned *dead, int dead_cap, int *n_dead) {
    if (!tex || !n_dead)
        return;
    if (dead && *n_dead < dead_cap)
        dead[*n_dead] = tex;
    (*n_dead)++;
}

rc_surface *rc_surface_find(rc_surfaces *c, int kind, uint32_t addr) {
    int i;
    for (i = 0; i < RC_MAX_SURFACES; i++)
        if (c->s[i].used && c->s[i].kind == kind && c->s[i].addr == addr) {
            c->s[i].last_use = ++c->clock;
            return &c->s[i];
        }
    return NULL;
}

rc_surface *rc_surface_get(rc_surfaces *c, int kind, uint32_t addr, uint32_t pitch,
                           uint32_t width, uint32_t height, uint32_t format, int *result,
                           unsigned *dead, int dead_cap, int *n_dead) {
    rc_surface *hit = NULL, *slot = NULL;
    uint64_t end;
    uint32_t bytes;
    int i;
    if (n_dead)
        *n_dead = 0;
    if (!c || !result || !width || !height || width > RC_MAX_DIM || height > RC_MAX_DIM)
        return NULL;
    if (pitch < width)
        pitch = width * 4; /* no reliable pitch: assume 32 bits per pixel */
    bytes = pitch * height;
    end = (uint64_t)addr + bytes;
    for (i = 0; i < RC_MAX_SURFACES; i++) {
        rc_surface *s = &c->s[i];
        if (!s->used)
            continue;
        if (s->kind == kind && s->addr == addr) {
            hit = s;
            continue;
        }
        /* Another surface in the same memory: its contents are no longer valid. */
        if ((uint64_t)s->addr < end && (uint64_t)s->addr + s->bytes > addr) {
            push_dead(s->gl_tex, dead, dead_cap, n_dead);
            memset(s, 0, sizeof *s);
        }
    }
    if (hit) {
        hit->last_use = ++c->clock;
        hit->bytes = bytes;
        if (hit->width == width && hit->height == height && hit->format == format) {
            *result = RC_SURF_HIT;
            return hit;
        }
        push_dead(hit->gl_tex, dead, dead_cap, n_dead);
        hit->gl_tex = 0;
        hit->width = width;
        hit->height = height;
        hit->format = format;
        *result = RC_SURF_RESIZED;
        return hit;
    }
    for (i = 0; i < RC_MAX_SURFACES; i++) {
        rc_surface *s = &c->s[i];
        if (!s->used) {
            slot = s;
            break;
        }
        if (!slot || s->last_use < slot->last_use)
            slot = s;
    }
    if (slot->used)
        push_dead(slot->gl_tex, dead, dead_cap, n_dead);
    memset(slot, 0, sizeof *slot);
    slot->used = 1;
    slot->kind = kind;
    slot->addr = addr;
    slot->bytes = bytes;
    slot->width = width;
    slot->height = height;
    slot->format = format;
    slot->last_use = ++c->clock;
    *result = RC_SURF_NEW;
    return slot;
}

static int same_key(const rc_texkey *a, const rc_texkey *b) {
    return a->addr == b->addr && a->format == b->format && a->control1 == b->control1 &&
           a->rect == b->rect && a->palette == b->palette;
}

static unsigned key_bucket(const rc_texkey *k) {
    uint32_t h = k->addr * 0x9E3779B1u ^ k->format * 0x85EBCA77u ^ k->control1 * 0xC2B2AE3Du ^
                 k->rect * 0x27D4EB2Fu ^ k->palette * 0x165667B1u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h & (RC_TEX_BUCKETS - 1);
}

static void bucket_unlink(rc_textures *c, rc_texture *t) {
    int16_t *link = &c->head[t->bucket], self = (int16_t)(t - c->t + 1);
    while (*link && *link != self)
        link = &c->t[*link - 1].next;
    if (*link)
        *link = t->next;
}

rc_texture *rc_texture_get(rc_textures *c, const rc_texkey *key, uint32_t frame, int *action,
                           unsigned *dead) {
    rc_texture *slot = NULL;
    unsigned b;
    int i;
    if (dead)
        *dead = 0;
    if (!c || !key || !action)
        return NULL;
    b = key_bucket(key);
    for (i = c->head[b]; i; i = c->t[i - 1].next) {
        rc_texture *t = &c->t[i - 1];
        if (t->used && same_key(&t->key, key)) {
            t->last_use = ++c->clock;
            if (t->checked_frame == frame && t->gl_tex) {
                c->hits++;
                *action = RC_TEX_USE;
            } else {
                *action = t->gl_tex ? RC_TEX_CHECK : RC_TEX_UPLOAD;
            }
            return t;
        }
    }
    for (i = 0; i < RC_MAX_TEXTURES; i++) {
        rc_texture *t = &c->t[i];
        if (!t->used) {
            slot = t;
            break;
        }
        if (!slot || t->last_use < slot->last_use)
            slot = t;
    }
    if (slot->used) {
        if (dead)
            *dead = slot->gl_tex;
        bucket_unlink(c, slot);
    }
    memset(slot, 0, sizeof *slot);
    slot->used = 1;
    slot->key = *key;
    slot->last_use = ++c->clock;
    slot->bucket = (uint16_t)b;
    slot->next = c->head[b];
    c->head[b] = (int16_t)(slot - c->t + 1);
    *action = RC_TEX_UPLOAD;
    return slot;
}

int rc_texture_checked(rc_textures *c, rc_texture *t, uint64_t hash, uint32_t frame) {
    c->checks++;
    t->checked_frame = frame;
    if (t->hash == hash)
        return 0;
    return 1;
}

void rc_texture_uploaded(rc_texture *t, uint64_t hash, uint32_t frame) {
    t->hash = hash;
    t->checked_frame = frame;
}

uint64_t rc_hash(const void *data, size_t len, uint64_t seed) {
    /* Two 32-bit accumulators (multiply and rotate, like murmur): fast on 32-bit x86 and
     * good enough to detect changes, not cryptographic. */
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h1 = (uint32_t)seed ^ 0x9E3779B9u, h2 = (uint32_t)(seed >> 32) ^ 0x85EBCA6Bu;
    size_t i, words = len / 4;
    for (i = 0; i < words; i++) {
        uint32_t w;
        memcpy(&w, p + i * 4, 4);
        w *= 0xCC9E2D51u;
        w = (w << 15) | (w >> 17);
        if (i & 1) {
            h2 ^= w;
            h2 = ((h2 << 13) | (h2 >> 19)) * 5u + 0xE6546B64u;
        } else {
            h1 ^= w;
            h1 = ((h1 << 13) | (h1 >> 19)) * 5u + 0xE6546B64u;
        }
    }
    for (i = words * 4; i < len; i++)
        h1 = (h1 ^ p[i]) * 0x01000193u;
    h1 ^= (uint32_t)len;
    h2 ^= (uint32_t)(len >> 16);
    h1 ^= h1 >> 16;
    h1 *= 0x85EBCA6Bu;
    h2 ^= h2 >> 13;
    h2 *= 0xC2B2AE35u;
    return ((uint64_t)h2 << 32) | h1;
}

int rc_attr_layout(uint32_t f, rc_attr *out) {
    int type = (int)(f & 0xFu), size = (int)((f >> 4) & 0xFu);
    memset(out, 0, sizeof *out);
    out->type = type;
    out->stride = (f >> 8) & 0xFFFFu; /* high bits of the game word: dropped */
    if (!size)
        return 0; /* disabled */
    if (size == 7)
        size = 3; /* SIZE_3W: three components */
    if (size > 4)
        return -1;
    out->enabled = 1;
    out->comps = size;
    switch (type) {
    case 0: /* UB_D3D: D3DCOLOR, always one word */
    case 4: /* UB_OGL */
        out->src_bytes = 4;
        out->dst_bytes = 4;
        out->inline_words = 1;
        break;
    case 1: /* S1 */
    case 5: /* S32K */
        out->src_bytes = 2 * size;
        out->dst_bytes = 2 * size;
        out->inline_words = (size + 1) / 2;
        break;
    case 2: /* F */
        out->src_bytes = 4 * size;
        out->dst_bytes = 4 * size;
        out->inline_words = size;
        break;
    case 6: /* CMP: normal 11:11:10 in one word; expanded to 3 floats */
        out->comps = 3;
        out->src_bytes = 4;
        out->dst_bytes = 12;
        out->inline_words = 1;
        break;
    default:
        memset(out, 0, sizeof *out);
        return -1;
    }
    return 0;
}

uint32_t rc_quads_to_triangles(const uint32_t *in, uint32_t n, uint32_t *out) {
    uint32_t i, k = 0;
    for (i = 0; i + 3 < n; i += 4) {
        out[k++] = in[i];
        out[k++] = in[i + 1];
        out[k++] = in[i + 2];
        out[k++] = in[i];
        out[k++] = in[i + 2];
        out[k++] = in[i + 3];
    }
    return k;
}

uint64_t rc_sparse_hash(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint8_t buf[RC_SPARSE_SAMPLES * 16];
    size_t i, n = 0, step;
    if (len <= sizeof buf)
        return rc_hash(data, len, 0x53505253u);
    step = (len - 16) / (RC_SPARSE_SAMPLES - 1);
    for (i = 0; i < RC_SPARSE_SAMPLES; i++, n += 16)
        memcpy(buf + n, p + (i + 1 == RC_SPARSE_SAMPLES ? len - 16 : i * step), 16);
    return rc_hash(buf, n, 0x53505253u ^ (uint64_t)len);
}

unsigned rc_surface_drop(rc_surface *s) {
    unsigned t = s->gl_tex;
    memset(s, 0, sizeof *s);
    return t;
}

void rc_dirty_mark(rc_dirty *d, const uint32_t *phys, size_t n, uint32_t frame) {
    size_t i;
    for (i = 0; i < n; i++) {
        uint32_t p = phys[i] / RC_DIRTY_PAGE;
        if (p < RC_DIRTY_PAGES)
            d->page_frame[p] = frame;
    }
}

uint32_t rc_dirty_last(const rc_dirty *d, uint32_t phys, uint32_t len) {
    uint32_t p, last, best = 0;
    if (!len || phys >= RC_DIRTY_PAGES * RC_DIRTY_PAGE || len > RC_DIRTY_PAGES * RC_DIRTY_PAGE - phys)
        return UINT32_MAX;
    last = (phys + len - 1) / RC_DIRTY_PAGE;
    for (p = phys / RC_DIRTY_PAGE; p <= last; p++)
        if (d->page_frame[p] > best)
            best = d->page_frame[p];
    return best;
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

uint32_t rc_compact_indices(uint32_t *idx, uint32_t n, uint32_t *unique) {
    uint32_t i, m = 0;
    if (!n)
        return 0;
    memcpy(unique, idx, (size_t)n * sizeof *idx);
    qsort(unique, n, sizeof *unique, cmp_u32);
    for (i = 0; i < n; i++)
        if (!m || unique[m - 1] != unique[i])
            unique[m++] = unique[i];
    for (i = 0; i < n; i++) {
        uint32_t lo = 0, hi = m - 1;
        while (lo < hi) { /* binary search: the index is always present */
            uint32_t mid = (lo + hi) / 2;
            if (unique[mid] < idx[i])
                lo = mid + 1;
            else
                hi = mid;
        }
        idx[i] = lo;
    }
    return m;
}
