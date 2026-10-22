---
tags: [ngb, target, adr, runtime]
updated: 2026-10-21
status: current
---

# ADR-05: native runtime (loader and HLE kernel)

## Context

[[adr-01-port-approach]] chose *rehosting*: run the XBE's x86 code as is in a 32-bit Win32 process.
H6 made it real ([[native-boot]]). This ADR records how, and the non-obvious decisions. The code is in
`port/` and its README has the details.

## Decisions

| Problem | Decision | Why |
| --- | --- | --- |
| The XBE sits at `0x10000`, but that area is filled before `main` | The `ngb-loader.exe` exe is linked at `0x10000` with `/FIXED` and a huge BSS (`.xbox`), so that Windows reserves the range when it creates the process. The logic lives in `ngb-core.dll` | It is the `cxbxr-ldr` technique of Cxbx-Reloaded (`INFERRED`: from memory, not checked against the source). Reserving with `VirtualAlloc` at startup is not enough (tested by the analyst) |
| When the PE header is overwritten with the XBE's, `CreateThread` fails (error 193) | **"Shadow" PE header**: the NT headers are copied to `0x01000000`, `MZ` is written at `0x10000` and `e_lfanew` at `0x1003C` | Those fields fall on the magic and the RSA signature of the XBE, which the game does not read at run time (`VERIFIED`: the game boots) |
| Contiguous memory and physical addresses for the GPU | 64 MB arena at `0x80000000` (`/LARGEADDRESSAWARE`), with `MmGetPhysicalAddress = va − 0x80000000` | Reproduces the Xbox mapping, with its 64 MB of RAM |
| Xbox code reads the KPCR through `fs:`, but on Windows `fs:` is the TEB | **TEB overlay**: `fs:[0x1C]` → fake KPCR, `[0x20]` → Prcb, `[0x24]` → `tid<<8` (IRQL 0), `[0x28]` → fake ETHREAD. `fs:[0..8]` (SEH and stack limits) mean the same in both systems | WOW64 has no LDT, so there is no other possible selector. Cost: `GetCurrentThreadId` returns garbage on Xbox threads |
| XAPI TLS (negative `_tls_index` under StackBase) | Each Xbox thread runs on its own stack (≥ 1 MB) with the TLS block at the top, and the TEB points to it. The SEH chain is reset with ntdll's final record | Compatible with SEHOP |
| Threads, IRQL, DPC | An Xbox thread is a Win32 thread with affinity to 1 CPU. IRQL ≥ DPC takes a recursive global lock. DPCs and timers run on an internal thread, and there are no interrupts | The minimum to boot. It is debt for H7+ |
| File system | `D:` is the game folder, read-only. `PartitionN\` is mapped to `NGB_DATA\runs\hdd\partitionN\`, and the raw volume to `partitionN.raw`. A FATX geometry (16 KB cluster) is simulated with the partition table of an 8 GB retail disk | Without FATX geometry, XAPI asks to format and calls `HalReturnToFirmware(2)` |
| Kernel stubs | They are **generated** from `src/ngb_tools/data/xboxkrnl_exports.tsv` with `tools/gen_kernel_stubs.py`, respecting the convention (stdcall/fastcall/variadic cdecl) and the number of arguments | A single source of truth. `--check` in the tests detects whether `stubs.c` is out of date |
| MMIO | The NV2A **stops** execution: it is the end of H6. USB, APU, AC97, NIC and flash are treated as "traced RAM": the NOACCESS page triggers the log, it is single-stepped and closed again | Advance as far as possible without modeling hardware that is not needed yet |

## Risks

- **Path confinement** (fixed in the review of PR #14): `port/src/kernel/hostpath.c` rejects `..`, `.`, `:`, `/`, wildcards and a trailing dot or space, and requires that after `GetFullPathNameA` the path stays under its root. The 22 checks of `hostpath_test.exe` cover it. `NtSetInformationFile` only admits an allowlist of classes, with no delete or rename.

- The TEB overlay and the shadow header depend on Windows internals: an update
  could break them.
- The image is RWX and the process runs without DEP. It is acceptable because it only executes the user's own
  copy of the game.

## Next decision: H7, how to reach the first frame

- **A. Emulate a minimal set of NV2A registers** (PMC, PBUS, PFB, PGRAPH and the push buffer FIFO), xemu style.
  It is faithful to the hardware, but the push buffer has to be interpreted.
- **B. HLE of D3D8**, Cxbx style: identify the D3D8 functions in the XBE and replace them with
  implementations over OpenGL or Vulkan. It is the [[opence]] route (`d3d8_gl.c`), but with **D3D8LTCG** the functions
  may be inlined and are hard to find ([[xbe-default]]).

It will be decided in H7 with data. Beforehand it is worth having the Ghidra project (H5) to see how much of D3D8 is identifiable.

- **Correction (H7a):** `fs:[0x24]` is NOT overlaid. On Windows it is `ClientId.UniqueThread`, and overwriting it caused lost CRITICAL_SECTION/SRW wakeups. The XBE's IRQL reads (`movzx r32, byte fs:[24h]`) are patched at load time ([[gpu-nv2a]]).
