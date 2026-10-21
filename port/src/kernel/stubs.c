/*
 * GENERADO por `python -m ngb_tools.port.kernel_stubs` (tools/gen_kernel_stubs.py)
 * desde src/ngb_tools/data/xboxkrnl_exports.tsv. NO EDITAR A MANO: regenerar.
 *
 * Un stub por export de xboxkrnl con su convención real (stdcall/fastcall/cdecl) y
 * N argumentos de 32 bits según arg_bytes. Todos acaban en ngb_kcall().
 */
#include <intrin.h>
#include "kernel.h"

#pragma intrinsic(_ReturnAddress)

/* 1 AvGetSavedDataAddress: stdcall, 0 args */
static uint64_t __stdcall kstub_001(void)
{
    return ngb_kcall(1u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 2 AvSendTVEncoderOption: stdcall, 4 args */
static uint64_t __stdcall kstub_002(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(2u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 3 AvSetDisplayMode: stdcall, 6 args */
static uint64_t __stdcall kstub_003(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    uint32_t args[6];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    return ngb_kcall(3u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 4 AvSetSavedDataAddress: stdcall, 1 args */
static uint64_t __stdcall kstub_004(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(4u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 5 DbgBreakPoint: stdcall, 0 args */
static uint64_t __stdcall kstub_005(void)
{
    return ngb_kcall(5u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 6 DbgBreakPointWithStatus: stdcall, 1 args */
static uint64_t __stdcall kstub_006(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(6u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 7 DbgLoadImageSymbols: stdcall, 3 args */
static uint64_t __stdcall kstub_007(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(7u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 8 DbgPrint: cdecl, 4 args */
static uint64_t __cdecl kstub_008(uint32_t a0, ...)
{
    return ngb_kcall(8u, &a0, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 9 HalReadSMCTrayState: stdcall, 2 args */
static uint64_t __stdcall kstub_009(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(9u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 10 DbgPrompt: stdcall, 3 args */
static uint64_t __stdcall kstub_010(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(10u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 11 DbgUnLoadImageSymbols: stdcall, 3 args */
static uint64_t __stdcall kstub_011(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(11u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 12 ExAcquireReadWriteLockExclusive: stdcall, 1 args */
static uint64_t __stdcall kstub_012(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(12u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 13 ExAcquireReadWriteLockShared: stdcall, 1 args */
static uint64_t __stdcall kstub_013(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(13u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 14 ExAllocatePool: stdcall, 1 args */
static uint64_t __stdcall kstub_014(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(14u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 15 ExAllocatePoolWithTag: stdcall, 2 args */
static uint64_t __stdcall kstub_015(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(15u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 17 ExFreePool: stdcall, 1 args */
static uint64_t __stdcall kstub_017(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(17u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 18 ExInitializeReadWriteLock: stdcall, 1 args */
static uint64_t __stdcall kstub_018(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(18u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 19 ExInterlockedAddLargeInteger: stdcall, 4 args */
static uint64_t __stdcall kstub_019(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(19u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 20 ExInterlockedAddLargeStatistic: fastcall, 2 args */
static uint64_t __fastcall kstub_020(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(20u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 21 ExInterlockedCompareExchange64: fastcall, 3 args */
static uint64_t __fastcall kstub_021(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(21u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 23 ExQueryPoolBlockSize: stdcall, 1 args */
static uint64_t __stdcall kstub_023(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(23u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 24 ExQueryNonVolatileSetting: stdcall, 5 args */
static uint64_t __stdcall kstub_024(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(24u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 25 ExReadWriteRefurbInfo: stdcall, 3 args */
static uint64_t __stdcall kstub_025(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(25u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 26 ExRaiseException: stdcall, 1 args */
static uint64_t __stdcall kstub_026(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(26u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 27 ExRaiseStatus: stdcall, 1 args */
static uint64_t __stdcall kstub_027(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(27u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 28 ExReleaseReadWriteLock: stdcall, 1 args */
static uint64_t __stdcall kstub_028(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(28u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 29 ExSaveNonVolatileSetting: stdcall, 4 args */
static uint64_t __stdcall kstub_029(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(29u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 32 ExfInterlockedInsertHeadList: fastcall, 2 args */
static uint64_t __fastcall kstub_032(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(32u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 33 ExfInterlockedInsertTailList: fastcall, 2 args */
static uint64_t __fastcall kstub_033(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(33u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 34 ExfInterlockedRemoveHeadList: fastcall, 1 args */
static uint64_t __fastcall kstub_034(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(34u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 35 FscGetCacheSize: stdcall, 0 args */
static uint64_t __stdcall kstub_035(void)
{
    return ngb_kcall(35u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 36 FscInvalidateIdleBlocks: stdcall, 0 args */
static uint64_t __stdcall kstub_036(void)
{
    return ngb_kcall(36u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 37 FscSetCacheSize: stdcall, 1 args */
static uint64_t __stdcall kstub_037(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(37u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 38 HalClearSoftwareInterrupt: fastcall, 1 args */
static uint64_t __fastcall kstub_038(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(38u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 39 HalDisableSystemInterrupt: stdcall, 1 args */
static uint64_t __stdcall kstub_039(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(39u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 43 HalEnableSystemInterrupt: stdcall, 2 args */
static uint64_t __stdcall kstub_043(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(43u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 44 HalGetInterruptVector: stdcall, 2 args */
static uint64_t __stdcall kstub_044(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(44u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 45 HalReadSMBusValue: stdcall, 4 args */
static uint64_t __stdcall kstub_045(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(45u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 46 HalReadWritePCISpace: stdcall, 6 args */
static uint64_t __stdcall kstub_046(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    uint32_t args[6];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    return ngb_kcall(46u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 47 HalRegisterShutdownNotification: stdcall, 2 args */
static uint64_t __stdcall kstub_047(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(47u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 48 HalRequestSoftwareInterrupt: fastcall, 1 args */
static uint64_t __fastcall kstub_048(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(48u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 49 HalReturnToFirmware: stdcall, 1 args */
static uint64_t __stdcall kstub_049(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(49u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 50 HalWriteSMBusValue: stdcall, 4 args */
static uint64_t __stdcall kstub_050(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(50u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 51 InterlockedCompareExchange: fastcall, 3 args */
static uint64_t __fastcall kstub_051(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(51u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 52 InterlockedDecrement: fastcall, 1 args */
static uint64_t __fastcall kstub_052(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(52u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 53 InterlockedIncrement: fastcall, 1 args */
static uint64_t __fastcall kstub_053(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(53u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 54 InterlockedExchange: fastcall, 2 args */
static uint64_t __fastcall kstub_054(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(54u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 55 InterlockedExchangeAdd: fastcall, 2 args */
static uint64_t __fastcall kstub_055(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(55u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 56 InterlockedFlushSList: fastcall, 1 args */
static uint64_t __fastcall kstub_056(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(56u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 57 InterlockedPopEntrySList: fastcall, 1 args */
static uint64_t __fastcall kstub_057(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(57u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 58 InterlockedPushEntrySList: fastcall, 2 args */
static uint64_t __fastcall kstub_058(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(58u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 59 IoAllocateIrp: stdcall, 1 args */
static uint64_t __stdcall kstub_059(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(59u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 60 IoBuildAsynchronousFsdRequest: stdcall, 6 args */
static uint64_t __stdcall kstub_060(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    uint32_t args[6];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    return ngb_kcall(60u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 61 IoBuildDeviceIoControlRequest: stdcall, 9 args */
static uint64_t __stdcall kstub_061(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8)
{
    uint32_t args[9];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    args[7] = a7;
    args[8] = a8;
    return ngb_kcall(61u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 62 IoBuildSynchronousFsdRequest: stdcall, 7 args */
static uint64_t __stdcall kstub_062(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    uint32_t args[7];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    return ngb_kcall(62u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 63 IoCheckShareAccess: stdcall, 5 args */
static uint64_t __stdcall kstub_063(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(63u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 65 IoCreateDevice: stdcall, 6 args */
static uint64_t __stdcall kstub_065(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    uint32_t args[6];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    return ngb_kcall(65u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 66 IoCreateFile: stdcall, 10 args */
static uint64_t __stdcall kstub_066(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9)
{
    uint32_t args[10];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    args[7] = a7;
    args[8] = a8;
    args[9] = a9;
    return ngb_kcall(66u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 67 IoCreateSymbolicLink: stdcall, 2 args */
static uint64_t __stdcall kstub_067(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(67u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 68 IoDeleteDevice: stdcall, 1 args */
static uint64_t __stdcall kstub_068(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(68u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 69 IoDeleteSymbolicLink: stdcall, 1 args */
static uint64_t __stdcall kstub_069(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(69u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 72 IoFreeIrp: stdcall, 1 args */
static uint64_t __stdcall kstub_072(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(72u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 73 IoInitializeIrp: stdcall, 3 args */
static uint64_t __stdcall kstub_073(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(73u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 74 IoInvalidDeviceRequest: stdcall, 2 args */
static uint64_t __stdcall kstub_074(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(74u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 75 IoQueryFileInformation: stdcall, 5 args */
static uint64_t __stdcall kstub_075(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(75u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 76 IoQueryVolumeInformation: stdcall, 5 args */
static uint64_t __stdcall kstub_076(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(76u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 77 IoQueueThreadIrp: stdcall, 1 args */
static uint64_t __stdcall kstub_077(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(77u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 78 IoRemoveShareAccess: stdcall, 2 args */
static uint64_t __stdcall kstub_078(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(78u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 79 IoSetIoCompletion: stdcall, 5 args */
static uint64_t __stdcall kstub_079(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(79u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 80 IoSetShareAccess: stdcall, 4 args */
static uint64_t __stdcall kstub_080(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(80u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 81 IoStartNextPacket: stdcall, 1 args */
static uint64_t __stdcall kstub_081(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(81u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 82 IoStartNextPacketByKey: stdcall, 2 args */
static uint64_t __stdcall kstub_082(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(82u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 83 IoStartPacket: stdcall, 3 args */
static uint64_t __stdcall kstub_083(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(83u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 84 IoSynchronousDeviceIoControlRequest: stdcall, 8 args */
static uint64_t __stdcall kstub_084(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    uint32_t args[8];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    args[7] = a7;
    return ngb_kcall(84u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 85 IoSynchronousFsdRequest: stdcall, 5 args */
static uint64_t __stdcall kstub_085(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(85u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 86 IofCallDriver: fastcall, 2 args */
static uint64_t __fastcall kstub_086(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(86u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 87 IofCompleteRequest: fastcall, 2 args */
static uint64_t __fastcall kstub_087(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(87u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 90 IoDismountVolume: stdcall, 1 args */
static uint64_t __stdcall kstub_090(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(90u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 91 IoDismountVolumeByName: stdcall, 1 args */
static uint64_t __stdcall kstub_091(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(91u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 92 KeAlertResumeThread: stdcall, 1 args */
static uint64_t __stdcall kstub_092(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(92u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 93 KeAlertThread: stdcall, 2 args */
static uint64_t __stdcall kstub_093(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(93u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 94 KeBoostPriorityThread: stdcall, 2 args */
static uint64_t __stdcall kstub_094(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(94u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 95 KeBugCheck: stdcall, 1 args */
static uint64_t __stdcall kstub_095(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(95u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 96 KeBugCheckEx: stdcall, 5 args */
static uint64_t __stdcall kstub_096(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(96u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 97 KeCancelTimer: stdcall, 1 args */
static uint64_t __stdcall kstub_097(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(97u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 98 KeConnectInterrupt: stdcall, 1 args */
static uint64_t __stdcall kstub_098(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(98u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 99 KeDelayExecutionThread: stdcall, 3 args */
static uint64_t __stdcall kstub_099(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(99u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 100 KeDisconnectInterrupt: stdcall, 1 args */
static uint64_t __stdcall kstub_100(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(100u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 101 KeEnterCriticalRegion: stdcall, 0 args */
static uint64_t __stdcall kstub_101(void)
{
    return ngb_kcall(101u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 103 KeGetCurrentIrql: stdcall, 0 args */
static uint64_t __stdcall kstub_103(void)
{
    return ngb_kcall(103u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 104 KeGetCurrentThread: stdcall, 0 args */
static uint64_t __stdcall kstub_104(void)
{
    return ngb_kcall(104u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 105 KeInitializeApc: stdcall, 7 args */
static uint64_t __stdcall kstub_105(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    uint32_t args[7];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    return ngb_kcall(105u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 106 KeInitializeDeviceQueue: stdcall, 1 args */
static uint64_t __stdcall kstub_106(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(106u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 107 KeInitializeDpc: stdcall, 3 args */
static uint64_t __stdcall kstub_107(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(107u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 108 KeInitializeEvent: stdcall, 3 args */
static uint64_t __stdcall kstub_108(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(108u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 109 KeInitializeInterrupt: stdcall, 7 args */
static uint64_t __stdcall kstub_109(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    uint32_t args[7];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    return ngb_kcall(109u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 110 KeInitializeMutant: stdcall, 2 args */
static uint64_t __stdcall kstub_110(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(110u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 111 KeInitializeQueue: stdcall, 2 args */
static uint64_t __stdcall kstub_111(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(111u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 112 KeInitializeSemaphore: stdcall, 3 args */
static uint64_t __stdcall kstub_112(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(112u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 113 KeInitializeTimerEx: stdcall, 2 args */
static uint64_t __stdcall kstub_113(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(113u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 114 KeInsertByKeyDeviceQueue: stdcall, 3 args */
static uint64_t __stdcall kstub_114(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(114u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 115 KeInsertDeviceQueue: stdcall, 2 args */
static uint64_t __stdcall kstub_115(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(115u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 116 KeInsertHeadQueue: stdcall, 2 args */
static uint64_t __stdcall kstub_116(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(116u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 117 KeInsertQueue: stdcall, 2 args */
static uint64_t __stdcall kstub_117(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(117u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 118 KeInsertQueueApc: stdcall, 4 args */
static uint64_t __stdcall kstub_118(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(118u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 119 KeInsertQueueDpc: stdcall, 3 args */
static uint64_t __stdcall kstub_119(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(119u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 121 KeIsExecutingDpc: stdcall, 0 args */
static uint64_t __stdcall kstub_121(void)
{
    return ngb_kcall(121u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 122 KeLeaveCriticalRegion: stdcall, 0 args */
static uint64_t __stdcall kstub_122(void)
{
    return ngb_kcall(122u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 123 KePulseEvent: stdcall, 3 args */
static uint64_t __stdcall kstub_123(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(123u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 124 KeQueryBasePriorityThread: stdcall, 1 args */
static uint64_t __stdcall kstub_124(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(124u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 125 KeQueryInterruptTime: stdcall, 0 args */
static uint64_t __stdcall kstub_125(void)
{
    return ngb_kcall(125u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 126 KeQueryPerformanceCounter: stdcall, 0 args */
static uint64_t __stdcall kstub_126(void)
{
    return ngb_kcall(126u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 127 KeQueryPerformanceFrequency: stdcall, 0 args */
static uint64_t __stdcall kstub_127(void)
{
    return ngb_kcall(127u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 128 KeQuerySystemTime: stdcall, 1 args */
static uint64_t __stdcall kstub_128(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(128u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 129 KeRaiseIrqlToDpcLevel: stdcall, 0 args */
static uint64_t __stdcall kstub_129(void)
{
    return ngb_kcall(129u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 130 KeRaiseIrqlToSynchLevel: stdcall, 0 args */
static uint64_t __stdcall kstub_130(void)
{
    return ngb_kcall(130u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 131 KeReleaseMutant: stdcall, 4 args */
static uint64_t __stdcall kstub_131(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(131u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 132 KeReleaseSemaphore: stdcall, 4 args */
static uint64_t __stdcall kstub_132(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(132u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 133 KeRemoveByKeyDeviceQueue: stdcall, 2 args */
static uint64_t __stdcall kstub_133(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(133u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 134 KeRemoveDeviceQueue: stdcall, 1 args */
static uint64_t __stdcall kstub_134(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(134u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 135 KeRemoveEntryDeviceQueue: stdcall, 2 args */
static uint64_t __stdcall kstub_135(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(135u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 136 KeRemoveQueue: stdcall, 3 args */
static uint64_t __stdcall kstub_136(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(136u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 137 KeRemoveQueueDpc: stdcall, 1 args */
static uint64_t __stdcall kstub_137(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(137u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 138 KeResetEvent: stdcall, 1 args */
static uint64_t __stdcall kstub_138(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(138u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 139 KeRestoreFloatingPointState: stdcall, 1 args */
static uint64_t __stdcall kstub_139(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(139u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 140 KeResumeThread: stdcall, 1 args */
static uint64_t __stdcall kstub_140(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(140u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 141 KeRundownQueue: stdcall, 1 args */
static uint64_t __stdcall kstub_141(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(141u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 142 KeSaveFloatingPointState: stdcall, 1 args */
static uint64_t __stdcall kstub_142(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(142u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 143 KeSetBasePriorityThread: stdcall, 2 args */
static uint64_t __stdcall kstub_143(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(143u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 144 KeSetDisableBoostThread: stdcall, 2 args */
static uint64_t __stdcall kstub_144(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(144u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 145 KeSetEvent: stdcall, 3 args */
static uint64_t __stdcall kstub_145(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(145u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 146 KeSetEventBoostPriority: stdcall, 2 args */
static uint64_t __stdcall kstub_146(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(146u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 147 KeSetPriorityProcess: stdcall, 2 args */
static uint64_t __stdcall kstub_147(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(147u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 148 KeSetPriorityThread: stdcall, 2 args */
static uint64_t __stdcall kstub_148(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(148u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 149 KeSetTimer: stdcall, 4 args */
static uint64_t __stdcall kstub_149(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(149u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 150 KeSetTimerEx: stdcall, 5 args */
static uint64_t __stdcall kstub_150(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(150u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 151 KeStallExecutionProcessor: stdcall, 1 args */
static uint64_t __stdcall kstub_151(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(151u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 152 KeSuspendThread: stdcall, 1 args */
static uint64_t __stdcall kstub_152(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(152u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 153 KeSynchronizeExecution: stdcall, 3 args */
static uint64_t __stdcall kstub_153(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(153u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 155 KeTestAlertThread: stdcall, 1 args */
static uint64_t __stdcall kstub_155(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(155u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 158 KeWaitForMultipleObjects: stdcall, 8 args */
static uint64_t __stdcall kstub_158(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    uint32_t args[8];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    args[7] = a7;
    return ngb_kcall(158u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 159 KeWaitForSingleObject: stdcall, 5 args */
static uint64_t __stdcall kstub_159(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(159u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 160 KfRaiseIrql: fastcall, 1 args */
static uint64_t __fastcall kstub_160(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(160u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 161 KfLowerIrql: fastcall, 1 args */
static uint64_t __fastcall kstub_161(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(161u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 163 KiUnlockDispatcherDatabase: fastcall, 1 args */
static uint64_t __fastcall kstub_163(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(163u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 165 MmAllocateContiguousMemory: stdcall, 1 args */
static uint64_t __stdcall kstub_165(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(165u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 166 MmAllocateContiguousMemoryEx: stdcall, 5 args */
static uint64_t __stdcall kstub_166(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(166u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 167 MmAllocateSystemMemory: stdcall, 2 args */
static uint64_t __stdcall kstub_167(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(167u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 168 MmClaimGpuInstanceMemory: stdcall, 2 args */
static uint64_t __stdcall kstub_168(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(168u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 169 MmCreateKernelStack: stdcall, 2 args */
static uint64_t __stdcall kstub_169(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(169u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 170 MmDeleteKernelStack: stdcall, 2 args */
static uint64_t __stdcall kstub_170(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(170u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 171 MmFreeContiguousMemory: stdcall, 1 args */
static uint64_t __stdcall kstub_171(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(171u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 172 MmFreeSystemMemory: stdcall, 2 args */
static uint64_t __stdcall kstub_172(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(172u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 173 MmGetPhysicalAddress: stdcall, 1 args */
static uint64_t __stdcall kstub_173(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(173u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 174 MmIsAddressValid: stdcall, 1 args */
static uint64_t __stdcall kstub_174(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(174u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 175 MmLockUnlockBufferPages: stdcall, 3 args */
static uint64_t __stdcall kstub_175(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(175u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 176 MmLockUnlockPhysicalPage: stdcall, 2 args */
static uint64_t __stdcall kstub_176(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(176u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 177 MmMapIoSpace: stdcall, 3 args */
static uint64_t __stdcall kstub_177(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(177u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 178 MmPersistContiguousMemory: stdcall, 3 args */
static uint64_t __stdcall kstub_178(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(178u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 179 MmQueryAddressProtect: stdcall, 1 args */
static uint64_t __stdcall kstub_179(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(179u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 180 MmQueryAllocationSize: stdcall, 1 args */
static uint64_t __stdcall kstub_180(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(180u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 181 MmQueryStatistics: stdcall, 1 args */
static uint64_t __stdcall kstub_181(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(181u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 182 MmSetAddressProtect: stdcall, 3 args */
static uint64_t __stdcall kstub_182(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(182u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 183 MmUnmapIoSpace: stdcall, 2 args */
static uint64_t __stdcall kstub_183(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(183u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 184 NtAllocateVirtualMemory: stdcall, 5 args */
static uint64_t __stdcall kstub_184(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(184u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 185 NtCancelTimer: stdcall, 2 args */
static uint64_t __stdcall kstub_185(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(185u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 186 NtClearEvent: stdcall, 1 args */
static uint64_t __stdcall kstub_186(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(186u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 187 NtClose: stdcall, 1 args */
static uint64_t __stdcall kstub_187(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(187u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 188 NtCreateDirectoryObject: stdcall, 2 args */
static uint64_t __stdcall kstub_188(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(188u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 189 NtCreateEvent: stdcall, 4 args */
static uint64_t __stdcall kstub_189(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(189u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 190 NtCreateFile: stdcall, 9 args */
static uint64_t __stdcall kstub_190(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8)
{
    uint32_t args[9];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    args[7] = a7;
    args[8] = a8;
    return ngb_kcall(190u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 191 NtCreateIoCompletion: stdcall, 4 args */
static uint64_t __stdcall kstub_191(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(191u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 192 NtCreateMutant: stdcall, 3 args */
static uint64_t __stdcall kstub_192(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(192u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 193 NtCreateSemaphore: stdcall, 4 args */
static uint64_t __stdcall kstub_193(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(193u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 194 NtCreateTimer: stdcall, 3 args */
static uint64_t __stdcall kstub_194(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(194u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 195 NtDeleteFile: stdcall, 1 args */
static uint64_t __stdcall kstub_195(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(195u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 196 NtDeviceIoControlFile: stdcall, 10 args */
static uint64_t __stdcall kstub_196(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9)
{
    uint32_t args[10];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    args[7] = a7;
    args[8] = a8;
    args[9] = a9;
    return ngb_kcall(196u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 197 NtDuplicateObject: stdcall, 3 args */
static uint64_t __stdcall kstub_197(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(197u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 198 NtFlushBuffersFile: stdcall, 2 args */
static uint64_t __stdcall kstub_198(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(198u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 199 NtFreeVirtualMemory: stdcall, 3 args */
static uint64_t __stdcall kstub_199(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(199u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 200 NtFsControlFile: stdcall, 10 args */
static uint64_t __stdcall kstub_200(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9)
{
    uint32_t args[10];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    args[7] = a7;
    args[8] = a8;
    args[9] = a9;
    return ngb_kcall(200u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 201 NtOpenDirectoryObject: stdcall, 2 args */
static uint64_t __stdcall kstub_201(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(201u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 202 NtOpenFile: stdcall, 6 args */
static uint64_t __stdcall kstub_202(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    uint32_t args[6];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    return ngb_kcall(202u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 203 NtOpenSymbolicLinkObject: stdcall, 2 args */
static uint64_t __stdcall kstub_203(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(203u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 204 NtProtectVirtualMemory: stdcall, 4 args */
static uint64_t __stdcall kstub_204(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(204u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 205 NtPulseEvent: stdcall, 2 args */
static uint64_t __stdcall kstub_205(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(205u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 206 NtQueueApcThread: stdcall, 5 args */
static uint64_t __stdcall kstub_206(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(206u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 207 NtQueryDirectoryFile: stdcall, 10 args */
static uint64_t __stdcall kstub_207(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9)
{
    uint32_t args[10];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    args[7] = a7;
    args[8] = a8;
    args[9] = a9;
    return ngb_kcall(207u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 208 NtQueryDirectoryObject: stdcall, 6 args */
static uint64_t __stdcall kstub_208(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    uint32_t args[6];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    return ngb_kcall(208u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 209 NtQueryEvent: stdcall, 2 args */
static uint64_t __stdcall kstub_209(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(209u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 210 NtQueryFullAttributesFile: stdcall, 2 args */
static uint64_t __stdcall kstub_210(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(210u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 211 NtQueryInformationFile: stdcall, 5 args */
static uint64_t __stdcall kstub_211(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(211u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 212 NtQueryIoCompletion: stdcall, 2 args */
static uint64_t __stdcall kstub_212(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(212u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 213 NtQueryMutant: stdcall, 2 args */
static uint64_t __stdcall kstub_213(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(213u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 214 NtQuerySemaphore: stdcall, 2 args */
static uint64_t __stdcall kstub_214(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(214u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 215 NtQuerySymbolicLinkObject: stdcall, 3 args */
static uint64_t __stdcall kstub_215(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(215u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 216 NtQueryTimer: stdcall, 2 args */
static uint64_t __stdcall kstub_216(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(216u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 217 NtQueryVirtualMemory: stdcall, 2 args */
static uint64_t __stdcall kstub_217(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(217u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 218 NtQueryVolumeInformationFile: stdcall, 5 args */
static uint64_t __stdcall kstub_218(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(218u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 219 NtReadFile: stdcall, 8 args */
static uint64_t __stdcall kstub_219(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    uint32_t args[8];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    args[7] = a7;
    return ngb_kcall(219u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 220 NtReadFileScatter: stdcall, 8 args */
static uint64_t __stdcall kstub_220(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    uint32_t args[8];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    args[7] = a7;
    return ngb_kcall(220u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 221 NtReleaseMutant: stdcall, 2 args */
static uint64_t __stdcall kstub_221(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(221u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 222 NtReleaseSemaphore: stdcall, 3 args */
static uint64_t __stdcall kstub_222(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(222u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 223 NtRemoveIoCompletion: stdcall, 5 args */
static uint64_t __stdcall kstub_223(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(223u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 224 NtResumeThread: stdcall, 2 args */
static uint64_t __stdcall kstub_224(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(224u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 225 NtSetEvent: stdcall, 2 args */
static uint64_t __stdcall kstub_225(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(225u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 226 NtSetInformationFile: stdcall, 5 args */
static uint64_t __stdcall kstub_226(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(226u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 227 NtSetIoCompletion: stdcall, 5 args */
static uint64_t __stdcall kstub_227(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(227u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 228 NtSetSystemTime: stdcall, 2 args */
static uint64_t __stdcall kstub_228(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(228u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 229 NtSetTimerEx: stdcall, 8 args */
static uint64_t __stdcall kstub_229(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    uint32_t args[8];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    args[7] = a7;
    return ngb_kcall(229u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 230 NtSignalAndWaitForSingleObjectEx: stdcall, 5 args */
static uint64_t __stdcall kstub_230(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(230u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 231 NtSuspendThread: stdcall, 2 args */
static uint64_t __stdcall kstub_231(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(231u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 232 NtUserIoApcDispatcher: stdcall, 3 args */
static uint64_t __stdcall kstub_232(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(232u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 233 NtWaitForSingleObject: stdcall, 3 args */
static uint64_t __stdcall kstub_233(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(233u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 234 NtWaitForSingleObjectEx: stdcall, 4 args */
static uint64_t __stdcall kstub_234(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(234u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 235 NtWaitForMultipleObjectsEx: stdcall, 6 args */
static uint64_t __stdcall kstub_235(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    uint32_t args[6];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    return ngb_kcall(235u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 236 NtWriteFile: stdcall, 8 args */
static uint64_t __stdcall kstub_236(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    uint32_t args[8];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    args[7] = a7;
    return ngb_kcall(236u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 237 NtWriteFileGather: stdcall, 8 args */
static uint64_t __stdcall kstub_237(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    uint32_t args[8];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    args[7] = a7;
    return ngb_kcall(237u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 238 NtYieldExecution: stdcall, 0 args */
static uint64_t __stdcall kstub_238(void)
{
    return ngb_kcall(238u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 239 ObCreateObject: stdcall, 4 args */
static uint64_t __stdcall kstub_239(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(239u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 241 ObInsertObject: stdcall, 4 args */
static uint64_t __stdcall kstub_241(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(241u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 242 ObMakeTemporaryObject: stdcall, 1 args */
static uint64_t __stdcall kstub_242(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(242u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 243 ObOpenObjectByName: stdcall, 4 args */
static uint64_t __stdcall kstub_243(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(243u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 244 ObOpenObjectByPointer: stdcall, 3 args */
static uint64_t __stdcall kstub_244(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(244u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 246 ObReferenceObjectByHandle: stdcall, 3 args */
static uint64_t __stdcall kstub_246(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(246u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 247 ObReferenceObjectByName: stdcall, 5 args */
static uint64_t __stdcall kstub_247(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(247u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 248 ObReferenceObjectByPointer: stdcall, 2 args */
static uint64_t __stdcall kstub_248(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(248u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 250 ObfDereferenceObject: fastcall, 1 args */
static uint64_t __fastcall kstub_250(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(250u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 251 ObfReferenceObject: fastcall, 1 args */
static uint64_t __fastcall kstub_251(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(251u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 252 PhyGetLinkState: stdcall, 1 args */
static uint64_t __stdcall kstub_252(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(252u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 253 PhyInitialize: stdcall, 2 args */
static uint64_t __stdcall kstub_253(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(253u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 254 PsCreateSystemThread: stdcall, 5 args */
static uint64_t __stdcall kstub_254(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(254u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 255 PsCreateSystemThreadEx: stdcall, 10 args */
static uint64_t __stdcall kstub_255(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9)
{
    uint32_t args[10];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    args[7] = a7;
    args[8] = a8;
    args[9] = a9;
    return ngb_kcall(255u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 256 PsQueryStatistics: stdcall, 1 args */
static uint64_t __stdcall kstub_256(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(256u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 257 PsSetCreateThreadNotifyRoutine: stdcall, 1 args */
static uint64_t __stdcall kstub_257(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(257u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 258 PsTerminateSystemThread: stdcall, 1 args */
static uint64_t __stdcall kstub_258(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(258u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 260 RtlAnsiStringToUnicodeString: stdcall, 3 args */
static uint64_t __stdcall kstub_260(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(260u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 261 RtlAppendStringToString: stdcall, 2 args */
static uint64_t __stdcall kstub_261(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(261u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 262 RtlAppendUnicodeStringToString: stdcall, 2 args */
static uint64_t __stdcall kstub_262(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(262u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 263 RtlAppendUnicodeToString: stdcall, 2 args */
static uint64_t __stdcall kstub_263(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(263u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 264 RtlAssert: stdcall, 4 args */
static uint64_t __stdcall kstub_264(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(264u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 265 RtlCaptureContext: stdcall, 1 args */
static uint64_t __stdcall kstub_265(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(265u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 266 RtlCaptureStackBackTrace: stdcall, 4 args */
static uint64_t __stdcall kstub_266(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(266u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 267 RtlCharToInteger: stdcall, 3 args */
static uint64_t __stdcall kstub_267(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(267u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 268 RtlCompareMemory: stdcall, 3 args */
static uint64_t __stdcall kstub_268(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(268u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 269 RtlCompareMemoryUlong: stdcall, 3 args */
static uint64_t __stdcall kstub_269(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(269u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 270 RtlCompareString: stdcall, 3 args */
static uint64_t __stdcall kstub_270(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(270u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 271 RtlCompareUnicodeString: stdcall, 3 args */
static uint64_t __stdcall kstub_271(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(271u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 272 RtlCopyString: stdcall, 2 args */
static uint64_t __stdcall kstub_272(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(272u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 273 RtlCopyUnicodeString: stdcall, 2 args */
static uint64_t __stdcall kstub_273(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(273u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 274 RtlCreateUnicodeString: stdcall, 2 args */
static uint64_t __stdcall kstub_274(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(274u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 275 RtlDowncaseUnicodeChar: stdcall, 1 args */
static uint64_t __stdcall kstub_275(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(275u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 276 RtlDowncaseUnicodeString: stdcall, 3 args */
static uint64_t __stdcall kstub_276(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(276u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 277 RtlEnterCriticalSection: stdcall, 1 args */
static uint64_t __stdcall kstub_277(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(277u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 278 RtlEnterCriticalSectionAndRegion: stdcall, 1 args */
static uint64_t __stdcall kstub_278(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(278u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 279 RtlEqualString: stdcall, 3 args */
static uint64_t __stdcall kstub_279(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(279u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 280 RtlEqualUnicodeString: stdcall, 3 args */
static uint64_t __stdcall kstub_280(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(280u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 281 RtlExtendedIntegerMultiply: stdcall, 3 args */
static uint64_t __stdcall kstub_281(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(281u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 282 RtlExtendedLargeIntegerDivide: stdcall, 4 args */
static uint64_t __stdcall kstub_282(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(282u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 283 RtlExtendedMagicDivide: stdcall, 5 args */
static uint64_t __stdcall kstub_283(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(283u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 284 RtlFillMemory: stdcall, 3 args */
static uint64_t __stdcall kstub_284(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(284u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 285 RtlFillMemoryUlong: stdcall, 3 args */
static uint64_t __stdcall kstub_285(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(285u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 286 RtlFreeAnsiString: stdcall, 1 args */
static uint64_t __stdcall kstub_286(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(286u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 287 RtlFreeUnicodeString: stdcall, 1 args */
static uint64_t __stdcall kstub_287(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(287u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 288 RtlGetCallersAddress: stdcall, 2 args */
static uint64_t __stdcall kstub_288(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(288u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 289 RtlInitAnsiString: stdcall, 2 args */
static uint64_t __stdcall kstub_289(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(289u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 290 RtlInitUnicodeString: stdcall, 2 args */
static uint64_t __stdcall kstub_290(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(290u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 291 RtlInitializeCriticalSection: stdcall, 1 args */
static uint64_t __stdcall kstub_291(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(291u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 292 RtlIntegerToChar: stdcall, 4 args */
static uint64_t __stdcall kstub_292(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(292u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 293 RtlIntegerToUnicodeString: stdcall, 3 args */
static uint64_t __stdcall kstub_293(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(293u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 294 RtlLeaveCriticalSection: stdcall, 1 args */
static uint64_t __stdcall kstub_294(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(294u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 295 RtlLeaveCriticalSectionAndRegion: stdcall, 1 args */
static uint64_t __stdcall kstub_295(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(295u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 296 RtlLowerChar: stdcall, 1 args */
static uint64_t __stdcall kstub_296(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(296u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 297 RtlMapGenericMask: stdcall, 2 args */
static uint64_t __stdcall kstub_297(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(297u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 298 RtlMoveMemory: stdcall, 3 args */
static uint64_t __stdcall kstub_298(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(298u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 299 RtlMultiByteToUnicodeN: stdcall, 5 args */
static uint64_t __stdcall kstub_299(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(299u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 300 RtlMultiByteToUnicodeSize: stdcall, 3 args */
static uint64_t __stdcall kstub_300(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(300u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 301 RtlNtStatusToDosError: stdcall, 1 args */
static uint64_t __stdcall kstub_301(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(301u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 302 RtlRaiseException: stdcall, 1 args */
static uint64_t __stdcall kstub_302(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(302u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 303 RtlRaiseStatus: stdcall, 1 args */
static uint64_t __stdcall kstub_303(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(303u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 304 RtlTimeFieldsToTime: stdcall, 2 args */
static uint64_t __stdcall kstub_304(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(304u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 305 RtlTimeToTimeFields: stdcall, 2 args */
static uint64_t __stdcall kstub_305(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(305u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 306 RtlTryEnterCriticalSection: stdcall, 1 args */
static uint64_t __stdcall kstub_306(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(306u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 307 RtlUlongByteSwap: fastcall, 1 args */
static uint64_t __fastcall kstub_307(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(307u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 308 RtlUnicodeStringToAnsiString: stdcall, 3 args */
static uint64_t __stdcall kstub_308(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(308u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 309 RtlUnicodeStringToInteger: stdcall, 3 args */
static uint64_t __stdcall kstub_309(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(309u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 310 RtlUnicodeToMultiByteN: stdcall, 5 args */
static uint64_t __stdcall kstub_310(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(310u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 311 RtlUnicodeToMultiByteSize: stdcall, 3 args */
static uint64_t __stdcall kstub_311(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(311u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 312 RtlUnwind: stdcall, 4 args */
static uint64_t __stdcall kstub_312(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t args[4];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    return ngb_kcall(312u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 313 RtlUpcaseUnicodeChar: stdcall, 1 args */
static uint64_t __stdcall kstub_313(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(313u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 314 RtlUpcaseUnicodeString: stdcall, 3 args */
static uint64_t __stdcall kstub_314(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(314u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 315 RtlUpcaseUnicodeToMultiByteN: stdcall, 5 args */
static uint64_t __stdcall kstub_315(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(315u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 316 RtlUpperChar: stdcall, 1 args */
static uint64_t __stdcall kstub_316(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(316u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 317 RtlUpperString: stdcall, 2 args */
static uint64_t __stdcall kstub_317(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(317u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 318 RtlUshortByteSwap: fastcall, 1 args */
static uint64_t __fastcall kstub_318(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(318u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 319 RtlWalkFrameChain: stdcall, 3 args */
static uint64_t __stdcall kstub_319(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(319u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 320 RtlZeroMemory: stdcall, 2 args */
static uint64_t __stdcall kstub_320(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(320u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 327 XeLoadSection: stdcall, 1 args */
static uint64_t __stdcall kstub_327(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(327u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 328 XeUnloadSection: stdcall, 1 args */
static uint64_t __stdcall kstub_328(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(328u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 329 READ_PORT_BUFFER_UCHAR: stdcall, 3 args */
static uint64_t __stdcall kstub_329(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(329u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 330 READ_PORT_BUFFER_USHORT: stdcall, 3 args */
static uint64_t __stdcall kstub_330(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(330u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 331 READ_PORT_BUFFER_ULONG: stdcall, 3 args */
static uint64_t __stdcall kstub_331(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(331u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 332 WRITE_PORT_BUFFER_UCHAR: stdcall, 3 args */
static uint64_t __stdcall kstub_332(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(332u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 333 WRITE_PORT_BUFFER_USHORT: stdcall, 3 args */
static uint64_t __stdcall kstub_333(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(333u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 334 WRITE_PORT_BUFFER_ULONG: stdcall, 3 args */
static uint64_t __stdcall kstub_334(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(334u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 335 XcSHAInit: stdcall, 1 args */
static uint64_t __stdcall kstub_335(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(335u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 336 XcSHAUpdate: stdcall, 3 args */
static uint64_t __stdcall kstub_336(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(336u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 337 XcSHAFinal: stdcall, 2 args */
static uint64_t __stdcall kstub_337(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(337u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 338 XcRC4Key: stdcall, 3 args */
static uint64_t __stdcall kstub_338(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(338u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 339 XcRC4Crypt: stdcall, 3 args */
static uint64_t __stdcall kstub_339(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(339u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 340 XcHMAC: stdcall, 7 args */
static uint64_t __stdcall kstub_340(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    uint32_t args[7];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    return ngb_kcall(340u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 341 XcPKEncPublic: stdcall, 3 args */
static uint64_t __stdcall kstub_341(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(341u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 342 XcPKDecPrivate: stdcall, 3 args */
static uint64_t __stdcall kstub_342(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(342u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 343 XcPKGetKeyLen: stdcall, 1 args */
static uint64_t __stdcall kstub_343(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(343u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 344 XcVerifyPKCS1Signature: stdcall, 3 args */
static uint64_t __stdcall kstub_344(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(344u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 345 XcModExp: stdcall, 5 args */
static uint64_t __stdcall kstub_345(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(345u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 346 XcDESKeyParity: stdcall, 2 args */
static uint64_t __stdcall kstub_346(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(346u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 347 XcKeyTable: stdcall, 3 args */
static uint64_t __stdcall kstub_347(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(347u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 348 XcBlockCrypt: stdcall, 5 args */
static uint64_t __stdcall kstub_348(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t args[5];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    return ngb_kcall(348u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 349 XcBlockCryptCBC: stdcall, 7 args */
static uint64_t __stdcall kstub_349(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    uint32_t args[7];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    args[3] = a3;
    args[4] = a4;
    args[5] = a5;
    args[6] = a6;
    return ngb_kcall(349u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 350 XcCryptService: stdcall, 2 args */
static uint64_t __stdcall kstub_350(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(350u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 351 XcUpdateCrypto: stdcall, 2 args */
static uint64_t __stdcall kstub_351(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(351u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 352 RtlRip: stdcall, 3 args */
static uint64_t __stdcall kstub_352(uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t args[3];
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ngb_kcall(352u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 358 HalIsResetOrShutdownPending: stdcall, 0 args */
static uint64_t __stdcall kstub_358(void)
{
    return ngb_kcall(358u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 359 IoMarkIrpMustComplete: stdcall, 1 args */
static uint64_t __stdcall kstub_359(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(359u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 360 HalInitiateShutdown: stdcall, 0 args */
static uint64_t __stdcall kstub_360(void)
{
    return ngb_kcall(360u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 361 RtlSnprintf: cdecl, 4 args */
static uint64_t __cdecl kstub_361(uint32_t a0, ...)
{
    return ngb_kcall(361u, &a0, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 362 RtlSprintf: cdecl, 4 args */
static uint64_t __cdecl kstub_362(uint32_t a0, ...)
{
    return ngb_kcall(362u, &a0, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 363 RtlVsnprintf: cdecl, 4 args */
static uint64_t __cdecl kstub_363(uint32_t a0, ...)
{
    return ngb_kcall(363u, &a0, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 364 RtlVsprintf: cdecl, 4 args */
static uint64_t __cdecl kstub_364(uint32_t a0, ...)
{
    return ngb_kcall(364u, &a0, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 365 HalEnableSecureTrayEject: stdcall, 0 args */
static uint64_t __stdcall kstub_365(void)
{
    return ngb_kcall(365u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 366 HalWriteSMCScratchRegister: stdcall, 1 args */
static uint64_t __stdcall kstub_366(uint32_t a0)
{
    uint32_t args[1];
    args[0] = a0;
    return ngb_kcall(366u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 374 MmDbgAllocateMemory: stdcall, 2 args */
static uint64_t __stdcall kstub_374(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(374u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 375 MmDbgFreeMemory: stdcall, 2 args */
static uint64_t __stdcall kstub_375(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(375u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 376 MmDbgQueryAvailablePages: stdcall, 0 args */
static uint64_t __stdcall kstub_376(void)
{
    return ngb_kcall(376u, NULL, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 377 MmDbgReleaseAddress: stdcall, 2 args */
static uint64_t __stdcall kstub_377(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(377u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

/* 378 MmDbgWriteCheck: stdcall, 2 args */
static uint64_t __stdcall kstub_378(uint32_t a0, uint32_t a1)
{
    uint32_t args[2];
    args[0] = a0;
    args[1] = a1;
    return ngb_kcall(378u, args, (uint32_t)(uintptr_t)_ReturnAddress());
}

const ngb_kexport_t ngb_kexports[] = {
    {1, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "AvGetSavedDataAddress", (const void *)kstub_001},
    {2, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "AvSendTVEncoderOption", (const void *)kstub_002},
    {3, NGB_KIND_FUNC, NGB_CONV_STDCALL, 6, "AvSetDisplayMode", (const void *)kstub_003},
    {4, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "AvSetSavedDataAddress", (const void *)kstub_004},
    {5, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "DbgBreakPoint", (const void *)kstub_005},
    {6, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "DbgBreakPointWithStatus", (const void *)kstub_006},
    {7, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "DbgLoadImageSymbols", (const void *)kstub_007},
    {8, NGB_KIND_FUNC, NGB_CONV_CDECL, 4, "DbgPrint", (const void *)kstub_008},
    {9, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "HalReadSMCTrayState", (const void *)kstub_009},
    {10, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "DbgPrompt", (const void *)kstub_010},
    {11, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "DbgUnLoadImageSymbols", (const void *)kstub_011},
    {12, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "ExAcquireReadWriteLockExclusive", (const void *)kstub_012},
    {13, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "ExAcquireReadWriteLockShared", (const void *)kstub_013},
    {14, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "ExAllocatePool", (const void *)kstub_014},
    {15, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "ExAllocatePoolWithTag", (const void *)kstub_015},
    {16, NGB_KIND_DATA, NGB_CONV_NONE, 0, "ExEventObjectType", NULL},
    {17, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "ExFreePool", (const void *)kstub_017},
    {18, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "ExInitializeReadWriteLock", (const void *)kstub_018},
    {19, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "ExInterlockedAddLargeInteger", (const void *)kstub_019},
    {20, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 2, "ExInterlockedAddLargeStatistic", (const void *)kstub_020},
    {21, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 3, "ExInterlockedCompareExchange64", (const void *)kstub_021},
    {22, NGB_KIND_DATA, NGB_CONV_NONE, 0, "ExMutantObjectType", NULL},
    {23, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "ExQueryPoolBlockSize", (const void *)kstub_023},
    {24, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "ExQueryNonVolatileSetting", (const void *)kstub_024},
    {25, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "ExReadWriteRefurbInfo", (const void *)kstub_025},
    {26, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "ExRaiseException", (const void *)kstub_026},
    {27, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "ExRaiseStatus", (const void *)kstub_027},
    {28, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "ExReleaseReadWriteLock", (const void *)kstub_028},
    {29, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "ExSaveNonVolatileSetting", (const void *)kstub_029},
    {30, NGB_KIND_DATA, NGB_CONV_NONE, 0, "ExSemaphoreObjectType", NULL},
    {31, NGB_KIND_DATA, NGB_CONV_NONE, 0, "ExTimerObjectType", NULL},
    {32, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 2, "ExfInterlockedInsertHeadList", (const void *)kstub_032},
    {33, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 2, "ExfInterlockedInsertTailList", (const void *)kstub_033},
    {34, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 1, "ExfInterlockedRemoveHeadList", (const void *)kstub_034},
    {35, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "FscGetCacheSize", (const void *)kstub_035},
    {36, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "FscInvalidateIdleBlocks", (const void *)kstub_036},
    {37, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "FscSetCacheSize", (const void *)kstub_037},
    {38, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 1, "HalClearSoftwareInterrupt", (const void *)kstub_038},
    {39, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "HalDisableSystemInterrupt", (const void *)kstub_039},
    {40, NGB_KIND_DATA, NGB_CONV_NONE, 0, "HalDiskCachePartitionCount", NULL},
    {41, NGB_KIND_DATA, NGB_CONV_NONE, 0, "HalDiskModelNumber", NULL},
    {42, NGB_KIND_DATA, NGB_CONV_NONE, 0, "HalDiskSerialNumber", NULL},
    {43, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "HalEnableSystemInterrupt", (const void *)kstub_043},
    {44, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "HalGetInterruptVector", (const void *)kstub_044},
    {45, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "HalReadSMBusValue", (const void *)kstub_045},
    {46, NGB_KIND_FUNC, NGB_CONV_STDCALL, 6, "HalReadWritePCISpace", (const void *)kstub_046},
    {47, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "HalRegisterShutdownNotification", (const void *)kstub_047},
    {48, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 1, "HalRequestSoftwareInterrupt", (const void *)kstub_048},
    {49, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "HalReturnToFirmware", (const void *)kstub_049},
    {50, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "HalWriteSMBusValue", (const void *)kstub_050},
    {51, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 3, "InterlockedCompareExchange", (const void *)kstub_051},
    {52, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 1, "InterlockedDecrement", (const void *)kstub_052},
    {53, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 1, "InterlockedIncrement", (const void *)kstub_053},
    {54, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 2, "InterlockedExchange", (const void *)kstub_054},
    {55, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 2, "InterlockedExchangeAdd", (const void *)kstub_055},
    {56, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 1, "InterlockedFlushSList", (const void *)kstub_056},
    {57, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 1, "InterlockedPopEntrySList", (const void *)kstub_057},
    {58, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 2, "InterlockedPushEntrySList", (const void *)kstub_058},
    {59, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "IoAllocateIrp", (const void *)kstub_059},
    {60, NGB_KIND_FUNC, NGB_CONV_STDCALL, 6, "IoBuildAsynchronousFsdRequest", (const void *)kstub_060},
    {61, NGB_KIND_FUNC, NGB_CONV_STDCALL, 9, "IoBuildDeviceIoControlRequest", (const void *)kstub_061},
    {62, NGB_KIND_FUNC, NGB_CONV_STDCALL, 7, "IoBuildSynchronousFsdRequest", (const void *)kstub_062},
    {63, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "IoCheckShareAccess", (const void *)kstub_063},
    {64, NGB_KIND_DATA, NGB_CONV_NONE, 0, "IoCompletionObjectType", NULL},
    {65, NGB_KIND_FUNC, NGB_CONV_STDCALL, 6, "IoCreateDevice", (const void *)kstub_065},
    {66, NGB_KIND_FUNC, NGB_CONV_STDCALL, 10, "IoCreateFile", (const void *)kstub_066},
    {67, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "IoCreateSymbolicLink", (const void *)kstub_067},
    {68, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "IoDeleteDevice", (const void *)kstub_068},
    {69, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "IoDeleteSymbolicLink", (const void *)kstub_069},
    {70, NGB_KIND_DATA, NGB_CONV_NONE, 0, "IoDeviceObjectType", NULL},
    {71, NGB_KIND_DATA, NGB_CONV_NONE, 0, "IoFileObjectType", NULL},
    {72, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "IoFreeIrp", (const void *)kstub_072},
    {73, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "IoInitializeIrp", (const void *)kstub_073},
    {74, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "IoInvalidDeviceRequest", (const void *)kstub_074},
    {75, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "IoQueryFileInformation", (const void *)kstub_075},
    {76, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "IoQueryVolumeInformation", (const void *)kstub_076},
    {77, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "IoQueueThreadIrp", (const void *)kstub_077},
    {78, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "IoRemoveShareAccess", (const void *)kstub_078},
    {79, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "IoSetIoCompletion", (const void *)kstub_079},
    {80, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "IoSetShareAccess", (const void *)kstub_080},
    {81, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "IoStartNextPacket", (const void *)kstub_081},
    {82, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "IoStartNextPacketByKey", (const void *)kstub_082},
    {83, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "IoStartPacket", (const void *)kstub_083},
    {84, NGB_KIND_FUNC, NGB_CONV_STDCALL, 8, "IoSynchronousDeviceIoControlRequest", (const void *)kstub_084},
    {85, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "IoSynchronousFsdRequest", (const void *)kstub_085},
    {86, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 2, "IofCallDriver", (const void *)kstub_086},
    {87, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 2, "IofCompleteRequest", (const void *)kstub_087},
    {88, NGB_KIND_DATA, NGB_CONV_NONE, 0, "KdDebuggerEnabled", NULL},
    {89, NGB_KIND_DATA, NGB_CONV_NONE, 0, "KdDebuggerNotPresent", NULL},
    {90, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "IoDismountVolume", (const void *)kstub_090},
    {91, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "IoDismountVolumeByName", (const void *)kstub_091},
    {92, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "KeAlertResumeThread", (const void *)kstub_092},
    {93, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "KeAlertThread", (const void *)kstub_093},
    {94, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "KeBoostPriorityThread", (const void *)kstub_094},
    {95, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "KeBugCheck", (const void *)kstub_095},
    {96, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "KeBugCheckEx", (const void *)kstub_096},
    {97, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "KeCancelTimer", (const void *)kstub_097},
    {98, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "KeConnectInterrupt", (const void *)kstub_098},
    {99, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "KeDelayExecutionThread", (const void *)kstub_099},
    {100, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "KeDisconnectInterrupt", (const void *)kstub_100},
    {101, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "KeEnterCriticalRegion", (const void *)kstub_101},
    {102, NGB_KIND_DATA, NGB_CONV_NONE, 0, "MmGlobalData", NULL},
    {103, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "KeGetCurrentIrql", (const void *)kstub_103},
    {104, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "KeGetCurrentThread", (const void *)kstub_104},
    {105, NGB_KIND_FUNC, NGB_CONV_STDCALL, 7, "KeInitializeApc", (const void *)kstub_105},
    {106, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "KeInitializeDeviceQueue", (const void *)kstub_106},
    {107, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "KeInitializeDpc", (const void *)kstub_107},
    {108, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "KeInitializeEvent", (const void *)kstub_108},
    {109, NGB_KIND_FUNC, NGB_CONV_STDCALL, 7, "KeInitializeInterrupt", (const void *)kstub_109},
    {110, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "KeInitializeMutant", (const void *)kstub_110},
    {111, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "KeInitializeQueue", (const void *)kstub_111},
    {112, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "KeInitializeSemaphore", (const void *)kstub_112},
    {113, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "KeInitializeTimerEx", (const void *)kstub_113},
    {114, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "KeInsertByKeyDeviceQueue", (const void *)kstub_114},
    {115, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "KeInsertDeviceQueue", (const void *)kstub_115},
    {116, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "KeInsertHeadQueue", (const void *)kstub_116},
    {117, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "KeInsertQueue", (const void *)kstub_117},
    {118, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "KeInsertQueueApc", (const void *)kstub_118},
    {119, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "KeInsertQueueDpc", (const void *)kstub_119},
    {120, NGB_KIND_DATA, NGB_CONV_NONE, 0, "KeInterruptTime", NULL},
    {121, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "KeIsExecutingDpc", (const void *)kstub_121},
    {122, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "KeLeaveCriticalRegion", (const void *)kstub_122},
    {123, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "KePulseEvent", (const void *)kstub_123},
    {124, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "KeQueryBasePriorityThread", (const void *)kstub_124},
    {125, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "KeQueryInterruptTime", (const void *)kstub_125},
    {126, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "KeQueryPerformanceCounter", (const void *)kstub_126},
    {127, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "KeQueryPerformanceFrequency", (const void *)kstub_127},
    {128, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "KeQuerySystemTime", (const void *)kstub_128},
    {129, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "KeRaiseIrqlToDpcLevel", (const void *)kstub_129},
    {130, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "KeRaiseIrqlToSynchLevel", (const void *)kstub_130},
    {131, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "KeReleaseMutant", (const void *)kstub_131},
    {132, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "KeReleaseSemaphore", (const void *)kstub_132},
    {133, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "KeRemoveByKeyDeviceQueue", (const void *)kstub_133},
    {134, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "KeRemoveDeviceQueue", (const void *)kstub_134},
    {135, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "KeRemoveEntryDeviceQueue", (const void *)kstub_135},
    {136, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "KeRemoveQueue", (const void *)kstub_136},
    {137, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "KeRemoveQueueDpc", (const void *)kstub_137},
    {138, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "KeResetEvent", (const void *)kstub_138},
    {139, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "KeRestoreFloatingPointState", (const void *)kstub_139},
    {140, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "KeResumeThread", (const void *)kstub_140},
    {141, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "KeRundownQueue", (const void *)kstub_141},
    {142, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "KeSaveFloatingPointState", (const void *)kstub_142},
    {143, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "KeSetBasePriorityThread", (const void *)kstub_143},
    {144, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "KeSetDisableBoostThread", (const void *)kstub_144},
    {145, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "KeSetEvent", (const void *)kstub_145},
    {146, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "KeSetEventBoostPriority", (const void *)kstub_146},
    {147, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "KeSetPriorityProcess", (const void *)kstub_147},
    {148, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "KeSetPriorityThread", (const void *)kstub_148},
    {149, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "KeSetTimer", (const void *)kstub_149},
    {150, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "KeSetTimerEx", (const void *)kstub_150},
    {151, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "KeStallExecutionProcessor", (const void *)kstub_151},
    {152, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "KeSuspendThread", (const void *)kstub_152},
    {153, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "KeSynchronizeExecution", (const void *)kstub_153},
    {154, NGB_KIND_DATA, NGB_CONV_NONE, 0, "KeSystemTime", NULL},
    {155, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "KeTestAlertThread", (const void *)kstub_155},
    {156, NGB_KIND_DATA, NGB_CONV_NONE, 0, "KeTickCount", NULL},
    {157, NGB_KIND_DATA, NGB_CONV_NONE, 0, "KeTimeIncrement", NULL},
    {158, NGB_KIND_FUNC, NGB_CONV_STDCALL, 8, "KeWaitForMultipleObjects", (const void *)kstub_158},
    {159, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "KeWaitForSingleObject", (const void *)kstub_159},
    {160, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 1, "KfRaiseIrql", (const void *)kstub_160},
    {161, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 1, "KfLowerIrql", (const void *)kstub_161},
    {162, NGB_KIND_DATA, NGB_CONV_NONE, 0, "KiBugCheckData", NULL},
    {163, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 1, "KiUnlockDispatcherDatabase", (const void *)kstub_163},
    {164, NGB_KIND_DATA, NGB_CONV_NONE, 0, "LaunchDataPage", NULL},
    {165, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "MmAllocateContiguousMemory", (const void *)kstub_165},
    {166, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "MmAllocateContiguousMemoryEx", (const void *)kstub_166},
    {167, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "MmAllocateSystemMemory", (const void *)kstub_167},
    {168, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "MmClaimGpuInstanceMemory", (const void *)kstub_168},
    {169, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "MmCreateKernelStack", (const void *)kstub_169},
    {170, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "MmDeleteKernelStack", (const void *)kstub_170},
    {171, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "MmFreeContiguousMemory", (const void *)kstub_171},
    {172, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "MmFreeSystemMemory", (const void *)kstub_172},
    {173, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "MmGetPhysicalAddress", (const void *)kstub_173},
    {174, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "MmIsAddressValid", (const void *)kstub_174},
    {175, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "MmLockUnlockBufferPages", (const void *)kstub_175},
    {176, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "MmLockUnlockPhysicalPage", (const void *)kstub_176},
    {177, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "MmMapIoSpace", (const void *)kstub_177},
    {178, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "MmPersistContiguousMemory", (const void *)kstub_178},
    {179, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "MmQueryAddressProtect", (const void *)kstub_179},
    {180, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "MmQueryAllocationSize", (const void *)kstub_180},
    {181, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "MmQueryStatistics", (const void *)kstub_181},
    {182, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "MmSetAddressProtect", (const void *)kstub_182},
    {183, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "MmUnmapIoSpace", (const void *)kstub_183},
    {184, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "NtAllocateVirtualMemory", (const void *)kstub_184},
    {185, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "NtCancelTimer", (const void *)kstub_185},
    {186, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "NtClearEvent", (const void *)kstub_186},
    {187, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "NtClose", (const void *)kstub_187},
    {188, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "NtCreateDirectoryObject", (const void *)kstub_188},
    {189, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "NtCreateEvent", (const void *)kstub_189},
    {190, NGB_KIND_FUNC, NGB_CONV_STDCALL, 9, "NtCreateFile", (const void *)kstub_190},
    {191, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "NtCreateIoCompletion", (const void *)kstub_191},
    {192, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "NtCreateMutant", (const void *)kstub_192},
    {193, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "NtCreateSemaphore", (const void *)kstub_193},
    {194, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "NtCreateTimer", (const void *)kstub_194},
    {195, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "NtDeleteFile", (const void *)kstub_195},
    {196, NGB_KIND_FUNC, NGB_CONV_STDCALL, 10, "NtDeviceIoControlFile", (const void *)kstub_196},
    {197, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "NtDuplicateObject", (const void *)kstub_197},
    {198, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "NtFlushBuffersFile", (const void *)kstub_198},
    {199, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "NtFreeVirtualMemory", (const void *)kstub_199},
    {200, NGB_KIND_FUNC, NGB_CONV_STDCALL, 10, "NtFsControlFile", (const void *)kstub_200},
    {201, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "NtOpenDirectoryObject", (const void *)kstub_201},
    {202, NGB_KIND_FUNC, NGB_CONV_STDCALL, 6, "NtOpenFile", (const void *)kstub_202},
    {203, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "NtOpenSymbolicLinkObject", (const void *)kstub_203},
    {204, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "NtProtectVirtualMemory", (const void *)kstub_204},
    {205, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "NtPulseEvent", (const void *)kstub_205},
    {206, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "NtQueueApcThread", (const void *)kstub_206},
    {207, NGB_KIND_FUNC, NGB_CONV_STDCALL, 10, "NtQueryDirectoryFile", (const void *)kstub_207},
    {208, NGB_KIND_FUNC, NGB_CONV_STDCALL, 6, "NtQueryDirectoryObject", (const void *)kstub_208},
    {209, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "NtQueryEvent", (const void *)kstub_209},
    {210, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "NtQueryFullAttributesFile", (const void *)kstub_210},
    {211, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "NtQueryInformationFile", (const void *)kstub_211},
    {212, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "NtQueryIoCompletion", (const void *)kstub_212},
    {213, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "NtQueryMutant", (const void *)kstub_213},
    {214, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "NtQuerySemaphore", (const void *)kstub_214},
    {215, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "NtQuerySymbolicLinkObject", (const void *)kstub_215},
    {216, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "NtQueryTimer", (const void *)kstub_216},
    {217, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "NtQueryVirtualMemory", (const void *)kstub_217},
    {218, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "NtQueryVolumeInformationFile", (const void *)kstub_218},
    {219, NGB_KIND_FUNC, NGB_CONV_STDCALL, 8, "NtReadFile", (const void *)kstub_219},
    {220, NGB_KIND_FUNC, NGB_CONV_STDCALL, 8, "NtReadFileScatter", (const void *)kstub_220},
    {221, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "NtReleaseMutant", (const void *)kstub_221},
    {222, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "NtReleaseSemaphore", (const void *)kstub_222},
    {223, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "NtRemoveIoCompletion", (const void *)kstub_223},
    {224, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "NtResumeThread", (const void *)kstub_224},
    {225, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "NtSetEvent", (const void *)kstub_225},
    {226, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "NtSetInformationFile", (const void *)kstub_226},
    {227, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "NtSetIoCompletion", (const void *)kstub_227},
    {228, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "NtSetSystemTime", (const void *)kstub_228},
    {229, NGB_KIND_FUNC, NGB_CONV_STDCALL, 8, "NtSetTimerEx", (const void *)kstub_229},
    {230, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "NtSignalAndWaitForSingleObjectEx", (const void *)kstub_230},
    {231, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "NtSuspendThread", (const void *)kstub_231},
    {232, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "NtUserIoApcDispatcher", (const void *)kstub_232},
    {233, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "NtWaitForSingleObject", (const void *)kstub_233},
    {234, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "NtWaitForSingleObjectEx", (const void *)kstub_234},
    {235, NGB_KIND_FUNC, NGB_CONV_STDCALL, 6, "NtWaitForMultipleObjectsEx", (const void *)kstub_235},
    {236, NGB_KIND_FUNC, NGB_CONV_STDCALL, 8, "NtWriteFile", (const void *)kstub_236},
    {237, NGB_KIND_FUNC, NGB_CONV_STDCALL, 8, "NtWriteFileGather", (const void *)kstub_237},
    {238, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "NtYieldExecution", (const void *)kstub_238},
    {239, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "ObCreateObject", (const void *)kstub_239},
    {240, NGB_KIND_DATA, NGB_CONV_NONE, 0, "ObDirectoryObjectType", NULL},
    {241, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "ObInsertObject", (const void *)kstub_241},
    {242, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "ObMakeTemporaryObject", (const void *)kstub_242},
    {243, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "ObOpenObjectByName", (const void *)kstub_243},
    {244, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "ObOpenObjectByPointer", (const void *)kstub_244},
    {245, NGB_KIND_DATA, NGB_CONV_NONE, 0, "ObpObjectHandleTable", NULL},
    {246, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "ObReferenceObjectByHandle", (const void *)kstub_246},
    {247, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "ObReferenceObjectByName", (const void *)kstub_247},
    {248, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "ObReferenceObjectByPointer", (const void *)kstub_248},
    {249, NGB_KIND_DATA, NGB_CONV_NONE, 0, "ObSymbolicLinkObjectType", NULL},
    {250, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 1, "ObfDereferenceObject", (const void *)kstub_250},
    {251, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 1, "ObfReferenceObject", (const void *)kstub_251},
    {252, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "PhyGetLinkState", (const void *)kstub_252},
    {253, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "PhyInitialize", (const void *)kstub_253},
    {254, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "PsCreateSystemThread", (const void *)kstub_254},
    {255, NGB_KIND_FUNC, NGB_CONV_STDCALL, 10, "PsCreateSystemThreadEx", (const void *)kstub_255},
    {256, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "PsQueryStatistics", (const void *)kstub_256},
    {257, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "PsSetCreateThreadNotifyRoutine", (const void *)kstub_257},
    {258, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "PsTerminateSystemThread", (const void *)kstub_258},
    {259, NGB_KIND_DATA, NGB_CONV_NONE, 0, "PsThreadObjectType", NULL},
    {260, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlAnsiStringToUnicodeString", (const void *)kstub_260},
    {261, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "RtlAppendStringToString", (const void *)kstub_261},
    {262, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "RtlAppendUnicodeStringToString", (const void *)kstub_262},
    {263, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "RtlAppendUnicodeToString", (const void *)kstub_263},
    {264, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "RtlAssert", (const void *)kstub_264},
    {265, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "RtlCaptureContext", (const void *)kstub_265},
    {266, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "RtlCaptureStackBackTrace", (const void *)kstub_266},
    {267, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlCharToInteger", (const void *)kstub_267},
    {268, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlCompareMemory", (const void *)kstub_268},
    {269, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlCompareMemoryUlong", (const void *)kstub_269},
    {270, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlCompareString", (const void *)kstub_270},
    {271, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlCompareUnicodeString", (const void *)kstub_271},
    {272, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "RtlCopyString", (const void *)kstub_272},
    {273, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "RtlCopyUnicodeString", (const void *)kstub_273},
    {274, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "RtlCreateUnicodeString", (const void *)kstub_274},
    {275, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "RtlDowncaseUnicodeChar", (const void *)kstub_275},
    {276, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlDowncaseUnicodeString", (const void *)kstub_276},
    {277, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "RtlEnterCriticalSection", (const void *)kstub_277},
    {278, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "RtlEnterCriticalSectionAndRegion", (const void *)kstub_278},
    {279, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlEqualString", (const void *)kstub_279},
    {280, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlEqualUnicodeString", (const void *)kstub_280},
    {281, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlExtendedIntegerMultiply", (const void *)kstub_281},
    {282, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "RtlExtendedLargeIntegerDivide", (const void *)kstub_282},
    {283, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "RtlExtendedMagicDivide", (const void *)kstub_283},
    {284, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlFillMemory", (const void *)kstub_284},
    {285, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlFillMemoryUlong", (const void *)kstub_285},
    {286, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "RtlFreeAnsiString", (const void *)kstub_286},
    {287, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "RtlFreeUnicodeString", (const void *)kstub_287},
    {288, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "RtlGetCallersAddress", (const void *)kstub_288},
    {289, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "RtlInitAnsiString", (const void *)kstub_289},
    {290, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "RtlInitUnicodeString", (const void *)kstub_290},
    {291, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "RtlInitializeCriticalSection", (const void *)kstub_291},
    {292, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "RtlIntegerToChar", (const void *)kstub_292},
    {293, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlIntegerToUnicodeString", (const void *)kstub_293},
    {294, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "RtlLeaveCriticalSection", (const void *)kstub_294},
    {295, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "RtlLeaveCriticalSectionAndRegion", (const void *)kstub_295},
    {296, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "RtlLowerChar", (const void *)kstub_296},
    {297, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "RtlMapGenericMask", (const void *)kstub_297},
    {298, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlMoveMemory", (const void *)kstub_298},
    {299, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "RtlMultiByteToUnicodeN", (const void *)kstub_299},
    {300, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlMultiByteToUnicodeSize", (const void *)kstub_300},
    {301, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "RtlNtStatusToDosError", (const void *)kstub_301},
    {302, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "RtlRaiseException", (const void *)kstub_302},
    {303, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "RtlRaiseStatus", (const void *)kstub_303},
    {304, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "RtlTimeFieldsToTime", (const void *)kstub_304},
    {305, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "RtlTimeToTimeFields", (const void *)kstub_305},
    {306, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "RtlTryEnterCriticalSection", (const void *)kstub_306},
    {307, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 1, "RtlUlongByteSwap", (const void *)kstub_307},
    {308, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlUnicodeStringToAnsiString", (const void *)kstub_308},
    {309, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlUnicodeStringToInteger", (const void *)kstub_309},
    {310, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "RtlUnicodeToMultiByteN", (const void *)kstub_310},
    {311, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlUnicodeToMultiByteSize", (const void *)kstub_311},
    {312, NGB_KIND_FUNC, NGB_CONV_STDCALL, 4, "RtlUnwind", (const void *)kstub_312},
    {313, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "RtlUpcaseUnicodeChar", (const void *)kstub_313},
    {314, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlUpcaseUnicodeString", (const void *)kstub_314},
    {315, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "RtlUpcaseUnicodeToMultiByteN", (const void *)kstub_315},
    {316, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "RtlUpperChar", (const void *)kstub_316},
    {317, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "RtlUpperString", (const void *)kstub_317},
    {318, NGB_KIND_FUNC, NGB_CONV_FASTCALL, 1, "RtlUshortByteSwap", (const void *)kstub_318},
    {319, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlWalkFrameChain", (const void *)kstub_319},
    {320, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "RtlZeroMemory", (const void *)kstub_320},
    {321, NGB_KIND_DATA, NGB_CONV_NONE, 0, "XboxEEPROMKey", NULL},
    {322, NGB_KIND_DATA, NGB_CONV_NONE, 0, "XboxHardwareInfo", NULL},
    {323, NGB_KIND_DATA, NGB_CONV_NONE, 0, "XboxHDKey", NULL},
    {324, NGB_KIND_DATA, NGB_CONV_NONE, 0, "XboxKrnlVersion", NULL},
    {325, NGB_KIND_DATA, NGB_CONV_NONE, 0, "XboxSignatureKey", NULL},
    {326, NGB_KIND_DATA, NGB_CONV_NONE, 0, "XeImageFileName", NULL},
    {327, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "XeLoadSection", (const void *)kstub_327},
    {328, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "XeUnloadSection", (const void *)kstub_328},
    {329, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "READ_PORT_BUFFER_UCHAR", (const void *)kstub_329},
    {330, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "READ_PORT_BUFFER_USHORT", (const void *)kstub_330},
    {331, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "READ_PORT_BUFFER_ULONG", (const void *)kstub_331},
    {332, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "WRITE_PORT_BUFFER_UCHAR", (const void *)kstub_332},
    {333, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "WRITE_PORT_BUFFER_USHORT", (const void *)kstub_333},
    {334, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "WRITE_PORT_BUFFER_ULONG", (const void *)kstub_334},
    {335, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "XcSHAInit", (const void *)kstub_335},
    {336, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "XcSHAUpdate", (const void *)kstub_336},
    {337, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "XcSHAFinal", (const void *)kstub_337},
    {338, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "XcRC4Key", (const void *)kstub_338},
    {339, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "XcRC4Crypt", (const void *)kstub_339},
    {340, NGB_KIND_FUNC, NGB_CONV_STDCALL, 7, "XcHMAC", (const void *)kstub_340},
    {341, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "XcPKEncPublic", (const void *)kstub_341},
    {342, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "XcPKDecPrivate", (const void *)kstub_342},
    {343, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "XcPKGetKeyLen", (const void *)kstub_343},
    {344, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "XcVerifyPKCS1Signature", (const void *)kstub_344},
    {345, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "XcModExp", (const void *)kstub_345},
    {346, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "XcDESKeyParity", (const void *)kstub_346},
    {347, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "XcKeyTable", (const void *)kstub_347},
    {348, NGB_KIND_FUNC, NGB_CONV_STDCALL, 5, "XcBlockCrypt", (const void *)kstub_348},
    {349, NGB_KIND_FUNC, NGB_CONV_STDCALL, 7, "XcBlockCryptCBC", (const void *)kstub_349},
    {350, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "XcCryptService", (const void *)kstub_350},
    {351, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "XcUpdateCrypto", (const void *)kstub_351},
    {352, NGB_KIND_FUNC, NGB_CONV_STDCALL, 3, "RtlRip", (const void *)kstub_352},
    {353, NGB_KIND_DATA, NGB_CONV_NONE, 0, "XboxLANKey", NULL},
    {354, NGB_KIND_DATA, NGB_CONV_NONE, 0, "XboxAlternateSignatureKeys", NULL},
    {355, NGB_KIND_DATA, NGB_CONV_NONE, 0, "XePublicKeyData", NULL},
    {356, NGB_KIND_DATA, NGB_CONV_NONE, 0, "HalBootSMCVideoMode", NULL},
    {357, NGB_KIND_DATA, NGB_CONV_NONE, 0, "IdexChannelObject", NULL},
    {358, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "HalIsResetOrShutdownPending", (const void *)kstub_358},
    {359, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "IoMarkIrpMustComplete", (const void *)kstub_359},
    {360, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "HalInitiateShutdown", (const void *)kstub_360},
    {361, NGB_KIND_FUNC, NGB_CONV_CDECL, 4, "RtlSnprintf", (const void *)kstub_361},
    {362, NGB_KIND_FUNC, NGB_CONV_CDECL, 4, "RtlSprintf", (const void *)kstub_362},
    {363, NGB_KIND_FUNC, NGB_CONV_CDECL, 4, "RtlVsnprintf", (const void *)kstub_363},
    {364, NGB_KIND_FUNC, NGB_CONV_CDECL, 4, "RtlVsprintf", (const void *)kstub_364},
    {365, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "HalEnableSecureTrayEject", (const void *)kstub_365},
    {366, NGB_KIND_FUNC, NGB_CONV_STDCALL, 1, "HalWriteSMCScratchRegister", (const void *)kstub_366},
    {374, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "MmDbgAllocateMemory", (const void *)kstub_374},
    {375, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "MmDbgFreeMemory", (const void *)kstub_375},
    {376, NGB_KIND_FUNC, NGB_CONV_STDCALL, 0, "MmDbgQueryAvailablePages", (const void *)kstub_376},
    {377, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "MmDbgReleaseAddress", (const void *)kstub_377},
    {378, NGB_KIND_FUNC, NGB_CONV_STDCALL, 2, "MmDbgWriteCheck", (const void *)kstub_378},
};

const size_t ngb_kexport_count = sizeof(ngb_kexports) / sizeof(ngb_kexports[0]);
const uint32_t ngb_kexport_max_ordinal = 378u;
