/*
 * xdvdfs_test.c: tests of the launcher's XDVDFS reader (launcher\xdvdfs.c) with tiny
 * synthetic images built here. No game data. Includes a Redump-style image whose partition
 * starts at 0x18300000 (written as a sparse file) and a few damaged images.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../launcher/xdvdfs.h"

static int g_fail;
#define CHECK(c)                                                                         \
    do {                                                                                 \
        if (!(c)) {                                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                          \
            g_fail++;                                                                    \
        }                                                                                \
    } while (0)

#define SECTORS 70
#define IMG_BYTES (SECTORS * XDVD_SECTOR)

static wchar_t g_tmp[MAX_PATH];

static uint8_t file_byte(uint32_t sector, uint32_t i) { return (uint8_t)(sector * 7 + i * 3); }

static void put16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, uint32_t v) {
    put16(p, v);
    put16(p + 2, v >> 16);
}

/* Writes a directory entry at byte `off` of a directory sector; returns the next free offset. */
static uint32_t put_entry(uint8_t *dir, uint32_t off, uint32_t left_dw, uint32_t right_dw,
                          uint32_t sector, uint32_t size, int is_dir, const char *name) {
    size_t n = strlen(name);
    put16(dir + off, left_dw);
    put16(dir + off + 2, right_dw);
    put32(dir + off + 4, sector);
    put32(dir + off + 8, size);
    dir[off + 12] = is_dir ? 0x10 : 0x20;
    dir[off + 13] = (uint8_t)n;
    memcpy(dir + off + 14, name, n);
    return (uint32_t)((off + 14 + n + 3) & ~3u);
}

/* variant 0: good. 1: tree loop. 2: file called "..". 3: file name with a backslash.
 * Root (byte offsets): media@0, default.xbe@20, empty@48, readme.txt@68; the tree is
 * media(left: default.xbe(right: empty), right: readme.txt). */
static void build_image(uint8_t *img, int variant) {
    uint8_t *desc = img + 32 * XDVD_SECTOR, *root = img + 33 * XDVD_SECTOR;
    uint8_t *media = img + 34 * XDVD_SECTOR;
    const char *def_name = variant == 2 ? ".." : variant == 3 ? "a\\b" : "default.xbe";
    memset(img, 0, IMG_BYTES);
    memcpy(desc, "MICROSOFT*XBOX*MEDIA", 20);
    put32(desc + 0x14, 33);
    put32(desc + 0x18, XDVD_SECTOR);
    memcpy(desc + 0x7EC, "MICROSOFT*XBOX*MEDIA", 20);
    put_entry(root, 0, 5, 17, 34, XDVD_SECTOR, 1, "media");
    put_entry(root, 20, 0, 12, 40, 5000, 0, def_name);
    put_entry(root, 48, 0, 0, 0, 0, 1, "empty");
    put_entry(root, 68, variant == 1 ? 5 : 0, 0, 50, 100, 0, "readme.txt");
    /* media: x.bin (root) with a.txt on the left */
    put_entry(media, 0, 5, 0, 55, 3000, 0, "x.bin");
    put_entry(media, 20, 0, 0, 60, 10, 0, "a.txt");
    /* file data */
    {
        static const struct {
            uint32_t sector, size;
        } files[] = {{40, 5000}, {50, 100}, {55, 3000}, {60, 10}};
        size_t f;
        uint32_t i;
        for (f = 0; f < sizeof files / sizeof files[0]; f++)
            for (i = 0; i < files[f].size; i++)
                img[(size_t)files[f].sector * XDVD_SECTOR + i] = file_byte(files[f].sector, i);
    }
}

static void write_image(const wchar_t *path, const uint8_t *img, int64_t base, int sparse) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER li;
    DWORD n;
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL cannot create %ls\n", path);
        g_fail++;
        return;
    }
    if (sparse)
        DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &n, NULL);
    li.QuadPart = base;
    SetFilePointerEx(h, li, NULL, FILE_BEGIN);
    WriteFile(h, img, IMG_BYTES, &n, NULL);
    CloseHandle(h);
}

typedef struct seen {
    char list[1024];
    int n;
} seen_t;

static int collect(void *ctx, const wchar_t *path, int is_dir, uint32_t sector, uint32_t size) {
    seen_t *s = (seen_t *)ctx;
    (void)sector;
    (void)size;
    s->n++;
    sprintf(s->list + strlen(s->list), "%ls%s;", path, is_dir ? "\\" : "");
    return 0;
}

static int same_file(const wchar_t *path, uint32_t sector, uint32_t size) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    uint8_t *buf = (uint8_t *)malloc(size + 1);
    DWORD got = 0;
    uint32_t i;
    int ok = h != INVALID_HANDLE_VALUE && buf && ReadFile(h, buf, size + 1, &got, NULL) && got == size;
    for (i = 0; ok && i < size; i++)
        ok = buf[i] == file_byte(sector, i);
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
    free(buf);
    return ok;
}

static void join(wchar_t *out, const wchar_t *a, const wchar_t *b) {
    _snwprintf(out, MAX_PATH, L"%ls\\%ls", a, b);
    out[MAX_PATH - 1] = 0;
}

static void test_good(const wchar_t *img_path, int64_t base, const char *label) {
    xdvd_t x;
    seen_t s;
    uint32_t sec = 0, size = 0;
    int64_t total = 0;
    int files = 0;
    wchar_t dest[MAX_PATH], p[MAX_PATH];
    printf("  %s\n", label);
    CHECK(xdvd_open(&x, img_path) == XDVD_OK);
    CHECK(x.base == base);
    memset(&s, 0, sizeof s);
    CHECK(xdvd_walk(&x, collect, &s) == XDVD_OK);
    CHECK(!strcmp(s.list, "default.xbe;empty\\;media\\;media\\a.txt;media\\x.bin;readme.txt;"));
    CHECK(xdvd_total_size(&x, &total, &files) == XDVD_OK && total == 8110 && files == 4);
    CHECK(xdvd_find(&x, L"DEFAULT.XBE", &sec, &size) == XDVD_OK && sec == 40 && size == 5000);
    CHECK(xdvd_find(&x, L"media\\x.bin", &sec, &size) == XDVD_OK && sec == 55 && size == 3000);
    CHECK(xdvd_find(&x, L"media\\none.bin", &sec, &size) == XDVD_E_NOFILE);
    CHECK(xdvd_find(&x, L"media", &sec, &size) == XDVD_E_NOFILE); /* a directory is not a file */
    join(dest, g_tmp, L"out");
    CHECK(xdvd_extract(&x, dest, NULL, NULL, NULL) == XDVD_OK);
    join(p, dest, L"default.xbe");
    CHECK(same_file(p, 40, 5000));
    join(p, dest, L"media\\x.bin");
    CHECK(same_file(p, 55, 3000));
    join(p, dest, L"media\\a.txt");
    CHECK(same_file(p, 60, 10));
    join(p, dest, L"readme.txt");
    CHECK(same_file(p, 50, 100));
    join(p, dest, L"empty");
    CHECK(GetFileAttributesW(p) & FILE_ATTRIBUTE_DIRECTORY);
    xdvd_close(&x);
}

static void remove_tree(const wchar_t *dir) {
    WIN32_FIND_DATAW fd;
    wchar_t pat[MAX_PATH], p[MAX_PATH];
    HANDLE h;
    join(pat, dir, L"*");
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
                continue;
            join(p, dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                remove_tree(p);
            else
                DeleteFileW(p);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

int main(void) {
    static uint8_t img[IMG_BYTES];
    wchar_t path[MAX_PATH];
    xdvd_t x;
    seen_t s;
    volatile LONG cancel = 1;
    wchar_t base[MAX_PATH];
    int v;
    GetTempPathW(MAX_PATH, base);
    _snwprintf(g_tmp, MAX_PATH, L"%lsngb-xdvdfs-test-%lu", base, GetCurrentProcessId());
    g_tmp[MAX_PATH - 1] = 0;
    CreateDirectoryW(g_tmp, NULL);

    printf("xdvdfs_test\n");
    build_image(img, 0);
    join(path, g_tmp, L"plain.iso");
    write_image(path, img, 0, 0);
    test_good(path, 0, "plain XISO (offset 0)");
    remove_tree((join(path, g_tmp, L"out"), path));

    join(path, g_tmp, L"redump.iso");
    write_image(path, img, XDVD_REDUMP_OFFSET, 1);
    test_good(path, XDVD_REDUMP_OFFSET, "Redump image (offset 0x18300000, sparse)");
    DeleteFileW(path);

    /* cancel: the first file notices the flag */
    join(path, g_tmp, L"plain.iso");
    CHECK(xdvd_open(&x, path) == XDVD_OK);
    join(base, g_tmp, L"out2");
    CHECK(xdvd_extract(&x, base, &cancel, NULL, NULL) == XDVD_E_CANCEL);
    xdvd_close(&x);

    /* damaged images */
    for (v = 1; v <= 3; v++) {
        build_image(img, v);
        join(path, g_tmp, L"bad.iso");
        write_image(path, img, 0, 0);
        CHECK(xdvd_open(&x, path) == XDVD_OK);
        memset(&s, 0, sizeof s);
        printf("  damaged variant %d\n", v);
        CHECK(xdvd_walk(&x, collect, &s) == XDVD_E_CORRUPT);
        xdvd_close(&x);
    }
    /* not an Xbox image */
    memset(img, 0x41, IMG_BYTES);
    join(path, g_tmp, L"junk.iso");
    write_image(path, img, 0, 0);
    CHECK(xdvd_open(&x, path) == XDVD_E_NOT_XISO);
    join(path, g_tmp, L"missing.iso");
    CHECK(xdvd_open(&x, path) == XDVD_E_OPEN);
    /* descriptor fine but the root directory lies outside the file */
    build_image(img, 0);
    put32(img + 32 * XDVD_SECTOR + 0x14, 5000);
    join(path, g_tmp, L"short.iso");
    write_image(path, img, 0, 0);
    CHECK(xdvd_open(&x, path) == XDVD_E_CORRUPT);

    remove_tree(g_tmp);
    if (g_fail) {
        printf("xdvdfs_test: %d FAILURES\n", g_fail);
        return 1;
    }
    printf("xdvdfs_test: all passed\n");
    return 0;
}
