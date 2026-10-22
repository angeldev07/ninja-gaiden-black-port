---
tags: [ngb, source, precedent]
updated: 2026-10-20
status: current
---

# OpenCE (native Halo CE): the precedent to follow

- Repo: https://github.com/OpenCommunityEdition/OpenCE. License AGPL-3.0-or-later (formerly CC0), created on 2026-09-26.
- Tested locally (`halo.exe`, SDL3, OpenGL 4.5), including online play. `VERIFIED` by `halo.log`.

## How it was made (`VERIFIED`, read in the repo's README.md and AGENTS.md)

- **It is a decompilation**, not an emulator. Lineage:
  [punpckhdq/halo](https://github.com/punpckhdq/halo) (*matching* decompilation, with its
  progress published on decomp.dev), then bnunu/halo-1 and then OpenCE.
- **The decompilation target is a development build:** Xbox build 01.01.14.2342,
  `cachebeta.exe` (sha256 `4cc87b45…9520`).
- **Types and names come from the debug symbols of later games:** the PDB
  of the Halo CEA beta, run through `pdb-decompiler`. **That is the shortcut NGB does not have.**
- The original *matching* decompilation requires the August 2001 XDK (leaked).
  OpenCE **forbids reading the SDK headers**: it wrote `port/include/xdk` from the
  debug information and public documentation.

## Port architecture (`VERIFIED`, read in the repo's README.md and AGENTS.md, 2026-10-20)

- `source/`: 466 C files of the decompilation, compiled almost unchanged. Every change is marked with `port:`.
- Platform layer (`port/linux/src/`): **Direct3D 8 over OpenGL (`d3d8_gl.c`)**, DirectSound and
  XInput over SDL3, files, threads, memory and sockets.
- Build: 32-bit x86 with clang on the MSVC ABI (`-fms-extensions -fshort-wchar -malign-double`), C89.
- **Deterministic math:** musl included in the repo and no FMA, so that all machines compute the same thing over the network.
- **Simulation at 30 ticks as on the Xbox. Rendering runs at the monitor's rate and
  interpolates between ticks ("Smooth Motion").** It is the technique to use in NGB to raise the fps without breaking the physics.
- The user supplies their own ISO, and the game extracts `maps/` on the first launch.
- It has an `AGENTS.md`, a sign that development is done with AI agents.

## What we take for NGB

- Layer model: game code plus platform layer ([[adr-01-port-approach]]).
- `d3d8_gl.c` as a reference for D3D8 over GL. If code is reused, the project becomes AGPL.
- The rule of not touching the XDK and the convention of marking changes with `port:`.
