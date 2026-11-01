/*
 * remote.c: see remote.h.
 *
 * One client at a time. Each line is a command, each answer one line ("ok ..." or "error ...").
 * The pipe rejects remote clients and keeps the default security (the user who started the
 * loader, administrators and SYSTEM can write to it).
 */
#include "remote.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ach/ach.h"
#include "gfx/present.h"
#include "input/input.h"
#include "ngb.h"

#define LINE_MAX_ 1024
#define PEEK_MAX 256

static int committed(uintptr_t at, MEMORY_BASIC_INFORMATION *m) {
    return VirtualQuery((const void *)at, m, sizeof *m) && m->State == MEM_COMMIT &&
           !(m->Protect & (PAGE_NOACCESS | PAGE_GUARD));
}

/* The game image (its code and globals: the variables the achievements read), in the
 * {u32 va, u32 len, bytes} records of --dump-ram, to %NGB_DATA%\runs\remote-<name>.bin. */
static int dump_image(const char *name, char *out, size_t n) {
    const char *data = getenv("NGB_DATA");
    char path[MAX_PATH];
    uintptr_t at = g_ngb_xbe.base, end = (uintptr_t)g_ngb_xbe.base + g_ngb_xbe.size_of_image;
    size_t total = 0;
    FILE *f;
    if (!data || !*data || strpbrk(name, "\\/:*?\"<>|") || !*name)
        return snprintf(out, n, "error a plain name, and NGB_DATA set"), -1;
    snprintf(path, sizeof path, "%s\\runs\\remote-%s.bin", data, name);
    if ((f = fopen(path, "wb")) == NULL)
        return snprintf(out, n, "error cannot write %s", path), -1;
    while (at < end) {
        MEMORY_BASIC_INFORMATION m;
        uintptr_t stop;
        if (!VirtualQuery((const void *)at, &m, sizeof m))
            break;
        stop = (uintptr_t)m.BaseAddress + m.RegionSize;
        if (stop > end)
            stop = end;
        if (committed(at, &m)) {
            uint32_t head[2] = {(uint32_t)at, (uint32_t)(stop - at)};
            fwrite(head, sizeof head, 1, f);
            fwrite((const void *)at, 1, stop - at, f);
            total += stop - at;
        }
        at = stop;
    }
    fclose(f);
    return snprintf(out, n, "ok %zu bytes to %s", total, path), 0;
}

/* Up to PEEK_MAX bytes of guest memory as hex. */
static void peek(const char *arg, char *out, size_t n) {
    char *end;
    unsigned long va = strtoul(arg, &end, 16), len = strtoul(end, NULL, 0), i;
    MEMORY_BASIC_INFORMATION m;
    size_t o;
    if (!len)
        len = 16;
    if (len > PEEK_MAX)
        len = PEEK_MAX;
    if (!committed(va, &m) || (uint64_t)va + len > (uint64_t)(uintptr_t)m.BaseAddress + m.RegionSize) {
        snprintf(out, n, "error 0x%08lX is not readable", va);
        return;
    }
    o = (size_t)snprintf(out, n, "ok 0x%08lX:", va);
    for (i = 0; i < len && o + 4 < n; i++)
        o += (size_t)snprintf(out + o, n - o, " %02X", *(volatile const uint8_t *)(uintptr_t)(va + i));
}

static void reply(HANDLE pipe, const char *text) {
    char line[LINE_MAX_ + 2];
    DWORD put;
    int n = snprintf(line, sizeof line, "%s\n", text);
    if (n > 0)
        WriteFile(pipe, line, (DWORD)(n < (int)sizeof line ? n : (int)sizeof line - 1), &put, NULL);
}

static int on_off(const char *arg, int *flag) {
    if (!strcmp(arg, "on"))
        *flag = 1;
    else if (!strcmp(arg, "off"))
        *flag = 0;
    else
        return -1;
    return 0;
}

static void command(HANDLE pipe, char *line) {
    char out[LINE_MAX_], err[160], *arg = strchr(line, ' ');
    if (arg)
        *arg++ = 0;
    else
        arg = line + strlen(line);
    if (!strcmp(line, "input")) {
        if (ngb_input_inject(arg, err, sizeof err)) {
            snprintf(out, sizeof out, "error %s", err);
            reply(pipe, out);
        } else {
            reply(pipe, "ok");
        }
    } else if (!strcmp(line, "trace") || !strcmp(line, "god")) {
        int on = 0;
        if (on_off(arg, &on)) {
            reply(pipe, "error expected on or off");
        } else {
            if (!strcmp(line, "trace"))
                ngb_ach_set_trace(on);
            else
                g_ngb_opts.ach_god = on;
            ngb_log("remote: %s %s", line, arg);
            reply(pipe, "ok");
        }
    } else if (!strcmp(line, "events")) {
        char ev[LINE_MAX_ - 8];
        ngb_ach_events(ev, sizeof ev);
        snprintf(out, sizeof out, "ok %s", ev);
        reply(pipe, out);
    } else if (!strcmp(line, "dump")) {
        dump_image(arg, out, sizeof out);
        reply(pipe, out);
    } else if (!strcmp(line, "dumpall")) {
        const char *data = getenv("NGB_DATA");
        char path[MAX_PATH];
        if (!data || !*data || !*arg || strpbrk(arg, "/:*?<>|") || strchr(arg, 92)) {
            reply(pipe, "error a plain name, and NGB_DATA set");
        } else {
            snprintf(path, sizeof path, "%s%cruns%cremote-%s-all.bin", data, 92, 92, arg);
            snprintf(out, sizeof out, ngb_dump_guest_ram(path) ? "error cannot write %s" : "ok all guest RAM to %s", path);
            reply(pipe, out);
        }
    } else if (!strcmp(line, "peek")) {
        peek(arg, out, sizeof out);
        reply(pipe, out);
    } else if (!strcmp(line, "shot")) {
        gfx_request_shot();
        reply(pipe, "ok the next frame goes to %NGB_DATA%\\runs\\shots");
    } else if (!strcmp(line, "status")) {
        char st[512];
        ngb_ach_status(st, sizeof st);
        snprintf(out, sizeof out, "ok polls %u %s", ngb_input_polls(), st);
        reply(pipe, out);
    } else if (!strcmp(line, "help") || !*line) {
        reply(pipe, "ok commands: input <script, times from now>, trace on|off, events, god on|off, shot, status, "
                    "dump <name>, dumpall <name>, peek <hex address> [length]");
    } else {
        snprintf(out, sizeof out, "error unknown command '%.60s' (help)", line);
        reply(pipe, out);
    }
}

static DWORD WINAPI serve(LPVOID unused) {
    (void)unused;
    for (;;) {
        char line[LINE_MAX_];
        size_t len = 0;
        HANDLE pipe = CreateNamedPipeA(NGB_REMOTE_PIPE, PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                       1, 4096, 4096, 0, NULL);
        if (pipe == INVALID_HANDLE_VALUE) {
            ngb_log("remote: could not create %s (error %lu)", NGB_REMOTE_PIPE, GetLastError());
            return 1;
        }
        if (ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            for (;;) {
                char c;
                DWORD got;
                if (!ReadFile(pipe, &c, 1, &got, NULL) || !got)
                    break;
                if (c == '\r')
                    continue;
                if (c != '\n' && len + 1 < sizeof line) {
                    line[len++] = c;
                    continue;
                }
                line[len] = 0;
                len = 0;
                command(pipe, line);
            }
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

void ngb_remote_start(void) {
    HANDLE t = CreateThread(NULL, 0, serve, NULL, 0, NULL);
    if (t) {
        ngb_log("remote: listening on %s (input, trace, god, shot, status)", NGB_REMOTE_PIPE);
        CloseHandle(t);
    }
}
