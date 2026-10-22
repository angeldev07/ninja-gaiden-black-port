---
tags: [ngb, formats, cri, audio]
updated: 2026-10-20
status: current
---

# ADX: CRI audio

There are 412 ADX entries (480.1 MB): the 95 music tracks of `bgm.afs` and the 317 voices of `sse.afs`.
It is a public format, and ffmpeg and vgmstream decode it.

## Header (`VERIFIED` on `bgm.afs#0`, read by the leader)

| Offset | Value in `bgm.afs#0` | Field |
| --- | --- | --- |
| `0x00` | `80 00` | magic |
| `0x02` | `0x0020` (u16 big endian) | copyright offset. `VERIFIED`. The data starts at `offset + 4` = `0x24`: `INFERRED` |
| `0x04` | 3 | encoding (standard ADX). `INFERRED` |
| `0x05` | 18 | block size. `INFERRED` |
| `0x06` | 4 | bits per sample. `INFERRED` |
| `0x07` | 2 | channels. `VERIFIED` |
| `0x08` | 44,100 (u32 big endian) | sample rate. `VERIFIED` |
| `0x0C` | u32 big endian | total samples. `INFERRED` |
| `0x1E` | `(c)CRI` | copyright. `VERIFIED` |

## Decoding (H4, with `ngb_tools.audio`: validated visually and statistically, not compared bit by bit)

- All 412 ADX in the game are **v3, encoding 3, 500 Hz highpass cutoff and no encryption** (byte `0x13` is 0).
- The predictor coefficients come from the public formula: with 500 Hz at 44,100 Hz, `(7334, −3283)` in
  ×4096 fixed point.
- Each 18 B block has a u16be scale and 32 signed nibbles. In v3 the scale is used as scale+1 and each
  product is shifted separately, as in vgmstream.
- Full batch:
  - **bgm 95/95** (about 150 min of music). The manifest marks 65 tracks as looping; the real loop offset
    in v3 still has to be checked (follow-up issue);
  - **sse 317/317** voices.
- Validation was visual and statistical (waveform, spectrogram and RMS). It was not compared bit by bit
  with another decoder because neither ffmpeg nor vgmstream is installed.

## Notes

- `bgm.afs#66` (`bgm_rtm_dummy.adx`) is one of only 2 high-entropy entries in the game.
- The engine links ADXT 9.50 and AIXP 2.32 ([[xbe-default]]). `movie\ninja.aix` is AIX (`AIXF`), multitrack ADX.
