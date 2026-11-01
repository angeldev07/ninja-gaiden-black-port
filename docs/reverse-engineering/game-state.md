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
  All `INFERRED` from the decompilation; not yet watched live. The flag bit 1 is "unlocked"; a dump of a profile with
  the first ten missions open has exactly those set (`VERIFIED`).
- `[0x00E12C10 + mission]` is the difficulty a mission starts on (the menu opens a difficulty choice when it is not
  Normal; a clear on it raises it by one, up to 4).

## Mission mode

- **The menu** (`FUN_001769e0`, `FUN_001779a0`) shows nine pages of five missions and a tenth page with one,
  "Eternal Legend". The table of mission indices, page by page, is `0x00469668` (PAL; `0x004474B8` in NTSC-U and
  the modified build, same content, `VERIFIED`): 46 missions, indices from 3 to 0x44, and **"Eternal Legend" is
  mission 50 (0x32)**. The page names are in the XBE (`0x00454E20` and on: "Path of the Master Ninja" …
  "Giants of the Underworld", then "Eternal Legend"). Two routines compare the mission with 0x32 on their own
  (`FUN_00168350`, `FUN_001dc460`). `VERIFIED` (decompilation and the tables).
- **Unlocks:** the number of missions cleared opens more of that table in tiers (`0x00448B6C`: 0 cleared, 10 open; 5,
  15; … 35, 45; 40, all), so "Eternal Legend" opens after 40 clears.
- **Starting one** (`FUN_001774a0`): mode = 3, mission = the menu's choice, difficulty = `[0x00E12C10 + mission]`
  (1 to 4).
- **The end of a stage** is `0x00E12E94` (u8; the chapter − 0x240 in every release): below 0x80 while playing, **0x80
  game over** (`FUN_002e3f60` when the health is 0 and nothing revives Ryu; `FUN_002ff940` for a failed mission), **0x81
  cleared**. `FUN_002e16a0` then shows the continue screen (0x80) or the results (0x81 → `FUN_002e3810`).
  `VERIFIED` for 0x80: Ryu left idle in "Giants of the Underworld" on the modified build died, the port logged the
  game over and the screen showed GAME OVER with the continue choice.
- **The results of a mission** (`FUN_002e3860`) set the chapter variable to **0x81** (0xFE in some states), as the
  story's ending does (`FUN_002e3a70`), and return; `FUN_001dba10` then records the clear in the profile. A mission
  clear is the chapter turning 0x81 or 0xFE while the mode is 3; the mission and the difficulty are read at that moment.
  `INFERRED`.

## Inventory and Golden Scarabs

- **Inventory:** `0x005B43F0`, 128 slots of `{u16 item, u8 count, u8}` (to `0x005B45F0`); an empty slot is item
  0xFFFF. `FUN_001541f0` finds an item's slot, `FUN_00154220` returns its count, `FUN_00154260` adds or removes, capped
  by the item's data (`[0x00E12EE4]` table, byte +0x24). `VERIFIED`: the four saves read agree with the game's own
  menus (chapter 5: Plasma Saber, Nunchaku, Wooden Sword, Lunar; Shuriken, Windmill Shuriken, Smoke Bomb, Bow).
- **Item ids** are the ids of the item names in the game's text file `messtr.mes` (records `{0x000B0000 | id,
  English name, Japanese name}`): Golden Scarab 0x59, Dragon Sword 0x01, Lunar 0x12B / 0x133 / 0x134 (one id per
  level), Kitetsu 0xCC / 0x182 / 0x183, Plasma Saber 0xCE, its Mk. II 0xF7, Arrows 0x8E. Some weapons change item id
  as they level up, others may keep it; **what "every weapon at its top level" means is not settled yet**.
- **Equipped:** u16 melee weapon at `0x005B47DA`, projectile at `0x005B47DC` (0x133 Lunar, 0x30 Windmill Shuriken in
  the chapter 5 save). `INFERRED`.
- **Golden Scarabs handed to Muramasa:** `0x005B4794`, u32, the karma + 0x14. `FUN_00187400` adds the scarabs carried
  (item 0x59) and caps the total at 50, then empties their slot; `FUN_00187480` picks the reward from a table by that
  total. `VERIFIED` (decompilation; 0, 0, 24, 44 in the saves of chapters 1, 5, 10 and 16; the same code at
  `0x00186871` in NTSC-U reads `0x00589574`, the same distance).
- **Totals of the run** next to it (`FUN_00164e70` sums them as a check): `0x005B4614` grows 110, 856, 2,339, 4,843
  over the same saves (a kill count, `INFERRED`); `0x005B4790` is the essence (99,999 at most).

## Karma and the end-of-chapter rank

| Address (PAL) | Type | What | Confidence |
| --- | --- | --- | --- |
| `0x005B4780` | u32 | Karma of the whole run so far. | `VERIFIED`: 405,327 and 415,007 at the end of chapter 1 in two runs |
| `0x005B4784` | u32 | Karma at the last chapter end; `FUN_002ed790` updates it. | `VERIFIED` (copied when the rank shows) |
| `0x005B4788` | u32 | Play time in game frames, 60 a second (the save name formats it, `FUN_001dab20` → `FUN_0022cc20`). | `VERIFIED`: 1,688,005 in a save named "7:47" |
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

## The player

- **Player 1's object:** pointer at `0x00E1349C` (the chapter + 0x3C8; `0x00DB9E9C` in NTSC-U). `FUN_00023520`, its
  per-frame update, reads `+4` (a byte: the current move or state), `+0x60E` (u16, the combo's hit count; the best is
  kept in `0x005B4620`) and `+0xBBE` (set for the frame an enemy is defeated). `VERIFIED` (decompilation).
- **Hits:** `0x005B4614` (u32, the saved chapter − 0x363) goes up by one in that routine when `+0xBBE` is set, up to
  0xFFFF: every attack that lands, not every enemy defeated. `VERIFIED` live: each X or Y that connects adds one.
- **Kills:** `0x005B4610` (u32, the saved chapter − 0x367) is the chapter's kill count, the "Kill" the game shows.
  The same routine walks the actors and adds one when an actor's health (`0x005B5784 + actor * 0x210 + 2`) reaches 0
  (`FUN_002edc10` gives its karma). At the chapter end `FUN_002edf90` adds it to the run's total, `0x005B4628`, and
  sets it back to 0. `VERIFIED` (decompilation; the same increment in the three XBEs).
- **Health and ki:** `0x005B5784` u16 max health, `0x005B5786` u16 health, `0x005B578C` u16 max ki, `0x005B578E` u16
  ki (the saved chapter + 0xE0D … + 0xE17; same reference counts in PAL and NTSC-U). `FUN_002e3f60` checks the health
  for a game over and refills it from the max when an item revives Ryu. `VERIFIED` live: a giant's hits take 2400
  down to 0 in a mission, then the game over. `0x005B5744`/`0x005B5754` (u16[8], max and current) hold the health
  again in their first entry; what the others are is not known.
- **Equipment in use:** u16 item ids at `0x005B5790` (melee weapon), `0x005B5794` (ninpo), `0x005B5798` and
  `0x005B579C`. `FUN_002fe200` copies them into the save block (`0x005B47DA`, `47DE`, `47E2`, `47E6`) only when the game
  saves, so the copy goes stale between saves. `VERIFIED` live: the chapter 16 checkpoint reads 0x00F7 (Plasma Saber Mk.
  II) and 0x000F (The Art of the Inferno). The first trace read the save copy and showed an old weapon.
- **Ninpo ids:** The Art of the Fire Wheels 0x9A; of the Inferno 0x0F, 0x9B, 0x9C; of the Ice Storm 0x9E, 0x9F, 0xA0; of
  the Inazuma 0xAC, 0xAD, 0xAE (one id per level, `messtr.mes`). A cast spends ki: 200 a cast in the owner's session.
- **Move ids:** `port/src/ach/ach.c` logs them with `--ach-trace`. In a scripted fight the moves seen most were 0x0D,
  0x00, 0x18, 0x46, 0x11, 0x1A; which one is the Izuna Drop or the Flying Swallow is not known yet.

## Not yet read

- Weapon levels and the list of weapons that counts for the collection achievements (see Inventory).

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
| Mission | `0x005B4978` | `0x00589758` |
| Mission flags (profile) | `0x00E12BAC` | `0x00DB95AC` |
| End of stage | `0x00E12E94` | `0x00DB9894` |
| Golden Scarabs handed in | `0x005B4794` | `0x00589574` |

The last three come from fixed distances (saved chapter − 0x1F7 and − 0x1F3; chapter − 0x210), `VERIFIED` on the
owner's dumps of the modified build; the port checks the rank table it reaches (17 pairs with max > min) before using
it. Code: `port/src/ach/sig.c`, tested with synthetic code in `port/tests/ach_test.c`. The mission is the saved
chapter + 1, the play time − 0x1EF, the scarab total − 0x1E3 (`VERIFIED` by finding `FUN_001dba10` and `FUN_00187400`
in the three XBEs); the end of stage is the chapter − 0x240 and is only used when the code stores 0x80 into it at least
four times (six in each of the three XBEs).

`VERIFIED`: the search gives these on the three XBEs, and a run of the modified build loads a save and starts
chapter 1 with the chapter read as 0 and the difficulty as 1.
