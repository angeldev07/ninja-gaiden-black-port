/*
 * data.c: variables exportadas por el kernel (17 las importa default.xbe).
 *
 * Cada ordinal `data` recibe un bloque de 0x400 bytes a cero, y el thunk apunta a él.
 * Aquí se ponen valores razonables en las que el arranque lee. Las claves (XboxHDKey,
 * XboxSignatureKey, XboxLANKey, XePublicKeyData...) quedan a cero a propósito: las reales
 * salen de la EEPROM/BIOS de una consola y no se incluyen.
 */
#include "../ngb.h"

#include <string.h>

#include "kernel.h"

#define KDATA_SIZE 0x400

static __declspec(align(16)) uint8_t g_kdata[NGB_MAX_ORDINAL + 1][KDATA_SIZE];
static DWORD g_tick0;
static char g_image_name[] = "\\Device\\CdRom0\\default.xbe";
static char g_disk_model[] = "NGB-PORT VIRTUAL HDD";
static char g_disk_serial[] = "NGB0000000000001";

void *ngb_kdata_address(uint32_t ordinal) {
    return ordinal <= NGB_MAX_ORDINAL ? g_kdata[ordinal] : NULL;
}

static void put16(uint32_t ordinal, uint32_t off, uint16_t v) {
    memcpy(&g_kdata[ordinal][off], &v, 2);
}

static void put32(uint32_t ordinal, uint32_t off, uint32_t v) {
    memcpy(&g_kdata[ordinal][off], &v, 4);
}

/* STRING/ANSI_STRING de Xbox: USHORT Length, USHORT MaximumLength, PCHAR Buffer. */
static void put_string(uint32_t ordinal, char *text) {
    uint16_t len = (uint16_t)strlen(text);
    put16(ordinal, 0, len);
    put16(ordinal, 2, (uint16_t)(len + 1));
    put32(ordinal, 4, (uint32_t)(uintptr_t)text);
}

static void put_object_type(uint32_t ordinal, const char tag[4]) {
    /* OBJECT_TYPE: 6 punteros a rutinas y PoolTag (0x18). */
    memcpy(&g_kdata[ordinal][0x18], tag, 4);
}

void ngb_kdata_init(const char *xbe_path) {
    (void)xbe_path;
    g_tick0 = GetTickCount();
    put32(40, 0, 3); /* HalDiskCachePartitionCount: X, Y, Z */
    put_string(41, g_disk_model);  /* HalDiskModelNumber */
    put_string(42, g_disk_serial); /* HalDiskSerialNumber */
    put32(156, 0, 0);              /* KeTickCount (lo avanza ngb_kdata_tick) */
    put32(164, 0, 0);              /* LaunchDataPage = NULL: arranque en frío */
    /* XboxHardwareInfo: Flags, GpuRevision, McpRevision (valores aproximados de retail). */
    put32(322, 0, 0);
    g_kdata[322][4] = 0xA2;
    g_kdata[322][5] = 0xB1;
    /* XboxKrnlVersion 1.0.5838.1, el kernel retail más común. */
    put16(324, 0, 1);
    put16(324, 2, 0);
    put16(324, 4, 5838);
    put16(324, 6, 1);
    put_string(326, g_image_name); /* XeImageFileName */
    put32(356, 0, 6);              /* HalBootSMCVideoMode: AV estándar (compuesto) */
    put_object_type(16, "Even");   /* ExEventObjectType */
    put_object_type(259, "Thre");  /* PsThreadObjectType */
}

void ngb_kdata_tick(void) {
    put32(156, 0, GetTickCount() - g_tick0);
}
