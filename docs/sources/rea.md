---
tags: [ngb, source, tooling]
updated: 2026-10-20
status: current
---

# REA (Reverse Engineer Anything): evaluation

- Repo: https://github.com/morluto/rea. MIT license. About 348k stars according to the repo page on
  2026-10-20. npm package `rea-agents` v6.7.0 (2026-10-18, according to `npm view`). It ships releases almost daily
  (CHANGELOG).
- It is an MCP plus a CLI that gives agents access to Ghidra, IDA or Hopper in headless
  mode: pseudocode, assembly, strings, xrefs, incoming and outgoing calls and function
  dossiers, all with evidence. It installs workflow skills into the agent.

## Fit with NGB (`VERIFIED` in docs/windows-ghidra-p0.md, raw-binary.md and native-investigation.md)

- **It does not accept XBE.** It only supports PE, ELF, Mach-O or headerless binaries (`raw-binary`).
- **On Windows it is experimental, read-only and PE-only.** 32-bit x86 PEs
  arrived in v6.0.0. x86 `raw-binary` is not supported on Windows.
- **Renaming and commenting** (`annotate_native_function`, `set_address_name`) **only works on Linux or macOS**,
  and on an **ephemeral** database. It is not usable as a persistent project.
- Requirements: Node ≥ 22.19 (the evaluation machine had 22.15.1), Ghidra 12.1.x (not installed) and JDK 21 (installed).
- WSL with Ubuntu 24.04 was available, which is the way to use it without the Windows limitations.
- Relevant showcase: **DX-Ball**, an x86 function rebuilt in C that passes 3,205
  cases and reproduces the 63 compiled bytes. It is the model for differential tests.

## Stance

It works as a **helper for one-off investigations**, not as the basis of the project
([[adr-02-re-tools]]). If it is ever used on Windows, a custom `xbe2pe` converter
would enable it, and writing it would also make a useful side project.

## Legal and security

- The disclaimer allows education, interoperability, preservation and analyzing one's own
  software. It does not help with "reproducing its protected code and assets".
- The README warns about clones. Install only from npm `rea-agents`, with the version pinned,
  and review the diff that `setup` proposes.
