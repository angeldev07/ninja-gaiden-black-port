/*
 * sha1_test.c: FIPS 180 and RFC 2202 vectors for sha1.c (run by build.cmd).
 */
#include <stdio.h>
#include <string.h>

#include "kernel/sha1.h"

static int g_fail, g_checks;

static void expect(const char *name, const uint8_t got[NGB_SHA1_DIGEST], const char *hex) {
    char buf[NGB_SHA1_DIGEST * 2 + 1];
    int i;
    for (i = 0; i < NGB_SHA1_DIGEST; i++)
        sprintf(buf + i * 2, "%02x", got[i]);
    g_checks++;
    if (strcmp(buf, hex)) {
        printf("FAIL %s: %s (expected %s)\n", name, buf, hex);
        g_fail++;
    }
}

static void sha1_of(const void *data, size_t len, uint8_t out[NGB_SHA1_DIGEST]) {
    ngb_sha1_t c;
    ngb_sha1_init(&c);
    ngb_sha1_update(&c, data, len);
    ngb_sha1_final(&c, out);
}

int main(void) {
    static uint8_t million[1000000];
    uint8_t d[NGB_SHA1_DIGEST], key[80];
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    ngb_sha1_t c;
    size_t i;

    sha1_of("", 0, d);
    expect("empty", d, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    sha1_of("abc", 3, d);
    expect("abc", d, "a9993e364706816aba3e25717850c26c9cd0d89d");
    sha1_of(two, strlen(two), d);
    expect("448 bits", d, "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    memset(million, 'a', sizeof million);
    sha1_of(million, sizeof million, d);
    expect("one million a", d, "34aa973cd4c4daa4f61eeb2bdbad27316534016f");

    /* In chunks of irregular size: same output as in one go. */
    ngb_sha1_init(&c);
    for (i = 0; i < sizeof million; i += 7919)
        ngb_sha1_update(&c, million + i, sizeof million - i < 7919 ? sizeof million - i : 7919);
    ngb_sha1_final(&c, d);
    expect("in chunks", d, "34aa973cd4c4daa4f61eeb2bdbad27316534016f");

    /* RFC 2202, cases 1, 2 and 6 (80-byte key, longer than the block). */
    memset(key, 0x0b, 20);
    ngb_hmac_sha1(key, 20, "Hi There", 8, NULL, 0, d);
    expect("hmac 1", d, "b617318655057264e28bc0b6fb378c8ef146be00");
    ngb_hmac_sha1("Jefe", 4, "what do ya want ", 16, "for nothing?", 12, d); /* in two parts */
    expect("hmac 2", d, "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79");
    memset(key, 0xaa, 80);
    ngb_hmac_sha1(key, 80, "Test Using Larger Than Block-Size Key - Hash Key First", 54, NULL, 0, d);
    expect("hmac 6", d, "aa4ae5e15272d00e95705637ce8a3b55ed402112");

    if (g_fail)
        return 1;
    printf("sha1_test: OK (%d checks)\n", g_checks);
    return 0;
}
