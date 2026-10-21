/*
 * loader_stub.c: ngb-loader.exe, el exe mínimo que reserva la memoria del XBE.
 *
 * El XBE no es reubicable: su imagen tiene que estar en 0x00010000. En un proceso Win32
 * esa zona baja se llena enseguida (heaps, pilas, NLS, parámetros del proceso), así que no
 * basta con un VirtualAlloc al empezar. El truco (el mismo que usa Cxbx-Reloaded con su
 * cxbxr-ldr.exe) es que este exe se enlace en 0x10000 con /FIXED y una sección BSS enorme
 * (.xbox): el cargador de Windows mapea la imagen al crear el proceso, antes que nada,
 * y [0x10000, NGB_RESERVE_END) queda nuestro.
 *
 * El exe no usa la CRT: carga ngb-core.dll (que vive en otra dirección) y le cede el
 * control. ngb_main() pisa la cabecera PE y este mismo código con el XBE, así que nunca
 * vuelve: termina el proceso ella misma.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define NGB_RESERVE_END 0x01000000u
#define NGB_RESERVE_TAIL 0x00010000u /* tras el XBE: página de la cabecera PE sombra (memory.c) */
#define STUB_TEXT_START 0x00011000u  /* la BSS va detrás del .text del stub: sobra un poco */

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
    /* Toca la reserva para que el enlazador no la descarte. */
    g_xbox_reserve[0] = 0;
    while (len && path[len - 1] != L'\\')
        len--;
    if (len + 13 >= MAX_PATH)
        fail("ngb-loader: ruta demasiado larga\n");
    path[len] = 0;
    lstrcatW(path, L"ngb-core.dll");
    core = LoadLibraryW(path);
    if (!core)
        fail("ngb-loader: no se encuentra ngb-core.dll junto al exe\n");
    entry = (ngb_main_fn)(void *)GetProcAddress(core, "ngb_main");
    if (!entry)
        fail("ngb-loader: ngb-core.dll no exporta ngb_main\n");
    ExitProcess((UINT)entry());
}
