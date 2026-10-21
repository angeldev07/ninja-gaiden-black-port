/*
 * sha1.c: SHA-1 (FIPS 180-4) y HMAC-SHA1 (RFC 2104). Ver sha1.h.
 */
#include "kernel/sha1.h"

#include <string.h>

static uint32_t rol(uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}

static void transform(uint32_t state[5], const uint8_t block[NGB_SHA1_BLOCK]) {
    uint32_t w[80], a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    int i;
    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 |
               (uint32_t)block[i * 4 + 2] << 8 | block[i * 4 + 3];
    for (; i < 80; i++)
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (i = 0; i < 80; i++) {
        uint32_t f, k, t;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        t = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = t;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void ngb_sha1_init(ngb_sha1_t *c) {
    c->state[0] = 0x67452301u;
    c->state[1] = 0xEFCDAB89u;
    c->state[2] = 0x98BADCFEu;
    c->state[3] = 0x10325476u;
    c->state[4] = 0xC3D2E1F0u;
    c->count_lo = c->count_hi = 0;
}

void ngb_sha1_update(ngb_sha1_t *c, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    size_t used = c->count_lo % NGB_SHA1_BLOCK;
    uint32_t old = c->count_lo;
    c->count_lo += (uint32_t)len;
    if (c->count_lo < old)
        c->count_hi++;
    while (len) {
        size_t n = NGB_SHA1_BLOCK - used;
        if (n > len)
            n = len;
        memcpy(c->buffer + used, p, n);
        used += n;
        p += n;
        len -= n;
        if (used == NGB_SHA1_BLOCK) {
            transform(c->state, c->buffer);
            used = 0;
        }
    }
}

void ngb_sha1_final(ngb_sha1_t *c, uint8_t digest[NGB_SHA1_DIGEST]) {
    uint8_t pad[NGB_SHA1_BLOCK + 8] = {0x80};
    uint8_t bits[8];
    uint32_t hi = c->count_hi << 3 | c->count_lo >> 29, lo = c->count_lo << 3;
    size_t used = c->count_lo % NGB_SHA1_BLOCK, padlen = used < 56 ? 56 - used : 120 - used;
    int i;
    for (i = 0; i < 4; i++) {
        bits[i] = (uint8_t)(hi >> (24 - i * 8));
        bits[4 + i] = (uint8_t)(lo >> (24 - i * 8));
    }
    ngb_sha1_update(c, pad, padlen);
    ngb_sha1_update(c, bits, 8);
    for (i = 0; i < NGB_SHA1_DIGEST; i++)
        digest[i] = (uint8_t)(c->state[i / 4] >> (24 - (i % 4) * 8));
}

void ngb_hmac_sha1(const void *key, size_t key_len, const void *data1, size_t len1,
                   const void *data2, size_t len2, uint8_t digest[NGB_SHA1_DIGEST]) {
    uint8_t k[NGB_SHA1_BLOCK] = {0}, pad[NGB_SHA1_BLOCK], inner[NGB_SHA1_DIGEST];
    ngb_sha1_t c;
    int i;
    if (key_len > NGB_SHA1_BLOCK) { /* claves largas: se usa su hash */
        ngb_sha1_init(&c);
        ngb_sha1_update(&c, key, key_len);
        ngb_sha1_final(&c, k);
    } else if (key_len) {
        memcpy(k, key, key_len);
    }
    for (i = 0; i < NGB_SHA1_BLOCK; i++)
        pad[i] = k[i] ^ 0x36;
    ngb_sha1_init(&c);
    ngb_sha1_update(&c, pad, NGB_SHA1_BLOCK);
    if (len1)
        ngb_sha1_update(&c, data1, len1);
    if (len2)
        ngb_sha1_update(&c, data2, len2);
    ngb_sha1_final(&c, inner);
    for (i = 0; i < NGB_SHA1_BLOCK; i++)
        pad[i] = k[i] ^ 0x5C;
    ngb_sha1_init(&c);
    ngb_sha1_update(&c, pad, NGB_SHA1_BLOCK);
    ngb_sha1_update(&c, inner, NGB_SHA1_DIGEST);
    ngb_sha1_final(&c, digest);
}
