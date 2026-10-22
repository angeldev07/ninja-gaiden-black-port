/*
 * import.h: getting the user's own copy of the game into the data folder.
 *
 * An Xbox disc image (.iso: Redump or XISO) is read with the XDVDFS reader and extracted to
 * <data>\game on a background thread; progress is polled from the UI thread. An already
 * extracted folder is only validated (the launcher then uses it where it is).
 */
#pragma once

#include <stdint.h>
#include <wchar.h>

enum imp_state { IMP_IDLE, IMP_RUNNING, IMP_DONE, IMP_ERROR };
enum imp_phase { IMPH_SCAN, IMPH_EXTRACT, IMPH_FINISH };
enum imp_err {
    IMPE_NONE,
    IMPE_OPEN,          /* the file could not be opened */
    IMPE_NOT_XISO,      /* not an Xbox disc image */
    IMPE_NO_XBE,        /* no default.xbe at the root of the image */
    IMPE_NOT_NGB,       /* a different game; the title is in `detail` */
    IMPE_READ,          /* read error / truncated image */
    IMPE_CORRUPT,       /* damaged directory tables */
    IMPE_WRITE,         /* cannot write to the data folder */
    IMPE_NOSPACE,       /* not enough free disk space; `need_gb` says how much */
    IMPE_CANCELLED,
    IMPE_XBE_BAD,       /* default.xbe is not a valid Xbox executable */
    IMPE_FOLDER_NO_XBE, /* the chosen folder has no default.xbe */
    IMPE_INTERNAL
};

typedef struct xbe_info {
    int valid;  /* has the XBEH magic and a readable certificate */
    int is_ngb; /* the certificate title says Ninja Gaiden Black */
    wchar_t title[48];
    int64_t size; /* bytes (files only) */
} xbe_info_t;

typedef struct imp_status {
    int state, phase, err;
    int64_t done, total; /* bytes */
    wchar_t current[260]; /* file being written */
    wchar_t detail[64];   /* title of the wrong game, for IMPE_NOT_NGB */
    double need_gb;
    unsigned long elapsed_ms;
} imp_status_t;

/* Reads the XBE header of an on-disk file. Returns 0 if the file cannot be read; otherwise
 * fills `out` (check out->valid). */
int xbe_probe_file(const wchar_t *path, xbe_info_t *out);
int xbe_probe_header(const uint8_t *hdr, uint32_t n, xbe_info_t *out);

/* Deletes a folder tree (does not follow links). Only used on folders the launcher made. */
void remove_tree(const wchar_t *dir);
/* Creates a folder and its parents. */
void make_dirs(const wchar_t *path);

/* Starts importing a disc image into <data_dir>\game. Returns 0 if one is already running. */
int import_start_iso(const wchar_t *iso, const wchar_t *data_dir);
void import_cancel(void);
void import_status(imp_status_t *out);
/* After the UI has shown DONE/ERROR: joins the worker and goes back to IDLE. */
void import_reset(void);
