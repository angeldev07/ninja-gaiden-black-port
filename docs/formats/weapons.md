---
tags: [ngb, formats, teamninja, weapons]
updated: 2026-10-21
status: current
---

# Weapons: models and attachment (`.itm`)

Each weapon is in `cmn.afs` as a `<weapon>.kmd` ([[kmd]]) + `<weapon>.itm` (`itm_dat2`,
[[chunk-teamninja]]) pair. Tool:
`python -I -m ngb_tools.kmd export <ryu.kmd> --attach <weapon.kmd>@<weapon.itm>#<state>` ([[tools]]).

## Inventory (`VERIFIED`, by entry name in `cmn.afs`)

| Weapon | `.kmd` | `.itm` |
| --- | --- | --- |
| Dragon Sword | `katana` #70 (upgrades `kata2` #72 and `kata3` #73) | #192 |
| Nunchaku | `nchak` #76 | #193 |
| Flail | `flail` #128 (`fl2` #129) | #208 |
| Lunar | `rod` #291 (`rod2`, `rod3`) | #290 |
| War Hammer | `hammer` #85 (`ham2`) | #196 |
| Dabilahro | `zanba` #89 (`zan2`, `zan3`) | #197 |
| Kitetsu | `yoto` #109 | #249 |
| Unlabored Flawlessness | `boku` #104 | #221 |
| Sheath | `sheath` #87 | #245 |
| ? | `skata` #116 | — |

The mapping from name to in-game weapon is `INFERRED`. In `katana.kmd`, obj0 is the sword (1,724 vertices),
with the blade along +z and the hilt at the origin; obj2-17 are group 6 effects.

## Attachment table: slot 1 of the `.itm` (`VERIFIED` structurally; the semantics of the states are `INFERRED`)

These are `0x20`-byte records that end in `0xffffff00`:

| Offset | Field |
| --- | --- |
| `+0x00` | `u32` key: the low 16 bits are the **state** and the high 16 the **variant** |
| `+0x04` | `u32` **bone**, as an object index in the character's KMD |
| `+0x08` | `f32×3` position |
| `+0x14` | `f32×3` rotation in degrees (Euler Rz·Ry·Rx) |

- Bones that appear: 11 (right hand), 5 (left hand), 0 (chest) and 2 (hip).
- **State 1 = stowed** (on the chest or hip). **States 2 and 3 = in hand** (bone 11).
- Katana, state 2: position (−0.058, 0, −0.025) and Ry(−13°). In the render, the hilt sits in the palm (`INFERRED`, visual inspection of `sword_preview.png` in `NGB_DATA\extracted\models\ryu\`).
- Katana, state 1: back, with rotation (84.2°, 180.3°, 31.0°). The render places it with the grip over the right
  shoulder, Ryu's iconic pose. It is **consistent with the Euler order Rz·Ry·Rx** with three axes (visual inspection, `INFERRED`).
- Weapons of the same type share a table: swords (katana, yoto, boku, skata), heavy ones (hammer, zanba) and
  nunchaku/flail.

## Result (`VERIFIED` with `sword_preview.png`, reviewed by the leader)

`NGB_DATA\extracted\models\ryu\ryu_sword.glb`: Ryu with the Dragon Sword in hand. Clips: guard and spinning slash
(`rm_kt`), idle and run (`ryu.mtn`). The sword follows the hand in all of them.

## Pending

- Switch between stowed and in hand according to the motion: today a single state is chosen per export.
- Variant `0x3b` (bone 16), states 4-9 and slot 0 of the `.itm` (impact volumes).
- Renders of the other weapons.
