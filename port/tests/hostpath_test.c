/*
 * hostpath_test.c: el confinamiento de rutas Xbox -> PC nunca sale de la raíz.
 * Lo compila y ejecuta port\build.cmd; termina con código 1 si algo falla.
 */
#include <stdio.h>
#include <string.h>

#include "../src/kernel/hostpath.h"

#define ROOT "C:\\ngb-test-root\\partition1"

static int g_failures, g_checks;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        g_checks++;                                                                 \
        if (!(cond)) {                                                              \
            fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            g_failures++;                                                           \
        }                                                                           \
    } while (0)

static int joins(const char *rel, char *out, size_t outlen) {
    return hostpath_join(ROOT, rel, out, outlen) == 0;
}

static void test_accepts_normal_paths(void) {
    char out[512];
    CHECK(joins("", out, sizeof out) && !_stricmp(out, ROOT));
    CHECK(joins("\\", out, sizeof out) && !_strnicmp(out, ROOT, strlen(ROOT)));
    CHECK(joins("\\TDATA\\5443000d\\save.dat", out, sizeof out) &&
          !_stricmp(out, ROOT "\\TDATA\\5443000d\\save.dat"));
    CHECK(joins("\\a\\\\b", out, sizeof out) && !_stricmp(out, ROOT "\\a\\b"));
    CHECK(joins("\\$u\\contentmeta.xbx", out, sizeof out));
    CHECK(joins("\\name.with.dots.afs", out, sizeof out));
}

static void test_rejects_parent_components(void) {
    char out[512] = "x";
    CHECK(!joins("\\..", out, sizeof out) && out[0] == 0);
    CHECK(!joins("\\..\\..\\Windows\\win.ini", out, sizeof out));
    CHECK(!joins("\\TDATA\\..\\..\\partition2\\x", out, sizeof out));
    CHECK(!joins("\\.\\x", out, sizeof out));
    CHECK(!joins("\\...", out, sizeof out));  /* Windows lo recorta a ".." */
    CHECK(!joins("\\.. ", out, sizeof out));  /* espacio final: idem */
    CHECK(!joins("\\a/../../b", out, sizeof out)); /* '/' también separa */
}

static void test_rejects_absolute_and_colon(void) {
    char out[512];
    CHECK(!joins("C:\\Windows\\win.ini", out, sizeof out));    /* no empieza por '\' */
    CHECK(!joins("\\C:\\Windows\\win.ini", out, sizeof out));  /* ':' en un componente */
    CHECK(!joins("\\file.txt:stream", out, sizeof out));       /* flujo alternativo NTFS */
    CHECK(!joins("\\\\?\\C:\\x", out, sizeof out));            /* '?' y ':' */
    CHECK(!joins("\\a*b", out, sizeof out));
    CHECK(!joins("\\a\x01" "b", out, sizeof out));
    CHECK(!joins("\\CON", out, sizeof out) || !_strnicmp(out, ROOT, strlen(ROOT)));
}

static void test_unc_like_stays_inside(void) {
    char out[512];
    if (joins("\\\\server\\share", out, sizeof out))
        CHECK(!_strnicmp(out, ROOT "\\", strlen(ROOT) + 1)); /* nunca \\server\share */
}

static void test_small_buffer(void) {
    char out[8];
    CHECK(!joins("\\x", out, sizeof out));
}

int main(void) {
    test_accepts_normal_paths();
    test_rejects_parent_components();
    test_rejects_absolute_and_colon();
    test_unc_like_stays_inside();
    test_small_buffer();
    if (g_failures) {
        fprintf(stderr, "hostpath_test: %d de %d comprobaciones fallan\n", g_failures, g_checks);
        return 1;
    }
    printf("hostpath_test: OK (%d comprobaciones)\n", g_checks);
    return 0;
}
