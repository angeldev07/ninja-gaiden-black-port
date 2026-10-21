/*
 * rcache_test.c: pruebas de las cachés del render GL (gpu/rcache.c), sin contexto GL.
 * Lo compila y ejecuta port\build.cmd; termina con código 1 si algo falla.
 */
#include <stdio.h>
#include <string.h>

#include "../src/gpu/rcache.h"

static int g_failures, g_checks;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        g_checks++;                                                                 \
        if (!(cond)) {                                                              \
            fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

static rc_surfaces g_s;
static rc_textures g_t;

/* Superficie nueva, acierto, cambio de tamaño y búsqueda por dirección. */
static void test_surface_lifecycle(void) {
    unsigned dead[4];
    int res, nd;
    rc_surface *a, *b;
    memset(&g_s, 0, sizeof g_s);
    a = rc_surface_get(&g_s, RC_COLOR, 0x2878000, 3072, 720, 480, 8, &res, dead, 4, &nd);
    CHECK(a != NULL && res == RC_SURF_NEW && nd == 0);
    CHECK(a->bytes == 3072u * 480u);
    a->gl_tex = 11;
    b = rc_surface_get(&g_s, RC_COLOR, 0x2878000, 3072, 720, 480, 8, &res, dead, 4, &nd);
    CHECK(b == a && res == RC_SURF_HIT && nd == 0 && b->gl_tex == 11);
    /* el mismo búfer con otro tamaño: hay que recrear la textura */
    b = rc_surface_get(&g_s, RC_COLOR, 0x2878000, 2560, 640, 480, 8, &res, dead, 4, &nd);
    CHECK(b == a && res == RC_SURF_RESIZED && nd == 1 && dead[0] == 11 && b->gl_tex == 0);
    CHECK(rc_surface_find(&g_s, RC_COLOR, 0x2878000) == a);
    CHECK(rc_surface_find(&g_s, RC_ZETA, 0x2878000) == NULL);
    CHECK(rc_surface_find(&g_s, RC_COLOR, 0x2878004) == NULL);
    /* argumentos que no valen */
    CHECK(rc_surface_get(&g_s, RC_COLOR, 0, 0, 0, 480, 8, &res, dead, 4, &nd) == NULL);
    CHECK(rc_surface_get(&g_s, RC_COLOR, 0, 0, 5000, 480, 8, &res, dead, 4, &nd) == NULL);
}

/* Dos búferes contiguos no se pisan; uno que se solapa invalida al anterior. */
static void test_surface_overlap(void) {
    unsigned dead[4];
    int res, nd;
    rc_surface *a, *b, *c;
    memset(&g_s, 0, sizeof g_s);
    a = rc_surface_get(&g_s, RC_COLOR, 0x2878000, 3072, 720, 480, 8, &res, dead, 4, &nd);
    a->gl_tex = 1;
    /* 0x2878000 + 3072 * 480 = 0x29E0000: justo a continuación */
    b = rc_surface_get(&g_s, RC_COLOR, 0x29E0000, 3072, 720, 480, 8, &res, dead, 4, &nd);
    b->gl_tex = 2;
    CHECK(res == RC_SURF_NEW && nd == 0);
    c = rc_surface_get(&g_s, RC_ZETA, 0x2B48000, 3072, 720, 480, 2, &res, dead, 4, &nd);
    CHECK(c != NULL && nd == 0);
    c->gl_tex = 3;
    CHECK(rc_surface_find(&g_s, RC_COLOR, 0x2878000) != NULL);
    /* textura de 256x256 dentro del primer búfer: el búfer deja de valer */
    c = rc_surface_get(&g_s, RC_COLOR, 0x2900000, 1024, 256, 256, 8, &res, dead, 4, &nd);
    CHECK(c != NULL && res == RC_SURF_NEW && nd == 1 && dead[0] == 1);
    CHECK(rc_surface_find(&g_s, RC_COLOR, 0x2878000) == NULL);
    CHECK(rc_surface_find(&g_s, RC_COLOR, 0x29E0000) != NULL);
}

/* Con la tabla llena se expulsa la menos usada. */
static void test_surface_lru(void) {
    unsigned dead[4];
    int res, nd, i;
    rc_surface *s;
    memset(&g_s, 0, sizeof g_s);
    for (i = 0; i < RC_MAX_SURFACES; i++) {
        s = rc_surface_get(&g_s, RC_COLOR, 0x100000u * (uint32_t)i, 256, 64, 64, 8, &res, dead, 4, &nd);
        s->gl_tex = 100u + (unsigned)i;
    }
    rc_surface_find(&g_s, RC_COLOR, 0); /* la 0 pasa a ser reciente: se expulsa la 1 */
    s = rc_surface_get(&g_s, RC_COLOR, 0x7000000, 256, 64, 64, 8, &res, dead, 4, &nd);
    CHECK(s != NULL && res == RC_SURF_NEW && nd == 1 && dead[0] == 101);
    CHECK(rc_surface_find(&g_s, RC_COLOR, 0) != NULL);
    CHECK(rc_surface_find(&g_s, RC_COLOR, 0x100000) == NULL);
}

/* Texturas: subida, uso en el mismo frame, comprobación por hash en el siguiente. */
static void test_texture_cache(void) {
    rc_texkey k = {0x01000000, 0x00010629, 0, 0, 0}, k2 = k;
    rc_texture *t, *u;
    unsigned dead;
    int action;
    uint8_t data[64];
    uint64_t h1, h2;
    memset(&g_t, 0, sizeof g_t);
    memset(data, 0x5A, sizeof data);
    h1 = rc_hash(data, sizeof data, 0);
    t = rc_texture_get(&g_t, &k, 1, &action, &dead);
    CHECK(t != NULL && action == RC_TEX_UPLOAD && dead == 0);
    t->gl_tex = 7;
    rc_texture_uploaded(t, h1, 1);
    u = rc_texture_get(&g_t, &k, 1, &action, &dead);
    CHECK(u == t && action == RC_TEX_USE);
    /* frame siguiente: hay que mirar si cambió */
    u = rc_texture_get(&g_t, &k, 2, &action, &dead);
    CHECK(u == t && action == RC_TEX_CHECK);
    CHECK(rc_texture_checked(&g_t, u, h1, 2) == 0);
    u = rc_texture_get(&g_t, &k, 2, &action, &dead);
    CHECK(action == RC_TEX_USE);
    data[17] ^= 1;
    h2 = rc_hash(data, sizeof data, 0);
    CHECK(h2 != h1);
    u = rc_texture_get(&g_t, &k, 3, &action, &dead);
    CHECK(action == RC_TEX_CHECK && rc_texture_checked(&g_t, u, h2, 3) == 1);
    /* otro formato en la misma dirección es otra entrada */
    k2.format = 0x00010529;
    u = rc_texture_get(&g_t, &k2, 3, &action, &dead);
    CHECK(u != t && action == RC_TEX_UPLOAD);
}

static void test_texture_lru(void) {
    rc_texkey k = {0, 0x00010629, 0, 0, 0};
    rc_texture *t;
    unsigned dead;
    int action, i;
    memset(&g_t, 0, sizeof g_t);
    for (i = 0; i < RC_MAX_TEXTURES; i++) {
        k.addr = 0x1000u * (uint32_t)i;
        t = rc_texture_get(&g_t, &k, 1, &action, &dead);
        t->gl_tex = 1000u + (unsigned)i;
    }
    k.addr = 0x9000000;
    t = rc_texture_get(&g_t, &k, 2, &action, &dead);
    CHECK(t != NULL && action == RC_TEX_UPLOAD && dead == 1000);
}

/* El hash ve cambios de un bit en cualquier posición, también en la cola. */
static void test_hash(void) {
    uint8_t d[37];
    uint64_t base;
    int i, distinct = 1;
    memset(d, 0, sizeof d);
    base = rc_hash(d, sizeof d, 0);
    for (i = 0; i < (int)sizeof d; i++) {
        d[i] = 1;
        if (rc_hash(d, sizeof d, 0) == base)
            distinct = 0;
        d[i] = 0;
    }
    CHECK(distinct);
    CHECK(rc_hash(d, sizeof d, 1) != base);
    CHECK(rc_hash(d, 36, 0) != base);
}

/* Firma dispersa: ve los cambios en los trozos que muestrea (el primero y el último
 * siempre) y la superficie se puede quitar de la caché. */
static void test_sparse_and_drop(void) {
    static uint8_t big[720 * 480 * 4];
    unsigned dead[4];
    int res, nd;
    uint64_t a, b;
    rc_surface *s;
    memset(big, 0, sizeof big);
    a = rc_sparse_hash(big, sizeof big);
    CHECK(rc_sparse_hash(big, sizeof big) == a);
    big[0] = 1;
    b = rc_sparse_hash(big, sizeof big);
    CHECK(b != a);
    big[sizeof big - 1] = 1;
    CHECK(rc_sparse_hash(big, sizeof big) != b);
    CHECK(rc_sparse_hash(big, 100) == rc_hash(big, 100, 0x53505253u)); /* corta: entera */
    memset(&g_s, 0, sizeof g_s);
    s = rc_surface_get(&g_s, RC_COLOR, 0x1000000, 3072, 720, 480, 8, &res, dead, 4, &nd);
    s->gl_tex = 9;
    CHECK(rc_surface_drop(s) == 9);
    CHECK(rc_surface_find(&g_s, RC_COLOR, 0x1000000) == NULL);
}

static void test_attr_layout(void) {
    rc_attr a;
    CHECK(rc_attr_layout(0x00000000, &a) == 0 && !a.enabled);
    /* posición XYZRHW: F x4, stride 28 */
    CHECK(rc_attr_layout(0x1C42, &a) == 0 && a.enabled && a.comps == 4 && a.src_bytes == 16 &&
          a.inline_words == 4 && a.stride == 28);
    /* difuso D3DCOLOR */
    CHECK(rc_attr_layout(0x1C40, &a) == 0 && a.comps == 4 && a.src_bytes == 4 && a.inline_words == 1);
    /* S1 x3: 6 bytes, 2 palabras en INLINE_ARRAY */
    CHECK(rc_attr_layout(0x0831, &a) == 0 && a.comps == 3 && a.src_bytes == 6 && a.inline_words == 2);
    /* CMP: una palabra en memoria, 3 floats en el VBO */
    CHECK(rc_attr_layout(0x0C16, &a) == 0 && a.comps == 3 && a.src_bytes == 4 && a.dst_bytes == 12);
    /* SIZE_3W (7) = 3 componentes */
    CHECK(rc_attr_layout(0x0C72, &a) == 0 && a.comps == 3 && a.src_bytes == 12);
    CHECK(rc_attr_layout(0x0C53, &a) == -1); /* tipo 3: no existe */
}

static void test_quads(void) {
    uint32_t in[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8}, out[12];
    static const uint32_t want[12] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    CHECK(rc_quads_to_triangles(in, 9, out) == 12); /* el vértice suelto se ignora */
    CHECK(!memcmp(out, want, sizeof want));
    CHECK(rc_quads_to_triangles(in, 3, out) == 0);
}

static void test_dirty(void) {
    static rc_dirty d; /* 64 KB: fuera de la pila */
    uint32_t w[3] = {0x1000u, 0x1FFFu, 0x03FFF000u};
    memset(&d, 0, sizeof d);
    CHECK(rc_dirty_last(&d, 0x1000u, 0x2000u) == 0);
    rc_dirty_mark(&d, w, 2, 5); /* dos escrituras en la misma página */
    rc_dirty_mark(&d, w + 2, 1, 7);
    CHECK(rc_dirty_last(&d, 0x0u, 0x1000u) == 0);      /* página 0: limpia */
    CHECK(rc_dirty_last(&d, 0x0u, 0x1001u) == 5);      /* toca la página 1 */
    CHECK(rc_dirty_last(&d, 0x1800u, 4u) == 5);
    CHECK(rc_dirty_last(&d, 0x2000u, 0x1000u) == 0);
    CHECK(rc_dirty_last(&d, 0x0u, 0x04000000u) == 7);   /* toda la RAM: la más reciente */
    CHECK(rc_dirty_last(&d, 0x03FFFFFFu, 2u) == UINT32_MAX); /* se sale de la RAM */
    CHECK(rc_dirty_last(&d, 0x08000000u, 4u) == UINT32_MAX);
    CHECK(rc_dirty_last(&d, 0x1000u, 0u) == UINT32_MAX);
}

static void test_compact(void) {
    uint32_t idx[6] = {900, 5, 900, 70000, 5, 6}, uniq[6];
    uint32_t m = rc_compact_indices(idx, 6, uniq);
    CHECK(m == 4);
    CHECK(uniq[0] == 5 && uniq[1] == 6 && uniq[2] == 900 && uniq[3] == 70000);
    CHECK(idx[0] == 2 && idx[1] == 0 && idx[2] == 2 && idx[3] == 3 && idx[4] == 0 && idx[5] == 1);
    CHECK(rc_compact_indices(idx, 0, uniq) == 0);
}

int main(void) {
    test_dirty();
    test_compact();
    test_surface_lifecycle();
    test_surface_overlap();
    test_surface_lru();
    test_texture_cache();
    test_texture_lru();
    test_hash();
    test_sparse_and_drop();
    test_attr_layout();
    test_quads();
    if (g_failures) {
        fprintf(stderr, "rcache_test: %d de %d comprobaciones fallan\n", g_failures, g_checks);
        return 1;
    }
    printf("rcache_test: OK (%d comprobaciones)\n", g_checks);
    return 0;
}
