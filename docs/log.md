# NGB Port Wiki: log

Append-only, newest at the bottom. Each entry starts with `## [YYYY-MM-DD] type | title`.

## [2026-10-20] ingest | Redump ISO converted to XISO
The Redump image (XGD2, game partition at `0x18300000`) was converted to XISO with
extract-xiso v2.7.1, downloaded from XboxDev because the earlier tool was no longer on
disk. The XISO weighs 6.34 GB and has 249 files. The original was left intact. See [[tools]].

## [2026-10-20] query | Port feasibility and precedents
We investigated [[opence]] (decompilation of Halo CE with debug symbols and D3D8 over GL), the
abandoned attempt [[decomp-ninja]] and Anthropic's terms of use. The Consumer Terms (§3,
in force since 2025-10-08, https://www.anthropic.com/legal/consumer-terms) only forbid reverse
engineering Anthropic's services. The Usage Policy (https://www.anthropic.com/legal/aup)
does not mention reverse engineering of third parties and forbids infringing intellectual property. This is a
reading of the texts, not legal advice: the real boundary is set by local law. We evaluated
[[rea]]: it does not accept XBE and on Windows it is read-only, so it stays as a helper.

## [2026-10-20] decision | Approach, tools and data
[[adr-01-port-approach]], [[adr-02-re-tools]] and [[adr-03-game-data-outside-repo]]. The project owner
delegated the technical decisions to the leader, with two conditions: document everything in this vault and
show tangible progress by milestones ([[roadmap]]).

## [2026-10-20] milestone | H0: brain and base
The git repo was initialized (no commits) with a `.gitignore` that blocks the game data. The vault
was created following the same vault standard: [[CLAUDE]], [[GOAL-ngb-port]], [[index]], this log
and the `.obsidian` configuration copied over. The XISO was extracted to `%NGB_DATA%\game\`.
The team was defined in [[team]] and [[agent-task-template]].

## [2026-10-20] ingest | Disc structure
[[ngb-disc]]: 206 AFS, 26 SFD, 5 XBE, 3 XPR and XACT. **All 206 `.afs` carry the `AFS\0` magic
(CRI Middleware)**, contrary to what [[decomp-ninja]] reported ("unknown formats").
`VERIFIED`: hexdump of 4 headers and parsing of the 206 with `afs inventory` (206/206 valid).
Two analysts were launched in parallel for H1: `tools/xbe_info.py` and `tools/afs.py`.

## [2026-10-20] decision | Version control delegated to the leader
[[adr-04-version-control]]: the GitHub repo `angeldev07/ninja-gaiden-black-port` is trunk-based,
with Conventional Commits, squash PRs, review posted as a comment (GitHub does not allow
approving your own PR), CI gates and a tag per milestone. Gotcha: git and gh are run outside the sandbox.

## [2026-10-20] ingest | Anatomy of default.xbe (H1)
`ngb_tools.xbe` analyzes the 5 XBEs on the disc ([[xbe-default]]). The essentials: the engine was called **"Kunai"**
(`c:\Kunai\~output\release_ltcg\Kunai.exe`, PDB with GUID `BAAFA588…`, which is not on the disc). It was compiled with
**MSVC 13.10 (VS.NET 2003) and XDK 5849**, in a retail LTCG build without asserts or engine-specific RTTI. It uses
**D3D8LTCG**, which will complicate identification by signatures in H5. It imports 145 kernel symbols (128 functions
and 17 data variables), which are the scope of the H6 HLE kernel. It links CRI middleware (ADX, AIX and Sofdec), from May and June 2005.
`NinjaTrilogy.xbe` and `NinjaTomoe.xbe` are the emulator of the NES Ninja Gaiden games (US and JP).

## [2026-10-20] milestone | PR #2 and PR #1 merged: quality gates and vault
`main` gains the `ngb_tools` package with ruff, strict mypy, CI on Ubuntu and Windows, and the
`ngb_tools.hygiene` guard, which went through three review rounds and closed two bypasses ([[adr-03-game-data-outside-repo]]).
The pending minor items are in issue #3. The repo only allows squash and `main` is protected
([[adr-04-version-control]]).

## [2026-10-20] ingest | AFS format and asset contents (H1)
`ngb_tools.afs` inventories the 206 AFS: 3,766 entries, all named, and valid invariants in 206/206.
**There is no general-purpose compression or encryption** in the 64 KiB window that was measured: only 2 entries of
high entropy and no zlib or gzip signature. ADPCM and DXT are known codecs. [[afs]], [[chunk-teamninja]],
[[kmd]], [[xpr0]], [[adx]] and [[wva]] were created. Quirk: the trailing `u32` of each AFS
attribute is not the size, but the header read as an array. `ryu.kmd` carries 19 textures (DXT1 and
DXT4/5), which are the target of H2.

## [2026-10-20] milestone | H1 closed (tag milestone/h1) and H2: first game textures as PNG
PRs #4 (AFS and `_paths`) and #5 (XBE) entered after 4 and 2 review rounds. The pending minor items
are in issues #6 and #7, and the tag `milestone/h1` is at `14fd808`.
**H2: `ngb_tools.xpr` exports the game textures to PNG.** `ryu.kmd` gives 19 textures (outfit, green eye, skin,
metal and effects), `menu.spr` 97 (Dragon Sword, frames and kanji) and `font.xpr` 1 (glyph atlas). The leader
reviewed them by eye. Findings in [[xpr0]]:
- `Data` is relative to `header_size`;
- the push buffer takes 24 B and not 20;
- `header_size` is not aligned to `0x800`;
- DXT is not swizzled.
In [[chunk-teamninja]]: the size field does not bound the slots. The table of Ryu's textures is in [[kmd]].

## [2026-10-20] milestone | H3: Ryu in 3D (glTF)
A spike resolved the geometry of `ryu.kmd`, validated with renders made by our own rasterizer:
- D3D FVF vertices (`0x112`/`0x116`/`0x042`);
- u16 triangle strips;
- the face is drawn from a push buffer;
- HIERARC: the vertices come in the space of their bone.
`ngb_tools.kmd` exports it to glTF 2.0 and OBJ. Ryu, Ayane, Murai and Doku's weapons come out complete; Doku's
body uses a descriptor variant that is not supported yet. Details in [[kmd]].
In addition a new policy was adopted, a single review round per PR ([[team]]), and PR #8 (H2) entered
`main` with the tag `milestone/h2`.

## [2026-10-20] milestone | H3 closed (tag milestone/h3) and H4: all the game's audio as WAV
PR #10 (`ngb_tools.kmd`, glTF) entered with a single review round; the minor items remain in issue #11.
**H4: `ngb_tools.audio`** decodes bgm 95/95, sse 317/317, 2,966 `.wva` records and the 7 of `Wave.xwb`. No
ADX is encrypted. The layout of [[wva]] was corrected: bit31 marks the size of the parameter block. It was validated
with spectrograms (music, voice and effects recognizable) and statistics. Details in [[adx]].

## [2026-10-21] decision | Session 2: state on resuming
- **PR #14** (H6, native loader) came out BLOCKING in its host-security review. The path mapping of the
  HLE kernel did not validate `..`, so it could write, create or delete outside `NGB_DATA`. Under the
  policy of [[team]], that does block. It is being fixed.
- Before the session 1 limit, the three branches in progress were saved as `wip` commits on origin:
  `feat/nv2a-min` (H7a: the D3D init passes and the first CLEAR and the first FLIP arrive; it hangs waiting for
  vblank), `feat/kmd-anim` (`--format glb` and the `.mtn` parser: 258 motions of Ryu) and `feat/stage-export`
  (stage parser: multipart descriptor and HITMDL). The handoff notes are in `NGB_DATA\handoff\`.
- **Ghidra 12.1.4 + ghidra-xbe** was installed with the owner's approval ([[tools]]) and H5 was launched.

## [2026-10-21] milestone | H6: NGB's code runs natively on PC
`ngb-loader.exe` (C, MSVC x86, `/W4 /WX`) loads `default.xbe` at `0x10000` inside a Win32 process and runs it
**natively**, with its own HLE kernel: 118 exports, 105 of the 128 functions the game imports. The game
creates its thread, initializes XAPI, mounts the drives, writes metadata to U:, formats the Z: cache, initializes the
USB devices and starts D3D8, up to the first read of the GPU (`0xFD001804`, NV_PBUS, at `EIP 0x003D171B`).
That is 285 kernel calls. The leader reproduced it independently. Decisions in [[adr-05-native-runtime]];
observation in [[native-boot]]. Next decision, H7: minimal NV2A emulation versus D3D8 HLE.

## [2026-10-21] ingest | Animated Ryu: MTN format deciphered
`ngb_tools.kmd` exports animations: `--motions bank[:ids]` on top of `--format glb`, with a humanoid rig that
solves the trunk FK and the 2-bone IK of the limbs. `ryu_anim.glb` includes idle, walk, run, jump,
guard with katana and spinning slash, all reviewed in `anim_preview.png`. The format is in [[mtn]]: 44 tracks,
signed 20-bit keys, units of 1e-4 and the limbs driven by IK targets.

## [2026-10-21] ingest | The game's 17 stages in 3D
`ngb_tools.stage` exports the 17 zones to `.glb`. Stage KMDs use a **multipart descriptor**, with the
vertices already in world space and no hierarchy; the same variant explains why Doku's body was not exported. The
HITMDL collision is also deciphered. Renders reviewed: Boff (city of Tairon and fortress), Hayabusa village and the
cathedral. Prop placement is in the `.sty` bytecode, pending. Details in [[stages]].

## [2026-10-21] ingest | Ryu with the Dragon Sword
`--attach` mounts a weapon on a bone with the `.itm` attachment table (records of state, bone, position and
rotation). Ryu's 10 weapons were found in `cmn.afs`. The pose of the katana stowed on the back confirms
the Euler order Rz·Ry·Rx. Details in [[weapons]].

## [2026-10-21] ingest | V8U8 and L6V5U5 bump textures
`ngb_tools.xpr` decodes the game's 358 signed bump textures, 0 failures ([[xpr0]]). With this the "texture without PNG"
warnings of 8 zones of [[stages]] disappear. Pending: have the glTFs use them as
`normalTexture` instead of base color.

## [2026-10-21] milestone | H5: Ghidra and XDK signatures. Render decision (ADR-06)
There is a persistent Ghidra project of `default.xbe`: 548 of its 9,118 functions are named thanks to
XbSymbolDatabase and FID. D3D8 is identifiable (97 entries, covering 91.5 % of the calls from `.text`).
But LTCG put about 900 inline state writes into the game, and `RunPushBuffer` executes raw push buffers from the assets.
So a **hybrid renderer** is decided: emulated PGRAPH, plus HLE in Swap, KickOff, fences and vblank
([[adr-06-hybrid-renderer]]). In addition, the exact vblank recipe that blocked H7a appeared (ISR `0x3D2040` → DPC
`0x3D2310`). Details in [[xbe-default]].

## [2026-10-21] ingest | Character gallery
`ngb_tools.gallery`: 49 of 62 characters as GLB, 20 of them with an animated idle, plus a contact sheet. Most
of the failures are the multipart descriptor. Details in [[characters]].

## [2026-10-21] ingest | Multipart descriptor in kmd: 59/60 characters
`ngb_tools.kmd` now reads the multipart descriptor used by the stages and 10 characters, and `stage` now uses the
same parser. Doku, army, aruma, sohei, tank, truck, ufo, whip, heli and s_aru export. Ryu and Ayane come out
byte-for-byte identical. Details in [[kmd]].

## [2026-10-21] milestone | H7a: the game reaches the warnings screen
With a minimal set of emulated NV2A registers, plus synthetic vblank and the push buffer, `default.xbe` gets through the
D3D8 initialization and submits 37,337 frames with 808,728 primitives in 30 s, still unrasterized. The textures
it loads are the "Warnings" caption and the font atlas, reviewed by the leader.
The real hang was an H6 bug: the TEB overlay overwrote `ClientId.UniqueThread` (`fs:[0x24]`), and as a result
Windows lost thread wakeups. Details in [[gpu-nv2a]].

## [2026-10-21] ingest | NV2A shaders to GLSL
`port/src/shaders/` translates the vertex programs (128-bit MAC+ILU, screen-space epilogue) and the register
combiners to GLSL 4.00. The 16 vertex programs in `default.xbe` decode. The `DOT_*` texture modes,
used by the 2 real pixel shaders, still need to be emulated. Details in [[shaders-nv2a]].

## [2026-10-22] milestone | H7: first frame of the game in a window
NGB's warnings screen shows in a PC window at 60 fps: the game's code running natively,
with the emulated NV2A, a software rasterizer and the Win32 presenter. `NtSuspendThread` had to be deferred
while the thread holds a loader lock, and the events that D3D clears in memory had to be synchronized. After about 40 s
the game goes to a black screen that waits for something. In addition, `.gitattributes` forces
CRLF on `.cmd` files: with LF and UTF-8, cmd.exe looped on returning from `call`. Details in [[gpu-nv2a]].

## [2026-10-22] ingest | H8: the gamepad reaches the game, but the hang is something else
HLE of the 8 XAPI input functions, with a deterministic script (`--input-script`), XInput gamepad and
keyboard. START and A reach the game (checked in its memory), but the black screen that follows the warnings does not
change. Hypothesis: it is a Sofdec video (`movie\*.sfd`) waiting for the audio clock, which does not advance without an APU.
Details in [[xapi-input]].

## [2026-10-22] ingest | OpenGL backend and shader validation
With our own OpenGL 4.6 backend (WGL, no dependencies), the game's 18 real shaders translated by
`port/src/shaders/` compile and link in the driver. `texconv` converts the NV2A textures (swizzle, P8,
DXT, signed, cube) the same way `ngb_tools.xpr` does, and `glstate` translates the NV097 state. It is not yet hooked into the
runtime. Details in [[shaders-nv2a]].

## [2026-10-22] milestone | The intro video plays
The black screen that followed the warnings was not caused by the audio or the gamepad. `GetExitCodeThread` reads
SignalState and ExitStatus of the ETHREAD, and the emulated kernel did not write them when a thread ended, so the
game waited forever for the save-game thread. With the fix, the game resets D3D and
plays its intro video (Sofdec) in the window. A research agent found it in Ghidra.
Lesson: on a hang, first find what the main thread is waiting for before assuming a cause. Details in
[[gpu-nv2a]].

## [2026-10-22] milestone | The game's title screen
After the warnings and the intro video (frozen on its first frame, skipped with START), the game
reaches the "PRESS START BUTTON" title screen. First `XcSHAInit/Update/Final` and
`XcHMAC` had to be implemented: our own SHA-1, which the game uses to verify its data. Still open are the audio, which probably
explains the frozen video and the repeated warnings, and getting from the title to the menu. Details in [[gpu-nv2a]].

## [2026-10-22] ingest | The intro video plays in full
DirectSound reads the position of each voice from a table in RAM that on the Xbox is advanced by the hardware (the APU's
voice processor), and Sofdec synchronizes the video with it. A minimal VP (`port/src/apu/vp.c`) that interprets the
PIO methods and advances the active voices with real time is enough for the video to advance and, when it ends, the game
to carry on by itself. Still no sound. The leader did this work in parallel with the GL render agent. Details in
[[audio-apu]].

## [2026-10-22] ingest | The "looping" warnings are the game in slow motion
With `--watch` (new monitoring of game variables) it can be seen that no wait is blocked: the cache
finishes in 1.5 s and the warnings scroll 0.3 px per frame, about 38 s at 60 fps. Since the game runs at 42 fps or
less with the software rasterizer, they last from 1 to 3 minutes. It is fixed with GPU rendering (H7c). In addition,
the loader asks the system for a 1 ms clock (`timeBeginPeriod`). Lesson: measure with `--watch` before assuming
a wait. Details in [[gpu-nv2a]].

## [2026-10-22] milestone | H7c: GPU rendering at 60 fps
PGRAPH draws with OpenGL (RX 6600): the title and the menu go from 16 to 60 fps, and the warnings last 40 s, which is their
design. The "flicker" after the warnings was the emulated NV2A kicking with the pusher stopped: it replayed the push
buffer that D3D had just freed. Fixed, the video, the Team NINJA logo and the title appear. Details in
[[gpu-nv2a]].

## [2026-10-22] milestone | The game makes sound
There is real mixing of the APU voices and output through `waveOut`: the intro video's audio plays. Three
pieces were missing:
- the SGE table, which DirectSound programs by PIO (`0x804`/`0x808`);
- the volumes, which also arrive by PIO (`0x360-0x368`);
- accepting the identity "physical" addresses that our `MmGetPhysicalAddress` returns.
The 4 internal voices fed by the DSP are silenced, because without a DSP they would be noise. Details in [[audio-apu]].

## [2026-10-22] milestone | 3D gameplay: the title demo
The title's gameplay demo, which the game plays with its own engine, shows in 3D at 35-60 fps and with sound:
Ryu fights with the Dragon Sword. The "Unable to load game settings" warning was also fixed: the emulated EEPROM
returned stack garbage, and the `system.dat` signature changed on every run. Still pending are the sky (flat
blue) and part of the floor. Details in [[gpu-nv2a]].

## [2026-10-22] ingest | The demo hang: D3D push buffer fixups
D3D marks with a `NO_OPERATION` with a special parameter the static push buffers whose JUMP back it cannot
rewrite yet; on the Xbox, the GPU stops and the interrupt routine patches it. Without emulating that, the emulated GPU
looped through old push buffers (up to 400 M words), overwrote the fence and hung the game when the demo
started. With the HLE of the patching, the demo runs at 55-60 fps without hangs. The leader diagnosed it with
`--watch`, kick traces and Ghidra. Details in [[gpu-nv2a]].

## [2026-10-22] ingest | Fixed pipeline with lights and shadow maps
Half the 3D draws use the fixed pipeline with lighting, which the GL renderer did not implement. Now there are
lights, material color, texgen and texture matrices, and the demo's stage appears lit. The shadow
maps (`0x2E` depth textures) are bound as render-to-texture and compared in the shader: the 46
draws that were being discarded now run. The sky and the floor are still pending. Details in [[gpu-nv2a]].

## [2026-10-22] ingest | RenderDoc: the floor was missing because of inverted culling
RenderDoc was integrated (`--rdoc-frame N`) and the captures were analyzed with its Python API. The *pixel history* of a
floor pixel showed 43 draws discarded by culling: `glstate` inverted the front-face winding, when
our projection already compensates for it. Without the inversion the demo's stage appears. Lesson: with GPU state,
a frame debugger answers in minutes what blind traces did not answer in hours. Details in
[[gpu-nv2a]].

## [2026-10-22] ingest | The demo's temple: inverted culling and fixed-pipeline skinning
- **The diagnosis of #52 was wrong:**
  - its change in `glstate` had no effect, because `pgraph_gl` set `glFrontFace` right after;
  - the improvement that was seen came from comparing different scenes.
- **What solved it:**
  - deterministic input (`t=…p`);
  - xemu captures made by the project owner as a reference;
  - RenderDoc: the target after each event and the input vertices.
- **Fix:**
  - the front face is inverted, as in xemu;
  - fixed-pipeline skinning (`SET_SKIN_MODE`) is implemented, whose absence left a piece of Ryu stuck to the
    camera covering everything.
- **Lesson:** never compare before and after without deterministic input; the same frame in two builds avoids
  false conclusions.

Details in [[gpu-nv2a]].

## [2026-10-22] fix | Complete characters: the skinning projection is in the composite matrix
- **Symptom:** the project owner saw characters with missing parts, as if they were transparent.
- **Diagnosis:**
  - the *pixel history* showed that no draw of the character touched those pixels;
  - a temporary matrix log showed that the projection register `0x0440` is zero, and that with skinning
    (`SET_SKIN_MODE` 1) the composite matrix contains only projection and viewport.
- **Fix:** the skinned position is the composite applied to the sum of the weighted model-views.
- **Lesson:** dump the real registers before assuming what D3D writes into them.

Details in [[gpu-nv2a]].

## [2026-10-22] fix | The demo gets past the first cutscene: APU free-voice notifications
- **Symptom:** when the demo's first cutscene started, the game looped waiting for
  `IDirectSoundBuffer_GetStatus` to stop saying PLAYING after a `StopEx`.
- **Cause:** DirectSound only marks a buffer as stopped when the APU ISR receives the
  `SE2FE_IDLE_VOICE` trap. Until now no APU interrupt was ever fired.
- **Fix:** a queue of free voices in the voice processor, and a level 5 interrupt from the DPC thread
  with simulated `ISTS`, `FECTL`, `FEDECMETH` and `FEDECPARAM`.
- **Result:** the demo continues, and the voices are also recovered; in 200 s it goes from 46 to 993 sounds.
- **New tool:** `H5DecompAt.java` (in NGB_DATA, outside the repo) decompiles routines that have no function
  in the analysis, such as ISRs and DPCs registered at run time.

Details in [[audio-apu]].

## [2026-10-22] perf | Asynchronous pusher: the game no longer waits for the GPU on every kick
- **Profile of the slow stretch:**
  - about 6.6 ms per frame in synchronous round trips between the game thread and the render thread (38 kicks per
    frame);
  - in addition, the game logic did not overlap with drawing.
- **Change:** now the render thread consumes the push buffer in parallel, like the GPU.
- **What D3D needed from an asynchronous GPU, discovered in three successive hangs:**
  - `PGRAPH_PATT_COLOR0`, a marker of the pattern object;
  - `NO_OPERATION(5)`, the interrupt that wakes `D3D_BlockOnTime`;
  - a real-time `GET`.
- **Result:** demo gameplay at 54-60 fps, versus 45-53. No push buffer errors and clean audio.

Details in [[gpu-nv2a]].

## [2026-10-22] perf | Uniforms by groups: only what changes is uploaded
- **Measurement:** in the demo gameplay, the uniforms cost 3.1-5.3 ms per frame. Almost every draw re-uploaded
  about 250 vec4, because the model-view changes on every object and was in the same hash as everything else.
- **Change:** change stamps per block of 16 methods and per constant, in the pusher. Each program uploads only
  the uniform groups whose stamp changed, and of `c[]` only the ranges written.
- **Result:** 0.8-1.5 ms per frame, about 5 times less data and identical captures. Drawing drops from 8.6-15 to
  6.7-9.5 ms per frame.
- **Next in cost:** push buffer decoding (about 6 ms) and vertex upload (2-3 ms).

Details in [[gpu-nv2a]].

## [2026-10-22] fix | Choppy music: priorities, 60 Hz vblank and the GPU and APU on other cores
- **Symptom:** with sound, the music played in chunks and gameplay dropped to 40-58 fps. With `--mute`, the fps were
  better.
- **Measurement:** 20-30 % of the spans of the music ring (voice 68) were repeated, because CRI wrote about
  32,000 samples per second for a voice of 44,100. Its vsync thread (`0x335710`) made 13 laps per second instead of
  60.
- **Causes:**
  - `KeSetBasePriorityThread` was a stub, so CRI's vsync thread did not have its high priority on the shared
    core;
  - vblank ran at 26-37 Hz because the DPC thread used `GetTickCount64`, with jumps of ~15.6 ms;
  - rendering and mixing shared the game's core.
- **Fix:**
  - thread priorities are applied to the host, and the DPC thread runs at TIME_CRITICAL;
  - the DPC thread uses a QPC clock, and vblank and the timers do not drift;
  - Xbox threads go to core 0, and rendering and mixing to the rest.
- **Result:** the music repeats no span, there are 0 underruns and gameplay runs at 60 fps, with sound.

Details in [[audio-apu]].

## [2026-10-22] fix | New game: chapter 1 no longer stays black
- **Symptom:** after the chapter 1 card, a black screen with the game drawing at 60 fps.
- **Cause:** when changing level, the game deletes an old AFS from its cache on Z: (`DeleteFile`). `NtSetInformationFile`
  rejected the deletion (class 13), and the game retried it endlessly behind the fade to black.
- **Fix:** deletion is allowed only inside the virtual hard disk in NGB_DATA.
- **Result:** chapter 1 is playable. It was closed with RenderDoc (the last draw was the fade) and with `--log-cap`
  (the `DeleteFile` loop).

Details in [[native-boot]].

## [2026-10-22] fix | Menu sounds are now heard
- **Symptom:** moving the menu cursor made no sound.
- **Cause:** the menu effects are in `.data` of the XBE. DirectSound puts in the SGE table the page of the XBE's
  physical window (`NGB_PHYS_XBE + VA`), and `apu_phys` did not translate it: every sample read 0.
- **Fix:** `apu_phys` translates that window, as `nv2a_phys` already did.
- **Result:** the cursor voice has signal and the envelope of an effect.

Details in [[audio-apu]].


## [2026-10-22] query | First long play session: up to chapter 6 and two render hangs
- **Owner's session:** 70 min (249,024 frames) plus 9 min. Reached the start of chapter 6 (The Monastery), and
  the save games load fine. Stable fps, except for one cutscene.
- **New tool:** `--rdoc-key`. F11 captures the next frame with RenderDoc.
- **Render hang (the "white chest" and the "red dragon"):**
  - Identical signature in both cases: `pb: stopped at 0x... without reaching PUT ...; GET = PUT forced`. After that, the
    frame counter does not advance. Vblank and audio carry on.
  - Run `h6-20261021-231324`: stopped at `0x018290D0` (frame 23675, the chest). Run `h6-20261021-232359`: stopped at
    `0x000097D0` (frame 249024, the dragon).
  - There are no pb errors or "huge kick". `pb_run` exhausted the cap of 4 M words looping through jumps (a
    jump does not count in `g_words`).
  - It is the same signature as the fixups hang (#50): a static push buffer whose JUMP back is not
    patched. It exits with GET = PUT, the semaphore or fence of the skipped stretch is lost, and D3D waits
    forever.
  - `INFERRED`: a race between the asynchronous pusher (#56) and D3D's patching (`FUN_003cd5e0` decides with GET whether
    the GPU has already passed). It is rare and grows with load (more `RunPushBuffer` per frame).
  - Next step: log the first jumps of the kick when it exhausts the cap, counting jumps too, and
    reproduce it.
- **In-game cutscene at 43-49 fps** (Rachel and Ryu, frames 204000-210600): `pb_run` 21-23 ms per frame, of which
  only ~9 ms is GL (vertices 3.9, textures 1.8, draws 1.7). About 13 ms is the word-by-word pb decode, with 35,000
  vertices per frame. The audio suffers because the game runs slowly. It is the next perf front.
- **F11 captures (5):** very light and flat walls and floors in the mansion and the village. It is the lighting problem
  already open ([[gpu-nv2a]]).

## [2026-10-22] milestone | Release v0.1.0: playable up to chapter 6
- Tag `v0.1.0` on `7796960`, with a GitHub release that attaches `ngb-loader.exe` and `ngb-core.dll`
  built from the tag (no game data).
- Return point before touching the pusher (the freeze) and the lighting: `git checkout v0.1.0`.

## [2026-10-22] feat | Launcher, icon and centered window
- **`ngb-launcher.exe`** (`port/launcher/launcher.c`, Win32 + GDI with no dependencies): dark theme inspired by
  the Bloodborne port's launcher.
  - Sections Play (path to the XBE and status), Graphics (`--renderer`, `--no-pacing`), Audio (`--mute`,
    `--audio-wav`), Controls (mapping, read-only), Advanced (`--rdoc-key`, `--lenient`, `--log-cap`) and Log
    (tail of the latest log in `runs`).
  - PLAY launches `ngb-loader.exe` without a console and Stop terminates it. Settings go in `ngb.ini`, next to the exe.
- **Original icon** (shuriken): `port/res/make_icon.py` (stdlib), embedded in the three binaries. The window
  loads it from `ngb-core.dll`, because the exe loses its PE header when the XBE is loaded at `0x10000`.
- **The game window** opens centered on the monitor of the cursor, with an icon and the title
  "Ninja Gaiden Black · native port".
- **Pending:** DPI scaling other than 100 %, keyboard navigation in the launcher, and closing the launcher
  with the game open not recording the session.
- It was done by a subagent (Sonnet) in a worktree; the leader reviewed and integrated it.

## [2026-10-22] fix | The render no longer freezes in long sessions (push buffer fixup race)
- **Symptom:** roughly once per hour of play (the chest, the red dragon), the render froze with
  `pb: stopped at X without reaching PUT`.
- **Cause (`VERIFIED` in Ghidra):**
  - `D3DDevice_RunPushBuffer` chains a new record to the last one if its next pointer is 0;
  - our HLE fixup, which runs on the render thread, read that next pointer and closed it in two steps;
  - if the game linked in between, the record was lost, its JUMP back stayed stale and the GPU looped
    up to the cap.
- **Fix:**
  - the close is atomic, and a late link is handled before the next jump (validated against D3D's last
    record, `dev+0x58`);
  - the ISR decrements the pending interrupts (`dev+0x50`), which previously never went down, and the NOP
    `0xE` is handled.
- **Test:** 150 s demo and 20 min test (70,969 frames): 0 pb errors and 0 stops. The definitive confirmation
  will be a long play session by the owner.
- **Mistake avoided along the way:** the first version of the late link read ring records that had already been rewritten
  (`invalid record 0x09AB09BA`). That is why the chain is now validated.

Details in [[gpu-nv2a]].

## [2026-10-22] ingest | Mods: hidden outfits, Intercept and `--dump-ram`
- There are 7 alternative Ryu outfits on the disc, and the guides list only 5 unlockable. The selector opens
  by holding RT or LT on NEW GAME, or with Y in Missions.
- The text of Intercept, removed in Black, is still in the technique list.
- `system.dat` is encrypted.
- New tool: `--dump-ram FRAMES` / F10, which dumps the game's RAM to compare states.
- Details in [[mods]].

## [2026-10-22] feat | Launcher v2: own Ninja Gaiden aesthetic, English and Spanish, import the ISO
- **Art:** procedural and original (own chrome lettering, red fog, speed lines, shuriken with a red slash)
  and a new icon. The official logo is not used: it is trademarked art.
- **Languages:** English and Spanish, defaulting to Windows' language, with a selector saved in `ngb.ini`.
- **Importing the game:** from an XISO or Redump image (own XDVDFS reader in `port/launcher/xdvdfs.c`, with the
  partition at `0x18300000` in Redump) or from an extracted folder. It validates the title of the XBE certificate.
- **Import test:** with the real XISO, 249 files (6.3 GB) in 6.7 s, byte-for-byte identical to
  `NGB_DATA\game`. ISOs of other games give a clear error. The Redump layout was tested with a hand-built
  image.
- **Data folder:** the one in `ngb.ini`, then `NGB_DATA`, then `%USERPROFILE%\ngb-data` if it already holds the game, and otherwise
  `%LOCALAPPDATA%\NGBPort`. PLAY passes `NGB_DATA` to the loader.
- It was done by a subagent (Sonnet) in a worktree; the leader reviewed and integrated it.

## [2026-10-22] chore | English translation, phase 1: `port/`
- Everything under `port/` is now in English: C comments, log and help strings, tests, `build.cmd` and the README.
  The goal is an open-source release.
- The loader log is English now. The Python matchers in `tests/port/` and the launcher's log colouring follow it.
- The project is no longer described as "pedagógico". The legal note reads: unofficial fan port, own copy of the
  game required, not affiliated with Koei Tecmo or Microsoft.
- Checked: zero warnings, 807 Python tests pass, and code and format specifiers are identical once comments and
  strings are stripped.
- Next: phase 2 (Python tooling), then phase 3 (vault, root README, CLAUDE.md rules).

## [2026-10-22] chore | English translation, phase 2: Python tooling
- Everything under `src/`, `tests/` and `tools/` (Python, the Ghidra Java scripts, `.ps1`, `pyproject.toml`) is now in
  English: 183 files.
- CLI and report output changed. For example: `sin_nombre` → `unnamed`, gallery `fallo` → `failed`, and Markdown
  report headers in English. The pinned SHA-256 of `xboxkrnl_exports.tsv` changed with its header comments.
- Intentional non-ASCII fixtures stay (`ñ`, `téxture`, Shift-JIS `修正版`).
- Checked: 807 tests pass, and ruff and mypy are clean.

## [2026-10-22] chore | English translation, phase 3: the vault
- Every page of `docs/` is now in English, including this schema ([[CLAUDE]]), [[GOAL-ngb-port]], [[index]] and this
  log. Entries written before the translation keep their original dates and content.
- Pages with Spanish file names were renamed so that `[[wikilinks]]` stay unique and readable:
  `arranque-nativo` → [[native-boot]], `disco-ngb` → [[ngb-disc]], `entrada-xapi` → [[xapi-input]],
  `personajes` → [[characters]], `escenarios` → [[stages]], `armas` → [[weapons]], `herramientas` → [[tools]],
  `equipo` → [[team]], `plantilla-tarea-agente` → [[agent-task-template]], and the ADRs to
  [[adr-01-port-approach]], [[adr-02-re-tools]], [[adr-03-game-data-outside-repo]], [[adr-04-version-control]],
  [[adr-05-native-runtime]] and [[adr-06-hybrid-renderer]].
- Open-source cleanup: the "pedagogical, personal project" framing was removed (the project is an unofficial,
  non-commercial fan port that requires your own copy of the game), machine-specific paths became `%NGB_DATA%` or
  generic descriptions, and the project rules now say that code, comments and docs are in English.
- Quoted CLI and log output was updated to match the English output of the tools and the loader.

## [2026-10-22] fix | Fixed-function lighting matches xemu: no more washed-out scenes
- `SET_MATERIAL_EMISSION` (`0x03A8`) was added as a color. It scales the vertex color, and D3D sets it to
  (1,1,1) for vertex-colored ambient, so those objects went white (the "white chest").
- Rewrote `k_ff_vs` after xemu's `vsh-ff.c`:
  - color-material sources, the rational specular through `SET_SPECULAR_PARAMS`, the spot cone, local eye and
    light range;
  - specular folding without `SEPARATE_SPECULAR`;
  - `SPECULAR_ENABLE` / `ALPHA_FROM_MATERIAL_SPECULAR` in the pixel shader for both pipelines.
- Demo: mean frame brightness ~130 → ~70. The scene is dark and contrasted, like the console.
- Detail in [[gpu-nv2a]].

## [2026-10-22] chore | No more automatic screenshots; `runs\` keeps the last 10 sessions
- The loader used to save a PNG every 300 frames and dump the first textures on every run. A long play session
  filled `runs\` with 1.6 GB, almost all of it PNGs.
- **Now:**
  - `--debug-shots N` turns the periodic PNGs and texture dumps on; they are off by default. Benchmarks and
    comparisons pass `--debug-shots 300`.
  - With the default log name, the loader deletes the files of the sessions older than the 10 most recent. It
    keeps the `.rdc` captures, and it never touches `hdd\` (the saves) or `shots\` (F12).

## [2026-10-22] fix | Polygon offset: the dot/moire acne on chapter 1 walls is gone
- The game sets `SET_POLY_OFFSET_FILL_ENABLE` and the `SET_POLYGON_OFFSET_SCALE_FACTOR`/`BIAS` pair about 4-5 times
  per frame (shadow-map passes), and we ignored them. The shadow compare then fought the surface itself, which
  showed up as regular dot and moire patches on the gorge walls.
- Now `glPolygonOffset(factor, bias · 2^24 / clip range)`, equivalent to xemu's in-shader depth offset for our
  24-bit depth buffer.
- The owner confirmed in chapter 1 that the patterns are gone and the lighting looks right.

## [2026-10-22] milestone | Release v0.2.0: launcher, lighting and stability (save point)
- Tag `v0.2.0` on `51864e5`. The GitHub release ships `ngb-launcher.exe`, `ngb-loader.exe` and `ngb-core.dll`, built
  from that commit. No game data.
- It is the save point before internal resolution scaling, the next session's work. To return to it:
  `git checkout v0.2.0`.

## [2026-10-22] feat | Internal resolution scaling (`--scale 1-4`) and 16x anisotropic filtering
- The game looked pixelated and jaggy because the GL render drew at the Xbox's native 720x480. Now `--scale N`
  draws every surface at N times its size (up to 2880x1920), and the launcher's Graphics page has an "Internal
  resolution" combo, saved as `[graphics] scale` in `ngb.ini`. Mipmapped game textures get 16x anisotropic filtering.
- Only the GL side grows. `surfaceSize`, the render-to-texture texel scale and the cache sizes stay native, so the
  game cannot tell. Details: [[gpu-nv2a]], section "Internal resolution scaling".
- Checked on the deterministic demo at 1x, 2x and 4x: sharper edges and textures, same lighting, no shadow acne.
  Pacing holds 60 fps at 2x and 4x on the RX 6600, and the 3D is still CPU-bound. Comparisons are in
  `NGB_DATA\comparisons\escalado-2026-10-22\`.
- Pending: the owner's chapter 1 test with the launcher.

## [2026-10-22] feat | 1080p done right: "Match my screen", the picture fills the height, fullscreen
- The owner asked for 1920x1080, the size of their screen. Three things stood in the way: the scale had to be an
  integer, the present also scaled by integers (so 1080p only showed 960 rows), and there was no fullscreen.
- `--scale auto` is now the monitor's height / 480 (2.25: 1620x1080 rendered). The picture fills the height at 4:3
  (1440x1080 with bars at the sides), downscaled with a box-filter shader instead of a bilinear blit.
  `--fullscreen` is borderless, and Alt+Enter toggles it. The loader is DPI aware. The launcher defaults to "Match
  my screen" plus fullscreen.
- Checked: the auto run logs `internal resolution 1620x1080 (scale 2.25, matching the monitor)` at 60 fps, and
  the window back buffer (`NGB_GL_SHOT_WINDOW`) shows the supersampled picture the right way up. The game stays
  4:3; true 16:9 is the H9+ widescreen work. Details in [[gpu-nv2a]].

## [2026-10-22] feat | Native widescreen: `--widescreen` reports a 16:9 TV and the picture fills 16:9
- The owner found the 4:3 bars ugly on their 16:9 screen. The game reads `XC_VIDEO_FLAGS` at boot, and with
  `XC_VIDEO_FLAGS_WIDESCREEN` (`0x10000`) it renders anamorphic 16:9 itself: a wider field of view, not a stretch.
  Before this the kernel always answered 0, which means a 4:3 TV.
- `--widescreen` sets the flag and presents at 16:9. `--scale auto` now covers the picture on both axes, which gives
  1920x1280 on 1080p (scale 2.67). The launcher has "Widescreen (16:9)", default on (`[graphics] widescreen`).
- Checked in the demo: the log shows `VIDEO_FLAGS = 0x10000` and `internal resolution 1920x1280`. The 3D shows more
  of the scene at the sides with correct proportions, and the window back buffer fills 16:9 with no bars. The 2D
  title screen is drawn for 4:3 and shows stretched, as on a real Xbox with a widescreen TV. Details in [[gpu-nv2a]].

## [2026-10-22] decision | Getting ready to go public: legal audit, governance and DCO
- The owner wants to open the repository. The work is split into six parts: audit, governance, launcher
  debugging and credits, release workflow, updater, and README plus player guide.
- **Audit** (two read-only passes over the tree and the whole history). No blockers: no game data, no
  Microsoft/Xbox keys (the EEPROM and kernel keys are zero on purpose), no XDK material, and no DRM bypass (signatures
  are neither checked nor forged, and the ISO reader only reads the user's disc). The launcher art and icon are
  generated by code.
- **Fixed:**
  - `THIRD_PARTY.md` credits xemu, nxdk, RenderDoc and the references;
  - [[wiki-llm]] is now our own summary with a link, instead of a verbatim copy of Karpathy's text;
  - two "public XDK" comments now name nxdk / XboxDevWiki;
  - the exe resources say "Unofficial" and carry the copyright of angeldev07 and contributors.
- **Governance:**
  - `CONTRIBUTING.md` with mandatory legal rules and the DCO;
  - Contributor Covenant 2.1 (`CODE_OF_CONDUCT.md`) and `SECURITY.md`;
  - issue forms (bug, compatibility, feature) and `CODEOWNERS`;
  - a `dco.yml` CI check. See [[adr-04-version-control]].
- **Identity:** the owner appears publicly only as `angeldev07`. One history rewrite (noreply email, personal paths)
  is planned as the last step before the visibility flip.

## [2026-10-22] feat | Launcher: Debugging section, short log by default, About page with credits
- **Sidebar:** Play, Graphics, Audio, Controls, Advanced, **About**. The log viewer is now a sub-view of
  Advanced (*View log*, with Back).
- **Advanced > Debugging:**
  - **Detailed log**, off by default. When it is off the launcher passes `--log-detail minimal`, and the log-cap
    combo shows only when it is on.
  - **Copy session info** puts an English block on the clipboard, ready for the bug form: version, Windows build,
    GPU line from the latest log, settings and last session.
- **Short log** (`port/src/logfilter.c`, tested by `logfilter_test.c`):
  - minimal mode drops the line of every kernel call and the `    · ` detail lines, except those that report a
    problem or the fps; it also turns off the PGRAPH method dumps;
  - the demo for 100 s writes **78 KB** instead of about 6 MB, and the stop summary and the perf lines stay;
  - in both modes the profile folder is written as `%USERPROFILE%`, with `\` and `/` alike.
- **About:** "Unofficial fan project by angeldev07 and contributors", buttons to the repository, the contributors and
  the license, the credits (xemu, nxdk, Cxbx-Reloaded and XboxDevWiki, envytools, RenderDoc; full list in
  `THIRD_PARTY.md`) and the legal notice. Nothing is added inside the game. The game window's title says
  "(unofficial)".
- `port/res/version.h` is now `0.3.0-dev`. It still said 0.1.0 after the v0.2.0 release; the release workflow will
  set it from the tag.

## [2026-10-22] feat | Tag-driven releases and the launcher updater
- Design: [[adr-07-releases-and-updater]].
- **Releases:**
  - Pushing `vX.Y.Z` runs `.github/workflows/release.yml`. It finds Visual Studio with vswhere, runs `build.cmd`
    and the tests, and packages with `ngb_tools.release`. The package is a deterministic zip with the three binaries,
    LICENSE, THIRD_PARTY and a README for players, checked by the hygiene guard, plus the `ngb-update.txt` manifest.
  - The workflow creates a **draft** that the owner publishes. `workflow_dispatch` runs a rehearsal with no release.
  - A local rehearsal with the real binaries gives a 0.9 MB zip and passes the guard.
- **Updater** (`port/launcher/update.c`):
  - Once a day it reads the latest manifest; the switch is Advanced > *Check for updates*.
  - The Play page shows "Version X is available" with *What's new* and *Update*.
  - The zip is downloaded to `NGB_DATA\updates`, its size and SHA-256 checked with BCrypt, and it is extracted with
    Windows' `tar.exe`.
  - The binaries are renamed to `*.old` and replaced, with rollback. The launcher restarts with `--updated` and cleans
    up.
- **Verified:**
  - `upd_test.exe` covers the manifest, versions, SHA-256 vectors, a whole local update and a tampered zip.
  - A real launcher, clicked through `--update-url` with a local fake v9.9.9, swapped its binaries, restarted itself
    and showed "Updated to version 9.9.9".
- **Gotcha:** Windows demands elevation for an unmanifested exe named `*update*`, so the C test is `upd_test.exe`.
- **Not verified:** the workflow itself has not run on GitHub yet. The first run will be a `workflow_dispatch`
  rehearsal.

## [2026-10-22] milestone | Release v0.3.0: internal resolution, native widescreen, updater
- The first `workflow_dispatch` rehearsal of `release.yml` (run 39867021002) was green on GitHub: vswhere found
  Visual Studio, and `build.cmd` (C tests included), the Python tests and the packaging all passed.
- `version.h` goes to `0.3.0` and the `v0.3.0` tag makes the first automatic draft release, which the owner
  authorized publishing.
- **In v0.3.0:**
  - internal resolution up to 4x, with "Match my screen";
  - native 16:9 widescreen, fullscreen and 16x anisotropic filtering;
  - the launcher's Debugging section, short log by default, the About page and its credits;
  - the governance and legal files;
  - the updater.
- The updater needs a public repository: anonymous downloads from a private repo's releases fail, and the launcher
  stays silent. It works for players once the repo is public. v0.2.0 users update by hand once.
- Next: `version.h` goes back to `0.4.0-dev`.

## [2026-10-22] docs | README for the public repo and a getting-started guide
- `README.md` rewritten for players and contributors:
  - CI, release and license badges, plus a launcher screenshot;
  - features, a four-step quick start, status and known gaps, building, layout, contributing, credits, license and
    the legal notice.
- New [[getting-started]] for players:
  - requirements and what the launcher accepts from your own disc. It gives no ripping instructions, tools or
    download links, says the local law applies, and says it is not legal advice;
  - install with the SmartScreen note, importing, controls, saves in `runs\hdd`, the graphics settings and updates;
  - troubleshooting, how to report a bug with *Copy session info*, and an FAQ (Master Collection is Σ, not Black;
    no donations).
- The screenshots in `docs/images/` are of the launcher only, from a clean first run with no personal paths. The
  launcher art is original. PNG is not a game-data extension, so the guard lets them through.

## [2026-10-22] chore | No personal paths in the tree before going public
- The audit missed some `C:\Users\<owner>` paths still in the tree: the `port/README.md` example, the logfilter test
  data, a launcher comment and the Ghidra H5 scripts. They now use `%USERPROFILE%` / `%NGB_DATA%` or a neutral
  `player` name.
- `tools/ghidra/h5-run.ps1` now reads `NGB_DATA` (default `%USERPROFILE%\ngb-data`) and needs `GHIDRA_HOME`.
- **Publication plan:**
  - the current repository is renamed to `ninja-gaiden-black-port-dev`, archived and kept private, with its PRs;
  - a new repository with the original name gets the rewritten history: the noreply email and no personal paths;
  - this is because GitHub keeps the commits of closed PRs forever, so an in-place rewrite would not hide them.

## [2026-10-23] milestone | Clean repository for publication
- GitHub keeps the commits of closed PRs forever, so an in-place rewrite would still expose the old author email.
  Instead:
  - the development repository was renamed `ninja-gaiden-black-port-dev`, archived and kept private, with its 84
    PRs;
  - a new `ninja-gaiden-black-port` received the rewritten history.
- **Rewrite** (`git filter-repo` on a fresh clone of `main` and the tags):
  - every commit is `angeldev07 <noreply>`;
  - the owner's profile paths in old files became `%USERPROFILE%` / `<repo>`;
  - PR references in messages read `(dev PR N)`, so they cannot link to unrelated new issues.
  - A full bundle of the old history is kept outside the repo, under `NGB_DATA\backups`.
- **Settings recreated:**
  - squash-only merges and branch deletion on merge;
  - protection on `main`: the five required checks, linear history, no force push, admins included;
  - topics and Discussions.
- **Releases:**
  - v0.3.0 was rebuilt by the release workflow from the rewritten tag and checked (the hash matches the manifest,
    and no personal path is in the binaries);
  - v0.1.0 and v0.2.0 keep their notes only. Their old hand-built binaries embedded the owner's PDB path, so they
    are not republished.
- **Gotchas:**
  - GitHub does not trigger workflows when more than three tags are pushed at once, so re-push a release tag alone;
  - `printf` eats `\1` in `filter-repo` rules, so write the rules file with an editor.

## [2026-10-23] milestone | Played into chapter 9; the repository goes public
- The owner played v0.3.0 through chapter 8 and into the start of chapter 9 (until now, chapter 6 was the furthest
  checked). The README's status says so.
- **SmartScreen note** in the README and in [[getting-started]]:
  - it is not a virus detection: SmartScreen warns about unsigned programs with few downloads;
  - *More info > Run anyway*, or *Unblock* the zip;
  - check the zip with `Get-FileHash` against `ngb-update.txt`;
  - report antivirus false positives.
  - On the owner's PC, Defender was active with no detection recorded, and Smart App Control was off.
- **Planned:** code-signing the releases (SignPath Foundation, which is free for open source, or Azure Trusted Signing)
  in `release.yml`.
- With the owner's confirmation, the repository is made public.

## [2026-10-23] feat | NTSC-U release support: library code found by section, console region from the XBE
- The NTSC-U `default.xbe` (version 5, 4,571,136 bytes) stopped at the warnings screen: the XAPI input hooks were
  tied to PAL VAs, so the pad never reached the game.
- **Found:** every XDK library section (`D3D`, `XPP`, `DSOUND`...) has the same size and bytes as in the PAL XBE, at
  another base. `port/src/libaddr.c` (plus `libaddr_test.c`) moves reference VAs by section and offset, and the
  three prologues that hold the kernel thunk address are compared with a mask. See [[xapi-input]].
- **Console region:** an XBE that may run in North America (game region bit 0) now gets an NTSC-M / region 1
  console. The PAL XBE (region 0x4) keeps the PAL console as before.
- **Launcher:** it recognizes the game by title id `0x5443000D`, with the title name as a second chance.
- **Verified:**
  - NTSC-U: warnings, title and demo gameplay, with all 8 XAPI functions patched;
  - PAL: unchanged;
  - an XBE with the game's own sections modified and an extra section also boots and runs the demo, because only
    the library sections have to match.

## [2026-10-23] milestone | Release v0.4.0: NTSC-U release support
- `version.h` goes to `0.4.0`, and the `v0.4.0` tag makes the draft that the owner asked to publish.
- **Main change:** the port runs the NTSC-U release as well as the PAL one (see the previous entry).
- **Also:**
  - the README and the guide cover play into chapter 9 and explain SmartScreen;
  - no personal paths remain in the tree.
- **First real updater release:** a v0.3.0 launcher should offer v0.4.0 on its daily check.
- Next: `version.h` goes back to `0.5.0-dev`.

## [2026-10-24] fix | Launcher: a new data folder brings its own game; the card shows the release
- **Bug:** choosing another data folder kept the previous `[game] xbe` path when it still existed. The launcher
  then ran the old folder's game with the new folder's saves and virtual hard disk. The owner switched to a folder
  holding an NTSC-U copy, and every session still booted the PAL XBE (`game region 0x4` in the logs).
- **Fix:** when a data folder is chosen, its own `game\default.xbe` wins if it exists (`adopt_default_game(1)`). At
  start-up nothing changes, so a folder used in place keeps working.
- The game card says which release it will run: "Verified: this is Ninja Gaiden Black (PAL)" or "(NTSC-U)", from
  the certificate's game region (bit 0, the same rule the loader uses for the console).

## [2026-10-24] milestone | Release v0.4.1: the data folder fix
- A patch release with the launcher fix above (a new data folder brings its own game; the card shows PAL or
  NTSC-U), at the owner's request. `version.h` goes to `0.4.1` and then back to `0.5.0-dev`.

## [2026-10-24] fix | Vertex programs see the viewport: the water in chapter 12 is drawn again
- **Symptom (owner, NTSC-U build, chapter 12):** the water surface was invisible. Ryu waded through it, and
  everything under it showed as if there was no water.
- **Evidence:** RenderDoc captures taken with the launcher's capture key.
  - The water draw samples a reflection texture that is rendered correctly (the mirrored corridor). Its pixel
    shader uses BUMPENVMAP, which is emulated.
  - The draw changed no pixel. Post-VS, all 469 vertices were at (-w, -w, -w, w): the mesh collapsed into a corner.
  - Its vertex program ends with D3D's epilogue (`oPos * c[58]`, `RCC`, `+ c[59]`), and c[58] and c[59] were 0.
- **Cause:** on the NV2A, c[58] and c[59] are the viewport registers. `SET_VIEWPORT_SCALE` and
  `SET_VIEWPORT_OFFSET` write them, and the port only stored those methods for the fixed pipeline. Every vertex
  program with the epilogue drew nothing, so other missing effects may come back too.
- **Fix:** the method tracking moves from `pgraph.c` to `port/src/gpu/pgl_track.c` (no GL, testable) and copies
  both methods into the constants ([[shaders-nv2a]]). New `pgl_track_test` (14 checks). The 150 s PAL demo shows
  no regression: 0 pb errors, 64 programs, 0 errors.
- **Also fixed:** with the GL render, F12 saved the software frame, which is black. Now the render thread saves the
  next flip (`gfx_take_shot_request`).
- **Still open:** the freezes seen at the end of the first Doku fight (chapter 11), where the picture stops and the
  audio goes on. The logs show the GPU still drawing 200-500 primitives per frame at 60 fps, with no pb errors, so
  the game logic waits on something while the render keeps redrawing. The minimal log does not say what. A
  frozen-picture detector with a per-thread dump is the next step.

## [2026-10-24] fix | Shadow maps: texel addressing and the border color
- **Symptom (owner, right after the viewport fix):** hard-edged dark wedges on walls and floors in chapters 11 and
  12. The PAL demo shows them too at frame 6000, which makes the case deterministic.
- **Cause:** the 19-instruction shadow pass draws for the first time, now that vertex programs see the viewport.
  Two bugs were hiding behind it:
  - the linear depth formats (`0x2E`-`0x31`) never got the `1/width` texel scale, because texconv does not
    describe depth formats;
  - `SET_TEXTURE_BORDER_COLOR` was never passed to GL. The game samples the map in BORDER, so outside the light's
    frustum it read depth 0 and shadowed everything.
- **How it was found:** RenderDoc scripts (`qrenderdoc --python`) for post-VS data, pixel history, raw depth
  values and uniforms, plus diffs of the render target after each draw. A temporary shader mode then wrote out the
  shadow coordinates and the depth read: outside the map it read 0.
  - Discarded on the way: a winding inversion (both paths end with the same NDC Y); clamping linear textures (xemu
    binds them as `GL_TEXTURE_2D` with the normal wrap); and rounding the compare to integers (no change).
- **Fix:** `stage_texture` scales linear depth textures by texel, and `bind_textures` sets `GL_TEXTURE_BORDER_COLOR`
  from `0x1B24` when a stage is in BORDER. The demo frame goes from about 95,000 darkened pixels to 429.
  Details in [[gpu-nv2a]].

## [2026-10-25] fix | Heat haze over the lava: DOTPRODUCT and DOT_ST texture modes
- **Symptom (owner, chapter 12, the lava caverns):** opaque black patches with irregular edges covered the scene,
  like black fog.
- **Evidence:** the two RenderDoc captures of the session (`h6-20261025-111648-rdoc_*.rdc`) show the patches.
  - A diff of the main render target after each draw points at 5 fixed-pipeline quads (`SrcAlpha / InvSrcAlpha`)
    that sample a 512x256 render target whose RGB is all 0.
  - That target is written by one draw: a 12-instruction vertex program and a pixel shader with t0 = a distortion
    map, t1 = `DOTPRODUCT` and t2 = `DOT_ST` over the color buffer. The generated GLSL had
    `t1 = t2 = vec4(0.0); /* mode ... not emulated */`, so the target came out black.
- **Not a regression of the viewport fix:** the program reads only c[0] and c[1], and the quads are fixed-pipeline.
- **Fix:** `psh.c` emulates `DOTPRODUCT` and `DOT_ST`, with the `SET_DOT_RGBMAPPING` mappings 0-3. Details in
  [[shaders-nv2a]]. Replacing the shader in the captures brings back the lava cavern (comparison images in
  `%NGB_DATA%\comparisons\ch12-heat-*.png`). Tests: `shaders_test` 242 checks, `gl_test --stress` 66 of 66,
  the 150 s PAL demo with 0 pb errors and 0 program errors (the demo has no heat haze).
- **Still open:** a band about 11 px wide at the right edge stays dark under the heat haze. The heat quad does not
  cover the last column of its target with GL's pixel centers (half-pixel offset, [[shaders-nv2a]] § Pending).

## [2026-10-25] milestone | Release v0.5.0: the whole story mode
- **Played through:** the owner finished the whole story mode with the launcher build of the heat haze fix. No more
  visual defects showed in chapter 12, and the rest of the story played well overall, with an occasional glitch.
- **Main changes since v0.4.1:** vertex programs see the viewport (the water in chapter 12), shadow maps with texel
  addressing and the border color, F12 screenshots of the GL render, and the heat haze over the lava (`DOTPRODUCT`
  and `DOT_ST`).
- **Known gaps, in the README:**
  - fights with many enemies in the mission mode drop below 60 fps;
  - a dark band at the right edge under the heat haze;
  - one crash in the mission mode after 2.9 h of play, an `ACCESS_VIOLATION` in the game's `DSOUND` library code
    on the port's DPC thread (`h6-20261025-121530.log`). It is the next task.
- `version.h` goes to `0.5.0` and then back to `0.6.0-dev`.

## [2026-10-25] fix | Mission mode crash: a stale free-voice notice in DSOUND
- **Symptom (owner, mission mode, after 2.9 h):** the game closed with an `ACCESS_VIOLATION`, a read of `0x64` in
  the game's `DSOUND` code (`DSOUND + 0x3C89`, PAL `0x380A09`) on the port's DPC thread.
- **Cause:** the free-voice handler (`0x3809D7`) read the voice slot of a handle that DirectSound had already
  released. The port could notify a voice twice (it ends, then gets `VOICE_OFF` before delivery: 66,373 notices
  for 65,960 `VOICE_ON` in that session), and the DPC thread ran DPCs before ISRs with game threads free to run in
  between. Details in [[audio-apu]].
- **Fix:** one pending notice per voice until its ISR returns (`vp.c`), and the DPC thread delivers interrupts and then DPCs in one
  `DISPATCH_LEVEL` section (`impl_ke.c`), as on the Xbox.
- **Tests:** `vp_test` 43 checks (new case for the double notice). The 150 s PAL demo: 8973 frames, 0 pb errors,
  590 `VOICE_ON` and 585 notices, none lost. The crash itself is not reproducible on demand; the owner's mission
  mode play is the real check.

## [2026-10-25] fix | Movies at 30 fps: subtitles no longer show twice
- **Symptom (owner):** in the pre-rendered movies the subtitles came back a second time and the voice seemed out of
  step; the owner suspected the movies were meant for 30 fps, not 60.
- **Cause (`VERIFIED` in Ghidra and with `--watch`):** the movies are 29.97 fps and the game counts each subtitle's
  time on screen in 30 fps frames. On the Xbox the movie loop waits for the second field of an interlaced frame;
  in the port the kernel reported no AV cable, so D3D had no display mode (mode flags `0xFFFFFFFF`), and the field
  port `0x80C0` read 0. The loop ran at 60 and each subtitle ran out halfway and came back. Details in
  [[gpu-nv2a]].
- **Fix:** `AvSendTVEncoderOption` reports a composite cable with the console's standard and video flags
  (`eeprom_av_capabilities`, tested in `eeprom_test`); port `0x80C0` alternates the field each vblank; a PAL
  console reports PAL60, because without it D3D emulates 60 Hz with a timer the port does not run.
- **Checks:** the New Game movie at 30.0 fps on PAL and NTSC-U with no repeated subtitle (6 before); 60 fps again
  after it. 150 s PAL title demo: 0 pb errors, 63 programs (0 errors). `--widescreen` boots.

## [2026-10-25] fix | Busy scenes at 60 fps: the push buffer readable check and the vertex upload
- **Symptom (owner, mission mode):** 35-45 fps in "Giants of the Underworld" and other effect-heavy fights, worse
  the longer the session.
- **Cause (`VERIFIED` with the owner's log and `QueryPerformanceCounter` probes):** the render thread had no time
  left. A `VirtualQuery` per 4 KB page of push buffer (about 3 us each, slower as the session went on) took up to
  10 ms per frame with vertices sent inside the push buffer; the vertex upload copied every attribute of every vertex
  on its own (4.3 ms) and called `glBufferData` twice per draw (2 ms); RenderDoc, loaded with `--rdoc-key`, doubled
  the draw time. Two linear searches (texture offsets, texture cache) also grew with the session. Details in
  [[gpu-nv2a]].
- **Fix:** a bitmap of pages known readable (D3D's write-combine `MmSetAddressProtect` calls no longer empty it);
  vertex arrays copied as interleaved blocks into a persistent mapped GL 4.4 ring, indices too; hash lookups for
  the texture offsets and the texture cache. The launcher now says RenderDoc slows drawing.
- **Tools:** `--profile` (sampling profiler of the render and audio threads), word counts in the `perf:` line, the
  `gather` phase in `perf gl`, and `--input-script` up to 256 presses.
- **Checks:** the scripted Giants fight at 1920x1280 goes from 6.8 to 1.8 ms of draws and from 3.5 to 0.8 ms of
  decoding per frame, 60 fps with pacing to spare. 150 s PAL title demo pixel-identical where frames line up, 0 pb
  errors, 63 programs (0 errors). Native tests (`rcache_test` 61 checks) and 829 Python tests pass.

## [2026-10-25] fix | Stutter at 60 fps and bloom shimmer
- **Symptoms (owner, "Eternal Legend"):** frequent short drops at a 60 fps average, and flickering light streaks
  around the characters.
- **Stutter (`VERIFIED`):** the new hitch fields of the `perf:` line showed the synthetic vblank missing about one
  tick in 15 (32 ms gaps) when Windows 11 ignored the 1 ms timer resolution of a process without a visible window.
  The loader now opts out of power throttling (`SetProcessInformation`), and the render thread presents at most
  once per vblank instead of keeping its own clock. 0 frames over 25 ms in the same test.
- **Bloom (`VERIFIED` with RenderDoc and a Python model of the passes):** the game shrinks the scene to 256x256 with
  one tap per pixel; at the internal resolution that tap skips about 7 texels and thin bright details flickered
  through the game's sparse blur. Rendered color surfaces read with linear filtering now get mipmaps.
- Details in [[gpu-nv2a]]. 150 s PAL title demo: 60 fps, 8401 flips, 0 push buffer errors.

## [2026-10-25] fix | Black screen after the "Eternal Legend" cutscene: a missed D3D wake-up
- **Symptom (owner):** sometimes the screen stays black when the opening cutscene of "Eternal Legend" ends.
- **Cause (`VERIFIED` with a new render watchdog in the log):** `D3D_BlockOnTime` patched a `NO_OPERATION(5)` wake-up
  into the ring and slept; the pusher had already passed that spot, so it never ran and the game slept forever.
- **Fix:** with the queue drained and all of D3D's writes run, the pusher sets the event (`idle_wake`). Six runs:
  no hang, one rescue. Details in [[gpu-nv2a]].
