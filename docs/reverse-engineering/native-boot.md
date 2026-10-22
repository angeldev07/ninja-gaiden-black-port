---
tags: [ngb, reverse-engineering, runtime, h6]
updated: 2026-10-21
status: current
---

# Native boot of default.xbe (H6): what the game does at startup

`ngb-loader.exe` runs the real code of `default.xbe` in a 32-bit Win32 process, with its own HLE kernel
([[adr-05-native-runtime]]). This page records what **the game itself does**, as seen in the loader log.
Sources:
- `NGB_DATA\runs\h6-20261021-000838.log` (471 lines), from the analyst;
- an independent run by the leader on 2026-10-21, which ends at the same point.

Everything is `VERIFIED` by the log, except where indicated.

## Boot sequence

1. **Main thread → game thread.** The entry point `0x0032DA55` creates a system thread with
   `PsCreateSystemThreadEx`, with the routine at `0x0032D9E1`, and closes its handle. Everything else runs in that thread
   (T2).
2. **XAPI init**, the XDK's high-level library:
   - critical sections;
   - shutdown notification;
   - DPC and timer;
   - reading the EEPROM settings (`ExQueryNonVolatileSetting` ×8);
   - memory allocation (`NtAllocateVirtualMemory`).
3. **Drive mounting:**
   - `\??\D:` → `\Device\CdRom0`, the game DVD;
   - `T:` → the TDATA folder of title `5443000d`;
   - U:, Z: and the hard-disk partitions.
4. **Version check:** XAPI compares the linked libraries (`RtlEqualString` ×45).
5. **Save metadata:** it writes `TitleMeta.xbx`, `TitleImage.xbx` and `SaveImage.xbx` to U: (`NtWriteFile` ×33).
6. **Hard-disk cache:** it reads the raw cache DB (partition0, sector 4) and **formats the Z: cache partition**
   (Partition5) with geometry and partition IOCTLs. It fits the `z:\%s.afs` paths of [[xbe-default]]: the game
   copies the AFS files to the hard-disk cache.
7. **XInitDevices:** the OHCI USB driver, for the controllers.
8. **Contiguous memory for the GPU:** `MmAllocateContiguousMemory` (0x2472000 + 0x200000 + …).
9. **Direct3D 8 init**, in the LTCG variant.

**End of native boot:** at `EIP 0x003D171B`, XBE code, the game executes `mov eax,[0xFD001804]` /
`or eax,4` / `mov [...],eax`, which reads and writes **NV_PBUS** of the NV2A. It is the first time it touches the GPU. H6
ends there.

## Most frequent calls (analyst's log, 285 calls, 39 distinct functions)

`RtlInitAnsiString` 57, `RtlEqualString` 45, `NtWriteFile` 33, `RtlEnter/LeaveCriticalSection` 18 each,
`RtlInitializeCriticalSection` 15, `NtClose` 13, `NtCreateFile` 8, `ExQueryNonVolatileSetting` 8,
`KeRaiseIrqlToDpcLevel`/`KfLowerIrql` 7 each.

On a second run, with the virtual hard disk already formatted, fewer distinct functions are called (35), because
XAPI no longer formats the cache.

## Beyond the first MMIO (exploration with `--mmio-gpu-ram`, `INFERRED`)

If the GPU registers are treated as RAM, the game moves forward:
- it claims GPU instance memory (`MmClaimGpuInstanceMemory`);
- it touches PCI and executes WBINVD;
- it stays **polling at `0x3CD560`**: it writes bit 16 to `0xFD100410` (PFB, write-combine flush) and waits
  for it to drop.

With RAM that bit never drops. This is the first proof that the NV2A has to be modeled, or bypassed with D3D8 HLE:
that is the H7 decision.

## HLE kernel status

- **105 of the 128 functions** imported by `default.xbe` are implemented, plus the 17 data variables (with approximate
  values; the keys are zeroed).
- 23 are missing:
  - the I/O stack with IRPs (`Io*Packet`, `IofCallDriver`, `IofCompleteRequest`, `IoBuildSynchronousFsdRequest`…);
  - `NtDeleteFile`, `NtQueryDirectoryFile` and `ObReferenceObjectByName`;
  - the `Xc*` cryptography functions.
- In total, 118 exports are implemented out of the 371 in the table.

## New game: chapter 1 stayed black (`VERIFIED`)

- **Symptom:** after the "Chapter 1 – Proceed" card (X button), the screen stayed black with the game
  drawing at 60 fps.
- **Diagnosis:**
  - with RenderDoc, the level scene was drawn in full, with Ryu and the life bar, and the last draw of the frame
    was an opaque full-screen black quad: the fade-in, with vertex color `(0,0,0,1)`;
  - the 587 primitives were identical in every frame: the game logic was not advancing;
  - with `--log-cap 20000`, the main thread endlessly repeated `DeleteFile("z:\rtm_yasik.afs")`, the deletion of an
    AFS from the hard-disk cache (Z:, partition 5) to make room for the new level's.
- **Cause:** `NtSetInformationFile` rejected class 13 (`FileDispositionInformation`), which XAPI uses to
  delete. It was on the blacklist so that the game could not delete files on the PC. `DeleteFile` failed and the
  game retried it on every frame.
- **Fix:** class 13 is accepted only if the handle is inside the virtual hard disk (`NGB_DATA\runs\hdd`),
  according to the final path that Windows reports (`GetFinalPathNameByHandle`, which resolves links and `..`). `NtOpenFile` asks
  the host for `DELETE` when the game asks for it outside D:.
- **Result:** chapter 1 starts: an engine cutscene and gameplay in the gorge.
- **Tool:** the new-game script (gamepad reads, no save game):
  `t=205p:START@12p,t=247p:START@12p,t=314p:START@12p,t=343p:START@12p,t=377p:START@12p,t=521p:A@12p,`
  `t=684p:START@12p,t=718p:START@12p,t=1114p:START@12p,t=7500p:X@12p`. Gameplay starts around frame 9000.
