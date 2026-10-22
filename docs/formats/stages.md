---
tags: [ngb, formats, teamninja, stage]
updated: 2026-10-21
status: current
---

# Stages: zone KMD and HITMDL collision

Each of the game's 17 zones ([[ngb-disc]]) is split into `<zone>_NN.afs` and `<zone>_cmn.afs`. The geometry
is in the KMD of each part (`boff_01.kmd`…). Tool: `python -I -m ngb_tools.stage export <zone>`
([[tools]]). Source: `ngb_tools.stage`, plus a spike that read the 418 zone and `cmn` KMD
(6.78 M triangles, push buffers 1886/1886).

## How a stage KMD differs from a character one (`VERIFIED` on 111 KMD)

| Aspect | Character ([[kmd]]) | Stage |
| --- | --- | --- |
| Mesh descriptor | `u32` = 1 | **`u32 n_parts`** (1-4) plus 8 `u32` per part; the header takes `8 + 0x20·n`. The `+0x04` field of each submesh indicates its part |
| VB per object | 1 | one **per part**: `USERDATA0[2]` counts VBs, not objects |
| Transform | hierarchy with T per bone | **no HIERARC**. T = R = 0 and S = 0: the vertices already come **in world coordinates** |
| Groups | 1-5 | 0 and 6 (group 6: billboards at the origin) |
| FVF | `0x112` / `0x116` / `0x042` | `0x142`, `0x152`, `0x012`… (with vertex color; no TEX2) |
| `0x60` header | bbox | sentinel ±99999 |
| Push buffer | clean | 64 KMD carry garbage in the padding `u32` after `END`; it is tolerated |

**Doku's body** uses the same multipart variant, which is why `ngb_tools.kmd` did not export it. `kmd` had to be
adapted (follow-up issue).

## HITMDL collision (`VERIFIED` on 323 KMD)

One slot per object, with the tag `VHD` and blocks of triangles (4 × `u16`), planes and `vec4` vertices in
world coordinates, with CCW winding. `--collision` exports it as a translucent node. It was checked to fall
exactly on Boff's visible geometry; the backdrop (`boff_99`) has no collision.

## Object placement

The `.sob` and `.itm` (`itm_dat2`) **place nothing**: they contain local states and hitboxes. The placement of the
props is in the bytecode of the `.sty` scripts, for example `s_boff01.sty @0x300`, with the opcode `0x80000006`
and 3 `f32`. It is undeciphered. `INFERRED`.

## Exported zones (2026-10-21, `VERIFIED` with the glTF validator and renders)

- **All 17 zones** generate their `.glb` in `NGB_DATA\extracted\stages\<zone>\`, and all 17 pass the validator.
- Renders checked by eye:
  - Boff: city of Tairon, walls, cathedral, forest and fortress;
  - Ssato: Hayabusa village with houses, river and cliffs;
  - Chrch: cathedral floor plan.
- 8 zones warn of a "texture without PNG". These are V8U8 / L6V5U5 bump textures that [[xpr0]] does not decode yet.
- Byte-for-byte identical variants (`tomb_04/05/06`, `tln_b_05`) are exported only once.
- Variants that merely look alike overlap with `--parts all`: `tln_a_0N`/`1N`, `ssato_0N`/`1N` and `chrch`
  01≈03, 02≈06, 04≈05. `INFERRED`: they are states of the zone in different chapters.

## Pending

- Prop placement from `.sty`.
- Material states (alpha test, MODULATE2X) and TEXANM.
- Bump textures.
- The group 6 billboards.
