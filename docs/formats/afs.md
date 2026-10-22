---
tags: [ngb, formats, afs, cri]
updated: 2026-10-20
status: current
---

# AFS: CRI Middleware container

Almost all NGB assets are in 206 `.afs` files ([[ngb-disc]]). The format is public.
There are open reference implementations: [AFSLib](https://github.com/MaikelChan/AFSLib) and
[Sewer56/AfsLib](https://github.com/Sewer56/AfsLib). Tool: `python -m ngb_tools.afs`
([[tools]]).

## Structure (`VERIFIED`: parsing of the 206 files with `afs inventory` and manual hexdump of 4)

| Offset | Type | Field |
| --- | --- | --- |
| `0x00` | `char[4]` | magic `AFS\0` (the `AFS ` variant of AFSLib never appears in NGB) |
| `0x04` | `u32` | N, number of entries |
| `0x08` | `N × (u32 offset, u32 size)` | table of contents (TOC) |
| `0x08 + 8N` | `u32 offset, u32 size` | pointer to the attribute table (names and dates) |
| `align(8 + 8N + 8, 0x800)` | — | first data. Data is contiguous and aligned to `0x800` |
| `align(end of last data)` | `N × 0x30` | attribute table |

Attribute entry (`0x30` bytes): `name[32]` (ASCII padded with NUL), six `u16` date fields
(year, month, day, hour, minute, second) and a trailing `u32`.

## NGB specifics

- **All 206 files have a name table**, so all 3,766 entries have a name (maximum 23 characters).
- **The trailing `u32` of each attribute is not the size.** In all 3,766 entries it equals the `u32` at
  offset `4 + 4·i` of the file: it is the header read as an array of `u32` (`custom[0] = N`,
  `custom[1] = TOC[0].offset`…). It only matches the size by coincidence in 177 cases. **It must not be used
  as a size**, and to repack, this quirk has to be reproduced.
- **Invariants the tool validates** (`ngb_tools.afs.invariants`), all valid in 206/206
  (`VERIFIED`): magic, offsets aligned to `0x800`, entries inside the file, no overlaps, attribute table
  size equal to `N × 0x30` and plausible dates.
- **Properties observed with an analyst's exploratory script**, which is not versioned and therefore the
  tool does not validate them yet: the TOC order matches the offset order, the data is
  contiguous, the attribute table starts at `align(end of last data)` and the file ends aligned.
  They were seen in 206/206 files. `INFERRED` until they move into `invariants`.
- **There are no nested AFS**: 0 entries with `AFS\0` (`VERIFIED`, `summary.nested_afs` of `afs inventory`).
- The dates range from 2002 to 2005: 2002 (1), 2003 (771), 2004 (1,416) and 2005 (1,578).

## What they contain: main entry magics (`VERIFIED`, `summary.magic_histogram` of `afs inventory`)

| Magic or tag | Entries | MiB | Example | Format |
| --- | --- | --- | --- | --- |
| `KMD` (chunk) | 724 | 751.8 | `boff_01.afs#0 boff_01.kmd` | model or scene → [[kmd]] |
| `XPR0` | 689 | 163.7 | `cmn.afs#253 menu.spr` | Xbox Packed Resource (sprites and UI) → [[xpr0]] |
| `itm_dat2` (chunk) | 600 | 1.4 | `boff_05.afs#1 bf_hpole.sob` | object and item placement (`.sob`, `.itm`) → [[chunk-teamninja]] |
| `ADX` | 412 | 480.1 | `bgm.afs#0 0611_bgm.adx` | CRI ADX audio → [[adx]] |
| `chr_dat` (chunk) | 328 | 116.7 | `boff_cmn.afs#3 eryun.mtn` | animation bank (`.mtn`) |
| `stry_dat` (chunk) | 326 | 35.5 | `boff_cmn.afs#0 s_boff01.sty` | bytecode script (`.sty`) |
| `.wva` (`u32 N` in the header) | 326 | 43.3 | `boff_cmn.afs#41 bf_wfall.wva` | sound-effects bank in Xbox ADPCM → [[wva]] |
| `rtm_dat` (chunk) | 140 | 1.5 | `boff_05.afs#4 chijo_00.rtm` | real-time cutscene sequence |
| `chr_dat2` (chunk) | 72 | 1.5 | `chr_army.afs#13 army.chr` | character parameters |
| rest (`MESSTR`, `JPFONT`, `STG_DAT`, 3 empty and loose binary headers; 152 distinct types in total in the histogram) | 149 | — | — | to be classified |
| **total** | **3,766** | | | |

In total there are 2,201 entries with a chunk tag (the `tag:*` types of the histogram: KMD, `itm_dat2`, `chr_dat`,
`stry_dat`, `rtm_dat`, `chr_dat2`, `MESSTR` 4, `JPFONT` 3, `STG_DAT` 3 and `STG_ETC` 1). XPR0 and ADX do not count: they are magics, not
chunk tags. The "(chunk)" ones share the generic header of
[[chunk-teamninja]] (`INFERRED`): the regularity was checked with an exploratory script on 2,198 of them. The remaining 3
are `STG_DAT`, which has a different layout.

## Entropy (`VERIFIED`, `summary.entropy_bands`; window of the first 64 KiB of each entry)

- Low (< 5): 1,956 entries (694.6 MiB). Medium (5 to 7.5): 1,805 (909.2 MiB). High (> 7.5): 2 (0.63 MiB). Empty: 3. Total: 3,766.
- **No type falls mostly in the high band, and no zlib or gzip signatures appear.** Within the measured
  window there is **no general-purpose compression or encryption** hiding the formats (`INFERRED` for
  the rest of each entry). Note: the codecs specific to each medium (ADPCM in audio, DXT in textures) are indeed
  compression, but their format is known. This contradicts the conclusion of [[decomp-ninja]].
- Averages by type: ADX 7.1 (ADPCM), `.wva` 6.9 (Xbox ADPCM), KMD 5.5 and XPR0 5.3 (DXT and P8 textures).
  Tables and scripts stay below 4.

## Notable containers

- **`chr_ryu.afs`:** 2 entries. `ryu.kmd` (1,018,944 bytes; 59 objects, XPR0 with **19 textures**: 14 DXT1 and
  5 DXT4/5) and `r_ryu.kmd` (63 objects; `INFERRED`: cutscene variant). It is the target of H2 and H3
  ([[roadmap]]).
- **`debug.afs`:** 39 entries.
  - 34 `.sty` scripts of development tools and tests (`s_m_edit`, `s_r_view`, `s_chrput`, `s_test`, and
    24 boss viewers `s_v<boss>`).
  - 2 test stages (`stage00`, `stage02`), 1 `.cam` and 2 test `.rtm`.
  - It contains no sources or symbols.
- **`bgm.afs`:** 95 ADX tracks. **`sse.afs`:** 317 ADX voices (`001_Murai.adx`…).
