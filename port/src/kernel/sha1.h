/*
 * sha1.h: SHA-1 (FIPS 180-4) and HMAC-SHA1 (RFC 2104), pure code with no dependencies.
 *
 * Used by XcSHAInit/XcSHAUpdate/XcSHAFinal and XcHMAC (impl_misc.c): the game signs and
 * verifies its save games with them. The context lives in game memory
 * (a 116-byte A_SHA_CTX on the Xbox), so it has to fit there.
 */
#ifndef NGB_SHA1_H
#define NGB_SHA1_H

#include <stddef.h>
#include <stdint.h>

#define NGB_SHA1_DIGEST 20
#define NGB_SHA1_BLOCK 64

typedef struct ngb_sha1 {
    uint32_t state[5];
    uint32_t count_lo, count_hi; /* length in bytes */
    uint8_t buffer[NGB_SHA1_BLOCK];
} ngb_sha1_t;

/* It has to fit in the 116-byte A_SHA_CTX that the game reserves. */
typedef char ngb_sha1_fits_xbox_ctx[sizeof(ngb_sha1_t) <= 116 ? 1 : -1];

void ngb_sha1_init(ngb_sha1_t *c);
void ngb_sha1_update(ngb_sha1_t *c, const void *data, size_t len);
void ngb_sha1_final(ngb_sha1_t *c, uint8_t digest[NGB_SHA1_DIGEST]);

/* HMAC-SHA1 over data1 || data2 (either can be NULL with length 0). */
void ngb_hmac_sha1(const void *key, size_t key_len, const void *data1, size_t len1,
                   const void *data2, size_t len2, uint8_t digest[NGB_SHA1_DIGEST]);

#endif
