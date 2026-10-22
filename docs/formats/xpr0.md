---
tags: [ngb, formats, xdk, texture]
updated: 2026-10-20
status: current
---

# XPR0: Xbox Packed Resource

It is the standard format of the Xbox ecosystem for packing GPU resources: textures, palettes, vertex
buffers, index buffers and push buffers. In NGB it appears standalone, in the 689 AFS entries with the `XPR0` magic
(mostly the `.spr` sprites and UI) and in `Media\*.xpr`, and also inside every [[kmd]]. Tool:
`python -I -m ngb_tools.xpr` ([[tools]]).

## Structure

| Offset | Field | Confidence |
| --- | --- | --- |
| `0x00` | magic `XPR0` | `VERIFIED` |
| `0x04` | `u32` total size | `VERIFIED` (ryu: `0xDC000`) |
| `0x08` | `u32` `header_size`: the GPU data starts there | `VERIFIED` (ryu: `0x6590`) |
| `0x0C…` | resource headers, up to `0xFFFFFFFF` | `VERIFIED` |

- **Each resource's `Data` is relative to `header_size`.** In ryu, the first index falls at `header+0` and
  texture 0 at `header+0xBA70`, that is at offset `0x12000` of the XPR, aligned to `0x1000`. `VERIFIED`: the
  manifest sha256 matches an independent hash of those bytes.
- **`header_size` need not be aligned to `0x800`**, although xboxdevwiki says it is: in ryu it is `0x6590`.
  `VERIFIED`.

### Resource headers, by the `Common` field (`VERIFIED` by walking the real files)

| `Common` | Resource | Header size |
| --- | --- | --- |
| `0x00040001` | texture: `Common`, `Data`, `Lock`, `Format`, `Size` | 20 B |
| `0x00830001` | palette | 12 B |
| `0x00800001` | vertex buffer | — |
| `0x00010001` | index buffer | — |
| push buffer | `Size`, `AllocationSize` and **an extra `u32` that is always 0** | **24 B** (Cxbx documents 20; with 20, the walk over the headers gets misaligned) |
| exactly `0x80000000` | USERDATA: `u32 size` and the payload (in the `.spr`, a table of UV atlases) | 8 B + payload |

Texture `Format` field: dimensions in bits 4 to 7, `D3DFORMAT` in bits 8 to 15, mip count
in bits 16 to 19, `log2 U` in bits 20 to 23 and `log2 V` in bits 24 to 27.

## Formats seen in the data (`VERIFIED`: exported and checked by eye)

| `D3DFORMAT` | Name | Where | Swizzle |
| --- | --- | --- | --- |
| `0x04` | A4R4G4B4 | `font.xpr` (512x256 glyph atlas) and `kfont.xpr` | yes (Morton) |
| `0x0B` | P8 with D3DCOLOR palette | 89 textures of `menu.spr` | yes |
| `0x0C` | DXT1 / BC1 | 14 textures of ryu | **no**: linear 4×4 blocks |
| `0x0E` | DXT2/3 / BC2 | `dlcustom.xpr` (3 textures of 1024x512) | no |
| `0x0F` | DXT4/5 / BC3 | 5 textures of ryu (effects with alpha) | no |
| `0x19` | A8 | 8 textures of `menu.spr` | yes |
| `0x28` | V8U8 (signed bump; alias G8B8) | 326 textures (water and stage surfaces) | yes |
| `0x27` / `0x37` | L6V5U5 (signed bump plus luminance; alias R6G5B5) | 32 textures (for example `chr_kotei`) | yes / no |

- The `ngb_tools.xpr.formats` table knows 54 Xbox `D3DFORMAT` codes. The depth and YUV ones are
  recognized but not decoded, because they do not appear in the data.
- **Signed bump** (V8U8 and L6V5U5): on the NV2A these are unsigned color formats, and the sign is set by the state
  of the texture stage, which the XPR0 does not store. The D3D semantics of the names are used; read as signed,
  the variation between neighboring pixels is 74 times smaller (`VERIFIED`, analyst's spike). In the PNG, R and G carry
  U and V in offset binary, B the z of the reconstructed normal and A the luminance. The manifest marks them with
  `encoding: signed-bump`.
- **DXT is not swizzled.** `VERIFIED`, in two ways:
  - in xemu, `pgraph/texture.c` only deswizzles the formats marked as swizzled and passes the compressed ones
    to `pgraph/s3tc.c` as linear blocks;
  - the 22 exported DXT textures (19 of ryu and 3 of `dlcustom.xpr`) look correct without swizzle.
  The swizzled formats use Morton, with x in the low bit (`pgraph/swizzle.c`). `VERIFIED` with `menu.spr` and
  `font.xpr`.
- **P8:** the palette is taken from the following header, if it is a palette, and otherwise the only one in the file. In
  `menu.spr` each P8 is followed by its palette both in the headers and in the data, and the images come out
  fine. `INFERRED`.
- **A8** is exported as white with alpha, as Cxbx does. On a white background it looks empty.
- The DXT5 alpha is decoded following xemu (`pgraph/s3tc.c`) and Microsoft's BC3 specification, not
  Cxbx. `INFERRED`: the analyst saw a dubious operator precedence in Cxbx's decoder (`XbConvert.cpp`),
  but did not pin down the exact line.

## Sources (the XDK was not used)

- xemu: [`nv2a_regs.h`](https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/nv2a_regs.h),
  [`pgraph/texture.c`](https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/texture.c),
  [`pgraph/swizzle.c`](https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/swizzle.c) and
  [`pgraph/s3tc.c`](https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/s3tc.c).
- Cxbx-Reloaded: [`XbD3D8Types.h`](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/core/hle/D3D8/XbD3D8Types.h)
  and [`XbConvert.cpp`](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/core/hle/D3D8/XbConvert.cpp).
- [xboxdevwiki/XPR](https://xboxdevwiki.net/XPR).
- [Block compression (Microsoft)](https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression).
- [Texture tiling and swizzling (F. Giesen)](https://fgiesen.wordpress.com/2011/01/17/texture-tiling-and-swizzling/).

## Gotcha for the port

On PC, swizzled textures have to be deswizzled when they are loaded, or handled in a shader. DXT can be
uploaded as is to the GPU without decoding on the CPU. BC1, BC2 and BC3 are mandatory in Direct3D 11/12.
In Vulkan they depend on the `textureCompressionBC` feature and in OpenGL on `EXT_texture_compression_s3tc`; both
are present on all desktop GPUs, but not on mobile.
