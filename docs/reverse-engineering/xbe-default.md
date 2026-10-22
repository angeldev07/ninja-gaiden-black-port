---
tags: [ngb, reverse-engineering, xbe]
updated: 2026-10-20
status: current
---

# default.xbe: anatomy of the executable

Source of all the data: `python -I -m ngb_tools.xbe <xbe> --strings`, which generates the report in
`build/reports/xbe/<name>.{json,md}`. Everything on this page is `VERIFIED` unless indicated
otherwise. The leader checked a sample against the binary: libraries, number of imports, sections
and the string `MSC1310 XTL5849`.

## Identity

| Field | Value |
| --- | --- |
| File | `default.xbe`, 4,743,168 bytes, sha256 `B61F6EB5…D618` |
| Title | `NINJA GAIDEN Black`, version 3 |
| Title ID | `0x5443000D` (TC-013). Alternative: TC-003 |
| Region | `0x4` REST_OF_WORLD (`INFERRED`: PAL copy, consistent with the Redump name «Europe») |
| Allowed media | `0x202` (DVD_X2 plus the MEDIA_BOARD bit; the real meaning of that bit is doubtful) |
| Certificate | timedate 2005-09-08 |
| Type | **retail**: the entry and the thunk are de-XORed with the retail keys without ambiguity |
| Base | `0x00010000` |
| Entry point | `0x0032DA55` (in `.text`) |
| Kernel thunk | `0x003E3AE0` (start of `.rdata`) |
| TLS | `0x003E4258` |
| Init flags | MOUNT_UTILITY_DRIVE and LIMIT_64MB |

## Build origin

- **Engine code name: "Kunai".** The debug path is `c:\Kunai\~output\release_ltcg\Kunai.exe`,
  and the strings contain `d:\Kunai.xbe` and `" TeamNINJA 2003"`.
- `RSDS` CodeView record in the `0x180` field of the header: PDB `c:\Kunai\~output\release_ltcg\Kunai.pdb`,
  GUID `BAAFA588-83BB-468F-9250-0560D06EC3A6`, age 1. **The PDB is not on the disc.** If any
  build with that GUID turned up, it would give us symbols.
- **Toolchain:** the string `MSC1310 XTL5849` indicates MSVC 13.10 (Visual Studio .NET 2003) with XDK 5849.
- **`release_ltcg` build** (link-time code generation): 0 asserts and 0 `.c`/`.cpp`/`.h` strings. The 45 paths that
  the tool detects are asset and system paths (`z:\%s.afs`, `D:\bgm\`, …), not source paths.
- The 4 RTTI descriptors present belong to the standard library (`exception`, `logic_error`…). No
  engine-specific `.?AV` appears. `INFERRED`: the engine was compiled without RTTI.

## Sections (16)

`.text`, `D3DX`, `DSOUND`, `XONLINE`, `XNET`, `PSFD00`, `XGRPH`, `D3D`, `XPP`, `.rdata`, `.data`, `DOLBY`,
`XON_RD`, `$$XTIMAGE`, `$$XSIMAGE` and `.XTLID`. The SHA-1 of the 16 sections matches the digest stored in
the header (`VERIFIED`), so the raw bytes the tool reads are the ones the linker signed. The
virtual-to-file mapping is `INFERRED`: it is backed by the strings, the certificate and the thunk, which
resolve well when following virtual pointers.

`INFERRED`: `PSFD00` is Sofdec (CRI video). `D3D`, `DSOUND` and `XGRPH` are XDK libraries linked
into their own sections, which makes them easier to isolate in the rehosting ([[adr-01-port-approach]]).

## XDK libraries (all from build 5849)

| Library | QFE | Note |
| --- | --- | --- |
| XAPILIB | 1 | high-level Xbox API |
| D3D8LTCG | 15 | **Direct3D 8 in LTCG variant**: the D3D code may be inlined into the game. This complicates identifying its functions by signatures in H5 |
| D3DX8 | 1 | D3DX utilities |
| XGRAPHCL | 9 | XGraphics (texture swizzle, etc.) |
| DSOUND | 11 | DirectSound |
| XONLINES | 17 | Xbox Live ("approved" flag = possible) |
| XBOXKRNL | 1 | kernel stubs |
| LIBCMT / LIBCPMT | 1 | C and C++ CRT |

## Kernel imports: 145 (128 functions and 17 data items)

All ordinals were resolved with the nxdk table (`src/ngb_tools/data/xboxkrnl_exports.tsv`, CC0).
The table's `kind` field separates them (`VERIFIED`, `kernel_imports` in the report):
The first ones are: `MmPersistContiguousMemory`, `MmAllocateContiguousMemory`, `LaunchDataPage`,
`MmFreeContiguousMemory`, `RtlNtStatusToDosError`, `HalReturnToFirmware`, `NtClose`,
`NtQueryVolumeInformationFile`, `NtOpenFile` and `RtlInitAnsiString`.
- **128 functions**: what the H6 HLE kernel has to implement or leave as stubs.
- **17 data exports**: variables the HLE kernel has to expose with their contents. They are
  `LaunchDataPage`, `KeTickCount`, `XboxHardwareInfo`, `XboxKrnlVersion`, `XeImageFileName`,
  `PsThreadObjectType`, `ExEventObjectType`, `IdexChannelObject`, `HalDiskCachePartitionCount`,
  `HalBootSMCVideoMode`, `HalDiskSerialNumber` and `HalDiskModelNumber`, plus the keys (`XboxHDKey`,
  `XboxSignatureKey`, `XboxAlternateSignatureKeys`, `XboxLANKey` and `XePublicKeyData`).

## Linked CRI middleware (version strings, builds from May and June 2005)

ADXT 9.50, ADXF 7.33, ADXXB 1.62, AIXP 2.32, ADXENC 1.10, SJ 6.32, SVM 1.60, CVFS 2.40, LSC 2.25, RNA 3.65,
WXCI 2.04, MFCI 1.10 and SKG 0.64. Sofdec: MWSFD 3.57.02, SFD 1.964, MPV 1.964, MPS 1.924, SFH 1.19 and SFX 2.29.
This is where the ADX/AIX audio ([[afs]]) and the `.sfd` video ([[ngb-disc]]) come from.

## Engine clues (strings)

- **Asset I/O:** `z:\%s.afs`, `%s.afs`, `%s\live00.afs`, `spr.afs`, `sse.afs`, `bgm.afs`, `D:\bgm\`,
  `Z:\instinfo.ins`, `movie\%s`, `ninja.aix`, `dsstdfx.bin`, `load_img.bin` and `MFS:%08x.%08x`.
  `INFERRED`: `z:` is the utility drive, used as a hard-disk cache.
- **Save games:** `save%03d.dat`, `system.dat` and `t:\system.dat`.
- **Internal launcher:** `D:\NinjaTomoe.xbe`, `D:\NinjaTrilogy.xbe`, `D:\default.xbe`, `d:\dashupdate.xbe`
  and `y:\xboxdash.xbe`.
- **1788 engine identifiers**, grouped by prefix: STRY 479 (163 are `STRY_EVEFLG_*`, event
  flags), ITEM 393, SOBJ 367 (stage objects), STG 214, CHAR 112, RM 45 and ITMCOMP 14. The
  zone prefixes (TLN, TOMB, RAIL…) match the names of the `.afs` files.

## The other XBEs on the disc

Source: `build/reports/xbe/<name>.json` of each one (`VERIFIED`, tool output).

| XBE | Title | Libs and notes |
| --- | --- | --- |
| `NinjaTrilogy.xbe` / `NinjaTomoe.xbe` | `ryukenden (us)` / `ryukenden (jp)` | Twins, one US and one JP. It is the emulator of the NES Ninja Gaiden games (Ryukenden). D3D8 **without** LTCG and 100 imports. The debug path is in Shift-JIS: `e:\prog\work\tmg\…\ryukenden (xbox us).exe` |
| `update.xbe` | Online Updater Application | Microsoft, `D:\xboxfre-dec03\private\online\xodash\…\TitleUpdate.exe` |
| `dashupdate.xbe` | Xbox Dashboard Updater | Microsoft, `…\dashupd\obj\i386\DashUpdate.exe` |

All 5 share the certificate (TC-013, same date): they were signed together.

## Discrepancies with the public documentation

Sources: [xboxdevwiki/Xbe](https://xboxdevwiki.net/Xbe) and
[Cxbx-Reloaded `Xbe.h`](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/common/xbe/Xbe.h),
the latter only to compare the layout (nothing was copied).

- `0x130`: xboxdevwiki calls it "stack size" and Cxbx `dwPeStackCommit`; the tool uses `stack_size`.
- `0x180`: xboxdevwiki calls it "Unknown2 (XDK ≥ 5455)", but here it holds the virtual address of the
  `RSDS` CodeView record. `0x178` is zero in all 5 XBEs.
- nxdk exports ordinals 1 to 366 and 374 to 378. Ordinals 367 to 373 do not exist, and none appears in these XBEs.

## Next

- H5: Ghidra project with ghidra-xbe, and identify D3D8LTCG, DSOUND and XAPI by signatures.
- H6: the HLE kernel starts from these 128 functions and 17 variables.

## H5: named functions (Ghidra + XbSymbolDatabase, `VERIFIED`)

The Ghidra project is in `NGB_DATA\ghidra\ngb`, and is reproduced with `tools/ghidra/h5-run.ps1` ([[tools]]).
The full coverage report, in `NGB_DATA\ghidra\reports\`, comes with the decompilations, which are
code derived from the game and **do not go in the repo**.

- **9,118 functions, 548 named (6.0 %):** D3D8 98, DSOUND 113, XAPI 130, XONLINE 58, D3DX8 4, XGRAPHICS 2 and
  LIBC/LIBCPMT 142 (Function ID).
- **By section:** in `D3D`, 96 of 220 functions are named (38 % of the code). In `.text`, which is the Kunai
  engine plus CRI, only 0.71 % of the bytes are named, because the signatures cover the XDK libraries and not the
  game code.
- **31 D3D8 globals** are named, for example `g_pDevice` at `0x3DAA88`, `g_RenderState` at `0x3DAA98`,
  `g_DeferredTextureState` at `0x3D83D0` and `g_Stream` at `0x3DBA68`. The 145 kernel thunks are also named.
- **First NV2A MMIO** (`0x3D171B`, [[native-boot]]): it falls in `FUN_003d1715`, inside
  `Direct3D_CreateDevice` → `FUN_003cf700` → `CMiniport_InitHardware`. This is where the `0xFD000000` registers are mapped.
- **H7a vblank wait** (`0x3CE6CF`): it is `KeWaitForSingleObject(dev+0x1DBC = m_VerticalBlankEvent)`, in the
  AvSetDisplayMode loop, inside the epilogue of `D3DDevice_Swap_0`. The event is signaled by the ISR `0x3D2040` through
  the DPC `0x3D2310`.

What this implies for rendering is in [[adr-06-hybrid-renderer]].
