---
tags: [ngb, reverse-engineering, runtime, audio, apu, h8]
updated: 2026-10-25
status: current
---

# Audio: the MCPX APU in the native runtime

DirectSound is statically linked into `default.xbe` (118 functions identified in H5, `DSOUND` library;
[[xbe-default]]) and talks directly to the audio hardware, the MCPX APU. The native runtime does not
emulate all of it: it has a fake DSP (H7a, [[gpu-nv2a]]) and a minimal, **silent voice processor (VP)**.
The code is in `port/src/apu/vp.c`.

## How DirectSound knows where a sound is (`VERIFIED` in Ghidra)

- `CMcpxBuffer_GetCurrentPosition` (`0x38344E`) calls `0x38262C`, which reads the position from the **voice table
  in RAM**:
  - each voice takes 0x80 bytes, and the table base is in `NV_PAPU_VPVADDR`;
  - the position (CBO, in samples) is at `+0x58`;
  - it is only read if the `NEW_VOICE` bit (20) of `+0x54` is off.
- `0x3811C7` converts samples to bytes: `(cbo >> 6) · blockAlign` in ADPCM (format `0x69`), and `cbo · blockAlign`
  in PCM.
- The Sofdec sound layer (`mwSnd`, from CRI) plays the video's audio in a DirectSound buffer, and **advances
  the video according to that position**. If the position does not move, the video freezes on its first frame.
- DirectSound drives the voices with **PIO methods** written to the MMIO page `0xFE820000`: `VOICE_ON` `0x124`,
  `VOICE_OFF` `0x128`, `VOICE_PAUSE` `0x140`, `SET_CURRENT_VOICE` `0x2F8` and `SET_VOICE_*` `0x300-0x3DC`. The
  names and offsets come from xemu's `apu_regs.h`, used only as a reference.

## The emulated VP

- PIO writes (which were already traced as RAM, `crash.c`) are passed to `vp_method`, which maintains the set
  of active voices and writes the `SET_VOICE_*` fields (format, pitch, LBO, CBO and EBO) into the table.
- The DPC thread calls `vp_advance` with the real elapsed time. For each active, unpaused voice:
  - it clears `NEW_VOICE`;
  - it advances the CBO at `48000 · 2^(pitch/4096)` samples per second;
  - at EBO, it returns to LBO if the voice has `LOOP`; otherwise it turns it off.
- Test: `port/tests/vp_test.c`, with 43 checks on a synthetic voice table.

## Result (`VERIFIED`)

**The intro video plays in full** (the stormy sea and then the mountains at dawn) and when it
ends the game carries on by itself. The leader reviewed the captures `vp2-pgraph-shot-09900.png` and `-10200.png`.

## Real sound (`port/src/apu/mix.c` and `audio_out.c`)

- **Sample reading:**
  - the address is `BA + CBO · block_size`, in a linear space of 4 KB pages;
  - each page is translated with the SGE table (`NV_PAPU_VPSGEADDR`, 8-byte entries holding the physical page);
  - formats: U8, S16, S24 and S32, and Xbox ADPCM (IMA, 36 bytes and 64 samples per channel and block).
- **DirectSound does not write the SGE table in RAM:** it programs it by PIO, with `SET_CURRENT_INBUF_SGE` (`0x804`)
  to choose the entry and `SET_CURRENT_INBUF_SGE_OFFSET` (`0x808`) to write the page. Without this, garbage was read.
- **Physical addresses outside the contiguous RAM:** Sofdec's audio buffer lives in normal process memory,
  and `MmGetPhysicalAddress` returns the identity for that memory (issue #31). The APU reader accepts those
  addresses if they are mapped.
- **Volumes:**
  - DirectSound sets them by PIO (`SET_VOICE_TAR_VOLA/B/C`, `0x360-0x368`): 8 attenuations of 12 bits, in steps of 1/64 dB,
    and `0xFFF` is silence;
  - slot b carries channel `b % channels` to the mixbin that `CFG_VBIN`/`CFG_FMT` indicates;
  - mixbins 0 and 1, front; 2, center; 4 and 5, rear (mixed down to stereo).
- **DirectSound internal voices** (in NGB, 64-67, looping over 32 samples): on the Xbox the DSP fills them
  with the effects mix. Without a DSP they would be noise at full volume, so they advance but are not heard.
- **Output:**
  - `waveOut` at 48 kHz, stereo and 16 bits;
  - the DPC thread mixes on each tick what corresponds to real time, and anything beyond 150 ms of
    queue is discarded;
  - `--mute` silences the sound;
  - `--audio-wav` saves the mix to `<log>-audio.wav`.
- **Verified:** the intro video's audio sounds between 40 and 58 s, with a level that varies second by
  second (RMS from 600 to 4,500) and an autocorrelation of 0.93-0.99, typical of a real signal. Noise would give ~0. The
  WAV is `NGB_DATA\runs\aud9.log-audio.wav`.

## Pending

- Reverb and effects (the GP/EP DSP is not emulated).
- Stream voices (SSL) and end-of-voice notifications.
- 3D voices (HRTF).
- Check by ear the menu music and the game's effects.

## Free voices: the SE2FE_IDLE_VOICE trap and the APU interrupt

- **Symptom:** the title demo hung when the first cutscene started, at frame 4690. The main thread
  was spinning in `IDirectSoundBuffer_GetStatus` (160 million `RtlEnterCriticalSection`).
- **What DirectSound waits for:**
  - the game (function `0x322F70`) calls `StopEx` and waits for `GetStatus` to stop returning PLAYING;
  - `CMcpxBuffer_GetStatus` reads the buffer flags (+0x12), and DirectSound only clears them when the APU signals
    that the voice became free.
- **The notification in hardware:**
  - after `VOICE_OFF`, `VOICE_RELEASE` or the end of a non-looping buffer, the voice processor sends the front
    end the method `SE2FE_IDLE_VOICE` (`0x8000`), with the voice as parameter;
  - the front end traps it (`FECTL` bits 8-11 = `0xF`, "requested"), leaves the method and parameter in
    `FEDECMETH` (`0xFE801300`) and `FEDECPARAM` (`0xFE801304`), and raises `ISTS` (`0xFE801000`) with
    `GINTSTS | FETINTSTS` (`0x11`).
- **The DirectSound ISR** (`0x381092`, connected to interrupt level 5):
  - reads `ISTS`; with bit 4 set, if `FECTL & 0xF00 == 0xF00` it dispatches the method;
  - `0x8000` frees the voice (`0x3809D7`).
- **Emulation:**
  - the voice processor queues each free voice (`vp_take_idle`);
  - the DPC thread delivers one per ISR call, at its IRQL;
  - while the call lasts, reads of those registers return the trap (`crash.c`, `mmio_access`);
    outside it, `ISTS` reads 0.
- **Result:**
  - the demo continues with its cutscene montage and reaches at least frame 10500;
  - in 200 s, 993 `VOICE_ON` against 46, because DirectSound now recovers the free voices;
  - 986 notifications delivered and none lost.

### A stale notice crashed DSOUND (2026-10-25)

- **Symptom (owner, mission mode, after 2.9 h of play):** `ACCESS_VIOLATION`, a read of `0x64` at
  `DSOUND + 0x3C89`, on the DPC thread (`h6-20261025-121530.log`; PAL `0x380A09`, same bytes, the library sections
  match at the same offsets).
- **The handler (`VERIFIED` in Ghidra, `0x3809D7`):** `this` is the APU object, the parameter the voice handle.
  - It does nothing if the handle is `>= 0x100`, if `this+0x84` is set, or if bit 23 of `+0x04` of the voice in the
    table is set.
  - Otherwise it reads the voice object `this[0x88 + 4·handle]` with no NULL check. It acts only if the notice is
    for the object's last hardware voice (`+0xC`, count at `+0x64`) and the object is still in its list (`+0x4C`).
  - Acting is `0x381F74`: it unlinks the voice from the hardware list, marks it free (`+0x65 = 0xFF`) and queues
    the DPC at `this+0x4C8` (routine `0x380EB2`), which finishes the release later.
  - The crash is that read with the slot already NULL: a notice for a voice that DirectSound had released.
- **Why the port produced it (`INFERRED`, matches the counters):**
  - the summary shows 66,373 notices for 65,960 `VOICE_ON`: some voices were notified twice;
  - a voice that ends at EBO and then gets `VOICE_OFF` before the notice is delivered was queued twice;
  - the DPC thread ran the DPCs before the ISRs, with no lock between them. A notice could arrive after the DPC
    that frees its voice, and a game thread could run between an ISR and the DPC it queued. On the Xbox,
    pending DPCs run as soon as the ISRs return, before any thread.
- **Fix:**
  - `vp.c` keeps one pending notice per voice (`idle_pending`), and the voice stays pending until the ISR returns;
  - `dpc_thread` (`impl_ke.c`) goes to `DISPATCH_LEVEL` when there is work, delivers the interrupts first, then
    runs the DPCs, including the ones the ISRs queued, and only then lowers.
  - It takes the lock only when there is work, so a game thread waiting at `DISPATCH_LEVEL` for the fake DSP
    does not block it.
- **Test:** `vp_test` covers a voice that ends and then gets `VOICE_OFF` before delivery: one notice, and a later
  `VOICE_OFF` notifies again. The crash itself does not reproduce on demand; it took one 2.9 h session.

## Choppy music: CRI's vsync thread without its priority (`VERIFIED`)

- **Symptom:** with sound, the music played in chunks although the output never ran dry (0 underruns and 0 dropped
  frames), and gameplay dropped to 40-58 fps. Sound effects were fine.
- **Measurement:**
  - The music is voice 68: 16-bit stereo PCM at 44.1 kHz (pitch `0xFE0B`), in a ring of 16,384 samples
    (about 0.37 s) that CRI fills.
  - The hashes of each 1,024-sample span were compared with those of the previous lap. Between 20 and 30 %
    of the spans played without having been rewritten, that is, repeated.
  - CRI wrote about 2,560 samples every 80 ms (about 32,000 per second) for a voice that consumes 44,100.
- **Chain (Ghidra):**
  - CRI's `mwSnd` layer has a function table at `0x433A00`. The position comes from `+0x38`
    (`IDirectSoundBuffer_GetCurrentPosition`) and the write is `+0x6C` (`Lock`, copy and `Unlock`).
  - The ADX server is driven by ADXM's vsync thread (`0x335710`): `D3DDevice_BlockUntilVerticalBlank`, servers
    and `ResumeThread` of CRI's main thread (`0x3353B0`), which decodes and suspends itself.
  - That thread ran about 13 laps per second instead of 60 (CRI's own counter at `0x489E38`).
- **Causes:**
  1. **Priorities ignored.** `KeSetBasePriorityThread` was a stub. CRI asks for high priority for its vsync thread
     and TIME_CRITICAL for the main one; the game's main thread reapplies it hundreds of times. All Xbox
     threads share one core, as on the console. Without priorities, the vsync thread waited a whole quantum
     behind other threads and skipped vblanks.
  2. **Vblank was not at 60 Hz.** The DPC thread used `GetTickCount64`, which advances in jumps of ~15.6 ms. A
     vblank "every 16 ms" fell every 31 ms: between 26 and 37 per second. Periodic `KTIMER`s were delayed the same way.
  3. **The hardware shared the game's core.** The whole process was pinned to one core, so the
     render thread (the GPU) and the mixing thread (the APU, at TIME_CRITICAL) took time away from the game. As soon as the game had
     its priorities, the audio thread sometimes ran out of CPU (11 underruns in 150 s).
- **Fix:**
  - `KeSetBasePriorityThread`, `KeQueryBasePriorityThread` and `KeSetDisableBoostThread` are applied to the host
    thread. The XAPI values (-2..2 and ±16) are the same `THREAD_PRIORITY_*`. The DPC thread runs at
    TIME_CRITICAL, because interrupts preempt everything.
  - The DPC thread uses a 1 ms QPC clock. Vblank is scheduled in microseconds and without drift, like the periodic
    timers.
  - Xbox threads are pinned to core 0 and the threads that emulate hardware (render and mixing) go to the rest
    (`ngb_pin_hardware_thread`), like the GPU and the APU, which on the Xbox work in parallel with the CPU.
- **Result** (deterministic demo, 150 s, with sound):
  - CRI's vsync thread makes 60 laps per second and no span of the music repeats;
  - 0 underruns;
  - gameplay runs at 60 fps in all windows (before, 40-58), with drawing at 1.4-2.6 ms per frame;
  - 0 push buffer errors.
- **Lesson:** the diagnostic counters altered the CPU distribution. An EIP sampler in the DPC thread
  dropped vblank to 30 per second and the music stopped failing. To measure this, prefer cheap counters inside
  the game itself (`0x489E38`) over threads that stop others.

## Menu sounds came out silent (`VERIFIED`)

- **Symptom:** nothing sounded when moving the menu cursor.
- **Measurement:**
  - each move turns on a voice (`VOICE_ON 68`): stereo ADPCM at 22 kHz (`fmt 0xA801E0E6`, pitch `0xEE0B`),
    about 0.28 s long and non-looping;
  - the voice advanced in full, but a dump of its samples gave 108,120 zeros.
- **Cause:** the sound is in the `.data` section of the XBE (`SetBufferData` on `0x00B05000`).
  `MmGetPhysicalAddress` places the XBE image in its physical window (`NGB_PHYS_XBE + VA`, `0x06B05000`), and
  DirectSound writes that page into the SGE table. `nv2a_phys` already knew that window, but `apu_phys`
  (`crash.c`) looked it up as a host address, did not find it and returned silence.
- **Fix:** `apu_phys` translates the XBE window the same way the GPU does.
- **Result:** the voice's samples have signal (peak 18,092) with the envelope of an effect: attack and decay
  in about 0.2 s. Any sound the game has in `.data` is now heard.
