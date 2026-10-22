---
tags: [ngb, formats, teamninja, model]
updated: 2026-10-20
status: draft
---

# KMD: models and scenes of the Kunai engine

It is a [[chunk-teamninja]] chunk with the tag `KMD`. There are 724 KMD in the game (751.8 MB), which are
characters and stages ([[afs]]).

## Fixed slots (`INFERRED`: the analyst's exploratory script, not versioned, over the 724 KMD; 709 have 10 slots and 15 have 9)

| Slot | Subchunk | KMD that use it | Hypothesis |
| --- | --- | --- | --- |
| 0 | `OBJINFO` | 724 | one record per object (mesh) |
| 1 | `XPR0` | 724 | **textures, vertex buffers and index buffers in one Xbox Packed Resource** → [[xpr0]] |
| 2 | `HIERARC` | 239 | bone and node hierarchy |
| 3 | `HITMDL` | 375 | collision mesh |
| 4 | `TEXANM` | 216 | texture animation |
| 5 | `MORPH` | 49 | morph targets |
| 6 | `ACSSRI` | 36 | accessories |
| 7 | `MISCDATA` | 153 | miscellaneous data |
| 8 | `OBJHIT` | 111 | hitboxes |
| 9 | `EFFSCR` | 55 | effect scripts |

- At `0x58` comes the name and at `0x60` eight floats (`INFERRED`: bounding box; in stages it holds garbage).

## `ryu.kmd` (`chr_ryu.afs#0`, 1,018,944 bytes)

- 59 objects: 59 OBJINFO, 59 vertex buffers and 59 index buffers, 1 pushbuffer and 60 USERDATA.
- XPR0 at `0x3000` with a total of `0xDC000` bytes and a header of `0x6590`. `VERIFIED` by the leader.
- **19 textures:** 14 DXT1 and 5 DXT4/5 (`VERIFIED` by the analyst, counting the D3D formats of the XPR0 resources).
- HIERARC with 59 slots (57 used), MORPH, ACSSRI, MISCDATA and OBJHIT with 11 hitboxes.

## Textures of `ryu.kmd` (H2, `VERIFIED`: exported with `ngb_tools.xpr` and checked by eye)

All have 1 mip. The XPR0 is in slot 1 (`0x3000`).

| # | Format | Size | What it shows |
| --- | --- | --- | --- |
| 0 | DXT1 | 256x512 | torso of the black outfit: straps and buckles |
| 1 | DXT1 | 128x256 | woven black cloth |
| 2 | DXT1 | 256x256 | gold metal band and folds of black cloth |
| 3 | DXT1 | 256x256 | folds of black cloth |
| 4 | DXT1 | 256x256 | metal ornaments, **green eye**, eyelid and skin |
| 5 | DXT5 | 128x128 | purple lightning (effect with alpha) |
| 6 | DXT5 | 32x32 | puff of smoke (particle) |
| 7 | DXT1 | 256x64 | metal band of the forehead or the mask |
| 8 | DXT5 | 128x128 | bright red ring with veins |
| 9 | DXT1 | 256x128 | blade or sheath, ornament and dark leather |
| 10 | DXT1 | 256x256 | skin (muscle) |
| 11 | DXT5 | 256x256 | purple lightning (alpha) |
| 12 | DXT1 | 256x256 | black outfit: straps, rivets and mesh |
| 13 | DXT5 | 64x64 | gold emblem (alpha) |
| 14 | DXT1 | 128x128 | palm and back of the hand, black glove |
| 15 | DXT1 | 128x128 | woven black cloth with a light stripe |
| 16 | DXT1 | 256x512 | black outfit with straps and buckles |
| 17 | DXT1 | 16x16 | round black dot (shadow or pupil) |
| 18 | DXT1 | 256x256 | atlas of an alternate blue outfit: eyelets and skin |

`r_ryu.kmd` has 26 textures.

## Geometry (H3, `VERIFIED` on the 59 objects of `ryu.kmd` and 63 of `r_ryu.kmd`)

**Correspondences.** Object *k* is at the same time entry *k* of OBJINFO, node *k* of HIERARC, USERDATA
*k+1*, and the *k*-th VB and the *k*-th IB of the [[xpr0]], in resource order. USERDATA 0 is a header:
63, **19 textures**, **59 objects**, …

**Mesh descriptor** (payload of USERDATA *k+1*):
- `0x28` B header: `+4` n_sub, `+8` **D3D FVF**, `+0x14` nv, `+0x18` stride, `+0x24` n_idx.
- In the 59 objects of ryu, nv·stride = VB size and ceil4(n_idx·2) = IB size (`VERIFIED`, spike
  script). The tool only requires that they fit: size ≥ what is needed.
- Then, one record per submesh:
  - `0x60` fixed B, plus `0x50` of D3DMATERIAL8 material if `+0x20 = 1`;
  - `n_tex` stages of `0x40` B: tail of the name, **texture index** (the same as in H2) and 14 `u32` of states;
  - at `+0x10` the source of the indices: `2` = IB, `0` = push buffer;
  - at `+0x14` and `+0x18` the start and the size in bytes.

| FVF | Stride | Fields |
| --- | --- | --- |
| `0x112` (XYZ\|NORMAL\|TEX1) | 32 | pos f32×3, normal f32×3, uv f32×2 |
| `0x116` (XYZB1\|NORMAL\|TEX1) | 36 | pos, weight f32, normal, uv (skinned pieces) |
| `0x042` (XYZ\|DIFFUSE) | 16 | pos, D3DCOLOR (shadows) |

`0x142` and a group 6 also appear in other characters; the generic FVF decoder covers them.

- **There is no NV2A vertex declaration in the push buffer.** The format comes from the FVF.
- Normals are floats of magnitude 1, with no NORMPACKED3.
- UVs wrap: glTF keeps them and the OBJ export writes `1−v`.

**Indices.**
- u16 in **triangle strips** without restart, joined with degenerates.
- Winding: even `abc`, odd `bac`. It matches the normals in 14,673 of 14,683 triangles.
- Ryu's head (submesh 1 of obj1) is drawn from the **push buffer**:
  - `BEGIN TRIANGLE_STRIP` and `ARRAY_ELEMENT16` × 1085 words = 2,170 indices, and `END`;
  - it uses exactly the 732 vertices that its IB does not touch (`VERIFIED`, `pbcheck.py` script of the spike, stored in
    `NGB_DATA\extracted\h3\spike\`).
  - `INFERRED`: it is the face, with MORPH for the expressions.

**Hierarchy (HIERARC).**
- Header with the root at `+0x28`.
- Entries `u16 self, parent (0xFFFF = root), n_children, subtree_size, children[n]`.
- The nodes are the objects themselves.
- TRS in OBJINFO (`+0x30` T, `+0x40` R, `+0x50` S). In Ryu R = 0 and S = 1, so the world position is the
  sum of the T along the chain.
- **Vertices come in the local space of their bone**, which is why the hierarchy has to be applied.
- Skinned pieces go on their parent bone and ignore their own T. The tool recognizes them by the weighted
  FVF (XYZB*), not by the group; in ryu they coincide with group 4.
- Skeleton:
  - hip 2 → chest 0 → head 1;
  - shoulders 8/14 → elbows 4/10 → hands 5/11;
  - thighs 6/12 → knees 7/13 → feet 3/9.
  - T-pose, 1.78 m tall.

**Object groups** (OBJINFO `+0xC`):
- 1: detail body.
- 4: skinned joints.
- 5: low LOD.
- 2: shadow, `INFERRED`.
- 3: effect with scale 0, `INFERRED`.
- The glTF exports 1 and 4 by default.

## Export to glTF (`VERIFIED`: the figures are the output of `ngb_tools.kmd export`; rendered from the re-read glTF and checked by eye)

| Model | Result |
| --- | --- |
| `ryu.kmd` | 27 objects, 9,143 vertices, 13,431 triangles, 0 issues |
| `r_ryu.kmd` | 39 objects, 20,623 triangles (cutscene variant) |
| Ayane (`chr_ayane#0`, `aya_a`) | complete; the issue is a small rotation in obj24/25, which is ignored |
| Murai (`murai`, `r_mur`) | complete |
| Doku: katana (`do_sw`) and mask (`do_face`) | complete |
| **Doku: body** (`chr_doku#0`) | complete since PR #29: it uses the multipart descriptor, see below. The face is assigned at run time |

Ryu's sword is not in `ryu.kmd`.

## Multipart descriptor (`VERIFIED`: 59 of 60 characters export)

Some characters use the same variant as the stages ([[stages]]). At the start of the descriptor comes a
`u32 n_parts`; the header takes `8 + 0x20·n`, and the `+0x04` field of each submesh says which part it belongs to.
Each part has its own VB/IB pair, and `USERDATA0[2]` counts VBs, not objects. `ngb_tools.kmd` presents an object
with several parts as a single one: it concatenates the vertices and offsets the indices; if a part lacks UV,
color or normal, it fills them in (normals are smoothed from the triangles, `INFERRED`).

- Now exporting: army, aruma, **doku** (samurai with a horned kabuto and sheathed katana; the face is assigned at
  run time from `do_face.kmd`), heli, s_aru, sohei, tank, truck (4 parts), ufo and whip.
- Push buffers: the first `u32` after `END` is ignored, whatever its value, and whatever follows must be 0. In the
  game there are 2,137 push buffers, all with exactly 1 `u32` after `END` (72 with garbage).
- Regression: Ryu and Ayane come out byte-for-byte identical, and so do 47 of the 49 characters that already exported.
  Daina and Rachel gain triangles that were previously skipped.
- **unagi** still fails: its HIERARC is another variant (root out of range).

## Pending

- The HIERARC variant of unagi.
- R ≠ 0 (unknown rotation order).
- Semantics of the skin weights.
- Material states: additive blend, alpha of the DXT5 and extra stages.
- MORPH (the animations are already done: [[mtn]]).
