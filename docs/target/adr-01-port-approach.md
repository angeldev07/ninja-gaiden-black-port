---
tags: [ngb, target, adr]
updated: 2026-10-20
status: current
---

# ADR-01: port approach (rehosting plus incremental decompilation)

## Context

- The Xbox CPU is a 733 MHz Pentium III (32-bit x86; public console specification,
  https://xboxdevwiki.net/Xbox_Hardware). The code of `default.xbe` can
  run **as is** in a 32-bit Win32 process, because no instructions need translating,
  unlike the 360 (PowerPC) or the N64 (MIPS).
- The Xbox SDK (D3D8, DirectSound, XAPI) is **statically linked** inside the
  XBE: there are `D3D`, `DSOUND`, `XGRPH` and other sections, and 9 XDK 5849 libraries in its
  header. The game calls the `xboxkrnl` kernel by ordinals, with 145 imports in the thunk.
  `VERIFIED`: [[xbe-default]].
- Team Ninja no longer has the complete NGB source code ([[decomp-ninja]] § official context).
- Direct precedent: [[opence]] (Halo CE) started from a decompilation with debug symbols
  of a development build. **NGB does not have that shortcut** (`UNVERIFIED` whether
  any prototype with symbols exists).

## Decision

Advance on three layers that can grow in parallel:

1. **Rehosting.** A 32-bit Win32 loader places the XBE sections at their virtual
   addresses (base `0x10000`, to be confirmed with [[xbe-default]]), resolves the
   kernel imports with its own **HLE kernel** and jumps to the entry point.
2. **Platform layer.** The SDK functions, identified by signatures, are
   replaced by PC implementations: D3D8 to OpenGL/Vulkan, DirectSound and XInput
   over SDL3, files, threads and memory. OpenCE has an equivalent layer
   (`d3d8_gl.c`) that serves as a reference.
3. **Incremental decompilation.** The game's functions are turned into C one by one (in the
   style of re3), each validated with **differential tests** against the original. In the
   end there is compilable source code, as in OpenCE.

## Discarded alternatives

- **"Matching" decompilation from day 1** (SM64 style): requires the same
  compiler. The string `MSC1310 XTL5849` in `default.xbe` indicates MSVC 13.10 (Visual
  Studio .NET 2003) with XDK 5849 (`VERIFIED`, [[xbe-default]]). That toolchain ships with
  the XDK, which we cannot use ([[CLAUDE]] § legal), and the path means years without anything tangible.
- **Full emulation** (like xemu): it already exists and does not teach porting. It is used as an oracle.
- **Static recompilation** (XenonRecomp style): it adds nothing if the source CPU is already
  x86. The problem is in the hardware and the SDK, not in the ISA.

## Consequences

- The first tangible executable result is the native boot up to the first
  unimplemented kernel call ([[roadmap]] H6), long before having any C.
- SDK library identification (signatures) has to be built without using the XDK.
- 32-bit process: the low address range has to be reserved at startup.
  `INFERRED`: it is the same problem Cxbx-Reloaded solves.
- License: if we reuse code from OpenCE, the project becomes AGPL-3.0, which is
  compatible with its non-commercial nature.
