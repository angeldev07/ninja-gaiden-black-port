---
tags: [ngb, reverse-engineering, mods]
updated: 2026-10-22
status: draft
---

# Mods: outfits and Intercept

Goal: port options, such as memory patches (never touching the copy of the game), that enable
the outfit selector with everything unlocked, including the "futuristic" one, and that restore the Intercept technique.

## What is on the disc and in the XBE (`VERIFIED`)

- **Ryu's outfits:** 7 alternative containers, `chr_ryua.afs` … `chr_ryug.afs`, in addition to `chr_ryu.afs`. They show
  on the sheet in [[characters]].
  - `load_img.bin` lists them one after another in its file table.
  - The game opens all 8 at startup (log of the 2026-10-21 run, lines 7668-7822).
  - In game it copies the chosen outfit to the Z: cache as `z:\chr_ryu.afs`.
- **Public guides list 5 unlockable outfits** (Comic, normal, Red Muffler, DOA and Ryuken), so there are
  spare files. Candidates for the "futuristic" one: `ryue` (red and black armor) and `ryub` (white with armor).
  `UNVERIFIED` until they are loaded in the game.
- **Selector:**
  - messages "A new costume has been added. Hold the **Right** Trigger when selecting NEW GAME…" (message
    table `0x4685a8`, 6 languages) and another like it with the **Left** Trigger (`0x468590`);
  - in Missions mode, "@BY Costume" (messages `0x46`/`0x47` at `0x44d568`). The entries are `{u32 id, 6 pointers
    per language}` and are used by ID, not by pointer: Ghidra sees no references.
- **Intercept:** the technique from the Hurricane Packs of the 2004 Ninja Gaiden, removed in Black. Its name is still
  on the "Ultimate Techniques" page of the technique list (`0x44d908`, 6 languages). `UNVERIFIED` whether the code of
  the technique is still there.
- **System data:**
  - `TDATA/5443000d/system.dat`: 0x550 bytes, encrypted (high entropy), with an XcSHA signature over `+0x14` (0x53C
    bytes);
  - it is read into a heap buffer that is freed right away, and the decrypted data lives elsewhere;
  - the unlocks should be in there. Save games (`UDATA/.../save000.dat`, 95,400 bytes) belong to the user:
    read-only.
- **Development leftovers:** the `debug` AFS carries test stages (`s_m_edit`, `s_chrput`, `s_r_view`…).

## Tool: `--dump-ram FRAMES` and F10

- Dumps the game's writable RAM: the XBE image, the heap and the contiguous RAM, about 150 MB, next to the log,
  as `{u32 va, u32 len, bytes}`.
- To compare states: two dumps separated by a known change.
- **First attempt:** change the language in OPTIONS → General Settings, frames 3300 and 3600. There is too much noise; neither
  the language nor the game clock (2:29 → 2:30) can be isolated with a single pair.

## Next step

- Iterative scanning, Cheat Engine style: several dumps changing the language back and forth, and keep the
  bytes that follow the pattern. That locates the options structure, which in Team Ninja games usually shares a block with
  the unlocks.
- Static alternative: the routine that decrypts `system.dat`. It is after the XcSHA calls of thread T2
  (`0x32D0F4`…); we need to climb up to its caller in the game.
