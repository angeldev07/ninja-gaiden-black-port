---
tags: [ngb, formats, teamninja, audio]
updated: 2026-10-20
status: current
---

# WVA: sound-effects bank

There are 326 `.wva` files (43.3 MB), one or several per stage (`bf_wfall.wva`, `bf_head.wva`…). They carry no
ASCII magic: the header starts with `u32 N`, the number of sounds. That is why the inventory groups them as
`hex:NN000000`.

## Structure (`INFERRED`, with strong evidence: with this layout the 2,966 records of the 326 `.wva` decode, and the audio looks correct in the spectrogram; the meaning of the parameter floats is unknown)

```
u32 N
N × u32 pointer           // 0 = empty slot (370 in the game)
                          // bit31 = 1 → 24 B parameter block; bit31 = 0 → 8 B
record:
  u32 (flags << 24 | data_size)     // flags seen: 0x00, 0x01, 0x04
  WAVEFORMATEX (20 B)               // wFormatTag 0x0069 = Xbox ADPCM
  parameter block (24 or 8 B, floats)
  Xbox ADPCM data
```

- This corrects the earlier hypothesis: bit31 is **not** a validity flag, but marks the size of the parameter
  block.
- Xbox ADPCM: 64 samples per block and channel (the header one plus 63 nibbles; the last is discarded), unlike
  the 65 of MS-IMA. There are 63 stereo records, and the sample rates range from 11,000 to 44,100 Hz.
- 93 records are pure silence (mostly padding slots).
- `Media\Wave.xwb` (WBND v3) contains 7 sounds in **PCM16**, not in Xbox ADPCM.

## Next

Map which game event triggers each sound (`.sty` and `chr_dat` tables).
