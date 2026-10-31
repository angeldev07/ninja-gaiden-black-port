/*
 * gamedata.c: readers for the launcher's game art mode. See gamedata.h.
 *
 * Every offset read from a file is checked against the bytes actually available before it is
 * used; a failed check means "not found", never a partial result.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "gamedata.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define AFS_MAX_ENTRIES 65535u
#define AFS_ATTR_SIZE 0x30u
#define XPR_MAX_HEADERS 4096
#define USERDATA_MARKER 0x80000000u
#define SPR_RECORD 24u

static uint32_t rd32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static uint16_t rd16(const uint8_t *p) {
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

static float rdf(const uint8_t *p) {
    float v;
    memcpy(&v, p, 4);
    return v;
}

static int read_at(HANDLE h, uint64_t off, void *buf, DWORD n) {
    LARGE_INTEGER li;
    DWORD got = 0;
    li.QuadPart = (LONGLONG)off;
    return SetFilePointerEx(h, li, NULL, FILE_BEGIN) && ReadFile(h, buf, n, &got, NULL) && got == n;
}

int afs_find(const wchar_t *afs_path, const char *name, uint64_t *off, uint32_t *size) {
    HANDLE h = CreateFileW(afs_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER fsize;
    uint8_t head[8], *toc = NULL, *attr = NULL;
    uint32_t n, i, attr_off, attr_len;
    size_t name_len = strlen(name);
    int found = 0;
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    if (!GetFileSizeEx(h, &fsize) || name_len == 0 || name_len > 32 || !read_at(h, 0, head, 8) ||
        memcmp(head, "AFS\0", 4))
        goto done;
    n = rd32(head + 4);
    if (n == 0 || n > AFS_MAX_ENTRIES || 8u + 8u * (uint64_t)n + 8u > (uint64_t)fsize.QuadPart)
        goto done;
    toc = (uint8_t *)malloc(8u * n + 8u);
    if (!toc || !read_at(h, 8, toc, 8u * n + 8u))
        goto done;
    attr_off = rd32(toc + 8u * n);
    attr_len = rd32(toc + 8u * n + 4u);
    if (attr_len < AFS_ATTR_SIZE * n || (uint64_t)attr_off + AFS_ATTR_SIZE * (uint64_t)n > (uint64_t)fsize.QuadPart)
        goto done;
    attr = (uint8_t *)malloc(AFS_ATTR_SIZE * n);
    if (!attr || !read_at(h, attr_off, attr, AFS_ATTR_SIZE * n))
        goto done;
    for (i = 0; i < n; i++) {
        const char *nm = (const char *)attr + (size_t)AFS_ATTR_SIZE * i;
        uint32_t eo = rd32(toc + 8u * i), es = rd32(toc + 8u * i + 4u);
        if (strnlen(nm, 32) != name_len || memcmp(nm, name, name_len))
            continue;
        if ((uint64_t)eo + es > (uint64_t)fsize.QuadPart)
            break; /* the entry runs past the end of the file */
        *off = eo;
        *size = es;
        found = 1;
        break;
    }
done:
    free(toc);
    free(attr);
    CloseHandle(h);
    return found;
}

uint8_t *gamedata_load(const wchar_t *afs_path, const char *name, size_t *len) {
    uint64_t off;
    uint32_t size;
    uint8_t *buf;
    HANDLE h;
    int ok;
    if (!afs_find(afs_path, name, &off, &size) || size == 0 || size > GAMEDATA_MAX_ENTRY)
        return NULL;
    h = CreateFileW(afs_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                    NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    buf = (uint8_t *)malloc(size);
    ok = buf && read_at(h, off, buf, size);
    CloseHandle(h);
    if (!ok) {
        free(buf);
        return NULL;
    }
    *len = size;
    return buf;
}

/* Walks the resource headers. Calls back with (type, header pointer); stops when it returns 0.
 * Returns 0 on any malformed header. type: 0..7 (Common bits 16-18) or 8 for USERDATA. */
typedef int (*xpr_visit)(void *ctx, int type, const uint8_t *hdr, uint32_t userdata_size);

static int xpr_walk(const uint8_t *x, size_t len, xpr_visit fn, void *ctx) {
    uint32_t header_size, total;
    size_t p = 12;
    int count;
    if (len < 16 || memcmp(x, "XPR0", 4))
        return 0;
    total = rd32(x + 4);
    header_size = rd32(x + 8);
    if (total > len || header_size > total || header_size < 16)
        return 0;
    for (count = 0; count < XPR_MAX_HEADERS; count++) {
        uint32_t common, type, hdr_len;
        if (p + 4 > header_size)
            return 0;
        common = rd32(x + p);
        if (common == 0xFFFFFFFFu)
            return 1;
        if (common == USERDATA_MARKER) {
            uint32_t size;
            if (p + 8 > header_size)
                return 0;
            size = rd32(x + p + 4);
            if (size > header_size - p - 8)
                return 0;
            if (!fn(ctx, 8, x + p, size))
                return 1;
            p += 8u + size;
            continue;
        }
        type = (common >> 16) & 7u;
        hdr_len = type == 4 ? 20u : type == 2 ? 24u : 12u;
        if (p + hdr_len > header_size)
            return 0;
        if (!fn(ctx, (int)type, x + p, 0))
            return 1;
        p += hdr_len;
    }
    return 0;
}

typedef struct tex_find {
    int want, seen, done;
    const uint8_t *tex, *pal_next; /* the header right after the texture, if a palette */
    int after;                     /* 1 while looking at the header after the wanted texture */
} tex_find_t;

static int visit_tex(void *ctx, int type, const uint8_t *hdr, uint32_t usize) {
    tex_find_t *f = (tex_find_t *)ctx;
    (void)usize;
    if (f->after) {
        if (type == 3)
            f->pal_next = hdr;
        f->done = 1;
        return 0;
    }
    if (type == 4 && f->seen++ == f->want) {
        f->tex = hdr;
        f->after = 1;
    }
    return 1;
}

int xpr_texture(const uint8_t *xpr, size_t len, int ordinal, xpr_tex_t *out) {
    tex_find_t f;
    uint32_t header_size, total, data;
    memset(&f, 0, sizeof f);
    f.want = ordinal;
    if (ordinal < 0 || !xpr_walk(xpr, len, visit_tex, &f) || !f.tex)
        return 0;
    total = rd32(xpr + 4);
    header_size = rd32(xpr + 8);
    data = rd32(f.tex + 4);
    if (data >= total - header_size)
        return 0;
    memset(out, 0, sizeof *out);
    out->format = rd32(f.tex + 12);
    out->data = xpr + header_size + data;
    out->data_len = total - header_size - data;
    if (f.pal_next) {
        uint32_t entries = 256u >> ((rd32(f.pal_next) >> 30) & 3u), pd = rd32(f.pal_next + 4);
        if ((uint64_t)pd + 4u * entries <= (uint64_t)(total - header_size)) {
            out->palette = xpr + header_size + pd;
            out->palette_len = 4u * entries;
        }
    }
    return 1;
}

typedef struct spr_find {
    const uint8_t *payload;
    uint32_t size;
} spr_find_t;

static int visit_spr(void *ctx, int type, const uint8_t *hdr, uint32_t usize) {
    spr_find_t *f = (spr_find_t *)ctx;
    if (type == 8 && !f->payload) {
        f->payload = hdr + 8;
        f->size = usize;
    }
    return 0; /* the sprite table is resource 0: stop after the first header */
}

static int to_px(float uv, int dim) {
    float v = uv * (float)dim;
    if (!(v >= 0.0f)) /* NaN or negative */
        return 0;
    return v > (float)dim ? dim : (int)floorf(v + 0.5f);
}

int spr_sprite(const uint8_t *xpr, size_t len, int index, spr_rect_t *out) {
    spr_find_t f;
    const uint8_t *r;
    int w, h;
    memset(&f, 0, sizeof f);
    if (index < 0 || !xpr_walk(xpr, len, visit_spr, &f) || !f.payload ||
        (uint32_t)index >= f.size / SPR_RECORD)
        return 0;
    r = f.payload + (size_t)index * SPR_RECORD;
    w = rd16(r + 20);
    h = rd16(r + 22);
    if (w == 0 || h == 0)
        return 0;
    out->tex = (int)rd32(r + 16);
    out->x0 = to_px(rdf(r), w);
    out->y0 = to_px(rdf(r + 4), h);
    out->x1 = to_px(rdf(r + 8), w);
    out->y1 = to_px(rdf(r + 12), h);
    return out->x1 > out->x0 && out->y1 > out->y0;
}
