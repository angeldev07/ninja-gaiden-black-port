/*
 * glpng.c: RGBA8 PNG with uncompressed deflate (see glpng.h).
 *
 * Structure: signature, IHDR (8 bits, color type 6), one IDAT with a zlib stream (header
 * 0x78 0x01, "stored" blocks of up to 65535 bytes, Adler-32 at the end) and IEND. Each row
 * is preceded by the filter byte 0. CRC-32 of each chunk with the polynomial 0xEDB88320.
 * Specification: https://www.w3.org/TR/png-3/ and RFC 1950/1951.
 */
#include "glpng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STORED_MAX 65535u

static uint32_t crc_update(uint32_t crc, const uint8_t *data, size_t len) {
    static uint32_t table[256];
    static int ready;
    size_t i;
    if (!ready) {
        uint32_t n;
        for (n = 0; n < 256; n++) {
            uint32_t c = n;
            int k;
            for (k = 0; k < 8; k++) {
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[n] = c;
        }
        ready = 1;
    }
    for (i = 0; i < len; i++) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int write_chunk(FILE *f, const char *type, const uint8_t *data, size_t len) {
    uint8_t head[8], tail[4];
    uint32_t crc;
    put_u32(head, (uint32_t)len);
    memcpy(head + 4, type, 4);
    crc = crc_update(0xFFFFFFFFu, head + 4, 4);
    crc = crc_update(crc, data, len) ^ 0xFFFFFFFFu;
    put_u32(tail, crc);
    return fwrite(head, 1, 8, f) == 8 && (len == 0 || fwrite(data, 1, len, f) == len) &&
           fwrite(tail, 1, 4, f) == 4;
}

int glb_write_png(const char *path, int width, int height, const uint8_t *rgba) {
    size_t row, raw_len, blocks, zlen, pos, done;
    uint8_t *raw, *z;
    uint8_t ihdr[13];
    uint32_t a = 1, b = 0;
    FILE *f;
    int ok, y;
    if (!path || !rgba || width < 1 || height < 1) {
        return -1;
    }
    row = (size_t)width * 4;
    raw_len = (row + 1) * (size_t)height;
    blocks = (raw_len + STORED_MAX - 1) / STORED_MAX;
    zlen = 2 + raw_len + 5 * blocks + 4;
    raw = (uint8_t *)malloc(raw_len);
    z = (uint8_t *)malloc(zlen);
    if (!raw || !z) {
        free(raw);
        free(z);
        return -1;
    }
    for (y = 0; y < height; y++) {
        raw[(row + 1) * (size_t)y] = 0; /* filter 0 */
        memcpy(raw + (row + 1) * (size_t)y + 1, rgba + row * (size_t)y, row);
    }
    z[0] = 0x78;
    z[1] = 0x01;
    pos = 2;
    for (done = 0; done < raw_len;) {
        size_t n = raw_len - done < STORED_MAX ? raw_len - done : STORED_MAX;
        z[pos++] = (uint8_t)(done + n == raw_len); /* BFINAL on the last one, BTYPE 00 */
        z[pos++] = (uint8_t)(n & 0xFFu);
        z[pos++] = (uint8_t)(n >> 8);
        z[pos++] = (uint8_t)(~n & 0xFFu);
        z[pos++] = (uint8_t)((~n >> 8) & 0xFFu);
        memcpy(z + pos, raw + done, n);
        pos += n;
        done += n;
    }
    for (done = 0; done < raw_len; done++) {
        a = (a + raw[done]) % 65521u;
        b = (b + a) % 65521u;
    }
    put_u32(z + pos, (b << 16) | a);
    pos += 4;
    put_u32(ihdr, (uint32_t)width);
    put_u32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8;  /* bits per channel */
    ihdr[9] = 6;  /* RGBA */
    ihdr[10] = 0; /* deflate */
    ihdr[11] = 0; /* adaptive filter (all rows with 0) */
    ihdr[12] = 0; /* no interlacing */
    f = fopen(path, "wb");
    ok = f != NULL;
    if (ok) {
        static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        ok = fwrite(signature, 1, 8, f) == 8 && write_chunk(f, "IHDR", ihdr, sizeof ihdr) &&
             write_chunk(f, "IDAT", z, pos) && write_chunk(f, "IEND", NULL, 0);
        ok = fclose(f) == 0 && ok;
    }
    free(raw);
    free(z);
    return ok ? 0 : -1;
}
