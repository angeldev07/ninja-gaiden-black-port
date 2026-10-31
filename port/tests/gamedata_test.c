/*
 * gamedata_test.c: the launcher's AFS, XPR0 and sprite-table readers (launcher\gamedata.c) on
 * synthetic files and buffers built here. No game data. Damaged inputs must fail cleanly.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../launcher/gamedata.h"

static int g_fail;
#define CHECK(c)                                                                         \
    do {                                                                                 \
        if (!(c)) {                                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                          \
            g_fail++;                                                                    \
        }                                                                                \
    } while (0)

static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void put16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static void putf(uint8_t *p, float v) { memcpy(p, &v, 4); }

static wchar_t g_dir[MAX_PATH];

static void write_file(const wchar_t *name, const uint8_t *p, size_t n, wchar_t *out) {
    FILE *f;
    _snwprintf(out, MAX_PATH, L"%ls\\%ls", g_dir, name);
    f = _wfopen(out, L"wb");
    fwrite(p, 1, n, f);
    fclose(f);
}

/* AFS with two entries "a.bin" (0x800 bytes of 0xAA) and "titlebg.spr" (16 bytes 0..15). */
static size_t make_afs(uint8_t *b, size_t cap) {
    size_t attr = 0x2000, i;
    memset(b, 0, cap);
    memcpy(b, "AFS\0", 4);
    put32(b + 4, 2);
    put32(b + 8, 0x800);
    put32(b + 12, 0x800);
    put32(b + 16, 0x1000);
    put32(b + 20, 16);
    put32(b + 24, (uint32_t)attr); /* attribute table pointer at 8 + 8N */
    put32(b + 28, 2 * 0x30);
    memset(b + 0x800, 0xAA, 0x800);
    for (i = 0; i < 16; i++)
        b[0x1000 + i] = (uint8_t)i;
    memcpy(b + attr, "a.bin", 5);
    memcpy(b + attr + 0x30, "titlebg.spr", 11);
    return attr + 2 * 0x30;
}

/* XPR0: USERDATA (2 sprite records), texture 0 (P8 1024x512) + palette, texture 1 (A8 4x4). */
static size_t make_xpr(uint8_t *b, size_t cap) {
    size_t p = 12, header_size = 0x100, total;
    memset(b, 0, cap);
    memcpy(b, "XPR0", 4);
    /* USERDATA */
    put32(b + p, 0x80000000u);
    put32(b + p + 4, 48);
    putf(b + p + 8, 74.0f / 1024.0f);
    putf(b + p + 12, 0.0f);
    putf(b + p + 16, 1014.0f / 1024.0f);
    putf(b + p + 20, 480.0f / 512.0f);
    put32(b + p + 24, 0);
    put16(b + p + 28, 1024);
    put16(b + p + 30, 512);
    putf(b + p + 32, 0.0f);
    putf(b + p + 36, 0.5f);
    putf(b + p + 40, 0.25f);
    putf(b + p + 44, 1.0f);
    put32(b + p + 48, 1);
    put16(b + p + 52, 4);
    put16(b + p + 54, 4);
    p += 8 + 48;
    /* push buffer header (24 bytes) that must be skipped */
    put32(b + p, 0x00020001u);
    p += 24;
    /* texture 0: P8 (0x0B), log2 w 10, log2 h 9, 1 level, 2D */
    put32(b + p, 0x00040001u);
    put32(b + p + 4, 0);
    put32(b + p + 12, (9u << 24) | (10u << 20) | (1u << 16) | (0x0Bu << 8) | (2u << 4));
    p += 20;
    /* palette (256 entries) at Data 0x40 */
    put32(b + p, 0x00030001u);
    put32(b + p + 4, 0x40);
    p += 12;
    /* texture 1: A8 (0x19) 4x4 at Data 0x440 */
    put32(b + p, 0x00040001u);
    put32(b + p + 4, 0x440);
    put32(b + p + 12, (2u << 24) | (2u << 20) | (1u << 16) | (0x19u << 8) | (2u << 4));
    p += 20;
    put32(b + p, 0xFFFFFFFFu);
    total = header_size + 0x440 + 16;
    put32(b + 4, (uint32_t)total);
    put32(b + 8, (uint32_t)header_size);
    return total;
}

int main(void) {
    static uint8_t afs[0x3000], xpr[0x1000];
    wchar_t path[MAX_PATH], bad[MAX_PATH];
    uint64_t off = 0;
    uint32_t size = 0;
    size_t n, len = 0;
    uint8_t *e;
    xpr_tex_t t;
    spr_rect_t r;

    GetTempPathW(MAX_PATH, g_dir);
    wcscat(g_dir, L"ngb_gamedata_test");
    CreateDirectoryW(g_dir, NULL);

    /* AFS: lookup by name, missing name, damaged tables. */
    n = make_afs(afs, sizeof afs);
    write_file(L"ok.afs", afs, n, path);
    CHECK(afs_find(path, "titlebg.spr", &off, &size) && off == 0x1000 && size == 16);
    CHECK(afs_find(path, "a.bin", &off, &size) && off == 0x800 && size == 0x800);
    CHECK(!afs_find(path, "demo.spr", &off, &size));
    CHECK(!afs_find(path, "titlebg", &off, &size)); /* exact names only */
    e = gamedata_load(path, "titlebg.spr", &len);
    CHECK(e && len == 16 && e[15] == 15);
    free(e);
    write_file(L"short.afs", afs, 20, bad); /* TOC cut short */
    CHECK(!afs_find(bad, "a.bin", &off, &size));
    make_afs(afs, sizeof afs);
    put32(afs + 24, 0x7fff0000u); /* attribute table past EOF */
    write_file(L"attr.afs", afs, n, bad);
    CHECK(!afs_find(bad, "a.bin", &off, &size));
    make_afs(afs, sizeof afs);
    put32(afs + 4, 0x7fffffffu); /* absurd entry count */
    write_file(L"count.afs", afs, n, bad);
    CHECK(!afs_find(bad, "a.bin", &off, &size));
    make_afs(afs, sizeof afs);
    put32(afs + 12, 0x7ffff000u); /* entry size runs past EOF */
    write_file(L"entry.afs", afs, n, bad);
    CHECK(!gamedata_load(bad, "a.bin", &len));
    memcpy(afs, "XPR0", 4);
    write_file(L"magic.afs", afs, n, bad);
    CHECK(!afs_find(bad, "a.bin", &off, &size));

    /* XPR0: textures by ordinal, palette after a P8, push buffer skipped. */
    n = make_xpr(xpr, sizeof xpr);
    CHECK(xpr_texture(xpr, n, 0, &t));
    CHECK(((t.format >> 8) & 0xFF) == 0x0B && t.data == xpr + 0x100);
    CHECK(t.palette == xpr + 0x140 && t.palette_len == 1024);
    CHECK(xpr_texture(xpr, n, 1, &t));
    CHECK(((t.format >> 8) & 0xFF) == 0x19 && t.data == xpr + 0x540 && !t.palette);
    CHECK(t.data_len == 16);
    CHECK(!xpr_texture(xpr, n, 2, &t));
    CHECK(!xpr_texture(xpr, 8, 0, &t)); /* truncated */
    make_xpr(xpr, sizeof xpr);
    put32(xpr + 0x5C + 4, 0x7ffffff0u); /* texture 0 Data far outside */
    CHECK(!xpr_texture(xpr, n, 0, &t));
    make_xpr(xpr, sizeof xpr);
    put32(xpr + 12 + 4, 0x10000); /* USERDATA size runs past header_size */
    CHECK(!xpr_texture(xpr, n, 0, &t));
    CHECK(!spr_sprite(xpr, n, 0, &r));

    /* Sprite table: rects in pixels of each record's texture. */
    make_xpr(xpr, sizeof xpr);
    CHECK(spr_sprite(xpr, n, 0, &r));
    CHECK(r.tex == 0 && r.x0 == 74 && r.y0 == 0 && r.x1 == 1014 && r.y1 == 480);
    CHECK(spr_sprite(xpr, n, 1, &r));
    CHECK(r.tex == 1 && r.x0 == 0 && r.y0 == 2 && r.x1 == 1 && r.y1 == 4);
    CHECK(!spr_sprite(xpr, n, 2, &r));

    printf("gamedata_test: %s\n", g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}
