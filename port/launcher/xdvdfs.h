/*
 * xdvdfs.h: a small read-only XDVDFS (Xbox DVD file system) reader and extractor.
 *
 * Supports a plain XISO (partition at byte 0) and a Redump image (the game partition
 * starts at byte 0x18300000). The partition is recognised by the "MICROSOFT*XBOX*MEDIA"
 * magic at sector 32 of the partition. Directories are binary search trees stored in
 * 2048-byte sectors; every entry is validated, so a damaged image fails with
 * XDVD_E_CORRUPT instead of looping or writing outside the destination folder.
 */
#pragma once

#include <stdint.h>
#include <wchar.h>

#include <windows.h>

#define XDVD_SECTOR 2048
#define XDVD_REDUMP_OFFSET 0x18300000LL

enum xdvd_err {
    XDVD_OK = 0,
    XDVD_E_OPEN,     /* the image could not be opened */
    XDVD_E_NOT_XISO, /* no XDVDFS volume descriptor found */
    XDVD_E_READ,     /* read error or image shorter than its own tables */
    XDVD_E_CORRUPT,  /* inconsistent directory tables */
    XDVD_E_WRITE,    /* could not create or write a destination file */
    XDVD_E_NOSPACE,  /* destination disk is full */
    XDVD_E_CANCEL,   /* the caller asked to stop */
    XDVD_E_NOMEM,
    XDVD_E_NOFILE    /* xdvd_find: the file is not in the image */
};

typedef struct xdvd {
    HANDLE file;
    int64_t base;        /* byte offset of the partition inside the image */
    int64_t image_size;  /* size of the image file */
    uint32_t root_sector;
    uint32_t root_size;
} xdvd_t;

/* Visits every directory and file (a directory comes before its contents). `path` is
 * relative to the root, with backslashes. Return nonzero to stop (XDVD_E_CANCEL). */
typedef int (*xdvd_visit_fn)(void *ctx, const wchar_t *path, int is_dir, uint32_t sector,
                             uint32_t size);

/* Called while extracting: `file` is the entry being written, `done`/`total` are bytes. */
typedef void (*xdvd_progress_fn)(void *ctx, const wchar_t *file, int64_t done, int64_t total);

/* Opens an image trying the partition offsets 0 and 0x18300000. */
int xdvd_open(xdvd_t *x, const wchar_t *path);
/* Same, with the partition offset given by the caller (used by tests). */
int xdvd_open_at(xdvd_t *x, const wchar_t *path, int64_t base);
void xdvd_close(xdvd_t *x);

/* Reads n bytes at partition sector `sector`, byte `skip` inside it. */
int xdvd_read(xdvd_t *x, uint32_t sector, int64_t skip, void *buf, uint32_t n);

int xdvd_walk(xdvd_t *x, xdvd_visit_fn fn, void *ctx);

/* Finds a file by its path inside the image (backslashes, case-insensitive). */
int xdvd_find(xdvd_t *x, const wchar_t *path, uint32_t *sector, uint32_t *size);

/* Extracts the whole tree below `dest` (created if missing). `cancel` is polled between
 * chunks. On failure the files written so far stay where they are. */
int xdvd_extract(xdvd_t *x, const wchar_t *dest, const volatile LONG *cancel,
                 xdvd_progress_fn progress, void *ctx);

/* Total bytes of all files, for progress bars and free-space checks. */
int xdvd_total_size(xdvd_t *x, int64_t *total, int *files);
