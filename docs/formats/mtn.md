---
tags: [ngb, formats, teamninja, animation]
updated: 2026-10-21
status: current
---

# MTN / MOT: animations of the Kunai engine

The characters' animation banks. Ryu's is `cmn.afs#22 ryu.mtn`, and those of his weapons are
`cmn.afs#6-21` (`rm_*`, `rs_*`, `rn_*.mot`). Tool: `python -I -m ngb_tools.kmd export <kmd> --motions <bank>`
([[tools]]). Source: `ngb_tools.kmd.motion` and the animation spike (renders checked by eye).

## Container (`VERIFIED`)

- **`.mtn`** is a `chr_dat` chunk ([[chunk-teamninja]]) with 3 slots:
  - slot 0: the bank;
  - slot 1: another offset table, not interpreted;
  - slot 2: `08 0 100 0`, not interpreted.
- **`.mot`** is the body of slot 0 on its own, with no chunk header.
- **Id table:** one `u32` per motion id, until a 0 is found, with offsets relative to the body. Many ids
  share a 4-frame filler motion. `ryu.mtn` has 1,890 ids and **258 distinct motions**; `rm_kt.mot`
  has 167. `INFERRED`: the ids form a global space that the weapon banks complete.
- **Motion:** a `u32` header terminated by 0, with one offset per track. The data of each track goes
  **just before** the header. **There are always 44 tracks**, all of the same duration (0 discrepancies in 425
  motions).

## Keys (`VERIFIED` by the sum of durations; interpolation is `INFERRED`)

- A control `u16`:
  - bits 0-1: type (1 = value, 2 = value plus 1 tangent, 3 = value plus 2 tangents, 0 = end);
  - bits 2-11: frames until the next key;
  - bits 12-15: high bits of a **signed 20-bit value**.
- Then come the low `u16` of the value and the `s16` tangents.
- 97 % are type 1 keys at 1 frame: the animation comes already sampled.
- Linear interpolation for type 1 and Hermite for types 2 and 3. The tangent is the slope per frame ×16.
  60 fps is assumed.
- **Units:** 1e-4 m and 1e-4 rad. There are exact jumps of 62,831 = 2π·1e4, and in the static pose the hip sits at
  0.99 m.

## Tracks (44 = 3 + 9 + 4×8, `VERIFIED` visually)

| Tracks | What |
| --- | --- |
| 0-2 | root position (hip, obj2) in model space; the floor is at y = 0 |
| 3-11 | Euler angles of hip, chest and head (local; R = Rz·Ry·Rx, `INFERRED`) |
| 12-43 | 4 limbs × 8: **IK target** (xyz), Euler of hand or foot and two parameters G and H (H = knee or elbow pole; G undeciphered) |

The limbs use **2-bone IK**: the engine stores where the hand or foot goes, not the angle of each bone.
`ngb_tools.kmd.rig` deduces the humanoid skeleton from HIERARC ([[kmd]]) and solves the trunk FK and the limb IK
for each frame.

## Identified motions (`VERIFIED` with `anim_preview.png`)

| Bank | Id | What it is |
| --- | --- | --- |
| `ryu.mtn` | 1 | idle (guard) |
| `ryu.mtn` | 3 | walk |
| `ryu.mtn` | 90 | run |
| `ryu.mtn` | 36 | forward jump (lands 1.28 m lower) |
| `rm_kt.mot` | 141 | guard with katana |
| `rm_kt.mot` | 151 | spinning slash |

## Pending

- The G parameter of the limbs and slots 1 and 2 of the `.mtn`.
- Confirm against the game the Euler order, the 60 fps and the ×16 tangent.
- The rig only covers 44-track humanoids: try other characters.
- The weapon is another model and is not yet mounted in the hand.
