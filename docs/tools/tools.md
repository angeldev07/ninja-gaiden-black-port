---
tags: [ngb, tools]
updated: 2026-10-20
status: draft
---

# Tools: how to use them

## extract-xiso (external)

- v2.7.1, Win64, from the [XboxDev/extract-xiso](https://github.com/XboxDev/extract-xiso) releases.
  Location: wherever you put `extract-xiso.exe`; the examples below assume it is on your `PATH`.
- The options are `VERIFIED` with the output of `extract-xiso.exe -h` (2026-10-20).
- **From Redump to XISO without touching the original:** `extract-xiso.exe -r -d "<target_folder>" "<redump>.iso"`.
  Without `-d` it rewrites in place and leaves a `.old`. `-r` applies the "media enable" patch to the XBE
  by default (disabled with `-m`).
- **Extract the tree:** `extract-xiso.exe -x -d "%NGB_DATA%\game" "<game>.xiso.iso"`.
- **List:** `extract-xiso.exe -l "<game>.xiso.iso"`.
- Naming convention: `<Game>.xiso.iso`.
- Gotcha: on the development machine, `Invoke-WebRequest` fails with `release-assets.githubusercontent.com`. `curl -L` works.

## Our own tools: the `ngb_tools` package (`src/ngb_tools/`)

They use only the Python 3.13 stdlib and have no runtime dependencies. The layout is `src/`, and each
tool is a subpackage with its own CLI.

**Setup, once per clone:**
```sh
python -m venv .venv
.venv/Scripts/python -m pip install -e ".[dev]"   # pinned ruff and mypy
git config core.hooksPath .githooks              # enables the pre-commit guard
```

**Running against game data:** with the package installed, `python -I -m ngb_tools.<tool> …`.
`-I` keeps the data directory off `sys.path`. In an uninstalled worktree use
`PYTHONPATH=src python -P -s -m ngb_tools.<tool> …` from the repo root, which is equivalent.

**Quality:** `ruff format .`, `ruff check .`, `mypy` (strict) and `python -m unittest discover -s tests -v`.

| Tool | What it does | Usage | Page |
| --- | --- | --- | --- |
| `ngb_tools.afs` | Lists, extracts and inventories AFS containers (structure, invariants, magics, entropy) | `python -I -m ngb_tools.afs list|extract|inventory …` | [[afs]] |
| `ngb_tools.xbe` | Analyzes XBE executables: header, certificate, sections, XDK libraries, kernel imports and strings | `python -I -m ngb_tools.xbe <xbe> [--strings] [--json …] [--md …]` | [[xbe-default]] |
| `ngb_tools.xpr` | Exports the textures of an XPR0 (standalone or inside a KMD) to PNG: DXT1/3/5, P8, A8, ARGB, deswizzle | `python -I -m ngb_tools.xpr list|export <file> [--kmd]` | [[xpr0]] |
| `ngb_tools.kmd` | Exports KMD models to glTF 2.0 or OBJ, with hierarchy, materials and textures | `python -I -m ngb_tools.kmd info|export <kmd>` | [[kmd]] |
| `ngb_tools.audio` | Decodes ADX, Xbox ADPCM (.wva) and XWB to WAV; spectrogram analysis | `python -I -m ngb_tools.audio adx|wva|xwb|analyze …` | [[adx]] |
| `ngb_tools.stage` | Exports complete stages (all the parts of a zone) to glTF/GLB, with optional collision | `python -I -m ngb_tools.stage export <zone>` | [[stages]] |
| `ngb_tools.hygiene` | Guard that prevents versioning game data (hook and CI) | `python -m ngb_tools.hygiene [--staged]` | [[adr-03-game-data-outside-repo]] |

### `ngb_tools.afs`

```sh
python -I -m ngb_tools.afs list    %NGB_DATA%\game\chr_ryu.afs
python -I -m ngb_tools.afs extract %NGB_DATA%\game\chr_ryu.afs            # → NGB_DATA\extracted\chr_ryu\
python -I -m ngb_tools.afs extract %NGB_DATA%\game\cmn.afs --index 253
python -I -m ngb_tools.afs inventory %NGB_DATA%\game --json build/reports/afs/inventory.json --md build/reports/afs/inventory.md
```

- Exit codes: 0 = OK, 1 = file that is not AFS or has broken invariants, 2 = usage or I/O error.
- `extract` refuses to write inside the repo. It writes each file atomically (`.part` and then
  rename) and leaves a `_manifest.json`.
- The inventory of the 206 AFS takes about 6 to 7 s. The analyst measured it on 2026-10-20 on the development machine (Ryzen 7 5700G with
  the data on a local disk).

### `ngb_tools.xbe`

```sh
python -I -m ngb_tools.xbe %NGB_DATA%\game\default.xbe --strings --json build/reports/xbe/default.json --md build/reports/xbe/default.md
python -I -m ngb_tools.xbe %NGB_DATA%\game\default.xbe            # Markdown summary to stdout
```

- Exit codes: 0 = OK, 1 = not an XBE or does not verify (section SHA-1 or unknown XOR keys; in the
  second case it still writes the report), 2 = usage or I/O error.
- The kernel exports table ships in the package (`ngb_tools/data/xboxkrnl_exports.tsv`, from nxdk, CC0).
  `--exports` allows using another one.
- Findings: [[xbe-default]].

### `ngb_tools.xpr`

```sh
python -I -m ngb_tools.xpr list   %NGB_DATA%\extracted\chr_ryu\0000_ryu.kmd --kmd
python -I -m ngb_tools.xpr export %NGB_DATA%\extracted\chr_ryu\0000_ryu.kmd --kmd    # → NGB_DATA\extracted\textures\0000_ryu\
python -I -m ngb_tools.xpr export %NGB_DATA%\game\Media\font.xpr
```

- One PNG per texture (`NNNN_<format>_<width>x<height>.png`, level 0) and a `_manifest.json` with the index, the
  format, the dimensions, the mips, the offset and the sha256 of the source bytes.
- `--kmd` looks, among the slots of the Team Ninja chunk, for the only one whose content starts with `XPR0` (in `ryu.kmd` it is
  slot 1, see [[kmd]]). Without `--kmd`, the file must be a
  standalone XPR0 (`.xpr`, `.spr`).
- The textures are game data: they are never written inside a repo (`ngb_tools._paths`).
- Formats: [[xpr0]].

### `ngb_tools.kmd`

```sh
python -I -m ngb_tools.kmd info   %NGB_DATA%\extracted\chr_ryu\0000_ryu.kmd
python -I -m ngb_tools.kmd export %NGB_DATA%\extracted\chr_ryu\0000_ryu.kmd             # → NGB_DATA\extracted\models\0000_ryu\
python -I -m ngb_tools.kmd export <kmd> --format obj      # OBJ + MTL (world positions)
python -I -m ngb_tools.kmd export <kmd> --skin --all-groups
python -I -m ngb_tools.kmd export <kmd> --format glb     # a single file with embedded textures
python -I -m ngb_tools.kmd export <ryu.kmd> --format glb --name ryu_sword --attach <katana.kmd>@<katana.itm>#2 --motions <rm_kt.mot>:141,151
python -I -m ngb_tools.kmd export <ryu.kmd> --format glb --motions <ryu.mtn>:1,3,90,36 --motions <rm_kt.mot>:141,151
```

- glTF 2.0 (`.gltf` + `.bin`): one node per HIERARC bone, one mesh per object and one primitive per submesh in
  TRIANGLES, with PBR materials that use the PNGs from [[xpr0]]. It is standard glTF, so any glTF viewer
  should open it (Windows 3D Viewer, Blender). So far it has only been validated with our own rasterizer
  (`preview.png`).
- It reuses the H2 PNGs if their manifest matches; otherwise it exports them.
- Format: [[kmd]].

### `ngb_tools.audio`

```sh
python -I -m ngb_tools.audio adx %NGB_DATA%\game\bgm.afs             # 95 tracks → NGB_DATA\extracted\audio\bgm\
python -I -m ngb_tools.audio adx %NGB_DATA%\game\sse.afs --index 2   # one voice
python -I -m ngb_tools.audio wva %NGB_DATA%\game\cmn.afs             # .wva banks → one WAV per record
python -I -m ngb_tools.audio xwb %NGB_DATA%\game\Media\Wave.xwb
python -I -m ngb_tools.audio analyze <wav...> --png-dir <dir>                       # RMS, clipping, waveform + spectrogram
```

- Output is PCM16 WAV with a `_manifest.json` (loops, sample rate, channels). Formats: [[adx]] and [[wva]].

## Ghidra 12.1.4 + ghidra-xbe (external)

- Install Ghidra 12.1.4 outside the repo and outside any cloud-synced folder, with a JDK 21 (Adoptium).
- `ghidra_12.1.4_PUBLIC_20260921.zip`, with a sha256 `ddac49f9…d4db` that matches the one in the notes of
  [the official release](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build).
  `VERIFIED`.
- Extension [XboxDev/ghidra-xbe](https://github.com/XboxDev/ghidra-xbe) build `202610161023`, compiled for
  12.1.4. It goes in `Ghidra\Extensions\ghidra-xbe` (XboxExecutableLoader: loads XBE and identifies XDK libraries
  with XbSymbolDatabase).
- The analysis project lives in `%NGB_DATA%\ghidra\` (project `ngb`), with its scripts and reports.
  Results in [[xbe-default]] (H5).

## Native loader (`port/`, C with MSVC x86)

```bat
port\build.cmd                                                        :: MSVC x86, /W4 /WX → build\port\ngb-loader.exe + ngb-core.dll
build\port\ngb-loader.exe %NGB_DATA%\game\default.xbe   :: runs the game up to the 1st NV2A MMIO
   [--log F] [--max-calls N] [--timeout S] [--idle S] [--echo] [--mmio-strict] [--mmio-gpu-ram]
python tools\gen_kernel_stubs.py [--check]                           :: regenerates port\src\kernel\stubs.c from the TSV
```

- Default log at `NGB_DATA\runs\h6-<date>.log`; virtual hard disk at `NGB_DATA\runs\hdd\`.
- Exit codes: 0 OK, 2 unimplemented call, 3 crash or MMIO, 4 `--max-calls`, 5 timeout or inactivity,
  6 the game asked to shut down.
- Design: [[adr-05-native-runtime]]. What the game does at boot: [[native-boot]].

### `ngb_tools.stage`

```sh
python -I -m ngb_tools.stage export boff                    # → NGB_DATA/extracted/stages/boff/boff.glb (all parts)
python -I -m ngb_tools.stage export ssato --parts 01,02,03,04,05
python -I -m ngb_tools.stage export boff --collision        # adds the HITMDL collision as a translucent mesh
python -I -m ngb_tools.stage info boff
```

- It joins the `<zone>_NN` parts of a zone; identical variants are exported once. Format: [[stages]].

### Headless analysis of `default.xbe` (H5, `tools/ghidra/`)

```powershell
powershell -ExecutionPolicy Bypass -File tools\ghidra\h5-run.ps1 [-Fresh]
```

- With `-Fresh` it reimports and reanalyzes (about 5 min). Without `-Fresh` it only regenerates the reports (about 67 s).
- `H5Fixup.java` turns the labels created by XbSymbolDatabase into functions.
- `H5Report.java` computes the coverage and the NV097 scan, and generates `symbols.csv`.
- `H5Xtlid.java` reads `.XTLID`.
- `H5Decomp.java` decompiles specific points.
- The output goes to `NGB_DATA\ghidra\reports\`. The decompilations are derived from the game and are not versioned.
