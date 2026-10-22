/*
 * logfilter.h: what the short log keeps (--log-detail minimal) and the user-name scrub.
 *
 * Minimal drops the line of every kernel call ("[n] T.. @.. ...") and the detail lines that the
 * kernel functions write ("    · ..."), except those that report a problem or the fps. What is
 * left (header, warnings, errors, the stop summary and the crash report) is a few KB per session:
 * enough to attach to a bug report.
 *
 * The scrub replaces the user's profile folder (C:\Users\<name>) with %USERPROFILE%, so a log
 * attached to a public issue does not carry the Windows user name. Pure functions: no globals.
 */
#ifndef NGB_LOGFILTER_H
#define NGB_LOGFILTER_H

#include <stddef.h>

/* 1 if `line` is a detail line that minimal mode drops. */
int ngb_logline_is_detail(const char *line);

/* Replaces every occurrence of `home` (ASCII case-insensitive, / and \ alike) in `buf` with "%USERPROFILE%",
 * in place, without passing `cap` bytes (NUL included). Returns the new length. An empty or
 * NULL `home` leaves `buf` as it is. */
size_t ngb_log_scrub(char *buf, size_t cap, const char *home);

#endif
