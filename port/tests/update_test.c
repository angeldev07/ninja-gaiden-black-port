/*
 * update_test.c: the launcher updater (launcher/update.c), with no network: the manifest, the
 * version order, SHA-256 vectors, and a whole update from a local manifest and zip into a
 * temporary folder (made with Windows' tar.exe, as a release would be).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>

#include "import.h"
#include "update.h"

static int g_fail;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

#define SHA_ABC "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"

static int parse(const char *text, upd_manifest_t *m) { return upd_parse_manifest(text, strlen(text), m); }

static void test_manifest(void) {
    upd_manifest_t m;
    CHECK(parse("version=0.3.0\nzip=ngb-port-v0.3.0-win32.zip\nsize=1234\nsha256=" SHA_ABC
                "\nnotes=https://github.com/x/y/releases/tag/v0.3.0\n",
                &m));
    CHECK(!strcmp(m.version, "0.3.0") && m.size == 1234 && !strcmp(m.sha256, SHA_ABC));
    /* CRLF, any order, unknown keys ignored. */
    CHECK(parse("sha256=" SHA_ABC "\r\nextra=1\r\nnotes=https://a/b\r\nsize=5\r\nzip=ngb-port-v1.0.0-rc1-win32.zip\r\n"
                "version=1.0.0-rc1\r\n",
                &m));
    /* Refused: a zip name that leaves the folder, bad hash, size, version or notes. */
    CHECK(!parse("version=0.3.0\nzip=..\\ngb-port-v0.3.0-win32.zip\nsize=5\nsha256=" SHA_ABC "\nnotes=https://a\n", &m));
    CHECK(!parse("version=0.3.0\nzip=ngb-port-v0.3.0-win32.zip\nsize=5\nsha256=ABC\nnotes=https://a\n", &m));
    CHECK(!parse("version=0.3.0\nzip=ngb-port-v0.3.0-win32.zip\nsize=0\nsha256=" SHA_ABC "\nnotes=https://a\n", &m));
    CHECK(!parse("version=0.3\nzip=ngb-port-v0.3-win32.zip\nsize=5\nsha256=" SHA_ABC "\nnotes=https://a\n", &m));
    CHECK(!parse("version=0.3.0\nzip=ngb-port-v0.3.0-win32.zip\nsize=5\nsha256=" SHA_ABC "\nnotes=http://a\n", &m));
    CHECK(!parse("", &m));
    /* The version names a folder and goes on a command line: only [0-9a-z.-] after X.Y.Z. */
    CHECK(!parse("version=0.3.0-a\\..\\x\nzip=ngb-port-v0.3.0-win32.zip\nsize=5\nsha256=" SHA_ABC "\nnotes=https://a\n", &m));
    CHECK(!parse("version=0.3.0-a b\nzip=ngb-port-v0.3.0-win32.zip\nsize=5\nsha256=" SHA_ABC "\nnotes=https://a\n", &m));
    CHECK(!parse("version=0.3.0-\nzip=ngb-port-v0.3.0-win32.zip\nsize=5\nsha256=" SHA_ABC "\nnotes=https://a\n", &m));
    /* An over-long hash is refused, not cut to 64 characters. */
    CHECK(!parse("version=0.3.0\nzip=ngb-port-v0.3.0-win32.zip\nsize=5\nsha256=" SHA_ABC "00\nnotes=https://a\n", &m));
}

static void test_versions(void) {
    CHECK(upd_version_cmp("0.3.0", "0.3.0") == 0);
    CHECK(upd_version_cmp("0.3.0-dev", "0.3.0") < 0);
    CHECK(upd_version_cmp("0.3.0", "0.3.0-dev") > 0);
    CHECK(upd_version_cmp("0.3.1", "0.3.0") > 0);
    CHECK(upd_version_cmp("0.10.0", "0.9.9") > 0);
    CHECK(upd_version_cmp("1.0.0-rc1", "1.0.0-rc2") < 0);
    CHECK(upd_version_cmp("0.2.0", "0.3.0-dev") < 0);
}

static void test_sha(void) {
    char hex[65];
    CHECK(upd_sha256_mem("abc", 3, hex) && !strcmp(hex, SHA_ABC));
    CHECK(upd_sha256_mem("", 0, hex) &&
          !strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

static void put(const wchar_t *path, const char *text) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD n;
    WriteFile(f, text, (DWORD)strlen(text), &n, NULL);
    CloseHandle(f);
}

static int has(const wchar_t *path, const char *text) {
    char buf[64];
    DWORD n = 0;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    ReadFile(f, buf, sizeof buf - 1, &n, NULL);
    CloseHandle(f);
    buf[n] = 0;
    return !strcmp(buf, text);
}

static int run(const wchar_t *cmd) {
    wchar_t line[1024];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    wcsncpy(line, cmd, 1023);
    line[1023] = 0;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(NULL, line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return 0;
    WaitForSingleObject(pi.hProcess, 60000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

static int wait_state(int want) {
    upd_status_t st;
    int i;
    for (i = 0; i < 600; i++) {
        upd_status(&st);
        if (st.state == want)
            return 1;
        if (st.state == UPD_ERROR || st.state == UPD_LATEST)
            return st.state == want;
        Sleep(50);
    }
    return 0;
}

static void test_end_to_end(void) {
    static const wchar_t *const bins[] = {L"ngb-launcher.exe", L"ngb-loader.exe", L"ngb-core.dll"};
    wchar_t root[MAX_PATH], src[MAX_PATH], app[MAX_PATH], data[MAX_PATH], p[MAX_PATH], q[MAX_PATH];
    wchar_t cmd[1024], why[256];
    char hex[65], man[512];
    upd_status_t st;
    WIN32_FILE_ATTRIBUTE_DATA fa;
    int k;
    GetTempPathW(MAX_PATH, root);
    wcsncat(root, L"ngb_update_test", MAX_PATH - wcslen(root) - 1);
    remove_tree(root);
    _snwprintf(src, MAX_PATH, L"%ls\\release", root);
    _snwprintf(app, MAX_PATH, L"%ls\\app", root);
    _snwprintf(data, MAX_PATH, L"%ls\\data", root);
    make_dirs(src);
    make_dirs(app);
    make_dirs(data);
    for (k = 0; k < 3; k++) {
        _snwprintf(p, MAX_PATH, L"%ls\\%ls", src, bins[k]);
        put(p, "new");
        _snwprintf(p, MAX_PATH, L"%ls\\%ls", app, bins[k]);
        put(p, "old");
    }
    _snwprintf(p, MAX_PATH, L"%ls\\ngb-port-v9.9.9-win32.zip", src);
    GetSystemDirectoryW(q, MAX_PATH);
    _snwprintf(cmd, 1024, L"\"%ls\\tar.exe\" -a -cf \"%ls\" -C \"%ls\" ngb-launcher.exe ngb-loader.exe ngb-core.dll",
               q, p, src);
    CHECK(run(cmd));
    CHECK(upd_sha256_file(p, hex));
    CHECK(GetFileAttributesExW(p, GetFileExInfoStandard, &fa));
    sprintf(man, "version=9.9.9\nzip=ngb-port-v9.9.9-win32.zip\nsize=%lu\nsha256=%s\nnotes=https://example.org/\n",
            fa.nFileSizeLow, hex);
    _snwprintf(q, MAX_PATH, L"%ls\\ngb-update.txt", src);
    put(q, man);

    /* Same version: nothing to do. */
    CHECK(upd_check_start(q, "9.9.9") && wait_state(UPD_LATEST));
    /* Older one running: download, verify, extract, apply. */
    CHECK(upd_check_start(q, "0.3.0-dev") && wait_state(UPD_AVAILABLE));
    CHECK(upd_download_start(data) && wait_state(UPD_READY));
    upd_status(&st);
    CHECK(st.done == (int64_t)fa.nFileSizeLow);
    CHECK(upd_apply(app, st.staged, why, 256));
    for (k = 0; k < 3; k++) {
        _snwprintf(p, MAX_PATH, L"%ls\\%ls", app, bins[k]);
        CHECK(has(p, "new"));
        _snwprintf(p, MAX_PATH, L"%ls\\%ls.old", app, bins[k]);
        CHECK(has(p, "old"));
    }
    upd_remove_old(app);
    CHECK(GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES);

    /* A staged folder without one binary is refused and nothing changes. */
    _snwprintf(p, MAX_PATH, L"%ls\\ngb-core.dll", st.staged);
    DeleteFileW(p);
    CHECK(!upd_apply(app, st.staged, why, 256));
    _snwprintf(p, MAX_PATH, L"%ls\\ngb-core.dll", app);
    CHECK(has(p, "new"));

    /* A zip that does not match the manifest hash never reaches the staged folder. */
    _snwprintf(p, MAX_PATH, L"%ls\\ngb-port-v9.9.9-win32.zip", src);
    put(p, "tampered!");
    sprintf(man, "version=9.9.9\nzip=ngb-port-v9.9.9-win32.zip\nsize=9\nsha256=%s\nnotes=https://example.org/\n", hex);
    put(q, man);
    CHECK(upd_check_start(q, "0.3.0") && wait_state(UPD_AVAILABLE));
    CHECK(upd_download_start(data) && wait_state(UPD_ERROR));
    upd_status(&st);
    CHECK(st.err == UPDE_HASH);
    remove_tree(root);
}

int main(void) {
    test_manifest();
    test_versions();
    test_sha();
    test_end_to_end();
    if (g_fail) {
        printf("update_test: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("update_test: OK\n");
    return 0;
}
