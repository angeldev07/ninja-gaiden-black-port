/*
 * png.c: minimal PNG writer (8-bit RGB, zlib with "stored" blocks, no compression).
 * Enough to dump the color buffer; spec: RFC 2083 (PNG) and RFC 1950/1951.
 */
#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];

static void crc_init(void) {
    uint32_t n, k, c;
    for (n = 0; n < 256; n++) {
        c = n;
        for (k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        crc = crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int write_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t hdr[8], crc[4];
    uint32_t c;
    put_be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    c = crc_update(0xFFFFFFFFu, hdr + 4, 4);
    c = crc_update(c, data, len) ^ 0xFFFFFFFFu;
    put_be32(crc, c);
    return fwrite(hdr, 1, 8, f) == 8 && (!len || fwrite(data, 1, len, f) == len) &&
           fwrite(crc, 1, 4, f) == 4;
}

int png_write_rgb(const char *path, uint32_t width, uint32_t height, const uint8_t *rgb) {
    static int crc_ready;
    uint64_t raw_len = (uint64_t)(width * 3u + 1u) * height;
    uint64_t nblocks = raw_len / 65535u + 1u;
    uint64_t z_len = 2u + nblocks * 5u + raw_len + 4u;
    uint8_t *z, *p, ihdr[13];
    uint32_t a = 1, b = 0, y, x;
    uint64_t done = 0;
    FILE *f;
    int ok;
    if (!width || !height || width > 8192 || height > 8192)
        return -1;
    if (!crc_ready) {
        crc_init();
        crc_ready = 1;
    }
    z = (uint8_t *)malloc((size_t)z_len);
    if (!z)
        return -1;
    p = z;
    *p++ = 0x78; /* zlib: deflate, 32 KB window */
    *p++ = 0x01;
    for (y = 0; y < height; y++) {
        uint32_t row = width * 3u + 1u;
        for (x = 0; x < row; x++) {
            uint8_t v = x ? rgb[(size_t)y * width * 3u + x - 1u] : 0; /* filter 0 */
            if (done % 65535u == 0) {
                uint64_t left = raw_len - done;
                uint16_t len = (uint16_t)(left < 65535u ? left : 65535u);
                *p++ = left <= 65535u ? 1 : 0; /* BFINAL on the last block, BTYPE 00 */
                *p++ = (uint8_t)len;
                *p++ = (uint8_t)(len >> 8);
                *p++ = (uint8_t)~len;
                *p++ = (uint8_t)(~len >> 8);
            }
            *p++ = v;
            a = (a + v) % 65521u;
            b = (b + a) % 65521u;
            done++;
        }
    }
    put_be32(p, (b << 16) | a);
    p += 4;
    put_be32(ihdr, width);
    put_be32(ihdr + 4, height);
    ihdr[8] = 8;  /* bits per channel */
    ihdr[9] = 2;  /* RGB */
    ihdr[10] = 0; /* deflate */
    ihdr[11] = 0; /* standard filters */
    ihdr[12] = 0; /* no interlacing */
    if (fopen_s(&f, path, "wb") || !f) {
        free(z);
        return -1;
    }
    ok = fwrite("\x89PNG\r\n\x1a\n", 1, 8, f) == 8 && write_chunk(f, "IHDR", ihdr, 13) &&
         write_chunk(f, "IDAT", z, (uint32_t)(p - z)) && write_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(z);
    return ok ? 0 : -1;
}
