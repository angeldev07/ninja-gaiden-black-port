/*
 * hostpath.h: safe translation of an Xbox path (what is left after the device
 * prefix) to a PC path inside a root folder.
 *
 * The game controls the paths it asks for (NtCreateFile, symbolic links), so the
 * result can never leave `root`: ".." components are rejected, as are those with
 * ':' (drives, NTFS alternate streams) or invalid characters, and in the end the full path
 * (GetFullPathNameA) has to start with `root\`. Used by impl_io.c and tested by
 * port/tests/hostpath_test.c.
 */
#pragma once

#include <stddef.h>

/*
 * `rel` is "" or starts with '\' (for example "\media\stage01.afs"). Writes to `out` the
 * full normalized path `root` + `rel`. Returns 0 if it is safe and fits; -1 if not
 * (and `out` is left empty).
 */
int hostpath_join(const char *root, const char *rel, char *out, size_t outlen);

/* 1 if `rel` (format above) only has valid components; does not touch the disk. */
int hostpath_rel_ok(const char *rel);
