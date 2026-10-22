/*
 * xdvdfs.c: read-only XDVDFS reader and extractor. See xdvdfs.h.
 *
 * Volume descriptor (sector 32 of the partition):
 *   0x000  "MICROSOFT*XBOX*MEDIA"
 *   0x014  u32 root directory sector, 0x018  u32 root directory size in bytes
 *   0x7EC  "MICROSOFT*XBOX*MEDIA" again
 * Directory entry (4-byte aligned, offsets are counted in dwords from the start of the
 * directory data):
 *   u16 left, u16 right, u32 start sector, u32 size, u8 attributes, u8 name length, name
 * A child offset of 0 or 0xFFFF means "no child". The tree is ordered by name, so an
 * in-order walk lists the entries sorted.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <string.h>

#include "xdvdfs.h"

#define MAGIC "MICROSOFT*XBOX*MEDIA"
#define MAGIC_LEN 20
#define MAX_DIR_BYTES (64u * 1024u * 1024u)
#define MAX_REL_PATH 600
#define MAX_DEPTH 32
#define COPY_CHUNK (2 * 1024 * 1024)
#define ATTR_DIRECTORY 0x10

static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t rd_u16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }

/* Reads exactly n bytes at an absolute position of the image file. */
static int read_abs(HANDLE h, int64_t pos, void *buf, uint32_t n) {
    uint8_t *p = (uint8_t *)buf;
    LARGE_INTEGER li;
    li.QuadPart = pos;
    if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN))
        return XDVD_E_READ;
    while (n) {
        DWORD got = 0;
        if (!ReadFile(h, p, n, &got, NULL) || got == 0)
            return XDVD_E_READ;
        p += got;
        n -= got;
    }
    return XDVD_OK;
}

int xdvd_open_at(xdvd_t *x, const wchar_t *path, int64_t base) {
    uint8_t desc[XDVD_SECTOR];
    LARGE_INTEGER sz;
    int64_t end;
    memset(x, 0, sizeof *x);
    x->file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                          OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (x->file == INVALID_HANDLE_VALUE) {
        x->file = NULL;
        return XDVD_E_OPEN;
    }
    if (!GetFileSizeEx(x->file, &sz)) {
        xdvd_close(x);
        return XDVD_E_OPEN;
    }
    x->image_size = sz.QuadPart;
    x->base = base;
    if (base + 33LL * XDVD_SECTOR > x->image_size ||
        read_abs(x->file, base + 32LL * XDVD_SECTOR, desc, XDVD_SECTOR) != XDVD_OK ||
        memcmp(desc, MAGIC, MAGIC_LEN) != 0 || memcmp(desc + 0x7EC, MAGIC, MAGIC_LEN) != 0) {
        xdvd_close(x);
        return XDVD_E_NOT_XISO;
    }
    x->root_sector = rd_u32(desc + 0x14);
    x->root_size = rd_u32(desc + 0x18);
    end = base + (int64_t)x->root_sector * XDVD_SECTOR + (int64_t)x->root_size;
    if (x->root_size == 0 || x->root_size > MAX_DIR_BYTES || end > x->image_size) {
        xdvd_close(x);
        return XDVD_E_CORRUPT;
    }
    return XDVD_OK;
}

int xdvd_open(xdvd_t *x, const wchar_t *path) {
    static const int64_t offsets[2] = {0, XDVD_REDUMP_OFFSET};
    int i, err = XDVD_E_NOT_XISO;
    for (i = 0; i < 2; i++) {
        err = xdvd_open_at(x, path, offsets[i]);
        if (err != XDVD_E_NOT_XISO)
            return err; /* opened, or a failure that another offset will not fix */
    }
    return err;
}

void xdvd_close(xdvd_t *x) {
    if (x->file)
        CloseHandle(x->file);
    x->file = NULL;
}

int xdvd_read(xdvd_t *x, uint32_t sector, int64_t skip, void *buf, uint32_t n) {
    int64_t pos = x->base + (int64_t)sector * XDVD_SECTOR + skip;
    if (pos < 0 || pos + n > x->image_size)
        return XDVD_E_READ;
    return read_abs(x->file, pos, buf, n);
}

/* ------------------------------------------------------------------ directories ---- */

typedef struct entry {
    wchar_t name[256];
    uint32_t sector, size;
    int is_dir;
} entry_t;

static int bad_name(const wchar_t *n) {
    static const wchar_t *reserved[] = {L"CON", L"PRN", L"AUX", L"NUL"};
    size_t len = wcslen(n), i;
    wchar_t stem[8];
    if (!len || !wcscmp(n, L".") || !wcscmp(n, L"..") || n[len - 1] == L'.' || n[len - 1] == L' ')
        return 1;
    for (i = 0; i < len; i++)
        if (n[i] < 0x20 || n[i] == 0x7f || wcschr(L"<>:\"/\\|?*", n[i]))
            return 1;
    for (i = 0; i < 7 && n[i] && n[i] != L'.'; i++)
        stem[i] = n[i];
    stem[i] = 0;
    for (i = 0; i < sizeof reserved / sizeof reserved[0]; i++)
        if (!_wcsicmp(stem, reserved[i]))
            return 1;
    if ((!_wcsnicmp(stem, L"COM", 3) || !_wcsnicmp(stem, L"LPT", 3)) && stem[3] >= L'0' &&
        stem[3] <= L'9' && !stem[4])
        return 1;
    return 0;
}

/* Parses the entry at `off` (bytes). Returns XDVD_OK or XDVD_E_CORRUPT. */
static int parse_entry(const uint8_t *d, uint32_t size, uint32_t off, entry_t *e, uint32_t *left,
                       uint32_t *right) {
    uint32_t nlen, i;
    if ((uint64_t)off + 14 > size)
        return XDVD_E_CORRUPT;
    *left = rd_u16(d + off);
    *right = rd_u16(d + off + 2);
    e->sector = rd_u32(d + off + 4);
    e->size = rd_u32(d + off + 8);
    e->is_dir = (d[off + 12] & ATTR_DIRECTORY) != 0;
    nlen = d[off + 13];
    if (!nlen || (uint64_t)off + 14 + nlen > size)
        return XDVD_E_CORRUPT;
    for (i = 0; i < nlen; i++)
        e->name[i] = d[off + 14 + i]; /* Latin-1: one byte per character */
    e->name[nlen] = 0;
    return bad_name(e->name) ? XDVD_E_CORRUPT : XDVD_OK;
}

/* Lists the byte offsets of the entries of one directory in name order. */
static int dir_order(const uint8_t *d, uint32_t size, uint32_t **order_out, uint32_t *count_out) {
    uint32_t cap = size / 16 + 2, n = 0, sp = 0;
    uint32_t *stack, *order;
    uint8_t *seen;
    int64_t cur = 0;
    int err = XDVD_OK;
    *order_out = NULL;
    *count_out = 0;
    if (size < 14 || (rd_u16(d) == 0xFFFF && rd_u16(d + 2) == 0xFFFF))
        return XDVD_OK; /* empty directory */
    stack = (uint32_t *)malloc(cap * sizeof(uint32_t));
    order = (uint32_t *)malloc(cap * sizeof(uint32_t));
    seen = (uint8_t *)calloc(size / 4 + 1, 1);
    if (!stack || !order || !seen) {
        err = XDVD_E_NOMEM;
        goto done;
    }
    for (;;) {
        while (cur >= 0) {
            entry_t probe;
            uint32_t off = (uint32_t)cur * 4, l, r;
            if (seen[cur] || sp >= cap || n >= cap) {
                err = XDVD_E_CORRUPT; /* a loop, or more entries than the sectors can hold */
                goto done;
            }
            seen[cur] = 1;
            if ((err = parse_entry(d, size, off, &probe, &l, &r)) != XDVD_OK)
                goto done;
            stack[sp++] = (uint32_t)cur;
            cur = (l == 0 || l == 0xFFFF) ? -1 : (int64_t)l;
        }
        if (!sp)
            break;
        cur = stack[--sp];
        order[n++] = (uint32_t)cur * 4;
        {
            uint32_t r = rd_u16(d + (uint32_t)cur * 4 + 2);
            cur = (r == 0 || r == 0xFFFF) ? -1 : (int64_t)r;
        }
    }
    *order_out = order;
    *count_out = n;
    order = NULL;
done:
    free(stack);
    free(order);
    free(seen);
    return err;
}

static int load_dir(xdvd_t *x, uint32_t sector, uint32_t size, uint8_t **out) {
    uint8_t *buf;
    int err;
    *out = NULL;
    if (size > MAX_DIR_BYTES)
        return XDVD_E_CORRUPT;
    buf = (uint8_t *)malloc(size ? size : 1);
    if (!buf)
        return XDVD_E_NOMEM;
    if (size && (err = xdvd_read(x, sector, 0, buf, size)) != XDVD_OK) {
        free(buf);
        return err;
    }
    *out = buf;
    return XDVD_OK;
}

static int walk_dir(xdvd_t *x, uint32_t sector, uint32_t size, wchar_t *prefix, size_t plen,
                    xdvd_visit_fn fn, void *ctx, int depth) {
    uint8_t *d = NULL;
    uint32_t *order = NULL, count = 0, i;
    int err;
    if (depth > MAX_DEPTH)
        return XDVD_E_CORRUPT;
    if ((err = load_dir(x, sector, size, &d)) != XDVD_OK)
        return err;
    if ((err = dir_order(d, size, &order, &count)) != XDVD_OK)
        goto done;
    for (i = 0; i < count; i++) {
        entry_t e;
        uint32_t l, r;
        size_t nlen;
        if ((err = parse_entry(d, size, order[i], &e, &l, &r)) != XDVD_OK)
            goto done;
        nlen = wcslen(e.name);
        if (plen + nlen + 2 >= MAX_REL_PATH) {
            err = XDVD_E_CORRUPT;
            goto done;
        }
        if (e.size && (int64_t)e.sector * XDVD_SECTOR + e.size + x->base > x->image_size) {
            err = XDVD_E_CORRUPT; /* points past the end of the image */
            goto done;
        }
        memcpy(prefix + plen, e.name, (nlen + 1) * sizeof(wchar_t));
        if (fn(ctx, prefix, e.is_dir, e.sector, e.size)) {
            err = XDVD_E_CANCEL;
            goto done;
        }
        if (e.is_dir) {
            prefix[plen + nlen] = L'\\';
            prefix[plen + nlen + 1] = 0;
            err = walk_dir(x, e.sector, e.size, prefix, plen + nlen + 1, fn, ctx, depth + 1);
            prefix[plen] = 0;
            if (err != XDVD_OK)
                goto done;
        }
    }
done:
    free(d);
    free(order);
    return err;
}

int xdvd_walk(xdvd_t *x, xdvd_visit_fn fn, void *ctx) {
    wchar_t prefix[MAX_REL_PATH];
    prefix[0] = 0;
    return walk_dir(x, x->root_sector, x->root_size, prefix, 0, fn, ctx, 0);
}

/* ------------------------------------------------------------------------ find ---- */

static int dir_lookup(xdvd_t *x, uint32_t sector, uint32_t size, const wchar_t *name, entry_t *out) {
    uint8_t *d = NULL;
    uint32_t *order = NULL, count = 0, i;
    int err = load_dir(x, sector, size, &d);
    if (err != XDVD_OK)
        return err;
    if ((err = dir_order(d, size, &order, &count)) == XDVD_OK) {
        err = XDVD_E_NOFILE;
        for (i = 0; i < count; i++) {
            entry_t e;
            uint32_t l, r;
            if (parse_entry(d, size, order[i], &e, &l, &r) != XDVD_OK)
                continue;
            if (!_wcsicmp(e.name, name)) {
                *out = e;
                err = XDVD_OK;
                break;
            }
        }
    }
    free(d);
    free(order);
    return err;
}

int xdvd_find(xdvd_t *x, const wchar_t *path, uint32_t *sector, uint32_t *size) {
    wchar_t part[256];
    uint32_t cur_sector = x->root_sector, cur_size = x->root_size;
    const wchar_t *p = path;
    for (;;) {
        size_t n = 0;
        entry_t e;
        int err;
        while (p[n] && p[n] != L'\\' && n < 255)
            part[n] = p[n], n++;
        part[n] = 0;
        if (!n)
            return XDVD_E_NOFILE;
        if ((err = dir_lookup(x, cur_sector, cur_size, part, &e)) != XDVD_OK)
            return err;
        p += n;
        if (!*p) {
            if (e.is_dir)
                return XDVD_E_NOFILE;
            *sector = e.sector;
            *size = e.size;
            return XDVD_OK;
        }
        if (!e.is_dir)
            return XDVD_E_NOFILE;
        p++;
        cur_sector = e.sector;
        cur_size = e.size;
    }
}

/* -------------------------------------------------------------------- extraction ---- */

typedef struct count_ctx {
    int64_t total;
    int files;
} count_ctx_t;

static int count_visit(void *ctx, const wchar_t *path, int is_dir, uint32_t sector, uint32_t size) {
    count_ctx_t *c = (count_ctx_t *)ctx;
    (void)path;
    (void)sector;
    if (!is_dir) {
        c->total += size;
        c->files++;
    }
    return 0;
}

int xdvd_total_size(xdvd_t *x, int64_t *total, int *files) {
    count_ctx_t c = {0, 0};
    int err = xdvd_walk(x, count_visit, &c);
    *total = c.total;
    *files = c.files;
    return err;
}

typedef struct extract_ctx {
    xdvd_t *x;
    const wchar_t *dest;
    const volatile LONG *cancel;
    xdvd_progress_fn progress;
    void *user;
    int64_t total, done;
    uint8_t *buf;
    int err;
} extract_ctx_t;

static void make_dirs(const wchar_t *path) {
    wchar_t tmp[MAX_PATH];
    size_t i, n = wcslen(path);
    if (n >= MAX_PATH)
        return;
    memcpy(tmp, path, (n + 1) * sizeof(wchar_t));
    for (i = 3; i < n; i++) /* skip "C:\" */
        if (tmp[i] == L'\\') {
            tmp[i] = 0;
            CreateDirectoryW(tmp, NULL);
            tmp[i] = L'\\';
        }
    CreateDirectoryW(tmp, NULL);
}

static int disk_error(void) {
    DWORD e = GetLastError();
    return e == ERROR_DISK_FULL || e == ERROR_HANDLE_DISK_FULL ? XDVD_E_NOSPACE : XDVD_E_WRITE;
}

static int extract_visit(void *ctx, const wchar_t *path, int is_dir, uint32_t sector,
                         uint32_t size) {
    extract_ctx_t *c = (extract_ctx_t *)ctx;
    wchar_t full[MAX_PATH];
    HANDLE h;
    uint32_t left = size, pos = 0;
    if (_snwprintf(full, MAX_PATH, L"%ls\\%ls", c->dest, path) < 0) {
        c->err = XDVD_E_WRITE;
        return 1;
    }
    full[MAX_PATH - 1] = 0;
    if (c->cancel && *c->cancel) {
        c->err = XDVD_E_CANCEL;
        return 1;
    }
    if (is_dir) {
        if (!CreateDirectoryW(full, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            c->err = disk_error();
            return 1;
        }
        return 0;
    }
    h = CreateFileW(full, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        c->err = disk_error();
        return 1;
    }
    if (c->progress)
        c->progress(c->user, path, c->done, c->total);
    while (left) {
        uint32_t n = left < COPY_CHUNK ? left : COPY_CHUNK;
        DWORD wrote = 0;
        int rerr = xdvd_read(c->x, sector, (int64_t)pos, c->buf, n);
        if (rerr != XDVD_OK) {
            c->err = rerr;
            CloseHandle(h);
            return 1;
        }
        if (!WriteFile(h, c->buf, n, &wrote, NULL) || wrote != n) {
            c->err = disk_error();
            CloseHandle(h);
            return 1;
        }
        pos += n;
        left -= n;
        c->done += n;
        if (c->progress)
            c->progress(c->user, path, c->done, c->total);
        if (c->cancel && *c->cancel) {
            c->err = XDVD_E_CANCEL;
            CloseHandle(h);
            return 1;
        }
    }
    CloseHandle(h);
    return 0;
}

int xdvd_extract(xdvd_t *x, const wchar_t *dest, const volatile LONG *cancel,
                 xdvd_progress_fn progress, void *ctx) {
    extract_ctx_t c;
    int files = 0, err;
    memset(&c, 0, sizeof c);
    c.x = x;
    c.dest = dest;
    c.cancel = cancel;
    c.progress = progress;
    c.user = ctx;
    if ((err = xdvd_total_size(x, &c.total, &files)) != XDVD_OK)
        return err;
    c.buf = (uint8_t *)malloc(COPY_CHUNK);
    if (!c.buf)
        return XDVD_E_NOMEM;
    make_dirs(dest);
    err = xdvd_walk(x, extract_visit, &c);
    free(c.buf);
    if (err == XDVD_E_CANCEL && c.err)
        return c.err; /* the visitor knows the real reason */
    return err;
}
