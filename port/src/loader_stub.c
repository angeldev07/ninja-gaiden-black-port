/*
 * loader_stub.c: ngb-loader.exe, the minimal exe that reserves the XBE memory.
 *
 * The XBE is not relocatable: its image has to be at 0x00010000. In a Win32 process
 * that low area fills up quickly (heaps, stacks, NLS, process parameters), so a
 * VirtualAlloc at startup is not enough. The trick (the same one Cxbx-Reloaded uses with its
 * cxbxr-ldr.exe) is to link this exe at 0x10000 with /FIXED and a huge BSS section
 * (.xbox): the Windows loader maps the image when creating the process, before anything else,
 * and [0x10000, NGB_RESERVE_END) is ours.
 *
 * The exe does not use the CRT: it loads ngb-core.dll (which lives at another address) and hands
 * over control. ngb_main() overwrites the PE header and this very code with the XBE, so it never
 * returns: it ends the process itself.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define NGB_RESERVE_END 0x01000000u
#define NGB_RESERVE_TAIL 0x00010000u /* after the XBE: page of the shadow PE header (memory.c) */
#define STUB_TEXT_START 0x00011000u  /* the BSS goes after the stub's .text: a little to spare */

#pragma bss_seg(".xbox")
unsigned char g_xbox_reserve[NGB_RESERVE_END + NGB_RESERVE_TAIL - STUB_TEXT_START];
#pragma bss_seg()

typedef int(__cdecl *ngb_main_fn)(void);

static void fail(const char *msg) {
    DWORD n = 0, len = 0;
    while (msg[len])
        len++;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, len, &n, NULL);
    ExitProcess(1);
}

void __stdcall ngb_stub_entry(void) {
    WCHAR path[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    HMODULE core;
    ngb_main_fn entry;
    /* Touch the reserve so the linker does not discard it. */
    g_xbox_reserve[0] = 0;
    while (len && path[len - 1] != L'\\')
        len--;
    if (len + 13 >= MAX_PATH)
        fail("ngb-loader: path too long\n");
    path[len] = 0;
    lstrcatW(path, L"ngb-core.dll");
    core = LoadLibraryW(path);
    if (!core)
        fail("ngb-loader: ngb-core.dll not found next to the exe\n");
    entry = (ngb_main_fn)(void *)GetProcAddress(core, "ngb_main");
    if (!entry)
        fail("ngb-loader: ngb-core.dll does not export ngb_main\n");
    ExitProcess((UINT)entry());
}
