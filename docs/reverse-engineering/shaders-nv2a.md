---
tags: [ngb, reverse-engineering, gpu, shaders, h7]
updated: 2026-10-24
status: current
---

# NV2A shaders: vertex programs and register combiners → GLSL

It is a piece of H7b ([[adr-06-hybrid-renderer]]): it translates the shader code that the game loads into the GPU to GLSL
4.00, for the OpenGL backend. Own implementation in `port/src/shaders/` (`vsh.c`, `psh.c` and `emit.c`). The
format was studied in xemu, Cxbx-Reloaded, envytools, GL_NV_register_combiners and the vs_1_1 specification, without
copying GPL code and without touching the XDK.

## Vertex programs (`VERIFIED`: tests plus the 16 real programs)

- Each instruction takes 128 bits, with **MAC and ILU in parallel**:
  - MAC: MOV MUL ADD MAD DP3 DPH DP4 DST MIN MAX SLT SGE ARL;
  - ILU: MOV RCP RCC RSQ EXP LOG LIT.
- Registers:
  - R0-R11; R12 is an alias of oPos;
  - inputs v0-v15 and constants c[0-191], with relative addressing by A0;
  - swizzles, negation and write masks.
  - If the ILU is paired with the MAC, it writes to R1.
- **Xbox screen-space epilogue:** the position comes out in pixels and is converted to clip space with
  `surfaceSize`/`clipRange`, multiplying xyz by w.
- **Real data:** in `.data` of `default.xbe` there are **16 vertex shader binaries** (header `0x2078`, 13 to 134
  instructions), and all 16 decode. They confirm the format:
  - `DP4 oPos` with the matrix in c[96-99];
  - epilogue `MUL c[58]` + `RCC R1.x` + `MAD c[59]`.
  - The generated GLSL is in `NGB_DATA\runs\shaders\` (it is derived from the game and does not go in the repo).
- **c[58] and c[59] are the viewport (`VERIFIED` in a RenderDoc capture, 2026-10-24):**
  - the constant memory and the fixed-function transform context are the same storage;
  - `SET_VIEWPORT_SCALE` (`0x0AF0`) lands in c[58] and `SET_VIEWPORT_OFFSET` (`0x0A20`) in c[59], D3D's
    reserved c[-38] and c[-37];
  - D3D never loads them with `SET_TRANSFORM_CONSTANT`. Before the fix they stayed at 0, so every program with
    the epilogue collapsed its vertices to (-w, -w, -w, w) and drew nothing. The water in chapter 12 was one of them
    (a 26-instruction program).
  - `port/src/gpu/pgl_track.c` copies both methods into the constants; `port/tests/pgl_track_test.c` checks it.

## Register combiners (pixel shaders)

- 1 to 8 stages plus the final one.
  - Inputs A-D, with the 8 mappings (unsigned, expand, signed…).
  - AB/CD as product or dot, and SUM or MUX.
  - Scale and bias, and blue-to-alpha.
  - Per-stage constants.
- Final stage: A·B + (1−A)·C + D, alpha = G, plus EF_PROD and V1R0_SUM.
- Emulated texture modes:
  - projective 2D, 3D and CUBE;
  - PASSTHRU;
  - CLIPPLANE;
  - BUMPENVMAP and BUMPENVMAP_LUM.
- **Real data:** 2 real pixel shaders were found, of 2 stages with dot-product bump
  (`DOTPRODUCT` + `DOT_ST`). They are translated, but **the `DOT_*` modes are not emulated yet**, so their result is
  not correct yet. `INFERRED`: most materials will use the fixed pipeline (texture stage states), not
  pixel shaders.

## Tests

- `port/tests/shaders_test.c`: 219 checks, with instructions hand-encoded word by word.
- **Compiled with the real driver (`VERIFIED`)**: the **18 real shaders** (16 vertex programs and 2 pixel shaders)
  compile and link in OpenGL 4.6 (AMD RX 6600), without touching the emitter. So do the 62 variants of the
  `--stress` mode of `gl_test`, which covers every operation, every mapping and every texture mode. It has only been tested on AMD.

## Pending

- The `DOT_*` modes, BRDF and DPNDNT.
- Non-linear fog, oB0/oB1 by face and the half-pixel offset.
- Alpha test, color key and alpha kill.
- Merge the programs that arrive through the push buffer (`0x0B00` + LOAD/START) into the PGRAPH interpreter.

## OpenGL backend (`port/src/gl/`)

It is not hooked into the PGRAPH interpreter yet: it is the base for accelerated rendering of the 3D geometry.

- **`glbackend`:**
  - GL 4.x context through WGL, with its own loader (`wglGetProcAddress`, no dependencies);
  - offscreen rendering into a 720x480 FBO;
  - attributes in the `SET_VERTEX_DATA_ARRAY_FORMAT` formats (float, D3DCOLOR, normalized short and CMP
    11:11:10) and indexed drawing.
- **`texconv`:** converts NV2A textures to GL.
  - Packed formats (swizzled and linear), P8 with palette and DXT1/3/5. DXT is uploaded compressed with
    S3TC if the driver supports it.
  - Signed: V8U8 and L6V5U5. Also cube maps and mipmaps.
  - **Matches `ngb_tools.xpr` byte for byte on 28 synthetic vectors** (`texconv_test`).
- **`glstate`:** translates the state of the NV097 methods to GL (blend, depth, stencil, cull, color mask, scissor,
  alpha test and fog). The front face is inverted, as in xemu (checked with RenderDoc and with xemu captures; see [[gpu-nv2a]]).
- **Visual test:** `gl_test` draws to `NGB_DATA\runs\shots\gl_test.png`:
  - a triangle with hand-encoded NV2A microcode;
  - a checkerboard;
  - a DXT1 quad and a swizzled P8 one;
  - a translucent band.
  The leader reviewed the capture.
