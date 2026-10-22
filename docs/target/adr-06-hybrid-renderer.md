---
tags: [ngb, target, adr, render]
updated: 2026-10-21
status: current
---

# ADR-06: hybrid renderer (H7b). Emulated PGRAPH as the base, HLE only for synchronization and presentation

## Context

[[adr-05-native-runtime]] left two routes open to reach the first frame: emulate the NV2A, xemu style, or
reimplement D3D8 (HLE), Cxbx style. H5 measured with Ghidra and XbSymbolDatabase how much of D3D8 can be
identified ([[xbe-default]] § H5). All the data is `VERIFIED` by the report
`NGB_DATA\ghidra\reports\` (coverage report).

- **D3D8 is identifiable:** 97 named entry points. From `.text`, 86 functions of the `D3D` section are called
  at 1,552 sites; 78 have a name and cover 1,420 sites (91.5 %).
- **But LTCG put state inside the game:**
  - about 900 direct writes to the D3D globals (`g_RenderState` 286, `g_DeferredRenderState` 135,
    `g_DeferredTextureState` 449);
  - SetTextureStageState almost always inline;
  - SetIndices, BeginPush/EndPush, texture creation and the Lock of textures and VBs, all inline;
  - inline pushes in 23 functions of `.text`, at 51 sites that call `MakeRequestedSpace`.
- **Raw push buffers in the assets:** `RunPushBuffer` (0x3CD680) executes the NV2A push buffers of the KMD
  ([[kmd]]).
- 8 functions use a register calling convention (261 sites), and so does `Swap_0`, which receives the flags in EAX.
- `.text` emits no direct draw method: everything goes through the D3D API or through `RunPushBuffer`.
- The LLE path already works: H6 and H7a get through the D3D init and reach the first `FLIP_STALL`.

## Decision

**Hybrid.** The base is to **emulate PGRAPH/PFIFO**: interpret the NV097 methods of the push buffer and translate them to
OpenGL or Vulkan. That interpreter is needed anyway because of `RunPushBuffer`, and it sees all the state even if it is
inline. On top goes **HLE at four points**:

| Point | Address | HLE |
| --- | --- | --- |
| `D3DDevice_Swap_0` | `0x3CE820` | frame boundary: present the surface and measure time |
| `KickOff` | `0x3CD390` | process the push buffer synchronously, instead of trapping `DMA_PUT` with the VEH |
| `BlockOnTime`, `BlockOnResource`, `SetFence` | (see report) | resolve instantly with a synchronous renderer |
| vblank | ISR `0x3D2040` → DPC `0x3D2310` → `KeSetEvent(dev+0x1DBC)` | synthetic PCRTC interrupt at 60 Hz |

## Discarded alternatives

- **Pure D3D8 HLE:** a hook at the API boundary does not see the inline state (about 900 writes), and
  `RunPushBuffer` requires an NV097 interpreter all the same.
- **Pure LLE, without HLE:** trapping every `DMA_PUT` and every fence poll with the VEH is slow and fragile. The frame
  boundary at `Swap_0` gives a natural point to present.

## Consequences

- The pieces to build:
  - the NV097 interpreter (a method decoder already exists in `feat/nv2a-min`);
  - the translator of NV2A vertex programs and register combiners to GLSL or SPIR-V;
  - texture conversion, reusing the logic of [[xpr0]] (swizzle, DXT and P8);
  - a backend with SDL3 and OpenGL 4.5, like [[opence]].
- The first visible frame will be a CLEAR followed by a FLIP presented in an SDL3 window, and then the first
  primitives.
