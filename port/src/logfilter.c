/*
 * logfilter.c: see logfilter.h.
 */
#include "logfilter.h"

#include <ctype.h>
#include <string.h>

/* Words that make a detail line worth keeping in the short log (lowercase). */
static const char *const k_keep[] = {
    "unknown", "unimplemented", "not implemented", "fail", "error", "warn", "could not",
    "invalid", "timeout", "crash", "exception", "perf:",
};

static int contains_nocase(const char *s, const char *word) {
    size_t n = strlen(word);
    for (; *s; s++) {
        size_t i;
        for (i = 0; i < n && s[i] && tolower((unsigned char)s[i]) == word[i]; i++)
            ;
        if (i == n)
            return 1;
    }
    return 0;
}

int ngb_logline_is_detail(const char *line) {
    size_t k;
    if (line[0] == '[' && isdigit((unsigned char)line[1]))
        return 1; /* one kernel call */
    if (strncmp(line, "    \xC2\xB7 ", 7)) /* "    · " in UTF-8 */
        return 0;
    for (k = 0; k < sizeof k_keep / sizeof k_keep[0]; k++)
        if (contains_nocase(line, k_keep[k]))
            return 0;
    return 1;
}

/* Path characters compare without case and with / and \ as the same separator. */
static int path_char(int c) {
    c = tolower((unsigned char)c);
    return c == '/' ? '\\' : c;
}

size_t ngb_log_scrub(char *buf, size_t cap, const char *home) {
    static const char repl[] = "%USERPROFILE%";
    const size_t rn = sizeof repl - 1;
    size_t hn = home ? strlen(home) : 0, len = strlen(buf), i = 0;
    if (!hn)
        return len;
    while (i + hn <= len) {
        size_t j;
        for (j = 0; j < hn && path_char(buf[i + j]) == path_char(home[j]); j++)
            ;
        if (j < hn) {
            i++;
            continue;
        }
        if (len - hn + rn + 1 > cap)
            break; /* no room: leave the rest as it is */
        memmove(buf + i + rn, buf + i + hn, len - i - hn + 1);
        memcpy(buf + i, repl, rn);
        len = len - hn + rn;
        i += rn;
    }
    return len;
}
