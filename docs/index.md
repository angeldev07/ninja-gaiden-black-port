---
tags: [ngb, index]
updated: 2026-10-22
status: current
---

# NGB Port Wiki: index

Catalog of all pages. Start with [[GOAL-ngb-port]]. The conventions are
in [[CLAUDE]] and the timeline in [[log]].

## Root

- [[GOAL-ngb-port]]: what, why, where everything is, rules and current decisions. **Entry point.**
- [[CLAUDE]]: vault schema, RE rules (anchors and confidence tags) and legal rules.
- [[log]]: chronological record, append-only.
- [[wiki-llm]]: the LLM Wiki pattern we follow (our summary of Karpathy's idea file).

## Players (`guide/`)

- [[getting-started]]: from zero to playing: requirements, your own copy, install, import, controls, saves, settings, updates, troubleshooting and FAQ.

## Observation: reverse engineering (`reverse-engineering/`)

- [[ngb-disc]]: Redump image and XISO, 249 files by type, AFS groups, executables and videos.
- [[native-boot]]: what `default.xbe` does when it boots natively (H6): threads, XAPI, drives, hard-disk cache, D3D init up to the first NV2A MMIO.
- [[characters]]: gallery of the 62 `chr_*`: 49 export to GLB, 20 with idle; failures grouped by cause.
- [[mods]]: hidden costumes and Intercept; `--dump-ram` to compare RAM states (draft)
- [[audio-apu]]: MCPX APU: voice processor, mixing to waveOut, free-voice interrupts and CRI music (thread priorities and 60 Hz vblank).
- [[xapi-input]]: HLE gamepad for the 8 XAPI functions. The hang after the warning screens is not the input (hypothesis: Sofdec video without audio).
- [[gpu-nv2a]]: H7a. Emulated NV2A registers, vblank and push buffer: the game reaches the warnings screen and submits 808k primitives (not yet rasterized).
- [[shaders-nv2a]]: translator from NV2A vertex programs and register combiners to GLSL; the game's 16 real vertex programs decode.
- [[xbe-default]]: anatomy of `default.xbe`. "Kunai" engine, MSVC 13.10 with XDK 5849, D3D8LTCG, 16 sections, 145 kernel imports, CRI middleware and the other 4 XBEs on the disc.

## Observation: formats (`formats/`)

- [[afs]]: CRI AFS container. Structure, quirk of the custom field, inventory of the 3,766 entries, magics and entropy.
- [[chunk-teamninja]]: common header of the Kunai engine formats (KMD, `chr_dat`, `stry_dat`…).
- [[kmd]]: models and scenes. Fixed slots (OBJINFO, XPR0, HIERARC…); `ryu.kmd` has 59 objects and 19 textures.
- [[xpr0]]: Xbox Packed Resource (textures, palettes, VB and IB). `Format` field and swizzle.
- [[mtn]]: `.mtn`/`.mot` animations: 44 tracks (root, trunk and 4-limb IK), 20-bit keys, 258 motions of Ryu.
- [[stages]]: zone KMD (multipart descriptor, world-space vertices), HITMDL collision and the 17 zones exported to glb.
- [[weapons]]: Ryu's 10 weapons (KMD + `.itm`) and the bone attachment table (state, bone, position and rotation).
- [[adx]]: CRI audio (95 music tracks and 317 voices).
- [[wva]]: sound-effects bank in Xbox ADPCM.

## Observation: findings (`findings/`)

_(still empty)_

## Proposal: design and decisions (`target/`)

- [[roadmap]]: tangible milestones H0 to H9+, with definition of done and status.
- [[adr-01-port-approach]]: rehosting plus platform layer plus incremental decompilation.
- [[adr-02-re-tools]]: Ghidra with ghidra-xbe, our own tools, REA as a helper, xemu as the oracle.
- [[adr-03-game-data-outside-repo]]: the game data in `NGB_DATA`, outside the repo and any cloud-synced folder.
- [[adr-05-native-runtime]]: fixed-base loader, shadow PE header, KPCR overlaid on the TEB, TLS, files and MMIO. Options for H7.
- [[adr-06-hybrid-renderer]]: H7b. Emulated PGRAPH as the base, with HLE in Swap, KickOff, fences and vblank. Decided with the Ghidra data.
- [[adr-07-releases-and-updater]]: tag-driven draft releases with an update manifest, and a launcher updater (check, SHA-256, rename-and-replace).
- [[adr-04-version-control]]: trunk-based, Conventional Commits, squash PRs, review, CI gates and a tag per milestone.

## Team (`team/`)

- [[team]]: roles, coordination rules, debugging and deployment history.
- [[agent-task-template]]: standard brief for subagents.

## Tools (`tools/`)

- [[tools]]: extract-xiso and our own tools (`xbe_info.py`, `afs.py`).

## External sources (`sources/`)

- [[opence]]: native Halo CE. Decompilation with symbols and D3D8 over GL. It is the precedent.
- [[rea]]: RE MCP. Evaluation and limits (does not accept XBE; read-only on Windows).
- [[decomp-ninja]]: abandoned earlier attempt and official context (lost source code).
