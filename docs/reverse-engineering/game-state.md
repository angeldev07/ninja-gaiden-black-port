---
tags: [ngb, reverse-engineering, achievements]
updated: 2026-10-31
status: draft
---

# Game state: chapter, difficulty, mode and the profile block

Variables of the game that the achievements read ([[achievements-brief]]). All addresses are in the **reference
build** (PAL `default.xbe`, title id `0x5443000D`); other releases need their own (see the last section). Found with
Ghidra (`H5Refs.java`, `H5Decomp.java` in `NGB_DATA\ghidra\scripts`) and checked live with `--watch` and
`--dump-ram`, loading the owner's saves in a copy of the data folder.

## Story progress

| Address | Type | What | Confidence |
| --- | --- | --- | --- |
| `0x00E130D4` | u8 | Current chapter, 0-based. The save name `L"%d. CHAPTER %d %s"` (`0x00406A20`) prints it + 1 (`FUN_001dab20`). | `VERIFIED`: loading "CHAPTER 10" sets 9, "CHAPTER 2" sets 1 |
| `0x00E130C6` | u16 | Stage within the chapter (0 at a chapter start). | `INFERRED` (`FUN_002ee750`, `FUN_002fe3c0`) |
| `0x005B4975` | u8 | Difficulty: 0 Ninja Dog, 1 Normal, 2 Hard, 3 Very Hard, 4 Master Ninja. 82 references; the code clamps it with `4 < x`. | `INFERRED`; Normal = 1 `VERIFIED` (owner's saves) |
| `0x005B497A` | u8 | Game mode: 1 story; 2 to 4 missions (the save name says "MISSION" for those). | `VERIFIED` for 1 |
| `0x005B4978` | u8 | Mission index (mission mode). | `INFERRED` (`FUN_001dba10`) |

- **Who writes the chapter:** `FUN_002ee750` when a chapter starts (`chapter = FUN_002ff0b0()`, stage = 0);
  `FUN_002fe3c0` when a save loads (`chapter = [0x005B4977]`, stage = `[0x005B4976]`); `FUN_001671e0` resets it to 0
  (new session); `FUN_002e3860`/`FUN_002e3a70` write `0x81` (a special value, not a chapter).
- **Save block:** `0x0059E160`, 0x17460 bytes, is what `FUN_001dac00` writes as `save000.dat` (95,400 bytes on disk:
  encrypted, entropy 8.0). `FUN_002fe090` copies chapter, stage and `0x00E130C2` into it (`0x005B4977`, `0x005B4976`,
  `0x005B497E`) at a save point; `FUN_002fe3c0` restores them. `VERIFIED` for the chapter copy.
- **Load order seen live:** the mode turns 1 first, the chapter stays 0 for a moment, then becomes the saved one with
  the save copy already equal to it. A chapter that ends naturally changes the chapter while the save copy still holds
  the old one: that is how the spike tells "chapter complete" from "a save loaded". The natural case is
  `UNVERIFIED` until a real chapter end is logged.

## Profile block (the decrypted `system.dat`)

- `0x00E12980`, 0x508 bytes, written to `TDATA/5443000d/system.dat` by `FUN_00311ca0(1, 0, 0x00E12980, 0x508, …)`.
  `VERIFIED` (decompilation). It holds the records and unlocks, so many completion achievements can read it directly:
  - `FUN_001db930` (**story completed**, mode 1): `[0x00E12E70 + difficulty] = [0x005B4780]` (a best value per
    difficulty), `[0x00E12A00] = [0x005B477C] + 1`, and 14 unlock checks (`FUN_001dc460` / `FUN_001dc220`).
  - `FUN_001dba10` (**mission cleared**, mode 3): best score `[0x00E12CD8 + mission*4]`, flags `[0x00E12BAC +
    mission] |= 2` (cleared), highest difficulty cleared `[0x00E12C74 + mission]`, and the number of cleared missions
    unlocks more (table `0x00448B6D`).
  All `INFERRED` from the decompilation; not yet watched live.

## Not found yet

- **Karma:** the load screen shows 5,685,887 for the "CHAPTER 10" save, but neither the u32, u64, f32 nor the text is
  in RAM. It may be computed from parts.
- **End-of-chapter results:** the English table reads "Time", "Kill", "Combos", "Karma", "Total Karma:" (strings near
  file offset `0x412B38`). Where the rank the owner mentions lives, and its values, need a RAM dump on that screen.

## Other releases

The game's own sections move between builds (only library code is handled by `port/src/libaddr.c`). The spike checks
that `0x00406A20` holds the save-name format and switches the detectors off otherwise. NTSC-U needs its own table,
found by the same string cross-reference or by a signature of `FUN_002ee750`.
