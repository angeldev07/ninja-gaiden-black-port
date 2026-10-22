---
tags: [ngb, formats, teamninja]
updated: 2026-10-20
status: draft
---

# Team Ninja chunk: common header of the Kunai engine formats

The engine's own formats (KMD, `itm_dat2`, `chr_dat`, `stry_dat`, `rtm_dat`, `chr_dat2`, `MESSTR`,
`JPFONT`, `STG_ETC`, and the subchunks `OBJINFO`, `HIERARC`, `MORPH`…) share this header.

## Header (`INFERRED`; regularity checked on 2,198 of 2,198 tagged entries)

| Offset | Type | Field |
| --- | --- | --- |
| `0x00` | `char[12]` | NUL-padded ASCII tag (`KMD`, `chr_dat`…) |
| `0x0C` | `u32` | chunk size. Matches the entry size except in KMD. **It does not bound the slots**: in `r_ryu.kmd` it is `0x1385e0` and slot 7 is at `0x138840` (`VERIFIED`, `ngb_tools.teamninja`). The last slot extends to the end of the data |
| `0x10` | `u32` | `n_slots` |
| `0x14` | `u32` | `n_used`: how many offsets are non-zero |
| `0x18` | `u32` | `table_pos` (`0x20`; in KMD `0x80` and in MESSTR `0x40`) |
| `0x1C` | `u32` | `0x20` (header size) |
| `0x20 … table_pos` | — | type-specific header |
| `table_pos` | `u32[n_slots]` | subchunk offsets, relative to the chunk. 0 = empty; the used ones are in ascending order |

`VERIFIED` on `ryu.kmd` (read by the leader): size `1018672`, 10 slots, 7 used, table at `0x80`, offsets
`0xb0` (OBJINFO), `0x3000` (XPR0), `0xdf000` (HIERARC), `0xdf4f0` (MORPH), `0xf7120` (ACSSRI), `0xf8490`
(MISCDATA) and `0xf8a40` (OBJHIT).

## Exceptions

- `STG_DAT` has a different layout: tag, `0x10`, the path `d:\nocare.stg` and `Stak` padding.

## Per-type hypotheses (`INFERRED`)

- `chr_dat` (`.mtn`): 3 slots. A table of offsets per motion with keyframes, a second table and a small tail.
- `itm_dat2` (`.sob`, `.itm`): 4 slots, 1 used. Placement records that end in `0x0FFFFFFF`.
- `stry_dat` (`.sty`): slot 0 is a `u32` stream. The tokens `0x80`/`0x81`/`0x86xxxxxx` are opcodes (id in the
  low `u16`) and the rest are int or float arguments. It fits the 479 `STRY_*` strings of [[xbe-default]].

KMD details: [[kmd]].
