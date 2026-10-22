---
tags: [ngb, target, roadmap]
updated: 2026-10-20
status: current
---

# Roadmap: tangible milestones

Each milestone ends with something that **you can see, hear or run**. The status is
updated at the close of each session, and the detail of each close is left in [[log]].

| Milestone | Tangible result | Definition of done | Status |
| --- | --- | --- | --- |
| **H0** Brain and base | Vault, repo, extracted data, team defined | A new session can resume the project with [[GOAL-ngb-port]] | **done** (2026-10-20) |
| **H1** Map of the disc and the binary | XBE report (XDK version, libraries, kernel imports, sections) and inventory of the `.afs` | [[xbe-default]] and [[afs]] with `VERIFIED` data; tools with green tests | **done** (2026-10-20, tag `milestone/h1`) |
| **H2** First texture | A game texture exported to PNG, for example Ryu's | Texture format documented in `formats/`; exporter with a test | **done** (2026-10-20): 19 textures of Ryu, menu and fonts ([[kmd]], [[xpr0]]) |
| **H3** First model | Ryu's model exported to glTF/OBJ and visible in a viewer | Model format documented; exporter | **done** (2026-10-20): Ryu, Ayane, Murai and Doku's weapons in glTF ([[kmd]]) |
| **H4** First sound | A track from `bgm.afs` played as WAV | Audio format documented | **done** (2026-10-20): music (bgm), voices (sse), effects (.wva) and XWB to WAV ([[adx]], [[wva]]). `movie/ninja.aix` (multitrack AIX) is missing |
| **H5** Ghidra project and SDK signatures | Persistent Ghidra project; XDK libraries identified by signatures; % of functions named | Coverage metric in [[xbe-default]] | **done** (2026-10-21): 548 of 9,118 functions named; D3D8 identifiable ([[adr-06-hybrid-renderer]]) |
| **H6** Native boot | `default.xbe` running in a 32-bit Win32 process up to the first unimplemented kernel call, with a call log | Reproducible log; minimal HLE kernel | **done** (2026-10-21): 285 kernel calls, up to the first NV2A MMIO ([[native-boot]], [[adr-05-native-runtime]]) |
| **H7** First frame | A game frame (logo or loading screen) in a PC window | Minimal D3D8 over GL/Vulkan | **done**: the game's warnings screen shows in a window at 60 fps ([[gpu-nv2a]]). Software rasterized ([[adr-06-hybrid-renderer]]) |
| **H8** Playable menu | Main menu with gamepad, audio and video | SDL3 input, audio, `.sfd` | **in progress**: the gamepad works (XAPI HLE, [[xapi-input]]) and the **title screen** is reached ("PRESS START BUTTON", [[gpu-nv2a]]). Missing: audio, animated video and getting past the title |
| **H9+** Decompilation and improvements | Functions in C with differential tests; widescreen, 60+ interpolated fps, high resolution | Per function or subsystem | pending |

H2, H3 and H4 (assets) do not depend on H5, H6 and H7 (execution), so they can advance
in parallel. The assets give visible results early and also reveal the
structures that the code uses later.

## Concrete next step

**H7, first frame.** We have to decide with data between emulating a minimal NV2A and doing D3D8 HLE
([[adr-05-native-runtime]] § Next decision). Beforehand **H5** is worth doing: the Ghidra project with ghidra-xbe, to measure
how much of D3D8LTCG is identifiable. It requires the project owner to approve installing Ghidra.
