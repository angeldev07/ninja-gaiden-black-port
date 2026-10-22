---
tags: [ngb, goal]
updated: 2026-10-22
status: current
---

# GOAL: native PC port of Ninja Gaiden Black

> **A new session reads this page first.** Then it reads [[index]] and then
> [[log]] (the latest entries: `grep "^## \[" docs/log.md | tail -5`).

## What

Run Ninja Gaiden Black (original Xbox, PAL version "Europe (En,Fr,De,Es,It)")
**natively on Windows**, without an emulator. The game code runs
on a layer of our own that replaces the Xbox hardware and SDK. The model is
[[opence]] (native Halo CE).

## Why

This is an **unofficial, non-commercial fan port**. What matters is the engineering process and
documenting it in the open: reverse engineering, loaders, kernels, graphics and
decompilation. Nothing from the game is distributed, and you need your own copy of the game.

**Progress criterion:** every milestone leaves something **tangible**: a report, an image
or a sound extracted, or a binary that boots. Progress is incremental, with AI
assistance, so the plan is counted in milestones, not in months.

## Where everything is

| What | Where |
| --- | --- |
| Repo (code, tools, vault) | the repo root (GitHub repo `angeldev07/ninja-gaiden-black-port`) |
| Vault (this brain) | `docs/`. Open it as a vault in Obsidian. |
| Extracted game data (immutable) | `%NGB_DATA%\game\` (`NGB_DATA` defaults to `%USERPROFILE%\ngb-data`). Outside the repo: [[adr-03-game-data-outside-repo]] |
| Content extracted from containers | `%NGB_DATA%\extracted\` |
| XISO image of the game | your own disc image ("Ninja Gaiden Black (Europe) (En,Fr,De,Es,It)", XISO format) |
| Reference emulator (oracle) | xemu 0.8.136, your own xemu install |
| extract-xiso | `extract-xiso.exe` from your extract-xiso build ([[tools]]) |
| Generated reports | `build/reports/` (ignored by git; regenerated with the tools) |

## Rules (short version)

- No byte of the game goes into the repo. Using the leaked XDK is forbidden. See [[CLAUDE]] § Legal rules.
- Every claim carries an anchor and a `VERIFIED` / `INFERRED` / `UNVERIFIED` tag.
- The leader is the only one who writes in the vault. Subagents report in caveman ultra. See [[team]].
- Tools that read game data run with `python -I` (isolated).
- Code, comments and docs are in English.

## Current decisions

- [[adr-01-port-approach]]: rehosting of the XBE plus incremental decompilation. The end state is like OpenCE.
- [[adr-02-re-tools]]: persistent Ghidra project with ghidra-xbe, our own Python tools, REA as a helper in WSL and xemu as the oracle.
- [[adr-03-game-data-outside-repo]]: the game data lives in `NGB_DATA`.
- [[adr-04-version-control]]: the leader manages git and the PRs. GitHub repo `angeldev07/ninja-gaiden-black-port`.

## Status and next step

See [[roadmap]]: milestones, definition of done and status of each one.
