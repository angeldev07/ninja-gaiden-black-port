---
tags: [ngb, reverse-engineering, runtime, gpu, h7]
updated: 2026-10-22
status: current
---

# NV2A GPU in the native runtime (H7): the warnings screen in a window

The native loader ([[native-boot]], [[adr-05-native-runtime]]) already **emulates a minimal set of NV2A registers** and
decodes the push buffer. With that, `default.xbe` gets through D3D8 initialization, submits frames and reaches the
**warnings screen** ("Warnings"). The code is in `port/src/gpu/` and the design decision in
[[adr-06-hybrid-renderer]].

Sources:
- `NGB_DATA\runs\h7a-run.log` and `h7a-run-pgraph.log`: 30 s run by the analyst;
- the dumped textures, reviewed by the leader.

Everything that follows is `VERIFIED` by those logs.

## What unblocked each step

1. **The hang was not vblank, but an H6 bug.** The TEB overlay wrote `fs:[0x24] = tid<<8`
   as the IRQL. On Windows that field is `ClientId.UniqueThread`, and the system uses it to wake whoever waits on
   a CRITICAL_SECTION or an SRW. Wakeups were lost and the DPC thread stayed blocked forever in
   `NtWaitForAlertByThreadId`.
   - **Fix:** `fs:[0x24]` is no longer touched. The 7 `movzx r32, byte fs:[24h]` reads that the XBE performs are patched
     at load time to `mov r32, 0` (IRQL 0).
   - **Lesson:** every overlaid TEB field must be checked against its use in Windows.
2. **Synthetic vblank at ~60 Hz**, with the H5 recipe ([[xbe-default]]): bit 24 of `PMC_INTR_0` and
   bit 0 of `PCRTC_INTR_0` are set, the ISR `0x3D2040` is called, it goes through the DPC `0x3D2310` and ends in
   `KeSetEvent(m_VerticalBlankEvent)`. Writing `PCRTC_INTR_0` clears both bits (W1C).
3. **Audio without blocking:**
   - APU `PIO_FREE = 0x80`;
   - AC97 `GLOB_STA` with the codecs ready and a reset bit that clears itself;
   - a GP DSP mailbox that empties itself.
4. `NtQueryDirectoryFile` implemented.

## Modeled registers

- `PMC_BOOT_0 = 0x02A000A3` (NV2A id) and W1C interrupts in PMC, PFIFO, PGRAPH, PCRTC and PTIMER.
- PFIFO: `DMA_PUT`/`GET`, USER channel 0 and `DMA_CTL`. Each PUT change is decoded and GET advances.
- PTIMER advances; `PFB_CSTATUS` = 64 MB; the PFB WBC flush clears itself; PGRAPH idle; `PCRTC_RASTER`; the PRAMDAC PLLs
  are locked.
- PRAMIN is an alias of RAM. RAMHT resolves the objects (classes `0x97` Kelvin/3D, `0x39`, `0x9F`, `0x62` and `0x44`).
- Anything not modeled behaves like RAM and is noted only once.

## What the game submits (30 s, no pacing)

| Metric | Value |
| --- | --- |
| Frames (`FLIP_STALL`) | 37,337. Not yet synchronized with vblank |
| `BEGIN_END` | 808,728, all `TRIANGLE_STRIP` with `INLINE_ARRAY` |
| First frame with primitives | frame 202 |
| `CLEAR` | 74,473 |
| Push buffer kicks | 117,874; semaphores 117,870 |
| Push buffer decoding errors | 0 |
| vblanks | 972 |
| Kernel calls | 3.99 M |

- **Color surface:** 720x480 A8R8G8B8, pitch 3072, at `0x02878000`.
- **Textures the game loads**, dumped from GPU memory and reviewed by the leader:
  - the **"Warnings" caption** (A8 256x64);
  - the game's **font atlas** (I8 with palette, 128x128 swizzled);
  - the two 720x480 buffers used as textures.
- The color buffer is **black**: nothing is rasterized yet, and the CLEAR is 0.

## H7b: the first visible frame (`VERIFIED`)

**The warnings screen shows in a PC window at 60 fps**: the "Warnings" caption with its glow and the text
scrolling and fading. It is the game's code running natively.

- Command: `build\port\ngb-loader.exe <default.xbe> --window`.
- Captures: with `--debug-shots N` (off by default since 2026-10-22) the loader saves one every N frames
  (`<log>-pgraph-shot-NNNNN.png`); the leader reviewed
  `h7b-int-pgraph-shot-00600.png`.

### How it is drawn

- **Software rasterizer** (`port/src/gpu/raster.c`, pure code with 23 hand-written tests):
  - triangles with the position already in screen space (XYZRHW), diffuse color and texcoord0;
  - point sampling of A8R8G8B8, X8R8G8B8, R5G6B5, A8 and I8 with palette, with or without swizzle;
  - texture × diffuse, alpha test, blend with the NV2A's GL factors and perspective correction;
  - the primitives TRIANGLES, TRIANGLE_STRIP, TRIANGLE_FAN, QUADS, QUAD_STRIP and POLYGON.
- **New NV097 methods:**
  - `SET_VERTEX_DATA_ARRAY_FORMAT`;
  - `SET_BEGIN_END`: drawing happens on close;
  - `INLINE_ARRAY` and `SET_VERTEX_DATA4UB`;
  - alpha test and blend.
- **Presenter** (`port/src/gfx/present.c`, Win32/GDI): on each `FLIP_STALL` it copies the 720x480 color buffer
  to the window. Then it waits for the next 1/60 s, which sets the rhythm (*pacing*). With `--no-pacing` it goes back to
  running unlimited: about 1,300 fps.
- **For accelerated rendering** there are already shader translators ([[shaders-nv2a]]) and an OpenGL 4.6 backend in
  `feat/gl-backend`. The software rasterizer is enough for the warnings, which are 2D.

### What had to be fixed in the emulated kernel

- **Deferred `NtSuspendThread`.** The game shares work between threads with Suspend/Resume, and could freeze a
  thread that held a loader lock (emulated kernel, NV2A VEH or IRQL ≥ DISPATCH). Now the suspension
  waits for the thread to leave that code.
- **Events cleared in memory.** D3D sets the `SignalState` of its vblank event to 0 directly, without
  going through the kernel. `KeWaitFor*` now synchronizes the Windows event with the header. Before,
  `BlockUntilVerticalBlank` returned instantly about 50,000 times per second.
- **Idle watchdog:** a race made "15 s without calls" fire at 5 s.

### How far it gets

1. The full warnings screen, about 40 s.
2. **The intro video plays** (Sofdec, a stormy sea), decoded by the game itself and
   drawn with the software rasterizer. The leader reviewed the capture of frame 3600 from `exit-fix.log`.
   - Before, the game stayed on a black screen. **The cause was a bug in the emulated kernel**, not the audio
     or the gamepad.
   - XAPI's `GetExitCodeThread` (`0x32BE72`) reads `Header.SignalState` (+4) and `ExitStatus` (+0x120) of the ETHREAD.
     The thread that enumerates the save games ends early, but `ngb_thread_exit` never wrote those
     fields.
   - So the main thread's fiber task (`0x168030`) waited in a `while (code == 0x103)` loop
     forever.
   - **Fix:** when a thread ends, it writes ExitStatus and sets SignalState to 1. A test with a synthetic
     XBE covers it.
3. To play the video, the game resets D3D twice (`AvSetDisplayMode` with pitch `0xB40`) and the pusher
   supports it.
4. **The video stays on its first frame** (`INFERRED`: Sofdec advances with the audio clock, and the APU is not
   emulated). START skips it.
5. **Title screen** ("PRESS START BUTTON", with Ryu in the background; capture `title4-pgraph-shot-15300.png`,
   reviewed by the leader). Before showing it, the game verifies its data with `XcSHAInit/Update/Final`, which
   are now implemented (own SHA-1 and HMAC in `port/src/kernel/sha1.c`, with the FIPS 180 and
   RFC 2202 vectors).

**The "looping" warnings run in slow motion (`VERIFIED` with `--watch`):**
- The warnings task (`0x168030`) does, in order:
  1. waits for T8;
  2. runs 5 phases (table `0x443B1C`);
  3. waits for the `z:\demo.afs` cache (`FUN_00304f10`, flag `0xD7DCF9`), which finishes in 1.5 s;
  4. draws the warnings (`FUN_001f73b0`, state at `0x906180`).
- On PAL (region 4), the warnings wait 3·60 frames (`0xE130DA = 60`) and then scroll the text 0.3 px per frame
  (`0x43018C`) until it has covered 640 px: about 2,300 frames, **38 s at 60 fps**. START or A skips them.
- **There is no loop:** the game runs at 42 fps or less, because the software rasterizer cannot reach 60, and
  the warnings stretch to 1-3 minutes. The fades that are seen belong to the scrolling itself.
- The solution is GPU rendering (H7c).
- Along the way:
  - the loader asks the system for 1 ms timer resolution (`timeBeginPeriod(1)`), because without it every short
    Xbox `Sleep` lasted 15.6 ms;
  - `--watch` was added to monitor game variables.

## H7c: GPU rendering with OpenGL (`VERIFIED`)

`--renderer gl` is the default mode if there is a window and a context; `--renderer soft` remains as a fallback. Code in
`port/src/gpu/pgraph_gl.c` and `rcache.c`; it uses [[shaders-nv2a]], `texconv` and `glstate`.

| Metric (`perf:` lines of the log) | Software | OpenGL (RX 6600) |
| --- | --- | --- |
| Title and menu | 16 fps (raster 59 ms/frame) | **60 fps** (pusher 0.7-2 ms) |
| Warnings | 38-53 fps | **60 fps**: they end at 40.0 s, which is their design duration |

- **Threads:**
  - the kick arrives on any Xbox thread, but the GL context is bound to a single thread. That is why there is a
    dedicated render thread, and `pgraph_kick` hands the work to it synchronously;
  - pacing is done outside the locks (issue #35).
- **Surfaces:**
  - color and depth surfaces are GL textures, indexed by physical address;
  - render-to-texture without going through the CPU;
  - a sparse signature of the RAM detects which surfaces the CPU has overwritten.
- **Drawing:**
  - INLINE_ARRAY, DRAW_ARRAYS and ARRAY_ELEMENT;
  - vertex programs (`0x0B00` + LOAD/START) or fixed function;
  - program and texture caches by hash.
- **FIFO bug that caused the "flicker" after the warnings** (also present in soft):
  - when D3D is reset for the video, the game stops the pusher (`CACHE1_PUSH0 = 0`), frees the old push buffer and
    sets PUT to 0;
  - the emulated NV2A kicked on any PUT write, even with the pusher stopped, and replayed
    about 4 M words of the freed buffer. FLIPs of the old warnings came out alternating with black for about 2
    minutes, plus 121 pb errors;
  - now, with the pusher stopped, PUT is only stored, and the kick happens when it is reactivated.
  - Result: the warnings give way to the video (sea, mountains, Team NINJA logo) and to the title, at 60 fps.
- **3D after New Game** (seen once):
  - 1,091 primitives per frame with vertex programs and 0 shader errors, but at 6.6 fps, and the capture shows an
    intermediate buffer;
  - missing: format `0x2E` (linear depth as texture), lowering the cost per draw and choosing which surface is
    presented.

## H8b: 3D gameplay (the title demo)

If the title screen is left without pressing anything, the game plays a **gameplay demo with its own engine**:
- it is a deterministic 3D scene that needs no input, so it is the best test case for 3D rendering;
- the project owner pointed it out, and it arrives on its own: warnings (40 s), video (~15 s), title and wait.

**Result (`VERIFIED`):**
- Ryu fights demons with the Dragon Sword and a fire Ninpo, with models, animations, textures and effects.
  The leader reviewed the captures `demo1-pgraph-shot-05400.png` and `-06600.png`.
- It runs at 35-60 fps (before, 6.6).
- 46 voices play, 38 of them one-shot effects, without cuts.

**What changed:**
- **Performance:** incremental GL state, less texture hashing, constants only if dirty and new caches
  in `rcache.c`.
- **EEPROM** (`kernel/eeprom.c`):
  - XAPI asks for the whole EEPROM (index `0xFFFF`) to sign the hard-disk files when `XboxHDKey` is
    zero;
  - before, only 4 bytes were written and the rest was stack garbage, so the signature of `T:\system.dat` changed
    between runs and the game said "Unable to load game settings";
  - now there is a full 256-byte image.
- **Deterministic input:** `--input-script` accepts the `p` unit (gamepad reads), for example
  `t=2400p:START@10p`. That way it does not depend on how long the loads take.

**Pending:**
- the sky and the background come out as a flat blue (skybox or fog) and part of the floor is missing;
- the drops to ~30 fps in loaded scenes;
- the rest of the minor items of issue #43.

**Process lesson:** the agent spent 1 h 30 min trying to reach the level via New Game, whose initial video cannot be
skipped without a save game, and filled `NGB_DATA` with captures. The title demo gave the same 3D scene without
input. Agents must clean up their captures and look first for the cheapest test case.

## Static push buffer fixups (the demo hang and the 3D fps)

**Symptom:** when the title demo started, the game hung in `D3D_BlockOnTime` (`0x3CD0D0`) in 2 out of
3 runs, and the demo ran at 35-44 fps.

**Cause (`VERIFIED` in Ghidra and with traces):**
- `D3DDevice_RunPushBuffer` (`0x3CD680`) jumps from the ring to a static push buffer that ends in a JUMP back,
  and D3D rewrites that JUMP on every use.
- If the GPU has not finished the previous use, D3D inserts `NO_OPERATION` (`0x100`) with the parameter `(i << 5) | 0xD`
  (record `i` of a device table) or `(va << 5) | 0xC` (record in the ring).
- On the Xbox, that NOP stops the GPU and raises an interrupt; the D3D routine does the patching, the same one
  `FUN_003cd5e0` does directly when the GPU has already passed.
- We treated it as an ordinary NOP:
  - the JUMP back still pointed to the previous use and the emulated GPU looped up to the cap of 4 M
    words (up to 400 M words per run);
  - it overwrote D3D's fence (`0x82774000`) with stale values, and the game waited forever.

**Fix:** HLE of the patching in `pgraph.c` (`d3d_pushbuffer_fixup`):
- it walks the record list, writes the JUMP back and applies the fixup list;
- it validates each pointer against the contiguous RAM or the XBE image.

**Result:** zero huge kicks, no hangs in the tests and the demo at **55-60 fps**.

**New tools:**
- the `perf:` lines now measure `pb_run`, `kick_body`, the lock wait and `pgl_call`;
- a kick of more than 1 M words is logged with its first jumps;
- every change of the D3D semaphore target goes to the log.

**Race with the asynchronous pusher (the render hang in long sessions), `VERIFIED` in Ghidra:**
- **Symptom:** about once per hour of play, the render freezes; the log shows
  `pb: stopped at X without reaching PUT`.
- **The record list:** `D3DDevice_RunPushBuffer` checks whether the last record (`dev+0x58`, with the device
  at `0x3D85E0`) has its next pointer at 0, that is, whether its interrupt is still pending. If so, it chains the
  new record there and does not insert another NOP.
- **The race:** our fixup runs on the render thread, in parallel with the game. It read the next pointer and
  closed it (`0xFFFFFFFF`) in two steps, and if the game linked in between, that record was lost. Its JUMP back
  stayed stale and the GPU looped up to the cap of 4 M words (`INFERRED`: the huge-kick warning did not appear,
  so the loop was almost all jumps, which do not count as data words).
- **Fix (`d3d_fixup_chain` and `d3d_fixup_late`):**
  - the close is an `InterlockedExchange`;
  - if the game still links to the already closed record, that link is handled before the next pb jump. It only
    counts if the chain reaches D3D's last record: a ring record may already be rewritten.
- **Faithful ISR (`h5-decomp-d3d-vblank.txt`):**
  - the NOPs `0xC` and `0xD` decrement the pending interrupts (`dev+0x50`, `DAT_003d8630`), which previously
    never went down;
  - `0xE` (record in the ring, no decrement) is now handled.
  - After the fixup, the ISR writes bit 16 of `0x100410` and waits for it to drop (`FUN_003cd540`), a PFB
    flush that is not needed here.
- **Diagnostics:** a kick that does not reach PUT dumps its first jumps to the log.

**Bulk vertex data (`on_bulk`):**
- `ARRAY_ELEMENT16` and `INLINE_ARRAY` are two thirds of the push buffer words (1,097 M and 751 M
  in a 70 min session).
- `pb_run` delivers the non-increasing runs at once, without crossing a page or passing PUT. With the active-frame
  log they still go word by word.
- **Measurement:** in the deterministic demo, frames 2400-6000, `pb_run` drops from ~8.4 to ~7.3 ms per frame.
- **Still unexplained:** about 4 ms of `pb_run` outside GL. It needs profiling.

**Tried and discarded:** replacing the event handoff to the render thread with a busy wait. On this CPU
(Ryzen 7 5700G) it added ~17 ms per frame, so the event handoff stays (~1.6 ms per frame in total).

## Fixed pipeline with lighting and shadow maps (`VERIFIED` in the demo)

In a demo frame, **half the draws use the fixed pipeline** (`SET_TRANSFORM_EXECUTION_MODE` `0x4`;
`0x6` is vertex program), almost all with lighting on: 1,396 light changes, 812 model-view matrices
and 372 texture matrices per frame. Before, the fixed mode only transformed the position and copied the vertex
color.

**Now, `k_ff_vs` in `pgraph_gl.c`:**
- normals with the inverse model-view (`0x0580`);
- 8 lights (infinite, local and spotlights without cone) with attenuation and range;
- material color (`0x0298`: emission, ambient, diffuse and specular, from the material or the vertex);
- specular, per-stage texgen (eye-linear and object-linear, sphere map, normal and reflection) and texture
  matrices;
- uniforms with a hash of the registers, which are not re-uploaded if they do not change.

**Result:** the stage's arches, walls and columns and the enemies appear lit and textured.

**Shadow maps:**
- 46 draws per frame (vertex program with blend) used a texture of format `0x2E`: the Z24S8 depth
  of a 128x128 surface that the game paints at the start of the frame from the light. It could not be converted and the
  draw was discarded.
- Now the stage binds the GL texture of that Z surface (render-to-texture). `psh` compares, like the NV2A,
  `depth · max` with the projected z (`0xFFFFFF` at 24 bits, `0xFFFF` at 16) using the function in
  `0x1E6C`.

**Approximations (superseded 2026-10-22, see below):** fixed specular power (16) instead of `SET_SPECULAR_PARAMS`,
spotlights without cone and no skinning in fixed mode.

**Lighting rewrite to match xemu's `vsh-ff.c` (2026-10-22, `VERIFIED` with RenderDoc on a gameplay capture):**
- **Symptom:** gameplay looked brighter and washed out, and some objects (the opened chest, some walls) went white.
- **Cause:** `k_ff_vs` always *added* `SET_MATERIAL_EMISSION` (`0x03A8`) as a color. On the NV2A it is a *scale* for
  the vertex color when the color-material source of emission or ambient is a vertex color. The scene ambient
  (`0x0A10`) already carries the material emission plus the global ambient.
  - In a mansion capture, 49 of 183 fixed-function draws use color material `4` (ambient from the vertex diffuse)
    with `0x03A8 = (1,1,1)`. We added full white to them.
- **Now:**
  - **Base color:** `sceneAmbient + src·0x03A8` when emission or ambient comes from the material, and `emission_src
    + ambient_src·0x03A8` when both come from the vertex.
  - **Per light:**
    - ambient, diffuse and specular, each scaled by its color-material source;
    - specular power through `SET_SPECULAR_PARAMS` (`0x09E0`), the rational `(x+L)/(Mx+N)`;
    - spot cone through falloff `0x1040` and direction `0x104C`;
    - local eye through `SET_LIGHT_CONTROL` bit 16 and `SET_EYE_POSITION` (`0x0A50`);
    - lights out of range contribute nothing.
  - **Specular folding:** without `SEPARATE_SPECULAR` (`0x0294` bit 0), the specular folds into D0.
  - **`SPECULAR_ENABLE` (`0x03B8`) and `ALPHA_FROM_MATERIAL_SPECULAR` (`0x0294` bit 17):** applied in the pixel
    shader for both pipelines (uniform `specMode`). D1 = (0,0,0,1) without specular, and D1.a = 1 when the
    alpha flag is off. D0 and D1 are clamped.
- **Result** (deterministic demo, same frames): the mean brightness of the gameplay frames drops from ~130 to ~70
  (0-255). The temple is dark and contrasted, and the reflective floor reads correctly.

**Open:**
- **The sky** is still the clear color (`0xFFA1A5E8`, lavender) and **the floor is missing**.
- With culling disabled, large dark polygons appear that cover the view. It may be a sky dome
  seen from inside or shadow volume geometry (there is stencil with INVERT); it is necessary to isolate which draw it is.

## RenderDoc: culling and skinning (the missing temple)

- **Tool (`--rdoc-frame N`, `port/src/gpu/rdoc.c`):**
  - loads the x86 RenderDoc DLL before creating the GL context and captures exactly frame N; the `.rdc` is left
    next to the log, in NGB_DATA;
  - the capture is analyzed headlessly with `qrenderdoc --python script.py` (module `renderdoc`): list of draws
    with their state, *pixel history*, the target saved after an event and the input and post-VS vertices. The
    scripts are in the session scratchpad.
- **Determinism:** with `--input-script "t=180p:START,t=600p:START"` (gamepad reads, not ms) the demo reaches
  the same frame on every run. That way two builds are compared on the same scene.
- **Reference:** xemu captures of the title demo, made by the project owner. The demo takes place inside a closed
  temple; in our render the walls and the floor were missing and the background color (lavender) showed.
- **First diagnosis, wrong (PR #52):**
  - the *pixel history* showed 43 `backfaceCulled` draws on a floor pixel, and the front-face inversion was
    removed from `glstate`;
  - but `pgraph_gl` called `glFrontFace` without inverting right after, so that change had no effect in the
    runtime;
  - the improvement that was seen came from comparing different scenes, before having deterministic input.
- **First cause, the face winding:**
  - it has to be **inverted**, as xemu does;
  - without inverting, culling discards the walls and the floor seen from inside.
- **Second cause, skinning:**
  - with the winding already inverted, an object covered the whole screen in front of the camera;
  - it was a piece of Ryu drawn with **fixed-pipeline skinning**, which was not implemented;
  - without skinning, its vertices (in the local space of each bone) ended up stuck to the camera.
- **Skinning implemented** (register `SET_SKIN_MODE`, `0x0328`):
  - 1 or 2 = 2 matrices, 3 or 4 = 3 matrices, 5 or 6 = 4 matrices; in the odd modes the last weight is 1 minus the
    sum of the others;
  - the model-views are at `0x0480 + m·0x40` and their inverses at `0x0580 + m·0x40`, weighted with the weights of
    attribute 1;
  - the position is the sum in eye space multiplied by the **composite matrix**. With skinning, D3D leaves in
    it only the projection and viewport, and the projection register `0x0440` is left at zero. #53 used `0x0440`, and
    the skinned pieces (shoulders, neck, torso) disappeared; it was fixed in #54;
  - without skinning the composite matrix is still used.
- **Result:** the temple appears complete and the scene matches the xemu capture.

## Asynchronous pusher (like the real GPU)

- **Before:** each write to `PUT` blocked the game thread while the render thread processed the whole
  kick, about 38 times per frame. Each round trip between threads cost about 0.17 ms (about 6.6 ms per frame), and
  the game logic did not overlap with drawing.
- **Now:** `pgraph_kick` records the new `PUT` and returns. The render thread is the pusher: it consumes
  `[GET, PUT)` in parallel with the game and does the `FLIP_STALL` pacing itself.
- **What D3D needs from an asynchronous GPU:**
  - **A real `GET`**, word by word (`pb_state.cur`). `D3D_BlockOnTime` computes how far the GPU is and,
    if it is more than 9 KB behind, patches the push buffer further ahead. With a lagging `GET` it would patch what has
    already been passed and wait forever.
  - **`NO_OPERATION(5)`:** that patch is `WAIT_FOR_IDLE` plus `NO_OPERATION` with parameter 5, a GPU
    interrupt. D3D sleeps on the device's KEVENT (+0x773 words), which its ISR signals on arrival. It is
    emulated by signaling it when the method is processed (`ngb_ke_set_event`).
  - **`PGRAPH_PATT_COLOR0` (`0x400B10`):** D3D writes a marker with the pattern object (class `0x44`, method
    `0x310`) and, when waiting for the GPU, compares its bits 2-6 with the fence. The pusher stores it when processing it.
- **Result** (deterministic demo, 200 s): no hangs or push buffer errors.
  - Gameplay goes from 45-53 to 54-60 fps.
  - The montage video runs at 30 fps, its source rate.
  - Clean audio: 0 dropped frames.

## Uniforms by groups with change stamps

- **Measurement** (deterministic demo, gameplay): the uniforms cost **3.1-5.3 ms per frame**. Almost every draw
  (400-560 of 450-600) re-uploaded everything, about 250 vec4 in the fixed pipeline, because the hash of the registers included
  the model-view, which changes on every object. The hash itself read about 3.3 KB per draw.
- **Change:**
  - `gl_track` (`pgraph.c`) only counts a value different from the previous one as a change, because D3D resends
    lights and material with the same value. Each block of 16 methods (`PGL_BLOCK`) and each constant `c[i]` store
    the `stamp` clock stamp of their last change. The stamps are 64-bit: with 32 bits they would wrap in minutes.
  - `pgraph_gl.c` groups the uniforms (`k_ug`: composite, model-view, inverses, texture matrices, planes,
    lights, material, fixed-pipeline integers, psC and bump). Each program uploads a group only if its stamp changed
    since the last upload to that program. The rest (surface, fog, alpha test...) still uses a small
    hash.
  - `c[]` uploads only the ranges that change, because the locations of the elements are consecutive. `link_uniforms`
    checks this and looks up the active size of the array; if they are not consecutive, it is uploaded whole as before.
- **Result** (`VERIFIED`, same demo and same phases): the uniforms drop to **0.8-1.5 ms per frame** and about
  19,000 vec4 per frame (before, about 100,000). The render thread's drawing goes from 8.6-15 to 6.7-9.5 ms. The
  captures show no changes.
- **Next in cost** inside drawing: uploading vertices (2.2-3.4 ms) and the draws (1.9-2.5 ms). Outside
  drawing, the rest of `pb_run` (about 6 ms) is the word-by-word push buffer decoding.

## Internal resolution scaling and anisotropic filtering (`VERIFIED` in the demo)

- **Option:** `--scale N` (1-4, fractional allowed) renders every surface at N times the size the game programmed,
  so 720x480 becomes 1440x960 ... 2880x1920. `--scale auto` uses the height of the window's monitor divided by 480:
  2.25 on a 1080p screen, so 1620x1080. In fullscreen each rendered row then lands on exactly one screen row. The
  launcher's Graphics page has an "Internal resolution" combo whose default is "Match my screen" (`[graphics]
  resolution = auto|1..4` in `ngb.ini`). Only the OpenGL renderer scales; the software rasterizer ignores it.
- **Scaled coordinates:** `px(v) = round(v * S)`. A span `[a, b)` maps to `[px(a), px(b))`, so scissors and viewports
  that touch at native size still touch at fractional scales.
- **Presentation** (`pgraph_gl.c → pgl_flip`, `present.c → display_rect`):
  - the picture now fills the window's height at 4:3 (1440x1080 on a 1920x1080 screen, with black bars at the
    sides). Before, the vertical scale was an integer, so a 1080p screen only got 960 rows;
  - a small shader replaces the bilinear blit. It takes four bilinear taps at a quarter of the footprint, which
    works like a box filter: exactly 2x2 texels at 2:1. At 4x on 1080p, 2880x1920 goes into 1440x1080, and the
    supersampling smooths the edges instead of shimmering;
  - `--fullscreen` (the launcher's "Fullscreen", default on) is a borderless window over the monitor, with the
    cursor hidden. Alt+Enter toggles it;
  - the loader is per-monitor DPI aware. Without that, at 125 % or 150 % scaling Windows draws the window smaller
    and stretches the bitmap, which blurs the picture whatever the internal resolution;
  - `NGB_GL_SHOT_WINDOW=1` together with `--debug-shots` also saves the window's back buffer
    (`<shot>-window.png`), to check the presentation without photographing the screen.
- **Widescreen is native** (`VERIFIED` in the demo). The game reads `ExQueryNonVolatileSetting(XC_VIDEO_FLAGS)`
  twice at boot. With `XC_VIDEO_FLAGS_WIDESCREEN` (`0x10000`, the dashboard's 16:9 TV setting) it renders
  anamorphic 16:9: a wider field of view in the same 720x480, meant for the TV to stretch. The disc also carries
  `2_VI_WIDE_B.sfd` next to `4_VI_STD_B.sfd`, see [[ngb-disc]].
  - `--widescreen` (the launcher's "Widescreen (16:9)", default on) sets that flag in the kernel and presents at 16:9,
    so a 16:9 screen has no bars.
  - `--scale auto` then covers the picture on both axes: `S = max(picture_w / 720, picture_h / 480)`. That is 2.67 on
    1080p (1920x1280, with a light vertical supersample) and stays 2.25 at 4:3.
  - The 2D title screen and menus are drawn for 4:3 and show stretched, as on the console with a widescreen TV.
  - Ultrawide (21:9) or 16:9 menus would need patching the game: that is the roadmap's H9+ item.
  - Without the flag the game is 4:3, with black bars at the sides.
- **What scales** (all in `pgraph_gl.c`): the surface textures (`create_surface_texture`), the viewport
  (`bind_target`), the draw and clear scissors (`apply_state`, `pgl_clear`), the feedback copy, the present blit,
  the screenshots and surface dumps, and `glPointSize`. A surface the CPU pre-filled is uploaded at native size into a
  temporary texture and stretched with a nearest blit.
- **What stays native, on purpose:**
  - `R.target_w/h` and the `surfaceSize` uniform. The vertex programs output native screen pixels and the shader
    turns them into NDC, which does not depend on the scale;
  - the `1/width` scale of render-to-texture on linear textures, because those coordinates are native texels;
  - the cache sizes in bytes and the CPU-write signature of surfaces. Nothing reads a surface back into guest RAM,
    so the game never sees the larger buffers.
- **Anisotropic filtering:** 16x (clamped to `GL_MAX_TEXTURE_MAX_ANISOTROPY`) on cached textures that have mipmaps.
  Single-level textures (HUD, fonts, video) do not get it, because there it would only blur.
- **Checks** (deterministic demo, `--debug-shots 300`, RX 6600). Side-by-side shots are in
  `NGB_DATA\comparisons\escalado-2026-10-22\`.
  - Edges and textures are clearly sharper at 2x and 4x. The lighting is the same, and the shadow acne fixed by
    the polygon offset does not come back: the shadow maps are scaled too.
  - The full-screen "ghosting" seen in some shots is the game's own motion blur (it shows up at 1x as well). The
    offset matches the camera motion, not a 2x magnification from the corner, which is what a scale bug would cause.
  - FPS with pacing: **60.0 at 1x, 2x and 4x**. With `--debug-shots`, 4x drops to about 58.7, because each 2880x1920
    PNG stalls the render thread. Without pacing the 3D windows run at 80-100 fps at every scale, so the
    bottleneck is still the CPU (push buffer decoding), not the GPU.

## Next

1. Go from the title screen to the main menu, and find out what the warnings and the video (audio) wait for.
2. 3D geometry: vertex programs ([[shaders-nv2a]]) and drawing from vertex arrays (`DRAW_ARRAYS`/`ARRAY_ELEMENT`)
   on the OpenGL backend.
