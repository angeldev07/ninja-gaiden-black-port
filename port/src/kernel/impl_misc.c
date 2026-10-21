/*
 * impl_misc.c: depuración (Dbg*), ajustes no volátiles (ExQueryNonVolatileSetting),
 * secciones del XBE (Xe*), vídeo (Av*), red (Phy*), PCI y criptografía mínima (Xc*).
 */
#include "../ngb.h"

#include <stdio.h>
#include <string.h>

#include "kernel.h"
#include "sha1.h"

/* 8 ULONG __cdecl DbgPrint(PCSZ Format, ...): args apunta a los argumentos en pila. */
static uint64_t DbgPrint(uint32_t *args) {
    char buf[1024];
    size_t n;
    const char *fmt = (const char *)PTR(0);
    if (!fmt)
        return 0;
    vsnprintf(buf, sizeof buf, fmt, (va_list)(void *)&args[1]);
    n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = 0;
    ngb_log("    · DbgPrint: %s", buf);
    return STATUS_SUCCESS;
}

/* 5 VOID DbgBreakPoint(void) / 6 DbgBreakPointWithStatus(ULONG) */
static uint64_t DbgBreakPoint(uint32_t *args) {
    (void)args;
    ngb_log("    · DbgBreakPoint: ignorado");
    return 0;
}

/* Valores de ExQueryNonVolatileSetting (índices XC_* de xboxdevwiki/Cxbx-Reloaded). */
#define REG_DWORD_ 4u
#define REG_BINARY_ 3u

/* 24 NTSTATUS ExQueryNonVolatileSetting(ULONG ValueIndex, PULONG Type, PVOID Value,
 *                                       ULONG ValueLength, PULONG ResultLength) */
static uint64_t ExQueryNonVolatileSetting(uint32_t *args) {
    uint32_t index = ARG(0), value = 0, type = REG_DWORD_, len = 4;
    uint8_t blob[64];
    const char *name = "?";
    memset(blob, 0, sizeof blob);
    switch (index) {
    case 0x0: name = "TIMEZONE_BIAS"; value = 0; break;
    case 0x7: name = "LANGUAGE"; value = 1; break;            /* inglés */
    case 0x8: name = "VIDEO_FLAGS"; value = 0; break;
    case 0x9: name = "AUDIO_FLAGS"; value = 0; break;         /* estéreo */
    case 0xA: name = "PARENTAL_CONTROL_GAMES"; value = 0; break;
    case 0xB: name = "PARENTAL_CONTROL_PASSWORD"; value = 0; break;
    case 0xC: name = "PARENTAL_CONTROL_MOVIES"; value = 0; break;
    case 0x11: name = "MISC_FLAGS"; value = 0; break;
    case 0x12: name = "DVD_REGION"; value = 2; break;
    case 0x103: name = "FACTORY_AV_REGION"; value = 0x00800300u; break; /* PAL-I */
    case 0x104: name = "FACTORY_GAME_REGION"; value = 4; break;        /* resto del mundo */
    case 0x101: name = "FACTORY_ETHERNET_ADDR"; type = REG_BINARY_; len = 6; break;
    case 0x100: name = "FACTORY_SERIAL_NUMBER"; type = REG_BINARY_; len = 12;
        memcpy(blob, "000000000000", 12); break;
    default:
        if (index >= 1 && index <= 6) { name = "TIMEZONE_*"; type = REG_BINARY_; len = 16; }
        else { ngb_log("    · ExQueryNonVolatileSetting: índice 0x%X desconocido", index); }
        break;
    }
    if (type == REG_DWORD_)
        memcpy(blob, &value, 4);
    if (PTR(1))
        *(uint32_t *)PTR(1) = type;
    if (PTR(4))
        *(uint32_t *)PTR(4) = len;
    if (ARG(3) < len)
        return RET32(0xC0000023u); /* STATUS_BUFFER_TOO_SMALL */
    if (PTR(2))
        memcpy(PTR(2), blob, len);
    ngb_log("    · ExQueryNonVolatileSetting(%s) = 0x%X", name, value);
    return STATUS_SUCCESS;
}

/* 327 NTSTATUS XeLoadSection(PXBEIMAGE_SECTION Section): todas las secciones se cargaron al
 * arrancar, así que solo se cuenta la referencia (SectionReferenceCount, offset 0x18). */
static uint64_t XeLoadSection(uint32_t *args) {
    uint32_t *refs = (uint32_t *)((uint8_t *)PTR(0) + 0x18);
    (*refs)++;
    return STATUS_SUCCESS;
}

/* 328 NTSTATUS XeUnloadSection(PXBEIMAGE_SECTION Section) */
static uint64_t XeUnloadSection(uint32_t *args) {
    uint32_t *refs = (uint32_t *)((uint8_t *)PTR(0) + 0x18);
    if (*refs)
        (*refs)--;
    return STATUS_SUCCESS;
}

/* 1 PVOID AvGetSavedDataAddress(void) */
static uint64_t AvGetSavedDataAddress(uint32_t *args) {
    (void)args;
    return 0;
}

/* 2 VOID AvSendTVEncoderOption(PVOID RegisterBase, ULONG Option, ULONG Param, PULONG Result) */
static uint64_t AvSendTVEncoderOption(uint32_t *args) {
    if (PTR(3))
        *(uint32_t *)PTR(3) = 0;
    return 0;
}

/* 3 ULONG AvSetDisplayMode(...) / 4 VOID AvSetSavedDataAddress(PVOID) */
static uint64_t AvNop(uint32_t *args) {
    (void)args;
    return 0;
}

/* 252 ULONG PhyGetLinkState(BOOLEAN Update): sin cable de red */
static uint64_t PhyGetLinkState(uint32_t *args) {
    (void)args;
    return 0;
}

/* 253 NTSTATUS PhyInitialize(BOOLEAN forceReset, PVOID param) */
static uint64_t PhyInitialize(uint32_t *args) {
    (void)args;
    return STATUS_SUCCESS;
}

/* 46 VOID HalReadWritePCISpace(ULONG Bus, ULONG Slot, ULONG Register, PVOID Buffer,
 *                              ULONG Length, BOOLEAN WritePCISpace) */
static uint64_t HalReadWritePCISpace(uint32_t *args) {
    ngb_log("    · PCI %s bus %u slot %u reg 0x%X len %u", (ARG(5) & 0xFF) ? "escritura" : "lectura",
            ARG(0), ARG(1), ARG(2), ARG(4));
    if (!(ARG(5) & 0xFF) && PTR(3))
        memset(PTR(3), 0, ARG(4));
    return 0;
}

/* 335 VOID XcSHAInit(PUCHAR pbSHAContext): el contexto (116 B) es memoria del juego. */
static uint64_t XcSHAInit(uint32_t *args) {
    ngb_sha1_init((ngb_sha1_t *)PTR(0));
    return 0;
}

/* 336 VOID XcSHAUpdate(PUCHAR pbSHAContext, PUCHAR pbInput, ULONG dwInputLength) */
static uint64_t XcSHAUpdate(uint32_t *args) {
    ngb_sha1_update((ngb_sha1_t *)PTR(0), PTR(1), ARG(2));
    return 0;
}

/* 337 VOID XcSHAFinal(PUCHAR pbSHAContext, PUCHAR pbDigest) */
static uint64_t XcSHAFinal(uint32_t *args) {
    ngb_sha1_final((ngb_sha1_t *)PTR(0), (uint8_t *)PTR(1));
    return 0;
}

/* 340 VOID XcHMAC(PBYTE pbKeyMaterial, ULONG cbKeyMaterial, PBYTE pbData, ULONG cbData,
 *                 PBYTE pbData2, ULONG cbData2, PBYTE HmacData)
 * Con las claves del kernel a cero (data.c), las firmas de las partidas son coherentes
 * dentro del port, que es lo único que el juego comprueba. */
static uint64_t XcHMAC(uint32_t *args) {
    ngb_hmac_sha1(PTR(0), ARG(1), PTR(2), ARG(3), PTR(4), ARG(5), (uint8_t *)PTR(6));
    return 0;
}

static const ngb_kimpl_t k_impls[] = {
    {1, AvGetSavedDataAddress},
    {2, AvSendTVEncoderOption},
    {3, AvNop},
    {4, AvNop},
    {5, DbgBreakPoint},
    {6, DbgBreakPoint},
    {8, DbgPrint},
    {24, ExQueryNonVolatileSetting},
    {46, HalReadWritePCISpace},
    {252, PhyGetLinkState},
    {253, PhyInitialize},
    {327, XeLoadSection},
    {328, XeUnloadSection},
    {335, XcSHAInit},
    {336, XcSHAUpdate},
    {337, XcSHAFinal},
    {340, XcHMAC},
};

void ngb_impl_register_misc(void) {
    ngb_kernel_register(k_impls, sizeof k_impls / sizeof k_impls[0]);
}
