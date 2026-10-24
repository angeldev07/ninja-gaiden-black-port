/*
 * impl_misc.c: debugging (Dbg*), non-volatile settings (ExQueryNonVolatileSetting),
 * XBE sections (Xe*), video (Av*), network (Phy*), PCI and minimal cryptography (Xc*).
 */
#include "../ngb.h"

#include <stdio.h>
#include <string.h>

#include "eeprom.h"
#include "kernel.h"
#include "sha1.h"

/* 8 ULONG __cdecl DbgPrint(PCSZ Format, ...): args points to the arguments on the stack. */
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
    ngb_log("    · DbgBreakPoint: ignored");
    return 0;
}

/* Values of ExQueryNonVolatileSetting (XC_* indices from xboxdevwiki/Cxbx-Reloaded). */
#define REG_DWORD_ 4u
#define REG_BINARY_ 3u

/* 24 NTSTATUS ExQueryNonVolatileSetting(ULONG ValueIndex, PULONG Type, PVOID Value,
 *                                       ULONG ValueLength, PULONG ResultLength) */
static uint64_t ExQueryNonVolatileSetting(uint32_t *args) {
    uint32_t index = ARG(0), value = 0, type = REG_DWORD_, len = 4, sec_off, sec_len;
    uint8_t blob[EEPROM_SIZE];
    const char *name = "?";
    memset(blob, 0, sizeof blob);
    if (!eeprom_section(index, &sec_off, &sec_len)) {
        /* XC_MAX_OS/FACTORY/ALL: a block of the EEPROM. It must be fully deterministic: XAPI
         * signs the disk files with 16 bytes from here when XboxHDKey is zero. */
        uint8_t image[EEPROM_SIZE];
        eeprom_image(image);
        memcpy(blob, image + sec_off, sec_len);
        name = index == 0xFFFFu ? "XC_MAX_ALL" : index == 0x1FFu ? "XC_MAX_FACTORY" : "XC_MAX_OS";
        type = REG_BINARY_;
        len = sec_len;
    } else {
        switch (index) {
        case 0x0: name = "TIMEZONE_BIAS"; value = 0; break;
        case 0x7: name = "LANGUAGE"; value = EEPROM_LANGUAGE; break;            /* English */
        case 0x8: /* XC_VIDEO_FLAGS_WIDESCREEN (0x10000): the dashboard's 16:9 TV setting. The game
                   * reads it and renders anamorphic 16:9 (a wider field of view in the same 720x480). */
            name = "VIDEO_FLAGS";
            value = g_ngb_opts.widescreen ? 0x10000u : 0;
            break;
        case 0x9: name = "AUDIO_FLAGS"; value = 0; break;         /* stereo */
        case 0xA: name = "PARENTAL_CONTROL_GAMES"; value = 0; break;
        case 0xB: name = "PARENTAL_CONTROL_PASSWORD"; value = 0; break;
        case 0xC: name = "PARENTAL_CONTROL_MOVIES"; value = 0; break;
        case 0x11: name = "MISC_FLAGS"; value = 0; break;
        case 0x12: name = "DVD_REGION"; value = eeprom_dvd_region(); break;
        case 0x103: name = "FACTORY_AV_REGION"; value = eeprom_av_region(); break;
        case 0x104: name = "FACTORY_GAME_REGION"; value = eeprom_game_region(); break;
        case 0x101: name = "FACTORY_ETHERNET_ADDR"; type = REG_BINARY_; len = 6; break;
        case 0x100: name = "FACTORY_SERIAL_NUMBER"; type = REG_BINARY_; len = 12;
            memcpy(blob, EEPROM_SERIAL, 12); break;
        default:
            if (index >= 1 && index <= 6) { name = "TIMEZONE_*"; type = REG_BINARY_; len = 16; }
            else { ngb_log("    · ExQueryNonVolatileSetting: unknown index 0x%X", index); }
            break;
        }
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

/* 327 NTSTATUS XeLoadSection(PXBEIMAGE_SECTION Section): all sections were loaded at
 * startup, so only the reference is counted (SectionReferenceCount, offset 0x18). */
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

/* 252 ULONG PhyGetLinkState(BOOLEAN Update): no network cable */
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
    ngb_log("    · PCI %s bus %u slot %u reg 0x%X len %u", (ARG(5) & 0xFF) ? "write" : "read",
            ARG(0), ARG(1), ARG(2), ARG(4));
    if (!(ARG(5) & 0xFF) && PTR(3))
        memset(PTR(3), 0, ARG(4));
    return 0;
}

/* 335 VOID XcSHAInit(PUCHAR pbSHAContext): the context (116 B) is game memory. */
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
 * With the kernel keys at zero (data.c), the save signatures are consistent
 * within the port, which is the only thing the game checks. */
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
