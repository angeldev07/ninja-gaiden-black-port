# Ninja Gaiden Black: native PC port

An unofficial fan project that runs **Ninja Gaiden Black** (original Xbox, PAL) natively on
Windows. There is no CPU emulator: the game's own x86 code runs inside a 32-bit Win32 process,
on top of a reimplemented Xbox kernel, an NV2A → OpenGL renderer and an APU audio engine.

> **You need your own copy of the game.** This repository contains no game data, no game
> assets and nothing from the Microsoft XDK.

## Status

The game is playable from the title screen through chapter 6, with saves, music, voices and
effects at 60 fps. Known gaps:

- Lighting is brighter and flatter than on the console.
- Floor reflections and the sword glow are missing.
- Some in-game cutscenes drop to about 45 fps.

The [roadmap](docs/target/roadmap.md) and the [development log](docs/log.md) have the details.

## Quick start

1. Download the latest release, or build from source (see below).
2. Run `ngb-launcher.exe`.
3. Click **Import game** and choose your Ninja Gaiden Black disc image (`.iso`, Redump or XISO)
   or a folder with the extracted files (`default.xbe`). The launcher checks that it is the right
   game and extracts it into its data folder.
4. Press **PLAY**.

Controls: an XInput gamepad, or the keyboard (the launcher's *Controls* page has the mapping).
F12 saves a screenshot.

## Building from source

Requirements:

- Visual Studio 2022 with the C++ x86 toolset;
- Python 3.13 for the tooling and the tests (stdlib only).

```bat
port\build.cmd
python -m unittest discover -s tests -v
```

`build.cmd` compiles with `/W4 /WX` and runs the C unit tests. The binaries end up in
`build\port\`:

| Binary | What it is |
| --- | --- |
| `ngb-loader.exe` | the loader |
| `ngb-core.dll` | the kernel, GPU and APU |
| `ngb-launcher.exe` | the launcher |

## Repository layout

| Path | Contents |
| --- | --- |
| `port/` | The native runtime in C: XBE loader, HLE kernel, NV2A/OpenGL renderer, APU, input, launcher |
| `src/ngb_tools/` | Python reverse-engineering tools: AFS, XPR, KMD, audio, XBE, galleries |
| `tests/` | Python unit tests |
| `docs/` | The project knowledge base (an Obsidian vault): formats, reverse-engineering notes, decisions |

New contributors should start with [`docs/GOAL-ngb-port.md`](docs/GOAL-ngb-port.md) and
[`docs/index.md`](docs/index.md).

## Legal

Ninja Gaiden is a trademark of Koei Tecmo. This project is not affiliated with or endorsed by
Koei Tecmo, Team Ninja or Microsoft. It is non-commercial. No game data is distributed. The
`ngb-hygiene` check (a pre-commit hook and CI) blocks game files from ever entering the
repository. The Xbox kernel declarations come from public sources (nxdk, CC0), never from the
leaked XDK.
