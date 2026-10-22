/*
 * import.c: disc image import (background thread) and XBE validation. See import.h.
 */
#define UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#include "import.h"
#include "xdvdfs.h"

#define XBE_HEADER_READ 4096
#define SPARE_BYTES (256LL * 1024 * 1024) /* free space to leave besides the game itself */

/* ------------------------------------------------------------------ XBE header ---- */

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int has_word_ci(const wchar_t *s, const wchar_t *word) {
    size_t n = wcslen(word), i, j;
    for (i = 0; s[i]; i++) {
        for (j = 0; j < n; j++)
            if (!s[i + j] || towupper(s[i + j]) != towupper(word[j]))
                break;
        if (j == n)
            return 1;
    }
    return 0;
}

/* The XBE header starts with "XBEH"; the base address is at 0x104 and the certificate
 * address at 0x118. The certificate holds the title id (+8) and the title name (+0x0C,
 * UTF-16, 40 characters). */
int xbe_probe_header(const uint8_t *hdr, uint32_t n, xbe_info_t *out) {
    uint32_t base, cert, off, i, len = 0;
    out->valid = 0;
    out->is_ngb = 0;
    out->title[0] = 0;
    if (n < 0x11C || memcmp(hdr, "XBEH", 4) != 0)
        return 0;
    base = rd32(hdr + 0x104);
    cert = rd32(hdr + 0x118);
    if (cert < base)
        return 0;
    off = cert - base;
    if ((uint64_t)off + 0x0C + 80 > n)
        return 0;
    for (i = 0; i < 40; i++) {
        wchar_t ch = (wchar_t)(hdr[off + 0x0C + 2 * i] | (hdr[off + 0x0C + 2 * i + 1] << 8));
        if (!ch)
            break;
        out->title[len++] = ch;
    }
    while (len && out->title[len - 1] == L' ')
        len--;
    out->title[len] = 0;
    out->valid = 1;
    out->is_ngb = has_word_ci(out->title, L"GAIDEN") && has_word_ci(out->title, L"BLACK");
    return 1;
}

int xbe_probe_file(const wchar_t *path, xbe_info_t *out) {
    uint8_t buf[XBE_HEADER_READ];
    DWORD got = 0;
    LARGE_INTEGER sz;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    memset(out, 0, sizeof *out);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    if (GetFileSizeEx(h, &sz))
        out->size = sz.QuadPart;
    if (!ReadFile(h, buf, sizeof buf, &got, NULL)) {
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    xbe_probe_header(buf, got, out);
    return 1;
}

/* --------------------------------------------------------------------- folders ---- */

void make_dirs(const wchar_t *path) {
    wchar_t tmp[MAX_PATH];
    size_t i, n = wcslen(path);
    if (n >= MAX_PATH)
        return;
    memcpy(tmp, path, (n + 1) * sizeof(wchar_t));
    for (i = 3; i < n; i++)
        if (tmp[i] == L'\\') {
            tmp[i] = 0;
            CreateDirectoryW(tmp, NULL);
            tmp[i] = L'\\';
        }
    CreateDirectoryW(tmp, NULL);
}

void remove_tree(const wchar_t *dir) {
    WIN32_FIND_DATAW fd;
    wchar_t pat[MAX_PATH], p[MAX_PATH];
    HANDLE h;
    _snwprintf(pat, MAX_PATH, L"%ls\\*", dir);
    pat[MAX_PATH - 1] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
                continue;
            _snwprintf(p, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
            p[MAX_PATH - 1] = 0;
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                remove_tree(p);
            else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                RemoveDirectoryW(p); /* a link: remove the link, never what it points to */
            else {
                SetFileAttributesW(p, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(p);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

/* ---------------------------------------------------------------------- worker ---- */

static struct {
    CRITICAL_SECTION cs;
    int cs_ready;
    volatile LONG state, phase, err, cancel;
    HANDLE thread;
    wchar_t iso[MAX_PATH], data[MAX_PATH], tmp[MAX_PATH], final_dir[MAX_PATH];
    volatile LONGLONG done, total;
    wchar_t current[260], detail[64];
    double need_gb;
    ULONGLONG t0, t_end;
} g;

static void set_error(int err) {
    InterlockedExchange(&g.err, err);
    InterlockedExchange(&g.state, IMP_ERROR);
}

static int map_xdvd_error(int e) {
    switch (e) {
    case XDVD_E_OPEN: return IMPE_OPEN;
    case XDVD_E_NOT_XISO: return IMPE_NOT_XISO;
    case XDVD_E_READ: return IMPE_READ;
    case XDVD_E_CORRUPT: return IMPE_CORRUPT;
    case XDVD_E_WRITE: return IMPE_WRITE;
    case XDVD_E_NOSPACE: return IMPE_NOSPACE;
    case XDVD_E_CANCEL: return IMPE_CANCELLED;
    default: return IMPE_INTERNAL;
    }
}

static void on_progress(void *ctx, const wchar_t *file, int64_t done, int64_t total) {
    (void)ctx;
    g.done = done;
    g.total = total;
    EnterCriticalSection(&g.cs);
    wcsncpy(g.current, file, 259);
    g.current[259] = 0;
    LeaveCriticalSection(&g.cs);
}

static DWORD WINAPI import_thread(LPVOID arg) {
    xdvd_t x;
    uint32_t sec = 0, size = 0;
    uint8_t hdr[XBE_HEADER_READ];
    xbe_info_t info;
    int64_t total = 0;
    int files = 0, e;
    ULARGE_INTEGER freeb, dummy1, dummy2;
    (void)arg;
    InterlockedExchange(&g.phase, IMPH_SCAN);
    if ((e = xdvd_open(&x, g.iso)) != XDVD_OK) {
        set_error(map_xdvd_error(e));
        return 0;
    }
    /* Fail early, before copying gigabytes: is it a game, and is it the right one? */
    if (xdvd_find(&x, L"default.xbe", &sec, &size) != XDVD_OK) {
        xdvd_close(&x);
        set_error(IMPE_NO_XBE);
        return 0;
    }
    memset(hdr, 0, sizeof hdr);
    if (xdvd_read(&x, sec, 0, hdr, size < sizeof hdr ? size : (uint32_t)sizeof hdr) != XDVD_OK) {
        xdvd_close(&x);
        set_error(IMPE_READ);
        return 0;
    }
    xbe_probe_header(hdr, size < sizeof hdr ? size : (uint32_t)sizeof hdr, &info);
    if (!info.valid) {
        xdvd_close(&x);
        set_error(IMPE_XBE_BAD);
        return 0;
    }
    if (!info.is_ngb) {
        wcsncpy(g.detail, info.title, 63);
        g.detail[63] = 0;
        xdvd_close(&x);
        set_error(IMPE_NOT_NGB);
        return 0;
    }
    if ((e = xdvd_total_size(&x, &total, &files)) != XDVD_OK) {
        xdvd_close(&x);
        set_error(map_xdvd_error(e));
        return 0;
    }
    g.total = total;
    make_dirs(g.data);
    if (GetDiskFreeSpaceExW(g.data, &freeb, &dummy1, &dummy2) &&
        (int64_t)freeb.QuadPart < total + SPARE_BYTES) {
        g.need_gb = (double)(total + SPARE_BYTES) / (1024.0 * 1024.0 * 1024.0);
        xdvd_close(&x);
        set_error(IMPE_NOSPACE);
        return 0;
    }
    if (g.cancel) {
        xdvd_close(&x);
        set_error(IMPE_CANCELLED);
        return 0;
    }
    InterlockedExchange(&g.phase, IMPH_EXTRACT);
    remove_tree(g.tmp); /* leftovers of an interrupted import */
    e = xdvd_extract(&x, g.tmp, &g.cancel, on_progress, NULL);
    xdvd_close(&x);
    if (e != XDVD_OK) {
        remove_tree(g.tmp);
        if (e == XDVD_E_NOSPACE)
            g.need_gb = (double)(total + SPARE_BYTES) / (1024.0 * 1024.0 * 1024.0);
        set_error(map_xdvd_error(e));
        return 0;
    }
    InterlockedExchange(&g.phase, IMPH_FINISH);
    remove_tree(g.final_dir); /* the user confirmed replacing an older copy */
    if (!MoveFileExW(g.tmp, g.final_dir, MOVEFILE_WRITE_THROUGH)) {
        remove_tree(g.tmp);
        set_error(IMPE_WRITE);
        return 0;
    }
    g.t_end = GetTickCount64();
    InterlockedExchange(&g.state, IMP_DONE);
    return 0;
}

int import_start_iso(const wchar_t *iso, const wchar_t *data_dir) {
    if (!g.cs_ready) {
        InitializeCriticalSection(&g.cs);
        g.cs_ready = 1;
    }
    if (g.state == IMP_RUNNING)
        return 0;
    if (g.thread) {
        WaitForSingleObject(g.thread, INFINITE);
        CloseHandle(g.thread);
        g.thread = NULL;
    }
    memset(&g.iso, 0, sizeof g.iso);
    wcsncpy(g.iso, iso, MAX_PATH - 1);
    wcsncpy(g.data, data_dir, MAX_PATH - 1);
    _snwprintf(g.tmp, MAX_PATH, L"%ls\\game.importing", data_dir);
    g.tmp[MAX_PATH - 1] = 0;
    _snwprintf(g.final_dir, MAX_PATH, L"%ls\\game", data_dir);
    g.final_dir[MAX_PATH - 1] = 0;
    g.done = 0;
    g.total = 0;
    g.current[0] = 0;
    g.detail[0] = 0;
    g.need_gb = 0;
    g.cancel = 0;
    g.err = IMPE_NONE;
    g.phase = IMPH_SCAN;
    g.t0 = GetTickCount64();
    g.state = IMP_RUNNING;
    g.thread = CreateThread(NULL, 0, import_thread, NULL, 0, NULL);
    if (!g.thread) {
        set_error(IMPE_INTERNAL);
        return 0;
    }
    return 1;
}

void import_cancel(void) { InterlockedExchange(&g.cancel, 1); }

void import_status(imp_status_t *out) {
    memset(out, 0, sizeof *out);
    out->state = (int)g.state;
    out->phase = (int)g.phase;
    out->err = (int)g.err;
    out->done = g.done;
    out->total = g.total;
    out->need_gb = g.need_gb;
    wcsncpy(out->detail, g.detail, 63);
    if (g.cs_ready) {
        EnterCriticalSection(&g.cs);
        wcsncpy(out->current, g.current, 259);
        LeaveCriticalSection(&g.cs);
    }
    out->elapsed_ms = (unsigned long)((g.state == IMP_RUNNING ? GetTickCount64() : g.t_end) - g.t0);
}

void import_reset(void) {
    if (g.thread) {
        WaitForSingleObject(g.thread, INFINITE);
        CloseHandle(g.thread);
        g.thread = NULL;
    }
    g.state = IMP_IDLE;
}
