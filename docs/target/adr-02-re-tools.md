---
tags: [ngb, target, adr, tooling]
updated: 2026-10-20
status: current
---

# ADR-02: reverse-engineering tools

## Decision

| Need | Tool | Why |
| --- | --- | --- |
| Persistent analysis base (thousands of names and types) | **Ghidra with the ghidra-xbe extension** | Loads the XBE natively (header, kernel thunk, sections) and the project is saved between sessions. |
| Let Claude operate on that project | **MCP connected to an open Ghidra** (GhidraMCP or other). `UNVERIFIED`: to be evaluated. | Rename and type in the real project. |
| Assisted one-off investigations | **REA** in WSL Ubuntu 24.04 ([[rea]]) | Good evidence workflow. On Windows it is read-only and does not accept XBE. |
| Reproducible formats and reports | **Our own Python tools** in `tools/` and `src/` ([[tools]]) | stdlib only, tested and readable. They are the source of the wiki's numbers. |
| Dynamic oracle | **xemu**, based on QEMU, whose GDB stub (`-s -S`) is `UNVERIFIED` in this installation | Breakpoints and memory reads of the real game to validate hypotheses. |
| Disc image | **extract-xiso** v2.7.1 | Redump to XISO and file extraction. |
| Debugging and method | Skills `systematic-debugging`, `test-driven-development` and `verification-before-completion` | Discipline: evidence before claiming. |

## Discarded alternatives

- **REA only:** it does not load XBE, on Windows it is read-only and it uses an ephemeral
  database ([[rea]]). It is not usable as a months-long project.
- **IDA:** it has a commercial license and offers no clear advantage over Ghidra with ghidra-xbe
  for 32-bit x86.

## Pending (requires installing, confirm with the project owner)

- Ghidra 12.1.x and ghidra-xbe (JDK 21 is already installed).
- Node ≥ 22.19 for REA (22.15.1 was available).
- The MCP for Ghidra.
