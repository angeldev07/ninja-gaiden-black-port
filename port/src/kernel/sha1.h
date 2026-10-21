/*
 * sha1.h: SHA-1 (FIPS 180-4) y HMAC-SHA1 (RFC 2104), código puro sin dependencias.
 *
 * Lo usan XcSHAInit/XcSHAUpdate/XcSHAFinal y XcHMAC (impl_misc.c): el juego firma y
 * verifica sus partidas guardadas con ellos. El contexto vive en memoria del juego
 * (A_SHA_CTX de 116 bytes en la Xbox), así que tiene que caber ahí.
 */
#ifndef NGB_SHA1_H
#define NGB_SHA1_H

#include <stddef.h>
#include <stdint.h>

#define NGB_SHA1_DIGEST 20
#define NGB_SHA1_BLOCK 64

typedef struct ngb_sha1 {
    uint32_t state[5];
    uint32_t count_lo, count_hi; /* longitud en bytes */
    uint8_t buffer[NGB_SHA1_BLOCK];
} ngb_sha1_t;

/* Tiene que caber en el A_SHA_CTX de 116 bytes que reserva el juego. */
typedef char ngb_sha1_fits_xbox_ctx[sizeof(ngb_sha1_t) <= 116 ? 1 : -1];

void ngb_sha1_init(ngb_sha1_t *c);
void ngb_sha1_update(ngb_sha1_t *c, const void *data, size_t len);
void ngb_sha1_final(ngb_sha1_t *c, uint8_t digest[NGB_SHA1_DIGEST]);

/* HMAC-SHA1 sobre data1 || data2 (cualquiera puede ser NULL con longitud 0). */
void ngb_hmac_sha1(const void *key, size_t key_len, const void *data1, size_t len1,
                   const void *data2, size_t len2, uint8_t digest[NGB_SHA1_DIGEST]);

#endif
