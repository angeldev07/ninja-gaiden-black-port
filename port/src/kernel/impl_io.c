/*
 * impl_io.c: sistema de archivos (Nt*File, Io*SymbolicLink).
 *
 * Rutas Xbox -> rutas del PC:
 *   \??\X:            -> tabla de enlaces simbólicos (IoCreateSymbolicLink)
 *   \Device\CdRom0    -> carpeta del XBE (los datos del juego): SOLO LECTURA, nunca se
 *                        escribe ni se crea nada ahí
 *   \Device\Harddisk0\PartitionN -> %NGB_DATA%\runs\hdd\partitionN (se crea al usarla)
 * Las clases de información de archivo/volumen tienen el mismo layout que en NT, así que
 * Query/SetInformation se reenvían a ntdll con el handle de Windows.
 */
#include "../ngb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hostpath.h"
#include "hostpath.h"
#include "hostpath.h"
#include "kernel.h"

#define MAX_LINKS 64
#define MAX_DIRS 256
#define XPATH 512

typedef struct link {
    char name[64];
    char target[XPATH];
} link_t;

typedef struct dir_handle {
    HANDLE h;
    char xpath[XPATH];
    HANDLE find; /* enumeración de NtQueryDirectoryFile en curso */
    int entries; /* entradas devueltas desde el último RestartScan */
} dir_handle_t;

static link_t g_links[MAX_LINKS];
static dir_handle_t g_dirs[MAX_DIRS];
static CRITICAL_SECTION g_io_lock;
static char g_game_dir[MAX_PATH];
static char g_hdd_root[MAX_PATH];
static INIT_ONCE g_io_once = INIT_ONCE_STATIC_INIT;
static int g_io_ok; /* 0 si no hay raíz del HDD (sin NGB_DATA ni USERPROFILE): se deniega */

typedef LONG(WINAPI *nt_query_info_fn)(HANDLE, void *, void *, ULONG, ULONG);

static BOOL CALLBACK io_init(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    char *slash;
    const char *data = getenv("NGB_DATA"), *home = getenv("USERPROFILE");
    (void)once;
    (void)param;
    (void)ctx;
    InitializeCriticalSection(&g_io_lock);
    GetFullPathNameA(g_ngb_opts.xbe_path, MAX_PATH, g_game_dir, NULL);
    slash = strrchr(g_game_dir, '\\');
    if (slash)
        *slash = 0;
    if (data && *data)
        snprintf(g_hdd_root, sizeof g_hdd_root, "%s\\runs\\hdd", data);
    else if (home && *home)
        snprintf(g_hdd_root, sizeof g_hdd_root, "%s\\ngb-data\\runs\\hdd", home);
    else
        ngb_log("    · archivo: sin NGB_DATA ni USERPROFILE: el disco duro virtual queda desactivado");
    g_io_ok = g_hdd_root[0] != 0;
    return TRUE;
}

static void io_init_once(void) {
    InitOnceExecuteOnce(&g_io_once, io_init, NULL, NULL);
}

/* ANSI_STRING/STRING de Xbox -> C string. Devuelve 1 si cupo entera, 0 si se truncó. */
static int xstring(const void *s, char *out, size_t outlen) {
    const uint16_t *str = (const uint16_t *)s;
    size_t len;
    if (!s) {
        out[0] = 0;
        return 1;
    }
    len = str[0] < outlen - 1 ? str[0] : outlen - 1;
    memcpy(out, (const char *)(uintptr_t) * (const uint32_t *)(str + 2), len);
    out[len] = 0;
    return len == str[0];
}

static const char *dir_xpath(HANDLE h) {
    int i;
    for (i = 0; i < MAX_DIRS; i++)
        if (g_dirs[i].h == h)
            return g_dirs[i].xpath;
    return NULL;
}

static void dir_remember(HANDLE h, const char *xpath) {
    int i;
    EnterCriticalSection(&g_io_lock);
    for (i = 0; i < MAX_DIRS; i++)
        if (!g_dirs[i].h) {
            g_dirs[i].h = h;
            snprintf(g_dirs[i].xpath, XPATH, "%s", xpath);
            break;
        }
    LeaveCriticalSection(&g_io_lock);
}

static void dir_forget(HANDLE h) {
    int i;
    EnterCriticalSection(&g_io_lock);
    for (i = 0; i < MAX_DIRS; i++)
        if (g_dirs[i].h == h) {
            if (g_dirs[i].find && g_dirs[i].find != INVALID_HANDLE_VALUE)
                FindClose(g_dirs[i].find);
            g_dirs[i].find = NULL;
            g_dirs[i].h = NULL;
        }
    LeaveCriticalSection(&g_io_lock);
}

/* Ruta Xbox completa (OBJECT_ATTRIBUTES con RootDirectory opcional). */
static void full_xpath(const uint32_t *oa, char *out, size_t outlen) {
    char name[XPATH];
    const char *root = oa[0] ? dir_xpath((HANDLE)(uintptr_t)oa[0]) : NULL;
    xstring((const void *)(uintptr_t)oa[1], name, sizeof name);
    if (oa[0] == 0xFFFFFFFDu) /* ObDosDevicesDirectory(): nombres relativos a \?? */
        root = "\\??";
    if (root)
        snprintf(out, outlen, "%s\\%s", root, name);
    else
        snprintf(out, outlen, "%s", name);
}

/* Crea `dir` y sus padres (ya validado: cuelga de la raíz del HDD). */
static void make_dirs(const char *dir) {
    char tmp[MAX_PATH];
    size_t k;
    snprintf(tmp, sizeof tmp, "%s", dir);
    for (k = 3; tmp[k]; k++)
        if (tmp[k] == '\\') {
            tmp[k] = 0;
            CreateDirectoryA(tmp, NULL);
            tmp[k] = '\\';
        }
    CreateDirectoryA(tmp, NULL);
}

/*
 * Ruta de dispositivo Xbox (ya sin enlaces) -> PC. 0 = sin mapeo o insegura, 1 = archivo
 * o carpeta, 2 = volumen en crudo. Todo resultado queda confinado (hostpath_join) a la
 * carpeta del juego (D:, solo lectura) o a la de su partición en el HDD virtual.
 */
static int device_path(const char *path, char *out, size_t outlen, int *readonly) {
    char dir[MAX_PATH];
    const char *rest;
    *readonly = 0;
    out[0] = 0;
    if (_strnicmp(path, "\\Device\\CdRom0", 14) == 0) {
        *readonly = 1;
        return hostpath_join(g_game_dir, path + 14, out, outlen) ? 0 : 1;
    }
    if (_strnicmp(path, "\\Device\\Harddisk0\\Partition", 27) != 0 || path[27] < '0' ||
        path[27] > '9' || (path[28] != 0 && path[28] != '\\') || !g_io_ok)
        return 0;
    rest = path + 28;
    /* Validar ANTES de crear carpetas: una ruta insegura no deja rastro en el disco. */
    if (!hostpath_rel_ok(rest))
        return 0;
    snprintf(dir, sizeof dir, "%s\\partition%c", g_hdd_root, path[27]);
    if (*rest == 0) {
        /* El volumen en crudo (sin '\' final): un archivo imagen partitionN.raw. */
        if ((size_t)snprintf(out, outlen, "%s.raw", dir) >= outlen)
            return 0;
        make_dirs(g_hdd_root);
        return 2;
    }
    if (hostpath_join(dir, rest, out, outlen))
        return 0;
    make_dirs(dir);
    return 1;
}

/* Sustituye los enlaces simbólicos (\??\D: ...) hasta llegar a un \Device\... */
static void resolve_links(const char *xpath, char *path, size_t pathlen) {
    int i, depth;
    snprintf(path, pathlen, "%s", xpath);
    for (depth = 0; depth < 4; depth++) {
        int replaced = 0;
        for (i = 0; i < MAX_LINKS && !replaced; i++) {
            size_t n = strlen(g_links[i].name);
            if (n && _strnicmp(path, g_links[i].name, n) == 0 &&
                (path[n] == 0 || path[n] == '\\')) {
                char rest[XPATH];
                snprintf(rest, sizeof rest, "%s", path + n);
                snprintf(path, pathlen, "%s%s", g_links[i].target, rest);
                replaced = 1;
            }
        }
        if (!replaced)
            break;
    }
}

/* Traduce una ruta Xbox a una del PC. Devuelve 0 si no hay mapeo; *readonly = 1 en D:. */
static int host_path(const char *xpath, char *out, size_t outlen, int *readonly) {
    char path[XPATH];
    resolve_links(xpath, path, sizeof path);
    return device_path(path, out, outlen, readonly);
}

#define MAX_RAW 16
static HANDLE g_raw[MAX_RAW];
static int g_raw_part[MAX_RAW];

/* Partición del volumen en crudo abierto con `h` (0..9), o -1 si no es uno. */
static int raw_partition(HANDLE h) {
    int i;
    for (i = 0; i < MAX_RAW; i++)
        if (g_raw[i] == h)
            return g_raw_part[i];
    return -1;
}

static int is_raw(HANDLE h) {
    return raw_partition(h) >= 0;
}

static void raw_remember(HANDLE h, int part) {
    int i;
    EnterCriticalSection(&g_io_lock);
    for (i = 0; i < MAX_RAW; i++)
        if (!g_raw[i]) {
            g_raw[i] = h;
            g_raw_part[i] = part;
            break;
        }
    LeaveCriticalSection(&g_io_lock);
}

/* Tabla de particiones del disco duro retail de 8 GB (xboxdevwiki: Xbox Hard Drive). */
static const uint64_t k_part_offset[6] = {0, 0xABE80000ull, 0x8CA80000ull, 0x00080000ull,
                                          0x2EE80000ull, 0x5DC80000ull};
static const uint64_t k_part_length[6] = {0x1DD156000ull, 0x1312D6000ull, 0x1F400000ull,
                                          0x2EE00000ull, 0x2EE00000ull, 0x2EE00000ull};

#define IOCTL_DISK_GET_DRIVE_GEOMETRY_ 0x00070000u
#define IOCTL_DISK_GET_PARTITION_INFO_ 0x00074004u

/* 196 NTSTATUS NtDeviceIoControlFile(HANDLE, HANDLE Event, PIO_APC_ROUTINE, PVOID ApcContext,
 *     PIO_STATUS_BLOCK, ULONG IoControlCode, PVOID In, ULONG InLen, PVOID Out, ULONG OutLen) */
static uint64_t NtDeviceIoControlFile(uint32_t *args) {
    int part = raw_partition((HANDLE)PTR(0));
    uint32_t code = ARG(5), *out = (uint32_t *)PTR(8), *iosb = (uint32_t *)PTR(4);
    uint64_t len = part >= 0 && part < 6 ? k_part_length[part] : 0;
    if (code == IOCTL_DISK_GET_DRIVE_GEOMETRY_ && part >= 0 && ARG(9) >= 0x18) {
        out[0] = (uint32_t)(len / 512); /* Cylinders = sectores */
        out[1] = (uint32_t)((len / 512) >> 32);
        out[2] = 12; /* FixedMedia */
        out[3] = 1;  /* TracksPerCylinder */
        out[4] = 1;  /* SectorsPerTrack */
        out[5] = 512;
        iosb[0] = STATUS_SUCCESS;
        iosb[1] = 0x18;
    } else if (code == IOCTL_DISK_GET_PARTITION_INFO_ && part >= 0 && ARG(9) >= 0x20) {
        uint64_t off = part < 6 ? k_part_offset[part] : 0;
        memset(out, 0, 0x20);
        out[0] = (uint32_t)off;
        out[1] = (uint32_t)(off >> 32);
        out[2] = (uint32_t)len;
        out[3] = (uint32_t)(len >> 32);
        out[4] = (uint32_t)(off / 512); /* HiddenSectors */
        out[5] = (uint32_t)part;        /* PartitionNumber */
        ((uint8_t *)out)[0x18] = 0x07;  /* PartitionType */
        ((uint8_t *)out)[0x1A] = 1;     /* RecognizedPartition */
        iosb[0] = STATUS_SUCCESS;
        iosb[1] = 0x20;
    } else {
        ngb_log("    · NtDeviceIoControlFile: IOCTL 0x%08X (partición %d) no soportado", code, part);
        iosb[0] = 0xC0000010u; /* STATUS_INVALID_DEVICE_REQUEST */
        iosb[1] = 0;
        return RET32(0xC0000010u);
    }
    ngb_log("    · NtDeviceIoControlFile: IOCTL 0x%08X partición %d (%llu MB)", code, part,
            len >> 20);
    if (ARG(1))
        SetEvent((HANDLE)PTR(1));
    return STATUS_SUCCESS;
}

/* 200 NTSTATUS NtFsControlFile(mismos argumentos que NtDeviceIoControlFile) */
static uint64_t NtFsControlFile(uint32_t *args) {
    uint32_t *iosb = (uint32_t *)PTR(4);
    ngb_log("    · NtFsControlFile: FSCTL 0x%08X (se acepta sin hacer nada)", ARG(5));
    iosb[0] = STATUS_SUCCESS;
    iosb[1] = 0;
    if (ARG(1))
        SetEvent((HANDLE)PTR(1));
    return STATUS_SUCCESS;
}

static uint32_t status_from_win32(DWORD err) {
    switch (err) {
    case ERROR_FILE_NOT_FOUND: return STATUS_OBJECT_NAME_NOT_FOUND;
    case ERROR_PATH_NOT_FOUND: return STATUS_OBJECT_PATH_NOT_FOUND;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS: return STATUS_OBJECT_NAME_COLLISION;
    case ERROR_ACCESS_DENIED: return STATUS_ACCESS_DENIED;
    default: return STATUS_UNSUCCESSFUL;
    }
}

#define FILE_DIRECTORY_FILE_ 0x1u
#define FILE_NON_DIRECTORY_FILE_ 0x40u

/* Implementación común de NtCreateFile/NtOpenFile. */
static uint32_t open_file(uint32_t *handle_out, uint32_t access, const uint32_t *oa, uint32_t *iosb,
                          uint32_t attributes, uint32_t disposition, uint32_t options) {
    static const DWORD dispo[] = {CREATE_ALWAYS, OPEN_EXISTING, CREATE_NEW,
                                  OPEN_ALWAYS,   TRUNCATE_EXISTING, CREATE_ALWAYS};
    char xpath[XPATH], hpath[MAX_PATH];
    int readonly, kind;
    DWORD waccess = 0, attrs, flags = FILE_FLAG_BACKUP_SEMANTICS;
    HANDLE h;
    io_init_once();
    full_xpath(oa, xpath, sizeof xpath);
    kind = host_path(xpath, hpath, sizeof hpath, &readonly);
    if (kind == 2) {
        h = CreateFileA(hpath, GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            iosb[0] = STATUS_UNSUCCESSFUL;
            return STATUS_UNSUCCESSFUL;
        }
        raw_remember(h, hpath[strlen(hpath) - 5] - '0');
        ngb_log("    · volumen en crudo: '%s' -> '%s' handle 0x%p", xpath, hpath, h);
        *handle_out = (uint32_t)(uintptr_t)h;
        iosb[0] = STATUS_SUCCESS;
        iosb[1] = 1;
        return STATUS_SUCCESS;
    }
    if (!kind) {
        ngb_log("    · archivo: '%s' sin mapeo en el PC", xpath);
        iosb[0] = STATUS_OBJECT_PATH_NOT_FOUND;
        iosb[1] = 0;
        return STATUS_OBJECT_PATH_NOT_FOUND;
    }
    if (access & (GENERIC_READ | FILE_READ_DATA | 0x80000000u))
        waccess |= GENERIC_READ;
    if (access & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE | 0x40000000u))
        waccess |= GENERIC_WRITE;
    if (!waccess)
        waccess = FILE_READ_ATTRIBUTES;
    if (disposition > 5)
        disposition = 1;
    /* Solo atributos de archivo: los bits FILE_FLAG_* (DELETE_ON_CLOSE, OVERLAPPED,
     * NO_BUFFERING...) que el juego pase no deben llegar a CreateFileA. */
    attributes &= FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM |
                  FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY;
    if (readonly && ((waccess & GENERIC_WRITE) || (disposition != 1 && disposition != 3))) {
        /* D: es el disco del juego: se abre solo para leer; crear algo ahí es un error. */
        if (disposition != 1 && disposition != 3) {
            ngb_log("    · archivo: '%s' (D:) pide crear/escribir: denegado", xpath);
            iosb[0] = STATUS_ACCESS_DENIED;
            return STATUS_ACCESS_DENIED;
        }
        waccess &= ~(DWORD)GENERIC_WRITE;
        if (!waccess)
            waccess = GENERIC_READ;
    }
    attrs = GetFileAttributesA(hpath);
    if (options & FILE_DIRECTORY_FILE_) {
        if (attrs == INVALID_FILE_ATTRIBUTES && !readonly && disposition != 1)
            CreateDirectoryA(hpath, NULL);
        h = CreateFileA(hpath, waccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, flags, NULL);
    } else {
        h = CreateFileA(hpath, waccess, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, readonly ? OPEN_EXISTING : dispo[disposition],
                        flags | (attributes ? attributes : FILE_ATTRIBUTE_NORMAL), NULL);
        /* `attributes` ya viene enmascarado a FILE_ATTRIBUTE_* (ver más arriba). */
    }
    if (h == INVALID_HANDLE_VALUE) {
        uint32_t st = status_from_win32(GetLastError());
        ngb_log("    · archivo: '%s' -> '%s' falló (%lu)", xpath, hpath, GetLastError());
        iosb[0] = st;
        iosb[1] = 0;
        return st;
    }
    attrs = GetFileAttributesA(hpath);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
        dir_remember(h, xpath);
    ngb_log("    · archivo: '%s' -> '%s' handle 0x%p%s", xpath, hpath, h, readonly ? " (D:, lectura)" : "");
    *handle_out = (uint32_t)(uintptr_t)h;
    iosb[0] = STATUS_SUCCESS;
    iosb[1] = 1; /* FILE_OPENED */
    return STATUS_SUCCESS;
}

/* 190 NTSTATUS NtCreateFile(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
 *     PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess,
 *     ULONG CreateDisposition, ULONG CreateOptions) */
static uint64_t NtCreateFile(uint32_t *args) {
    return RET32(open_file((uint32_t *)PTR(0), ARG(1), (const uint32_t *)PTR(2), (uint32_t *)PTR(3),
                           ARG(5), ARG(7), ARG(8)));
}

/* 202 NTSTATUS NtOpenFile(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
 *                         ULONG ShareAccess, ULONG OpenOptions) */
static uint64_t NtOpenFile(uint32_t *args) {
    return RET32(open_file((uint32_t *)PTR(0), ARG(1), (const uint32_t *)PTR(2), (uint32_t *)PTR(3),
                           0, 1, ARG(5)));
}

static void finish_io(uint32_t event, uint32_t *iosb, uint32_t status, uint32_t info) {
    iosb[0] = status;
    iosb[1] = info;
    if (event)
        SetEvent((HANDLE)(uintptr_t)event);
}

/* 219 NTSTATUS NtReadFile(HANDLE, HANDLE Event, PIO_APC_ROUTINE, PVOID ApcContext,
 *     PIO_STATUS_BLOCK, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset) */
static uint64_t NtReadFile(uint32_t *args) {
    OVERLAPPED ov;
    DWORD got = 0;
    BOOL ok;
    const uint32_t *off = (const uint32_t *)PTR(7);
    memset(&ov, 0, sizeof ov);
    if (off) {
        ov.Offset = off[0];
        ov.OffsetHigh = off[1];
    }
    ok = ReadFile((HANDLE)PTR(0), PTR(5), ARG(6), &got, off ? &ov : NULL);
    if (is_raw((HANDLE)PTR(0))) {
        /* Volumen en crudo: lo que no existe en la imagen se lee como ceros. */
        if (got < ARG(6))
            memset((uint8_t *)PTR(5) + got, 0, ARG(6) - got);
        ngb_log("    · lectura cruda de %u B en 0x%llX", ARG(6),
                off ? ((uint64_t)off[1] << 32 | off[0]) : 0ull);
        finish_io(ARG(1), (uint32_t *)PTR(4), STATUS_SUCCESS, ARG(6));
        return STATUS_SUCCESS;
    }
    if (ARG(2))
        ngb_log("    · NtReadFile con rutina APC 0x%08X: no se llama", ARG(2));
    if (!ok && GetLastError() != ERROR_HANDLE_EOF) {
        finish_io(ARG(1), (uint32_t *)PTR(4), STATUS_UNSUCCESSFUL, 0);
        return RET32(STATUS_UNSUCCESSFUL);
    }
    if (!got && ARG(6)) {
        finish_io(ARG(1), (uint32_t *)PTR(4), STATUS_END_OF_FILE, 0);
        return RET32(STATUS_END_OF_FILE);
    }
    finish_io(ARG(1), (uint32_t *)PTR(4), STATUS_SUCCESS, got);
    return STATUS_SUCCESS;
}

/* 236 NTSTATUS NtWriteFile(... mismos argumentos que NtReadFile) */
static uint64_t NtWriteFile(uint32_t *args) {
    OVERLAPPED ov;
    DWORD put = 0;
    const uint32_t *off = (const uint32_t *)PTR(7);
    memset(&ov, 0, sizeof ov);
    if (off) {
        ov.Offset = off[0];
        ov.OffsetHigh = off[1];
    }
    if (is_raw((HANDLE)PTR(0)))
        ngb_log("    · escritura cruda de %u B en 0x%llX", ARG(6),
                off ? ((uint64_t)off[1] << 32 | off[0]) : 0ull);
    if (!WriteFile((HANDLE)PTR(0), PTR(5), ARG(6), &put, off ? &ov : NULL)) {
        finish_io(ARG(1), (uint32_t *)PTR(4), STATUS_ACCESS_DENIED, 0);
        return RET32(STATUS_ACCESS_DENIED);
    }
    finish_io(ARG(1), (uint32_t *)PTR(4), STATUS_SUCCESS, put);
    return STATUS_SUCCESS;
}

static FARPROC ntdll_proc(const char *name) {
    return GetProcAddress(GetModuleHandleA("ntdll.dll"), name);
}

/* 211 NtQueryInformationFile(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG Length, CLASS) y
 * 218 NtQueryVolumeInformationFile: mismo layout que NT, se reenvían a ntdll. */
static uint64_t NtQueryInformationFile(uint32_t *args) {
    static nt_query_info_fn fn;
    if (!fn)
        fn = (nt_query_info_fn)(void *)ntdll_proc("NtQueryInformationFile");
    return RET32(fn((HANDLE)PTR(0), PTR(1), PTR(2), ARG(3), ARG(4)));
}

static uint64_t NtQueryVolumeInformationFile(uint32_t *args) {
    static nt_query_info_fn fn;
    uint32_t *iosb = (uint32_t *)PTR(1), *info = (uint32_t *)PTR(2);
    if (ARG(4) == 3 && ARG(3) >= 0x18) {
        /* FileFsSizeInformation: geometría FATX (clúster de 16 KB = 32 sectores de 512 B),
         * que es lo que XAPI comprueba; el tamaño de NTFS (4 KB) le haría pedir formateo. */
        ULARGE_INTEGER avail;
        uint64_t free_clusters = 0x40000; /* 4 GB por defecto */
        if (GetDiskFreeSpaceExA(g_hdd_root[0] ? g_hdd_root : NULL, &avail, NULL, NULL))
            free_clusters = avail.QuadPart / 16384u;
        if (free_clusters > 0x40000)
            free_clusters = 0x40000;
        info[0] = 0x4E6E0; /* TotalAllocationUnits: ~4,9 GB, como la partición E: */
        info[1] = 0;
        info[2] = (uint32_t)free_clusters;
        info[3] = 0;
        info[4] = 32;  /* SectorsPerAllocationUnit */
        info[5] = 512; /* BytesPerSector */
        iosb[0] = STATUS_SUCCESS;
        iosb[1] = 0x18;
        return STATUS_SUCCESS;
    }
    if (!fn)
        fn = (nt_query_info_fn)(void *)ntdll_proc("NtQueryVolumeInformationFile");
    return RET32(fn((HANDLE)PTR(0), PTR(1), PTR(2), ARG(3), ARG(4)));
}

/* 226 NTSTATUS NtSetInformationFile(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, CLASS) */
static uint64_t NtSetInformationFile(uint32_t *args) {
    static nt_query_info_fn fn;
    /* Lista blanca: básica (4), asignación (19), fin de archivo (20) y posición (14).
     * Fuera Disposition (13) y DispositionEx (64), que borran en el PC, y Rename (10) y
     * Link (11), cuyo nombre (ANSI en Xbox) escaparía del confinamiento de rutas. */
    switch (ARG(4)) {
    case 4: case 14: case 19: case 20:
        break;
    default:
        ngb_log("    · NtSetInformationFile: clase %u no permitida", ARG(4));
        if (PTR(1)) {
            ((uint32_t *)PTR(1))[0] = STATUS_NOT_IMPLEMENTED;
            ((uint32_t *)PTR(1))[1] = 0;
        }
        return RET32(STATUS_NOT_IMPLEMENTED);
    }
    if (!fn)
        fn = (nt_query_info_fn)(void *)ntdll_proc("NtSetInformationFile");
    return RET32(fn((HANDLE)PTR(0), PTR(1), PTR(2), ARG(3), ARG(4)));
}

/* Máscara de NtQueryDirectoryFile: solo un nombre con comodines, sin separadores ni ':'. */
static int mask_ok(const char *m) {
    size_t i, n = strlen(m);
    if (!n || n > 255 || !strcmp(m, ".."))
        return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)m[i];
        if (c < 0x20 || c == '\\' || c == '/' || c == ':' || c == '"' || c == '<' || c == '>' ||
            c == '|')
            return 0;
    }
    return 1;
}

static dir_handle_t *dir_find(HANDLE h) {
    int i;
    for (i = 0; i < MAX_DIRS; i++)
        if (g_dirs[i].h == h)
            return &g_dirs[i];
    return NULL;
}

static void put_time(uint8_t *p, FILETIME t) {
    memcpy(p, &t.dwLowDateTime, 4);
    memcpy(p + 4, &t.dwHighDateTime, 4);
}

/* 207 NTSTATUS NtQueryDirectoryFile(HANDLE, HANDLE Event, PIO_APC_ROUTINE, PVOID ApcContext,
 *     PIO_STATUS_BLOCK, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS,
 *     POBJECT_STRING FileName (máscara ANSI), BOOLEAN RestartScan)
 * Solo FileDirectoryInformation (1), una entrada por llamada; nombre ANSI (OCHAR). */
static uint64_t NtQueryDirectoryFile(uint32_t *args) {
    uint32_t *iosb = (uint32_t *)PTR(4), status = STATUS_SUCCESS, info = 0;
    uint8_t *out = (uint8_t *)PTR(5);
    uint32_t len = ARG(6);
    char mask[256] = "*", host[MAX_PATH], pattern[MAX_PATH + 260];
    WIN32_FIND_DATAA fd;
    dir_handle_t *d;
    int ro, have = 0;
    io_init_once();
    if (PTR(8) && (!xstring(PTR(8), mask, sizeof mask) || !mask_ok(mask))) {
        ngb_log("    · NtQueryDirectoryFile: máscara '%s' no permitida", mask);
        status = STATUS_INVALID_PARAMETER;
        goto done;
    }
    if (ARG(7) != 1 || len < 0x41) {
        ngb_log("    · NtQueryDirectoryFile: clase %u / tamaño %u no soportados", ARG(7), len);
        status = STATUS_INVALID_PARAMETER;
        goto done;
    }
    EnterCriticalSection(&g_io_lock);
    d = dir_find((HANDLE)PTR(0));
    if (!d) {
        LeaveCriticalSection(&g_io_lock);
        status = STATUS_INVALID_HANDLE;
        goto done;
    }
    if ((ARG(9) & 0xFF) || !d->find) {
        if (d->find && d->find != INVALID_HANDLE_VALUE)
            FindClose(d->find);
        d->find = INVALID_HANDLE_VALUE;
        d->entries = 0;
        if (host_path(d->xpath, host, sizeof host, &ro) == 1) {
            snprintf(pattern, sizeof pattern, "%s\\%s", host, mask);
            d->find = FindFirstFileA(pattern, &fd);
            have = d->find != INVALID_HANDLE_VALUE;
        }
    } else if (d->find != INVALID_HANDLE_VALUE) {
        have = FindNextFileA(d->find, &fd) != 0;
    }
    while (have && (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")))
        have = FindNextFileA(d->find, &fd) != 0;
    if (!have) {
        status = d->entries ? STATUS_NO_MORE_FILES : STATUS_NO_SUCH_FILE;
    } else {
        uint32_t nlen = (uint32_t)strlen(fd.cFileName);
        uint64_t size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        uint64_t alloc = (size + 0x3FFFu) & ~0x3FFFull; /* clúster FATX de 16 KB */
        if (nlen > len - 0x40)
            nlen = len - 0x40;
        memset(out, 0, 0x40);
        put_time(out + 0x08, fd.ftCreationTime);
        put_time(out + 0x10, fd.ftLastAccessTime);
        put_time(out + 0x18, fd.ftLastWriteTime);
        put_time(out + 0x20, fd.ftLastWriteTime);
        memcpy(out + 0x28, &size, 8);
        memcpy(out + 0x30, &alloc, 8);
        memcpy(out + 0x38, &fd.dwFileAttributes, 4);
        memcpy(out + 0x3C, &nlen, 4);
        memcpy(out + 0x40, fd.cFileName, nlen);
        info = 0x40 + nlen;
        d->entries++;
    }
    LeaveCriticalSection(&g_io_lock);
done:
    if (iosb) {
        iosb[0] = status;
        iosb[1] = info;
    }
    if (ARG(1))
        SetEvent((HANDLE)PTR(1));
    return RET32(status);
}

/* 198 NTSTATUS NtFlushBuffersFile(HANDLE, PIO_STATUS_BLOCK) */
static uint64_t NtFlushBuffersFile(uint32_t *args) {
    FlushFileBuffers((HANDLE)PTR(0));
    ((uint32_t *)PTR(1))[0] = STATUS_SUCCESS;
    ((uint32_t *)PTR(1))[1] = 0;
    return STATUS_SUCCESS;
}

/* 65 NTSTATUS IoCreateDevice(PDRIVER_OBJECT, ULONG DeviceExtensionSize, POBJECT_STRING Name,
 *                            DEVICE_TYPE, BOOLEAN Exclusive, PDEVICE_OBJECT *DeviceObject) */
static uint64_t IoCreateDevice(uint32_t *args) {
    char name[128];
    uint32_t ext = (ARG(1) + 15u) & ~15u;
    uint8_t *d = (uint8_t *)VirtualAlloc(NULL, 0x50 + ext, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    xstring(PTR(2), name, sizeof name);
    if (!d)
        return RET32(STATUS_NO_MEMORY);
    /* DEVICE_OBJECT de Xbox (0x48 bytes) y detrás la extensión del driver. */
    *(uint16_t *)(d + 0x00) = 3; /* IO_TYPE_DEVICE */
    *(uint16_t *)(d + 0x02) = 0x48;
    *(uint32_t *)(d + 0x04) = 1;
    *(uint32_t *)(d + 0x08) = ARG(0);
    *(uint32_t *)(d + 0x0C) = (uint32_t)(uintptr_t)d;
    *(uint32_t *)(d + 0x14) = 0x80; /* DO_DEVICE_INITIALIZING */
    *(uint32_t *)(d + 0x18) = ARG(1) ? (uint32_t)(uintptr_t)(d + 0x50) : 0;
    d[0x1C] = (uint8_t)ARG(3);
    d[0x1E] = 1; /* StackSize */
    *(uint32_t *)(d + 0x2C) = (uint32_t)(uintptr_t)(d + 0x2C); /* DeviceQueue.DeviceListHead */
    *(uint32_t *)(d + 0x30) = (uint32_t)(uintptr_t)(d + 0x2C);
    d[0x34] = 1; /* DeviceLock: EventSynchronizationObject */
    d[0x36] = 4;
    *(uint32_t *)(d + 0x38) = 1;
    *(uint32_t *)PTR(5) = (uint32_t)(uintptr_t)d;
    ngb_log("    · IoCreateDevice '%s' tipo 0x%X extensión %u -> %p", name[0] ? name : "(sin nombre)",
            ARG(3), ARG(1), d);
    return STATUS_SUCCESS;
}

/* 67 NTSTATUS IoCreateSymbolicLink(PSTRING SymbolicLinkName, PSTRING DeviceName) */
static uint64_t IoCreateSymbolicLink(uint32_t *args) {
    char name[64], target[XPATH], resolved[XPATH], host[MAX_PATH];
    int i, slot = -1, ro;
    io_init_once();
    if (!xstring(PTR(0), name, sizeof name) || !xstring(PTR(1), target, sizeof target)) {
        ngb_log("    · enlace: nombre o destino demasiado largo (máx. %u/%u B): rechazado",
                (unsigned)sizeof name - 1, (unsigned)sizeof target - 1);
        return RET32(0xC0000106u); /* STATUS_NAME_TOO_LONG */
    }
    /* El destino tiene que acabar en un dispositivo conocido y en una ruta segura. */
    resolve_links(target, resolved, sizeof resolved);
    if (!device_path(resolved, host, sizeof host, &ro)) {
        ngb_log("    · enlace '%s' -> '%s': destino no permitido", name, target);
        return RET32(STATUS_INVALID_PARAMETER);
    }
    EnterCriticalSection(&g_io_lock);
    for (i = 0; i < MAX_LINKS; i++) {
        if (!_stricmp(g_links[i].name, name)) {
            slot = -2;
            break;
        }
        if (!g_links[i].name[0] && slot < 0)
            slot = i;
    }
    if (slot >= 0) {
        snprintf(g_links[slot].name, sizeof g_links[slot].name, "%s", name);
        snprintf(g_links[slot].target, sizeof g_links[slot].target, "%s", target);
    }
    LeaveCriticalSection(&g_io_lock);
    if (slot == -1) {
        ngb_log("    · enlace '%s' -> '%s': tabla de enlaces llena (%d)", name, target, MAX_LINKS);
        return RET32(0xC000009Au); /* STATUS_INSUFFICIENT_RESOURCES */
    }
    ngb_log("    · enlace '%s' -> '%s'%s", name, target, slot == -2 ? " (ya existía)" : "");
    return slot == -2 ? RET32(STATUS_OBJECT_NAME_COLLISION) : STATUS_SUCCESS;
}

/* 69 NTSTATUS IoDeleteSymbolicLink(PSTRING SymbolicLinkName) */
static uint64_t IoDeleteSymbolicLink(uint32_t *args) {
    char name[64];
    int i;
    io_init_once();
    xstring(PTR(0), name, sizeof name);
    EnterCriticalSection(&g_io_lock);
    for (i = 0; i < MAX_LINKS; i++)
        if (!_stricmp(g_links[i].name, name))
            g_links[i].name[0] = 0;
    LeaveCriticalSection(&g_io_lock);
    return STATUS_SUCCESS;
}

/* 203 NTSTATUS NtOpenSymbolicLinkObject(PHANDLE LinkHandle, POBJECT_ATTRIBUTES) */
static uint64_t NtOpenSymbolicLinkObject(uint32_t *args) {
    char xpath[XPATH];
    int i;
    io_init_once();
    full_xpath((const uint32_t *)PTR(1), xpath, sizeof xpath);
    for (i = 0; i < MAX_LINKS; i++)
        if (g_links[i].name[0] && !_stricmp(g_links[i].name, xpath)) {
            /* Handle sintético: índice del enlace con una marca reconocible. */
            *(uint32_t *)PTR(0) = 0x4C4E0000u | (uint32_t)i;
            return STATUS_SUCCESS;
        }
    ngb_log("    · enlace '%s' no existe", xpath);
    return RET32(STATUS_OBJECT_NAME_NOT_FOUND);
}

/* 215 NTSTATUS NtQuerySymbolicLinkObject(HANDLE, PSTRING LinkTarget, PULONG ReturnedLength) */
static uint64_t NtQuerySymbolicLinkObject(uint32_t *args) {
    uint32_t i = ARG(0) & 0xFFFFu;
    uint16_t *dst = (uint16_t *)PTR(1);
    size_t len;
    if ((ARG(0) & 0xFFFF0000u) != 0x4C4E0000u || i >= MAX_LINKS)
        return RET32(STATUS_INVALID_HANDLE);
    len = strlen(g_links[i].target);
    if (PTR(2))
        *(uint32_t *)PTR(2) = (uint32_t)len;
    if (len > dst[1])
        return RET32(0xC0000023u);
    memcpy((void *)(uintptr_t) * (uint32_t *)(dst + 2), g_links[i].target, len);
    dst[0] = (uint16_t)len;
    return STATUS_SUCCESS;
}

/* NtClose de un handle de enlace sintético o de directorio: ver impl_ke.c (NtClose). */
int ngb_io_close(void *h) {
    int i;
    io_init_once();
    if (((uint32_t)(uintptr_t)h & 0xFFFF0000u) == 0x4C4E0000u)
        return 1;
    dir_forget(h);
    EnterCriticalSection(&g_io_lock);
    for (i = 0; i < MAX_RAW; i++)
        if (g_raw[i] == h)
            g_raw[i] = NULL;
    LeaveCriticalSection(&g_io_lock);
    return 0;
}

static const ngb_kimpl_t k_impls[] = {
    {65, IoCreateDevice},
    {67, IoCreateSymbolicLink},
    {69, IoDeleteSymbolicLink},
    {190, NtCreateFile},
    {196, NtDeviceIoControlFile},
    {200, NtFsControlFile},
    {198, NtFlushBuffersFile},
    {202, NtOpenFile},
    {207, NtQueryDirectoryFile},
    {203, NtOpenSymbolicLinkObject},
    {211, NtQueryInformationFile},
    {215, NtQuerySymbolicLinkObject},
    {218, NtQueryVolumeInformationFile},
    {219, NtReadFile},
    {226, NtSetInformationFile},
    {236, NtWriteFile},
};

void ngb_impl_register_io(void) {
    ngb_kernel_register(k_impls, sizeof k_impls / sizeof k_impls[0]);
}
