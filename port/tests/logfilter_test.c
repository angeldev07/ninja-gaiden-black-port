/* logfilter_test.c: the short log's filter and the user-name scrub (src/logfilter.c). */
#include <stdio.h>
#include <string.h>

#include "logfilter.h"

static int g_fail;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

static void test_detail(void) {
    /* Kernel calls and plain detail lines go. */
    CHECK(ngb_logline_is_detail("[1234] T2 @003DCC99 113 KeSetEvent(0x1) -> 0x00000000"));
    CHECK(ngb_logline_is_detail("    \xC2\xB7 file: '\\??\\d:\\bgm.afs' -> 'x'"));
    CHECK(ngb_logline_is_detail("    \xC2\xB7 APU: VOICE_ON 0: vbin 1"));
    /* Problems and fps stay, whatever the case. */
    CHECK(!ngb_logline_is_detail("    \xC2\xB7 ExQueryNonVolatileSetting: unknown index 0x99"));
    CHECK(!ngb_logline_is_detail("    \xC2\xB7 NtCreateFile FAILED with 0xC0000034"));
    CHECK(!ngb_logline_is_detail("    \xC2\xB7 perf: frames 0-600: 60.0 real fps"));
    CHECK(!ngb_logline_is_detail("    \xC2\xB7 Warning: no audio device"));
    /* Everything else stays: header, stop, summary, a bracket that is not a call. */
    CHECK(!ngb_logline_is_detail("xbe: base 0x00010000"));
    CHECK(!ngb_logline_is_detail("==== STOP OK (0) at 150.822 s: window closed by the user"));
    CHECK(!ngb_logline_is_detail("[gl] context 4.6"));
    CHECK(!ngb_logline_is_detail(""));
}

static void test_scrub(void) {
    char buf[128];
    size_t n;
    strcpy(buf, "xbe: C:\\Users\\player\\ngb-data\\game\\default.xbe");
    n = ngb_log_scrub(buf, sizeof buf, "C:\\Users\\player");
    CHECK(!strcmp(buf, "xbe: %USERPROFILE%\\ngb-data\\game\\default.xbe"));
    CHECK(n == strlen(buf));
    /* Case-insensitive, every occurrence. */
    strcpy(buf, "c:\\users\\PLAYER\\a -> C:\\Users\\player\\b");
    ngb_log_scrub(buf, sizeof buf, "C:\\Users\\player");
    CHECK(!strcmp(buf, "%USERPROFILE%\\a -> %USERPROFILE%\\b"));
    /* Forward slashes (paths given on the command line) count as separators too. */
    strcpy(buf, "xbe: %USERPROFILE%/ngb-data/default.xbe");
    ngb_log_scrub(buf, sizeof buf, "C:\\Users\\player");
    CHECK(!strcmp(buf, "xbe: %USERPROFILE%/ngb-data/default.xbe"));
    /* No home, or no room to grow: unchanged. */
    strcpy(buf, "C:\\Users\\player\\x");
    ngb_log_scrub(buf, sizeof buf, "");
    CHECK(!strcmp(buf, "C:\\Users\\player\\x"));
    strcpy(buf, "C:\\U\\x");
    ngb_log_scrub(buf, 8, "C:\\U");
    CHECK(!strcmp(buf, "C:\\U\\x"));
}

int main(void) {
    test_detail();
    test_scrub();
    if (g_fail) {
        printf("logfilter_test: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("logfilter_test: OK\n");
    return 0;
}
