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
  the old one: that is how the spike tells "chapter complete" from "a save loaded". `VERIFIED` on
  2026-10-31: the owner finished chapter 1 on the modified build and the log reads `chapter 0 -> 1 (save copy 0,
  difficulty 1)`, then `unlocked chapter-01`.

## Profile block (the decrypted `system.dat`)

- `0x00E12980`, 0x508 bytes, written to `TDATA/5443000d/system.dat` by `FUN_00311ca0(1, 0, 0x00E12980, 0x508, …)`.
  `VERIFIED` (decompilation). It holds the records and unlocks, so many completion achievements can read it directly:
  - `FUN_001db930` (**story completed**, mode 1): `[0x00E12E70 + difficulty] = [0x005B4780]` (a best value per
    difficulty), `[0x00E12A00] = [0x005B477C] + 1`, and 14 unlock checks (`FUN_001dc460` / `FUN_001dc220`).
  - `FUN_001dba10` (**mission cleared**, mode 3): best score `[0x00E12CD8 + mission*4]`, flags `[0x00E12BAC +
    mission] |= 2` (cleared), highest difficulty cleared `[0x00E12C74 + mission]`, and the number of cleared missions
    unlocks more (table `0x00448B6D`).
  All `INFERRED` from the decompilation; not yet watched live.

## Karma and the end-of-chapter rank

| Address (PAL) | Type | What | Confidence |
| --- | --- | --- | --- |
| `0x005B4780` | u32 | Karma of the whole run so far. | `VERIFIED`: 405,327 and 415,007 at the end of chapter 1 in two runs |
| `0x005B4784` | u32 | Karma at the last chapter end; `FUN_002ed790` updates it. | `VERIFIED` (copied when the rank shows) |
| `0x005B4788` | u32 | Play time (the save name formats it, `FUN_001dab20` → `FUN_0022cc20`). | `INFERRED` |
| `0x00E12EC4` | ptr | A game data file in RAM that holds the rank thresholds. | `VERIFIED` |

All of them are 0 after `FUN_002e3580` (new game). The first three are in the save block, so their offsets from the
saved chapter hold in every release (karma = saved chapter − 0x1F7); the data pointer is the chapter − 0x210 in PAL
and in the modified NTSC-U build.

**The rank** (the "MASTER NINJA" card after the chapter results; a picture, not text) is not stored: `FUN_002ed790`
computes and returns it, `VERIFIED` against the owner's screen:

- chapter karma = `[0x005B4780] − [0x005B4784]`, then `[0x005B4784] = [0x005B4780]`;
- `{max, min}` = the u32 pair at `table + chapter*8`, where `table = base + [[base + 4] + 0x18 + base]` and
  `base = [0x00E12EC4]`; chapter ≥ 16 uses the pair at `+0x80` (the whole game: the final rank, and the total is
  kept in `0x005B4798`);
- `d = (max − min) / 3`; rank 4 below `min`, 3 below `min + d`, 2 below `min + 2d`, 1 below `max`, **0 (Master Ninja)
  from `max` up**;
- on Ninja Dog difficulty the result maps through `{2, 3, 3, 4, 4}`: rank 0 or 1 is out of reach.

Thresholds read from the owner's dumps (same in both runs):

| Chapter | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | Game |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Master Ninja from | 260,000 | 280,000 | 360,000 | 366,667 | 380,000 | 463,333 | 423,333 | 410,000 | 643,333 | 860,000 | 330,000 | 923,333 | 480,000 | 426,667 | 973,333 | 393,333 | 7,973,333 |
| Rank 4 below | 120,000 | 110,000 | 170,000 | 170,000 | 180,000 | 250,000 | 240,000 | 200,000 | 450,000 | 650,000 | 150,000 | 700,000 | 320,000 | 220,000 | 820,000 | 200,000 | 4,950,000 |

The names of ranks 1 to 4 are pictures too and not read yet (the owner remembers "Ninja Dog" as the lowest).

**Detector:** poll `[0x005B4784]`; when it changes during story mode with the chapter below 16, the chapter karma is
the difference and the rank follows from the table: RAM only, no hook.

## Not yet read

- The kill count, essence and ninpo of the results screen ("Clear Time", "Kill", "Essence", "Ninpo", "Karma"; drawn
  as pictures).

## Other releases

The game's own sections move between builds (only library code is handled by `port/src/libaddr.c`), so the port
finds these variables at start in the game's code (`port/src/ach/ach.c`, `find_vars`):

- the save-name routine (PAL `FUN_001dab20`) is `mov al, [mode]` … `cmp al, 2` … `movzx eax, byte [chapter]` …
  `push L"%d. CHAPTER %d %s"`: the format string is found in the data, then the one `push` of its address;
- the load routine (PAL `FUN_002fe3c0`) is `mov cl, [saved chapter]` … `mov [chapter], cl`;
- the save block has the same layout in every release (the saves of one load in another, `VERIFIED`), so the
  difficulty is the saved chapter − 2 and the mode the saved chapter + 3; the second fact cross-checks the first.

| Variable | PAL | NTSC-U and its modified build |
| --- | --- | --- |
| Chapter | `0x00E130D4` | `0x00DB9AD4` |
| Difficulty | `0x005B4975` | `0x00589755` |
| Mode | `0x005B497A` | `0x0058975A` |
| Saved chapter | `0x005B4977` | `0x00589757` |
| Karma of the run | `0x005B4780` | `0x00589560` |
| Karma at the last chapter end | `0x005B4784` | `0x00589564` |
| Rank data pointer | `0x00E12EC4` | `0x00DB98C4` |

The last three come from fixed distances (saved chapter − 0x1F7 and − 0x1F3; chapter − 0x210), `VERIFIED` on the
owner's dumps of the modified build; the port checks the rank table it reaches (17 pairs with max > min) before using
it. Code: `port/src/ach/sig.c`, tested with synthetic code in `port/tests/ach_test.c`.

`VERIFIED`: the search gives these on the three XBEs, and a run of the modified build loads a save and starts
chapter 1 with the chapter read as 0 and the difficulty as 1.
