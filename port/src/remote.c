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
#include <string.h>

#include "ach/ach.h"
#include "gfx/present.h"
#include "input/input.h"
#include "ngb.h"

#define LINE_MAX_ 1024

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
    } else if (!strcmp(line, "shot")) {
        gfx_request_shot();
        reply(pipe, "ok the next frame goes to %NGB_DATA%\\runs\\shots");
    } else if (!strcmp(line, "status")) {
        char st[512];
        ngb_ach_status(st, sizeof st);
        snprintf(out, sizeof out, "ok polls %u %s", ngb_input_polls(), st);
        reply(pipe, out);
    } else if (!strcmp(line, "help") || !*line) {
        reply(pipe, "ok commands: input <script, times from now>, trace on|off, events, god on|off, shot, status");
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
