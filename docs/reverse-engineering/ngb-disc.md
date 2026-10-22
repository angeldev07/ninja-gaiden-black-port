---
tags: [ngb, reverse-engineering, disc]
updated: 2026-10-20
status: current
---

# The NGB disc (PAL): structure and contents

## Image

- **Redump** `Ninja Gaiden Black (Europe) (En,Fr,De,Es,It).iso`: 7,825,162,240 bytes. It is an
  **XGD2** disc with the video partition first. The `MICROSOFT*XBOX*MEDIA` magic is at `0x18310000`, so
  the game partition starts at `0x18300000`. `VERIFIED` by reading the bytes.
- **XISO** generated with `extract-xiso -r -d`: 6,341,263,360 bytes, with the magic at `0x10000`. `VERIFIED`.
  See [[tools]].
- Extracted tree in `NGB_DATA\game\`: **249 files, 6,340,649,100 bytes**. `VERIFIED` with `extract-xiso -x`.

## By type (`VERIFIED`, counted with PowerShell over `game/`)

| Extension | Files | Bytes | What it is |
| --- | --- | --- | --- |
| `.sfd` | 26 | 4,530,176,000 | CRI Sofdec video (MPEG-1/2 plus ADX audio). ffmpeg decodes it. `INFERRED` |
| `.afs` | 206 | 1,686,644,736 | CRI AFS container (`AFS\0`). See [[afs]] |
| `.xbe` | 5 | 72,003,584 | Executables. See [[xbe-default]] |
| `.ttf` | 2 | 46,549,144 | `Media\ArialUni.Ttf` and `Media\font.ttf`, **identical** (same sha256 `58E770F0…`) |
| `.aix` | 1 | 3,102,720 | `movie\ninja.aix`: CRI AIX audio (multitrack ADX). `INFERRED` |
| `.xpr` | 3 | 1,929,216 | Xbox Packed Resource (textures and XDK resources): `font.xpr`, `dlcustom.xpr`, `Fonts\kfont.xpr` |
| `.xwb`/`.xsb` | 1 + 1 | 190,012 + 684 | XACT wave bank and sound bank (`Media\Wave.xwb`, `Sound.xsb`) |
| `.bin` | 2 | 52,984 | `dsstdfx.bin` (15,308; by name, DirectSound standard-effects DSP image, `INFERRED`) and `load_img.bin` (37,676) |
| `.def` | 1 | 16 | `NinjaLaunchXBEName.def` = `D:\default.xbe\r\n`. `VERIFIED` |
| `.ins` | 1 | 4 | `verchk.ins` = `10 BF 17 43`. `VERIFIED`; meaning unknown |

## Executables

| File | Bytes | Note |
| --- | --- | --- |
| `default.xbe` | 4,743,168 | Main game. sha256 `B61F6EB5…D618` |
| `NinjaTrilogy.xbe` | 3,248,128 | `INFERRED`: the NES Ninja Gaiden games, unlockable |
| `NinjaTomoe.xbe` | 3,248,128 | Same size as Trilogy but a **different hash** (`723083BC…` versus `2957569A…`) |
| `update.xbe` | 2,342,912 | `INFERRED`: updater |
| `dashupdate.xbe` | 58,421,248 | `INFERRED`: dashboard update (Xbox Live) |

## The AFS files (206), by group

Grouped by file name (`VERIFIED`, listing of `game/`). The entry counts
come from the `u32` at `0x04` of each AFS (`VERIFIED`, hexdump of `cmn`, `debug`, `chr_ryu` and
`bgm`, and `afs inventory` over the 206). The meaning of the groups is `INFERRED`.

- **Globals:** `bgm.afs` (445 MB, 95 entries: music), `spr.afs` (158 MB), `sse.afs` (58 MB), `cmn.afs`
  (50 MB, 353 entries), `demo.afs`, `debug.afs` (39 entries), `live00.afs`.
- **Characters (`chr_*`, 62):** for example `chr_ryu`, the variants `chr_ryua…chr_ryug` (`INFERRED`: outfits),
  `chr_ayane`, `chr_rei`, `chr_doku`, `chr_murai`, `chr_kotei`, `chr_dino`, `chr_dragn` and `chr_tank`.
  `chr_ryu.afs` has 2 entries.
- **Stages (17 prefixes, `<zone>_NN` and `<zone>_cmn`):** `boff, cave, chrch, hell, hori, makai, pyrmd,
  rail, ssato, stdim, suiro, tln_a, tln_b, tomb, tower, yasik, zppln`. `INFERRED`: `ssato` = Hayabusa village
  (sato), `tln` = Tairon, `zppln` = zeppelin, `yasik` = mansion (yashiki), `chrch` = cathedral, `tomb` =
  tomb/monastery, `suiro` = canal (suiro).
- **`rtm_*` (12):** one per zone (`rtm_cave`, `rtm_tlna`, `rtm_tlnb`, …). Note: Tairon is spelled `tln_a`/`tln_b`
  in the stages and `tlna`/`tlnb` in the `rtm_` files (`VERIFIED`, file names). `INFERRED`: real-time
  cutscene resources of each zone.

## CRI video and audio (`movie/`: 26 `.sfd` and 1 `.aix`, 27 files)

`pre_movie1…14`, several with their `_j` (Japanese) variant. Also `cre.sfd` (`INFERRED`: credits), `epi.sfd`,
`ninja_gaiden.sfd`, `d_aruma.sfd`, `2_VI_WIDE_B.sfd` and `4_VI_STD_B.sfd` (`INFERRED`: 16:9 and 4:3 variants),
and `doa4.sfd` (716 MB; `INFERRED`: Dead or Alive 4 trailer).

## Next

- AFS contents: [[afs]]. Executable: [[xbe-default]].
