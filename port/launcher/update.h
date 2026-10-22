/*
 * update.h: the launcher updater (docs/target/adr-07-releases-and-updater.md).
 *
 * A release publishes ngb-update.txt next to its zip. The launcher fetches the manifest of the
 * latest release, and if it is newer than NGB_VERSION_STR it offers the update:
 *   1. download the zip into <data>\updates (background thread, progress polled by the UI);
 *   2. check its size and SHA-256 against the manifest;
 *   3. extract it with Windows' own tar.exe;
 *   4. upd_apply: rename each running binary to *.old and copy the new one in (rolled back on
 *      failure); the launcher then restarts itself and upd_remove_old deletes the *.old files.
 *
 * A URL without "://" is read as a local file: that is how the tests (--update-url) run the
 * whole flow without a real release.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#define UPD_MANIFEST_URL                                                                           \
    L"https://github.com/angeldev07/ninja-gaiden-black-port/releases/latest/download/ngb-update.txt"
#define UPD_MAX_ZIP (64 * 1024 * 1024)

typedef struct upd_manifest {
    char version[32];
    char zip[96];     /* file name only: ngb-port-vX.Y.Z-win32.zip */
    int64_t size;     /* bytes of the zip */
    char sha256[65];  /* lowercase hex */
    char notes[256];  /* release page */
} upd_manifest_t;

/* ---- pure functions (tests/update_test.c) ---- */

/* Parses the key=value manifest. 1 if every field is present and valid (the zip name is checked
 * so it cannot leave the download folder). */
int upd_parse_manifest(const char *text, size_t n, upd_manifest_t *out);

/* Orders X.Y.Z[-suffix] versions: <0, 0 or >0. A version with a suffix comes before the same
 * version without one (0.3.0-dev < 0.3.0). */
int upd_version_cmp(const char *a, const char *b);

/* SHA-256 as lowercase hex (Windows BCrypt). 1 if it worked. */
int upd_sha256_mem(const void *data, size_t n, char hex[65]);
int upd_sha256_file(const wchar_t *path, char hex[65]);

/* ---- background work ---- */

enum upd_state {
    UPD_IDLE,
    UPD_CHECKING,
    UPD_LATEST,      /* the manifest says nothing newer */
    UPD_AVAILABLE,   /* manifest in `man` is newer */
    UPD_DOWNLOADING,
    UPD_READY,       /* downloaded, verified and extracted into `staged` */
    UPD_ERROR
};
enum upd_err {
    UPDE_NONE,
    UPDE_NETWORK,  /* could not download (offline, HTTP error) */
    UPDE_MANIFEST, /* the manifest is not valid */
    UPDE_SIZE,     /* the zip size is not the one in the manifest */
    UPDE_HASH,     /* the zip SHA-256 is not the one in the manifest */
    UPDE_EXTRACT,  /* tar.exe failed or the zip lacks a binary */
    UPDE_WRITE,    /* cannot write to the data or the launcher folder */
    UPDE_INTERNAL
};

typedef struct upd_status {
    int state, err;
    upd_manifest_t man;
    int64_t done, total; /* bytes of the download */
    wchar_t staged[260]; /* folder with the extracted binaries (UPD_READY) */
} upd_status_t;

/* Fetches the manifest on a background thread (silent on errors: UPD_ERROR, then the UI ignores
 * it). `current` is the running version. Returns 0 if work is already running. */
int upd_check_start(const wchar_t *manifest_url, const char *current);
/* Downloads, verifies and extracts the zip of the manifest found by the last check into
 * <data_dir>\updates. Only from UPD_AVAILABLE. */
int upd_download_start(const wchar_t *data_dir);
void upd_status(upd_status_t *out);

/* Replaces the three binaries in `exe_dir` with the ones in `staged`. 1 on success; on failure
 * everything is put back and `why` says what failed. */
int upd_apply(const wchar_t *exe_dir, const wchar_t *staged, wchar_t *why, size_t whylen);
/* Deletes the *.old files a previous update left (they may still be in use for a moment). */
void upd_remove_old(const wchar_t *exe_dir);
