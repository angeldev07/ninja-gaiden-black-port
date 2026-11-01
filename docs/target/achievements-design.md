---
tags: [ngb, target, achievements, design]
updated: 2026-10-31
status: draft
---

# Achievements: design spec

> PROPOSAL, approved by the owner on 2026-10-31 ("vamos con todas las que sugieres") from the mockups on the private
> design canvas "NGB Achievements". Background and decisions: [[achievements-brief]]. Game state: [[game-state]].
> Plan: [[achievements-plan]].

## Goal

27 port-side achievements, unlocked while playing without patching the game, announced by a small toast drawn by the
port's presenter, recorded in the data folder and listed on a new launcher page. Every release the port supports
(PAL, NTSC-U and modified builds of either) gets them: the game's variables are found in its own code at start.

## The list

Points are "honor" (誉), 1,680 in all. "Source" is what detects it: **RAM** (variables already found), **RAM-R**
(RAM, but the variable still has to be found), **Hook** (phase 2: a guest function hook or a per-frame state machine
over many variables).

| # | Id | English | Spanish | Unlocks when | Pts | Source | Phase |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `chapter-01` | The First Step | El primer paso | Chapter 1 completed, any difficulty | 10 | RAM | 1 |
| 2 | `story-ninja-dog` | Faithful Dog | Perro fiel | Story completed on Ninja Dog | 10 | RAM | 1 |
| 3 | `story-normal` | Way of the Ninja | Camino del ninja | Story completed on Normal | 25 | RAM | 1 |
| 4 | `story-hard` | Tempered Steel | Acero templado | Story completed on Hard | 50 | RAM | 1 |
| 5 | `story-very-hard` | No Quarter | Sin tregua | Story completed on Very Hard | 75 | RAM | 1 |
| 6 | `story-master-ninja` | Master Ninja | Maestro Ninja | Story completed on Master Ninja | 100 | RAM | 1 |
| 7 | `rank-master-ninja` | Rank: Master Ninja | Calificación: Master Ninja | A chapter ends with the Master Ninja rank | 15 | RAM | 1 |
| 8 | `rank-sixteen-seals` | Sixteen Seals | Dieciséis sellos | Master Ninja rank in all 16 chapters on one difficulty (progress n / 16) | 100 | RAM | 1 |
| 9 | `true-ultimate` | The True Ultimate Master Ninja | El verdadero Ultimate Master Ninja | Story on Master Ninja without healing items, ninpo, a hit taken, or life and ki upgrades. **Secret** | 250 | Hook | 2 |
| 10 | `mission-eternal-legend` | Eternal Legend | Leyenda eterna | The "Eternal Legend" mission cleared | 75 | RAM | 1b |
| 11 | `mission-eternal-legend-mn` | Legend Without End | Leyenda sin final | "Eternal Legend" cleared on Master Ninja | 150 | RAM | 1b |
| 12 | `mission-stadium-mn` | The Arena | La arena | The stadium challenge cleared on Master Ninja (which mission it is: ask the owner) | 100 | RAM | 1b |
| 13 | `mission-stadium-untouched` | Untouchable | Intocable | The stadium on Master Ninja without healing or a hit taken | 150 | Hook | 2 |
| 14 | `missions-all-mn` | Every Trial | Todas las pruebas | All 46 missions cleared on Master Ninja (progress n / 46) | 150 | RAM | 1b |
| 15 | `izuna-1000` | A Thousand Falls | Mil caídas | 1,000 enemies finished with the Izuna Drop (progress) | 50 | Hook | 2 |
| 16 | `dragon-sword-500` | Disciple of the Dragon | Discípulo del dragón | 500 enemies defeated with the Dragon Sword (progress) | 25 | Hook | 2 |
| 17 | `fire-ninpo-100` | Rain of Fire | Lluvia de fuego | 100 enemies defeated with fire ninpo (progress) | 25 | Hook | 2 |
| 18 | `flying-swallow-500` | Flight of the Swallow | Vuelo de la golondrina | The Flying Swallow used 500 times (progress) | 20 | Hook | 2 |
| 19 | `golden-scarabs` | Golden Scarabs | Escarabajos dorados | All 50 Golden Scarabs handed to Muramasa (progress) | 50 | RAM | 1b |
| 20 | `all-weapons` | Full Arsenal | Arsenal completo | Every weapon obtained (progress) | 30 | RAM-R | 1b |
| 21 | `weapons-max` | Master Forge | Forja maestra | Every weapon at its top level | 50 | RAM-R | 1b |
| 22 | `boss-no-damage` | Not a Scratch | Sin un rasguño | A boss defeated without taking damage | 30 | Hook | 2 |
| 23 | `dragon-sword-only` | Only the Sword | Solo la espada | Story completed using only the Dragon Sword. **Secret** | 100 | Hook | 2 |
| 24 | `deaths-100` | Always Back Up | Siempre en pie | 100 game overs (progress) | 5 | RAM | 1b |
| 25 | `port-first-session` | First Session | Primera sesión | The first game session in the native port | 5 | Port | 1 |
| 26 | `port-100-hours` | A Hundred Hours | Cien horas | 100 hours played in the port (progress) | 25 | Port | 1 |
| 27 | `port-screenshot` | Photographer | Fotógrafo | A screenshot saved with F12 | 5 | Port | 1 |

Rules agreed with the owner:
- **Rank:** only the Master Ninja rank (rank 0 of the game's formula) counts; the other ranks are not named. A chapter
  played from a mid-chapter save counts too.
- **Retroactive:** nothing is granted from old saves or from `system.dat`; achievements unlock when they happen.
- Each achievement unlocks once; the record keeps the date and the difficulty.

## Detectors (phase 1, RAM)

All run on the loader's main thread every 5 ms (`port/src/ach/`). Addresses are found at start ([[game-state]] §
Other releases); a detector whose variables are missing stays off and the log says which.

- **Chapter complete:** the chapter goes from *n* to *n* + 1 while the save copy still holds *n* (a load sets both).
  Unlocks #1 when *n* = 0.
- **Story complete:** the chapter goes from 15 (the last) to a value of 16 or more, the same way. `FUN_002ed790`
  takes the whole-game branch for a chapter ≥ 16, so the variable must hold one at the end. `INFERRED`: confirmed
  when the owner clears the last chapter. Unlocks #2 to #6 by the difficulty.
- **Rank:** the karma at the last chapter end (the save copy − 0x1F3) changes while the chapter is below 16: the
  chapter karma is the difference, and the rank follows from the game's own `{max, min}` table (the data pointer is
  the chapter − 0x210, checked: 17 pairs with max > min). Rank 0 unlocks #7 and adds the chapter to the difficulty's
  seal mask for #8. Ninja Dog maps through `{2, 3, 3, 4, 4}`, so it never yields rank 0.
- **Port:** #25 when the game reaches gameplay for the first time (the first chapter change); #26 adds the session's
  play time (the time the window is open and the game runs) to a counter; #27 when F12 writes a shot.

## Detectors (phase 1b, RAM)

Found without the owner's help, from the code and from dumps of the owner's saves ([[game-state]] § Mission mode,
§ Inventory and Golden Scarabs).

- **Mission cleared:** in mission mode (mode 3) the results screen sets the chapter variable to 0x81 (0xFE in some
  states). The mission (the saved chapter + 1) and the difficulty are read then. Mission 50 is "Eternal Legend" (#10,
  and #11 on Master Ninja). A clear on Master Ninja sets the mission's bit, by its place in the game's menu, in a
  46-bit mask kept as a hidden progress entry (`missions-mn-mask`); #14 shows the bit count and unlocks at 46.
- **Game over:** the end-of-stage state (the chapter − 0x240) turns 0x80 in story or mission mode; each one counts
  for #24. Revived by an item, Ryu does not reach it. `VERIFIED` live on the modified build.
- **Golden Scarabs:** the total handed to Muramasa (the saved chapter − 0x1E3; the game caps it at 50) rises in story
  mode while the run's play time moves on steadily (a load copies the whole save block and makes the time jump; the
  two seconds after a jump are ignored). #19 shows the total and unlocks at 50. Checked: loading a save with 24
  handed in counts nothing.
- **Still open:** #12 needs the owner to say which mission "the stadium" is (the detector then only compares the
  mission index); #20 and #21 need the weapon list and levels settled (the inventory and the item ids are known).

## Phase 2 (in progress)

- **Run tracker (#9 and #23), RAM only:** a story run is followed from New Game (the play time under 20 s, no karma,
  chapter 1). Each read while playing compares health, ki, their maxima, the healing and ki items carried and the
  melee weapon with the read before (`ach_run_marks` in `rules.c`) and keeps marks: a hit taken, an item used, ki
  spent (ninpo), an upgrade, a weapon other than the Dragon Sword (ids 0x01 and 0xC9). When the game writes a save
  (the kernel's `NtWriteFile` under `UDATA`), the play time, the karma and the marks become a checkpoint in
  `achievements.json` (`run-TTTTTTTT-KKKKKKKK`, the 40 newest kept). A load makes the play time jump: the run goes on
  only if the loaded save matches a checkpoint (same karma, play time up to 10 s later), so a save from before the
  port, or from another run, is never followed. A natural story end unlocks #23 with no weapon mark and #9 on Master
  Ninja with none of the other marks. `VERIFIED`: New Game starts following and the trip into chapter 1 adds no mark.
  `INFERRED`: checkpoints (no scripted save yet) and the end.
- **Disciple of the Dragon (#16):** each rise of the chapter's kill count while the melee weapon in use is the Dragon
  Sword (0x01) or the True Dragon Sword (0xC9). Loads and chapter ends are ignored.
- **Rain of Fire (#17), changed with the owner:** "cast fire ninpo 100 times" (The Art of the Fire Wheels or of the
  Inferno), not kills. A cast is the ki going down with its max unchanged; the kind is the ninpo in use at that moment.
- **Izuna Drop (#15) and Flying Swallow (#18): left out for now** (the owner's call: only deterministic detectors). In
  the owner's session no field of the player object took one value per Flying Swallow across 11 tries; finding them
  needs the game's move tables.
- **Boss without damage (#22)** and **the stadium untouched (#13):** the health is read; they need to know when a
  boss fight starts and ends, and which mission the stadium is.

## Toast

From the mockup's variants A and D (owner's verdict on the spike toast: too ugly and too big).

- **A, "seal"** (default): bottom centre of the game picture, height 8.75 % of it (42 px at 480 lines), 22 px from
  the bottom edge (scaled). A vermilion square with 誉 in cream; to its right a lacquer strip (`#13110f` at 94 %)
  with a 1 px vermilion top rule, a micro label "ACHIEVEMENT UNLOCKED · 15 誉" (Josefin SemiBold, tracked) and the
  name (Josefin Light). A soft shadow under it.
- **D, "rare"** (100 誉 or more, and secret ones): A with the seal holding an ensō instead of 誉 and a second
  vermilion rule 3 px outside, label "SECRET ACHIEVEMENT UNLOCKED" for secret ones. 7 s instead of 5.
- **Motion:** rises 12 px and fades in over 0.35 s (ease out), holds, fades out over the last 0.4 s. Several unlocks
  queue; each waits for the one before.
- **Sound:** a short, low drum hit synthesized by the port (no game audio), played with `PlaySound` from memory;
  silent with `--mute`.
- **Fonts:** Josefin Sans and the Shippori Mincho subset (with 誉 and 極 added) are embedded in `ngb-core.dll`, like
  in the launcher, and loaded with `AddFontMemResourceEx`.
- **Language:** the launcher passes `--lang en|es`; the toast uses it.
- **Off switch:** `--no-ach-toast` (the launcher's switch); achievements still unlock and record.
- Only on the GL renderer; captures (F12, `--debug-shots`) never include it.

## Storage: `%NGB_DATA%\achievements.json`

Written by the loader only (temp file and rename), read by the launcher. One record per line, so both sides parse it
with `sscanf` and no JSON library:

```json
{
  "version": 2,
  "unlocked": [
    {"id": "chapter-01", "time": "2026-10-31T20:38:34Z", "difficulty": 1}
  ],
  "progress": [
    {"id": "port-100-hours", "value": 111600},
    {"id": "rank-seals-1", "value": 1}
  ]
}
```

- `progress` holds counters (seconds played) and the per-difficulty chapter masks for #8 (`rank-seals-<difficulty>`,
  bit *n* = chapter *n* + 1).
- A version 1 file (the spike's) loads as is; its `demo` record is dropped.

## Catalog

One table, `port/src/ach/catalog.c`, compiled into both the core and the launcher: id, category, points, secret flag,
progress goal, icon key, and the name and description in English and Spanish. A unit test checks ids are unique, the
points add up and both languages are filled.

## Launcher page "Achievements"

From the mockup "Launcher · página Logros", in the launcher's own system ([[launcher-laca-design]]):

- A tab **ACHIEVEMENTS / LOGROS** between Controls and Advanced (page number 05).
- Left column: ghost number, title with 誉, one sentence, totals (unlocked n / 27, honor n / 1,680) with a 2 px
  vermilion progress rule, and filters: All, Story and rank, Missions, Counters and collection, Challenges and port.
- Right column: hairline rows (44 px tile with the achievement's kanji, name, description, progress rule or state,
  points), newest unlocks first, then in progress, then locked; the mouse wheel scrolls. Secret ones show as "Secret
  achievement" until unlocked, or in full with the "Show secrets" switch.
- Tile glyphs (Shippori subset): story 龍, rank 極, missions 試, counters 数, collection 蒐, challenges 挑, port 灯.
- A switch "Achievement notices in the game" on the same page (`[achievements] toast=1` in `ngb.ini`).
- The Play page's spec column shows "Achievements n / 27".

## Out of scope

- Phase 2 hooks (#9, #13, #15 to #18, #22, #23).
- Any online feature, sharing or sync.
- Achievements in the software renderer.
