/*
 * update.c: see update.h.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "import.h" /* make_dirs, remove_tree */
#include "update.h"
#include "version.h"

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

static const wchar_t *const k_bins[] = {L"ngb-launcher.exe", L"ngb-loader.exe", L"ngb-core.dll"};
#define N_BINS (sizeof k_bins / sizeof k_bins[0])

/* ------------------------------------------------------------ pure functions ---- */

static int is_hex64(const char *s) {
    int i;
    for (i = 0; i < 64; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return 0;
    return s[64] == 0;
}

/* ngb-port-v<version chars>-win32.zip, nothing that could name another folder. */
static int zip_name_ok(const char *s) {
    size_t n = strlen(s), i;
    if (n < 20 || strncmp(s, "ngb-port-v", 10) || strcmp(s + n - 10, "-win32.zip"))
        return 0;
    for (i = 10; i < n - 10; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'z') || s[i] == '.' || s[i] == '-'))
            return 0;
    return 1;
}

/* X.Y.Z with an optional -suffix of [0-9a-z.-]: the version also names the staging folder and
 * goes on the restart command line, so nothing else may get through. */
static int version_ok(const char *s) {
    int dots = 0;
    if (!(*s >= '0' && *s <= '9'))
        return 0;
    for (; *s && *s != '-'; s++) {
        if (*s == '.')
            dots++;
        else if (!(*s >= '0' && *s <= '9'))
            return 0;
    }
    if (*s == '-' && !s[1])
        return 0;
    for (; *s; s++)
        if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'z') || *s == '.' || *s == '-'))
            return 0;
    return dots == 2;
}

/* Copies a value; one that does not fit leaves the field empty, which fails validation. */
static void copy_field(char *dst, size_t cap, const char *src, size_t n) {
    if (n >= cap)
        n = 0;
    memcpy(dst, src, n);
    dst[n] = 0;
}

int upd_parse_manifest(const char *text, size_t n, upd_manifest_t *out) {
    size_t i = 0;
    int have_size = 0;
    memset(out, 0, sizeof *out);
    while (i < n) {
        size_t a = i, eq, e;
        while (i < n && text[i] != '\n')
            i++;
        e = i++;
        if (e > a && text[e - 1] == '\r')
            e--;
        for (eq = a; eq < e && text[eq] != '='; eq++)
            ;
        if (eq == e)
            continue;
#define KEY(k) (eq - a == sizeof k - 1 && !memcmp(text + a, k, sizeof k - 1))
        if (KEY("version"))
            copy_field(out->version, sizeof out->version, text + eq + 1, e - eq - 1);
        else if (KEY("zip"))
            copy_field(out->zip, sizeof out->zip, text + eq + 1, e - eq - 1);
        else if (KEY("sha256"))
            copy_field(out->sha256, sizeof out->sha256, text + eq + 1, e - eq - 1);
        else if (KEY("notes"))
            copy_field(out->notes, sizeof out->notes, text + eq + 1, e - eq - 1);
        else if (KEY("size")) {
            char num[24];
            char *end;
            copy_field(num, sizeof num, text + eq + 1, e - eq - 1);
            out->size = _strtoi64(num, &end, 10);
            have_size = end != num && !*end;
        }
#undef KEY
    }
    return version_ok(out->version) && zip_name_ok(out->zip) && is_hex64(out->sha256) && have_size &&
           out->size > 0 && out->size <= UPD_MAX_ZIP && !strncmp(out->notes, "https://", 8);
}

int upd_version_cmp(const char *a, const char *b) {
    int k;
    const char *sa, *sb;
    for (k = 0; k < 3; k++) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y)
            return x < y ? -1 : 1;
        if (*a == '.')
            a++;
        if (*b == '.')
            b++;
    }
    sa = *a == '-' ? a + 1 : NULL;
    sb = *b == '-' ? b + 1 : NULL;
    if (!sa || !sb)
        return sa ? -1 : sb ? 1 : 0; /* a suffix (pre-release) comes first */
    return strcmp(sa, sb) < 0 ? -1 : strcmp(sa, sb) > 0;
}

typedef struct sha_ctx {
    BCRYPT_ALG_HANDLE alg;
    BCRYPT_HASH_HANDLE hash;
} sha_ctx;

static int sha_begin(sha_ctx *c) {
    memset(c, 0, sizeof *c);
    if (BCryptOpenAlgorithmProvider(&c->alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0)
        return 0;
    if (BCryptCreateHash(c->alg, &c->hash, NULL, 0, NULL, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(c->alg, 0);
        return 0;
    }
    return 1;
}

static int sha_end(sha_ctx *c, int ok, char hex[65]) {
    UCHAR d[32];
    int i;
    ok = ok && BCryptFinishHash(c->hash, d, sizeof d, 0) >= 0;
    BCryptDestroyHash(c->hash);
    BCryptCloseAlgorithmProvider(c->alg, 0);
    for (i = 0; ok && i < 32; i++)
        sprintf(hex + i * 2, "%02x", d[i]);
    hex[ok ? 64 : 0] = 0;
    return ok;
}

int upd_sha256_mem(const void *data, size_t n, char hex[65]) {
    sha_ctx c;
    if (!sha_begin(&c))
        return 0;
    return sha_end(&c, BCryptHashData(c.hash, (PUCHAR)data, (ULONG)n, 0) >= 0, hex);
}

int upd_sha256_file(const wchar_t *path, char hex[65]) {
    static UCHAR buf[1 << 16];
    sha_ctx c;
    DWORD got;
    int ok = 1;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    if (!sha_begin(&c)) {
        CloseHandle(f);
        return 0;
    }
    while (ok && (ok = ReadFile(f, buf, sizeof buf, &got, NULL) != 0) && got)
        ok = BCryptHashData(c.hash, buf, got, 0) >= 0;
    CloseHandle(f);
    return sha_end(&c, ok, hex);
}

/* ---------------------------------------------------------------- download ---- */

typedef int (*sink_fn)(void *ctx, const void *data, DWORD n);

static volatile LONG64 g_done, g_total;

/* GET `url` into `sink`. HTTPS only, or a local file when the URL has no "://". */
static int fetch(const wchar_t *url, sink_fn sink, void *ctx) {
    static UCHAR buf[1 << 16];
    URL_COMPONENTS uc;
    wchar_t host[256], path[2048];
    HINTERNET s = NULL, c = NULL, r = NULL;
    DWORD got, code = 0, len = sizeof code;
    int ok = 0;
    if (!wcsstr(url, L"://")) {
        HANDLE f = CreateFileW(url, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (f == INVALID_HANDLE_VALUE)
            return 0;
        ok = 1;
        while (ok && (ok = ReadFile(f, buf, sizeof buf, &got, NULL) != 0) && got)
            ok = sink(ctx, buf, got);
        CloseHandle(f);
        return ok;
    }
    memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url, 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS)
        return 0;
    s = WinHttpOpen(L"ngb-launcher/" NGB_VERSION_STR, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (s)
        WinHttpSetTimeouts(s, 10000, 10000, 15000, 30000);
    if (s)
        c = WinHttpConnect(s, host, uc.nPort, 0);
    if (c)
        r = WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                               WINHTTP_FLAG_SECURE);
    /* GitHub answers .../releases/latest/download/X with a redirect, which WinHTTP follows
     * (never from HTTPS down to HTTP). */
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, NULL) &&
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX) &&
        code == 200) {
        ok = 1;
        while (ok && (ok = WinHttpReadData(r, buf, sizeof buf, &got) != 0) && got)
            ok = sink(ctx, buf, got);
    }
    if (r)
        WinHttpCloseHandle(r);
    if (c)
        WinHttpCloseHandle(c);
    if (s)
        WinHttpCloseHandle(s);
    return ok;
}

typedef struct mem_sink {
    char data[4096];
    size_t n;
} mem_sink;

static int to_mem(void *ctx, const void *data, DWORD n) {
    mem_sink *m = (mem_sink *)ctx;
    if (m->n + n > sizeof m->data)
        return 0; /* a manifest is a few lines */
    memcpy(m->data + m->n, data, n);
    m->n += n;
    return 1;
}

typedef struct file_sink {
    HANDLE f;
    int64_t limit;
} file_sink;

static int to_file(void *ctx, const void *data, DWORD n) {
    file_sink *s = (file_sink *)ctx;
    DWORD put;
    if (g_done + n > s->limit)
        return 0; /* bigger than the manifest says */
    if (!WriteFile(s->f, data, n, &put, NULL) || put != n)
        return 0;
    InterlockedExchangeAdd64(&g_done, n);
    return 1;
}

/* ------------------------------------------------------------ worker thread ---- */

static struct {
    volatile LONG state, err;
    upd_manifest_t man;
    char current[32];
    wchar_t url[1024], data[MAX_PATH], staged[MAX_PATH];
    HANDLE thread;
} g;

static void finish(int state, int err) {
    InterlockedExchange(&g.err, err);
    InterlockedExchange(&g.state, state);
}

static DWORD WINAPI check_thread(LPVOID param) {
    mem_sink m;
    upd_manifest_t man;
    (void)param;
    m.n = 0;
    if (!fetch(g.url, to_mem, &m)) {
        finish(UPD_ERROR, UPDE_NETWORK);
        return 0;
    }
    if (!upd_parse_manifest(m.data, m.n, &man)) {
        finish(UPD_ERROR, UPDE_MANIFEST);
        return 0;
    }
    g.man = man; /* published by the state change below */
    finish(upd_version_cmp(man.version, g.current) > 0 ? UPD_AVAILABLE : UPD_LATEST, UPDE_NONE);
    return 0;
}

/* The zip sits next to the manifest: same URL with the last segment replaced. */
static void sibling_url(const wchar_t *url, const char *name, wchar_t *out, size_t cap) {
    const wchar_t *slash = wcsrchr(url, L'/'), *back = wcsrchr(url, L'\\');
    size_t keep;
    if (back > slash)
        slash = back;
    keep = slash ? (size_t)(slash - url + 1) : 0;
    if (keep >= cap)
        keep = 0;
    wmemcpy(out, url, keep);
    _snwprintf(out + keep, cap - keep, L"%hs", name);
    out[cap - 1] = 0;
}

/* Runs %SystemRoot%\System32\tar.exe -xf zip -C dir (bsdtar refuses absolute and ".." paths). */
static int extract(const wchar_t *zip, const wchar_t *dir) {
    wchar_t tar[MAX_PATH], cmd[3 * MAX_PATH];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    if (!GetSystemDirectoryW(tar, MAX_PATH))
        return 0;
    wcsncat(tar, L"\\tar.exe", MAX_PATH - wcslen(tar) - 1);
    _snwprintf(cmd, 3 * MAX_PATH, L"\"%ls\" -xf \"%ls\" -C \"%ls\"", tar, zip, dir);
    cmd[3 * MAX_PATH - 1] = 0;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(tar, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return 0;
    if (WaitForSingleObject(pi.hProcess, 120000) != WAIT_OBJECT_0)
        TerminateProcess(pi.hProcess, 1);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

static DWORD WINAPI download_thread(LPVOID param) {
    wchar_t dir[MAX_PATH], part[MAX_PATH], zip[MAX_PATH], url[1200];
    char hex[65];
    file_sink fs;
    size_t k;
    int ok;
    (void)param;
    _snwprintf(dir, MAX_PATH, L"%ls\\updates", g.data);
    dir[MAX_PATH - 1] = 0;
    make_dirs(dir);
    _snwprintf(zip, MAX_PATH, L"%ls\\%hs", dir, g.man.zip);
    zip[MAX_PATH - 1] = 0;
    _snwprintf(part, MAX_PATH, L"%ls.part", zip);
    part[MAX_PATH - 1] = 0;
    sibling_url(g.url, g.man.zip, url, 1200);
    fs.limit = g.man.size;
    fs.f = CreateFileW(part, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (fs.f == INVALID_HANDLE_VALUE) {
        finish(UPD_ERROR, UPDE_WRITE);
        return 0;
    }
    ok = fetch(url, to_file, &fs);
    CloseHandle(fs.f);
    if (!ok || g_done != g.man.size) {
        DeleteFileW(part);
        finish(UPD_ERROR, ok ? UPDE_SIZE : UPDE_NETWORK);
        return 0;
    }
    if (!upd_sha256_file(part, hex) || strcmp(hex, g.man.sha256)) {
        DeleteFileW(part);
        finish(UPD_ERROR, UPDE_HASH);
        return 0;
    }
    if (!MoveFileExW(part, zip, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(part);
        finish(UPD_ERROR, UPDE_WRITE);
        return 0;
    }
    _snwprintf(g.staged, MAX_PATH, L"%ls\\v%hs", dir, g.man.version);
    g.staged[MAX_PATH - 1] = 0;
    remove_tree(g.staged);
    make_dirs(g.staged);
    ok = extract(zip, g.staged);
    for (k = 0; ok && k < N_BINS; k++) {
        wchar_t p[MAX_PATH];
        _snwprintf(p, MAX_PATH, L"%ls\\%ls", g.staged, k_bins[k]);
        p[MAX_PATH - 1] = 0;
        ok = GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
    }
    if (!ok) { /* nothing half-extracted stays behind */
        remove_tree(g.staged);
        DeleteFileW(zip);
    }
    finish(ok ? UPD_READY : UPD_ERROR, ok ? UPDE_NONE : UPDE_EXTRACT);
    return 0;
}

static int busy(void) { return g.state == UPD_CHECKING || g.state == UPD_DOWNLOADING; }

static int start(LPTHREAD_START_ROUTINE fn, int state) {
    if (g.thread) {
        WaitForSingleObject(g.thread, INFINITE);
        CloseHandle(g.thread);
        g.thread = NULL;
    }
    g.err = UPDE_NONE;
    g.state = state;
    g.thread = CreateThread(NULL, 0, fn, NULL, 0, NULL);
    if (!g.thread) {
        finish(UPD_ERROR, UPDE_INTERNAL);
        return 0;
    }
    return 1;
}

int upd_check_start(const wchar_t *manifest_url, const char *current) {
    if (busy())
        return 0;
    wcsncpy(g.url, manifest_url, 1023);
    g.url[1023] = 0;
    strncpy(g.current, current, sizeof g.current - 1);
    return start(check_thread, UPD_CHECKING);
}

int upd_download_start(const wchar_t *data_dir) {
    if (g.state != UPD_AVAILABLE && !(g.state == UPD_ERROR && g.man.version[0]))
        return 0;
    wcsncpy(g.data, data_dir, MAX_PATH - 1);
    g.data[MAX_PATH - 1] = 0;
    g_done = 0;
    g_total = g.man.size;
    return start(download_thread, UPD_DOWNLOADING);
}

void upd_status(upd_status_t *out) {
    memset(out, 0, sizeof *out);
    out->state = (int)g.state;
    out->err = (int)g.err;
    out->man = g.man; /* empty until a check finds a valid manifest */
    out->done = InterlockedCompareExchange64(&g_done, 0, 0);
    out->total = InterlockedCompareExchange64(&g_total, 0, 0);
    if (out->state == UPD_READY)
        wcsncpy(out->staged, g.staged, 259);
}

/* ------------------------------------------------------------------- apply ---- */

static void bin_path(wchar_t *out, const wchar_t *dir, size_t k, const wchar_t *suffix) {
    _snwprintf(out, MAX_PATH, L"%ls\\%ls%ls", dir, k_bins[k], suffix);
    out[MAX_PATH - 1] = 0;
}

int upd_apply(const wchar_t *exe_dir, const wchar_t *staged, wchar_t *why, size_t whylen) {
    wchar_t cur[MAX_PATH], old[MAX_PATH], neu[MAX_PATH];
    int had[N_BINS];
    size_t k, done = 0;
    for (k = 0; k < N_BINS; k++) {
        bin_path(neu, staged, k, L"");
        if (GetFileAttributesW(neu) == INVALID_FILE_ATTRIBUTES) {
            _snwprintf(why, whylen, L"%ls is missing from the update", k_bins[k]);
            return 0;
        }
    }
    for (k = 0; k < N_BINS; k++, done++) {
        bin_path(cur, exe_dir, k, L"");
        bin_path(old, exe_dir, k, L".old");
        bin_path(neu, staged, k, L"");
        had[k] = GetFileAttributesW(cur) != INVALID_FILE_ATTRIBUTES;
        DeleteFileW(old);
        /* A running exe or a loaded DLL can be renamed, not overwritten. */
        if (had[k] && !MoveFileExW(cur, old, MOVEFILE_REPLACE_EXISTING)) {
            _snwprintf(why, whylen, L"cannot rename %ls (error %lu)", k_bins[k], GetLastError());
            break;
        }
        if (!CopyFileW(neu, cur, FALSE)) {
            _snwprintf(why, whylen, L"cannot write %ls (error %lu)", k_bins[k], GetLastError());
            done++; /* this one has to be put back too */
            break;
        }
    }
    if (done == N_BINS)
        return 1;
    while (done--) { /* roll back, newest first */
        bin_path(cur, exe_dir, done, L"");
        bin_path(old, exe_dir, done, L".old");
        DeleteFileW(cur);
        if (had[done])
            MoveFileExW(old, cur, MOVEFILE_REPLACE_EXISTING);
    }
    if (whylen)
        why[whylen - 1] = 0;
    return 0;
}

void upd_remove_old(const wchar_t *exe_dir) {
    wchar_t old[MAX_PATH];
    size_t k;
    for (k = 0; k < N_BINS; k++) {
        bin_path(old, exe_dir, k, L".old");
        DeleteFileW(old);
    }
}
