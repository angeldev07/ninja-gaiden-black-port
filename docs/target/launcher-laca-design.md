---
tags: [ngb, target, launcher, design]
updated: 2026-10-31
status: current
---

# Launcher redesign: "lacquer and ink" (design spec)

> PROPOSAL, built for v0.8.0 (see [[log]] 2026-10-31). Deviations found while building it are in the log entry.
> The current launcher is described in [[log]] (entries "Launcher v2" and later). Plan: [[launcher-laca-plan]].

## Why

The v2 launcher ([[log]], 2026-10-22) works, but its art reads as generic: neon red glow, chrome bevelled
lettering, speed lines and a shuriken. The owner wants it professional, elegant and visibly made with care,
with elements of the game itself.

## Goals and success criteria

- One coherent visual system, "lacquer and ink", applied to every page and state of the launcher.
- **Original art by default:** a procedural ink-wash night landscape and an original kamon. Nothing from the game
  ships in the repo or in the release zip ([[adr-03-game-data-outside-repo]]).
- **Game art mode (optional):** once the game is imported, the launcher reads artwork from the user's own copy at
  runtime and builds the hero from it. Never cached outside the data folder, never redistributed.
- Same features as today: import (ISO or folder), play, settings, log viewer, About, updater, English and Spanish,
  `--shot` screenshots. No regressions in behaviour.
- Still Win32 + GDI with no third-party libraries. The exe grows by less than 400 KB.
- Released as **v0.8.0** with new README screenshots (original art only).

## Visual system

### Tokens

| Token | Value | Use |
| --- | --- | --- |
| `bg` | `#13110f` | Warm near-black lacquer, every surface |
| `cream` | `#efe7d6` | Primary text, text on vermilion |
| `muted` | cream at 56 % | Body copy, values |
| `dim` | cream at 36 % | Micro labels, descriptions, inactive nav |
| `sep` | cream at 10 % | Every 1 px hairline |
| `verm` | `#d23f2f` | **The only accent:** kamon, kickers, active tab rule, primary button, switches on |
| `ok` / `warn` / `bad` | `#8fb08a` / `#d9a648` / `#d8584c` | Status dots only, never large areas |

Rules: one accent only; flat surfaces (no glow, no chrome, no bevel); square buttons; the kamon is the only icon.

### Typography

- **Latin:** Josefin Sans (SIL OFL 1.1), weights 100, 300 and 600, embedded as `RT_RCDATA` and loaded with
  `AddFontMemResourceEx` (private to the process). About 3 × 60 KB. Credited in `THIRD_PARTY.md`; the TTFs go in
  `port/res/fonts/` with `git add -f` (`.gitignore` ignores `*.ttf` on purpose).
  - Display: weight 100, wide tracking (0.06 to 0.1 em). Micro labels: 600, 10.5 px, tracking 0.26 em, uppercase.
  - GDI has no letter-spacing: tracked text is drawn glyph by glyph with `GetTextExtentPoint32` advances (the
    v2 `txt_chrome` already does this).
- **Kanji:** a subset of Shippori Mincho B1 SemiBold (OFL), embedded: Yu Mincho is not on a stock Windows 11.
  Fallbacks: Yu Mincho, MS Mincho, then skip the kanji (they are ornament, never the only carrier of meaning).
- If the embedded font fails to load, fall back to Segoe UI Light / Semibold.

### Layout (1240 × 700 logical, scaled to the system DPI)

- **Top bar** (64 px): kamon and "NINJA GAIDEN **BLACK**"; the six sections as tracked micro labels (active one
  in cream with a 1 px vermilion rule); `ES / EN` on the right. Replaces the v2 sidebar.
- **Play page:** full-bleed hero art, darkened toward the left for legibility. Vertical kanji `忍者外伝`, a kicker,
  the title in Josefin 100 at 88 px and "BLACK" tracked below, one status line. A bottom bar (112 px) with spec
  columns (renderer, resolution, pace, version) and the primary button `JUGAR 始` / `PLAY 始`.
- **Play states:** first run (import: steps `一 二 三`, the two import buttons, drop hint, data folder), importing
  (progress as a 2 px vermilion rule plus file count), error, running (button becomes STOP), update band.
- **Inner pages** (Graphics, Audio, Controls, Advanced, About, log view): left column with a ghost page number,
  title, kanji and one sentence; right column with hairline rows (label, description, control). Switches are
  square; combos are text plus a small caret.

## Art modules

### `ink.c`: procedural ink-wash landscape (default hero)

Port of the prototype validated in the brainstorm (see [[log]] 2026-10-31):

- **Silhouettes:** four depth bands, each a 1D ridged fBm (`r = 1 − |2n − 1|`, squared, 5 octaves, frequency × 2.02),
  scaled so ranges rise toward the right, then two smoothing passes.
- **Ink:** darkest at the crest, fading down into mist with `exp(−depth / fade)`, modulated by a 2D fBm wash
  texture stretched horizontally and a low-frequency macro mask that leaves blank areas.
- **Air perspective** toward the sky tone for far bands; two horizontal mist bands; boundary noise; a pale moon
  with a soft halo; paper fibre noise and a vignette.
- Deterministic (fixed seed, integer hash, no `rand()`), so `--shot` output is stable.
- Rendered once per DPI into the cached backdrop. Budget: under 120 ms at 100 % and under 400 ms at 200 % on the
  owner's PC. If slower, render at half resolution and upscale bilinearly (the art is soft).

### `kamon.c`: the emblem

An original crest: a ring with two crossed straight swords (a traditional motif, drawn from scratch with the
existing signed-distance shapes). Used in the top bar, the About page and **the new app icon** (`make_icon.py`
regenerates `ngb.ico` from the same geometry).

### `gameart.c`: game art mode

Reads the user's imported copy (`<data>\game\`) at runtime and composes the hero from it.

- **Readers:** a minimal AFS table reader ([[afs]]) and an XPR0 resource walker ([[xpr0]]), both bounds-checked
  and fuzz-tolerant (any malformed input means "no game art", never a crash).
- **Decoding:** links the core's `port/src/gl/texconv.c` (pure C, no GL, already tested against `ngb_tools.xpr`
  vectors) for de-swizzle, P8 palettes and DXT1/3/5 to RGBA8.
- **Assets and composition:** see the next section.
- **Treatment:** to sit inside the lacquer palette, the art is graded toward it (lift the blacks to `bg`,
  desaturate everything except reds, which move toward vermilion), then the same left-side darkening as the
  procedural hero. The UI on top stays the same.
- **Setting:** `[launcher] game_art=1` in `ngb.ini`, on by default once a valid game is imported; a switch in
  Graphics ("Game art in the launcher"). Any read or decode failure falls back silently to the ink landscape and
  writes one line to the launcher log.
- Decoding happens on a worker thread after import or at start-up; until it is ready the ink hero is shown.

### Game art: assets

From the art survey of 2026-10-31 (exports under `%NGB_DATA%\extracted\launcher-art-survey\`, never in the repo).
`.spr` files are XPR0 whose resource 0 is USERDATA: a table of 24-byte sprite records
`{f32 u0, v0, u1, v1; u32 texture ordinal; u16 w, h}` that gives the used crop of each texture. `VERIFIED` on
`demo.spr`, `cos_menu.spr` and `chapst*.spr`.

| Use | Source | Texture | Notes |
| --- | --- | --- | --- |
| Hero (default) | `demo.afs` entry `demo.spr`, texture 0 | P8 1024 × 512, swizzled | Ryu key art. Sprite 0 crop (74,0)–(1014,480). Scaled to 660 px tall, subject anchored at x ≈ 870. |
| Hero (rotation) | `demo.spr`, texture 1 | P8 1024 × 512 | Ayane key art; same treatment. |
| Title | `demo.spr`, textures 2 and 3 | P8 512 × 256 and 512 × 128, with alpha | The "NINJA GAIDEN" and "BLACK" logotypes, used **as masks**: cream and vermilion, the red glow removed by an alpha curve. They replace the typeset title in this mode. |
| Inner pages | `demo.afs` entry `titlebg.spr`, texture 1 | P8 512 × 512 | The title screen's dark wood with waves; graded and laid at low opacity behind the left column. Crop to sprite 0 (511 × 409). |
| Ornament | `titlebg.spr`, textures 3 to 5 | A8 256 × 256 | Ornate rings, tinted cream at ~5 % behind the page number. |

- **Locating:** by AFS entry name (the AFS name table), then the XPR0 magic, then the texture ordinal with its
  format and size checked against the table above. A mismatch (another region's disc, a mod) disables only that
  asset.
- **Hero rotation:** each launch shows the next key art (`[launcher] hero_index`), Ryu first.
- **Formats needed:** P8 with its D3DCOLOR palette (the palette is the next resource header), A8 and the NV2A
  swizzle. All single-mip. All already handled by `texconv.c`.
- **Grade** (validated on the Ryu key art in the brainstorm): luminance through a smoothstep (0.04 to 0.92, power
  1.15) as a duotone from `bg` to 86 % cream, 14 % of the original chroma kept, saturated reds pulled toward
  vermilion; the frame then darkens to the left (x 30 % to 66 %), at the top and at the bottom.
- The prototype grader is the reference for the C port: it is a single small function, documented in `gameart.c`.
- Logos, characters and the title art are Koei Tecmo's. They are only ever read from the user's copy, at runtime,
  in memory; the README screenshots use the default ink art.

### Title music (removed)

Built and then removed at the owner's request (2026-10-31): music in a launcher was felt as a nuisance. The ADX facts
found on the way are in [[log]].

### Living backdrop (both modes)

- A 30 fps timer while the window is visible, the game is not running and Windows' "Show animations" setting
  (`SPI_GETCLIENTAREAANIMATION`) is on; otherwise the backdrop is static.
- Layers over the cached hero: two drifting mist strips (6 and 10 px/s, alpha ≤ 10 %) (a mouse parallax was built
  and removed at the owner's request); in ink mode the moon halo breathes (±3 % over 8 s); in game art mode the ornament ring turns
  (one turn per 120 s) on inner pages.
- Budget: frame composition under 6 ms at 100 % DPI, under 3 % of one core.

## Roadmap: v0.9.0, "the dojo" (3D)

The next release brings the game's 3D models into the launcher, with their own spec:

- **Ryu alive in the hero:** the real model and textures in his idle guard animation, lit by the moon, turnable with
  the mouse; it replaces the static key art in game art mode.
- **Archive page:** a bestiary of the characters (59 models, 20 with idle) and an armory (the Dragon Sword and the
  other weapons), each with its name in kanji and Latin.
- **Work:** port the KMD, XPR0 texture and MTN motion readers to C (the Python tools in `src/ngb_tools/kmd` are the
  reference; about 2,000 lines), and render through an offscreen OpenGL context read back into the GDI frame.

## Code structure

| File | Change |
| --- | --- |
| `port/launcher/art.c`, `art.h` | Rewritten: backdrop composition only (hero source, darkening, bars). The chrome wordmark, red mist, speed lines and shuriken are removed. |
| `port/launcher/ink.c`, `ink.h` | New: procedural landscape into a `canvas_t`. |
| `port/launcher/kamon.c`, `kamon.h` | New: the crest at any size. |
| `port/launcher/gamedata.c`, `gamedata.h` | New: AFS, XPR0 and sprite-table readers (pure, unit-tested). |
| `port/launcher/gameart.c`, `gameart.h` | New: decode via `texconv.c`, grading, worker thread. |
| `port/launcher/ui.c`, `ui.h` | New palette and fonts (embedded Josefin, Yu Mincho), tracked text, square button, switch, top tabs; old chrome helpers removed. |
| `port/launcher/launcher.c` | Top bar instead of sidebar; Play page and inner pages re-laid out; `game_art` setting. |
| `port/launcher/strings.h` | New and changed strings (English and Spanish). |
| `port/launcher/launcher.rc` | Fonts as `RCDATA`; new icon. |
| `port/build.cmd` | New sources; `texconv.c` linked into the launcher; a C test for the readers. |
| `port/res/make_icon.py`, `ngb.ico` | Kamon icon. |
| `THIRD_PARTY.md` | Josefin Sans (OFL). |

`launcher.c` is 2,197 lines; the page code that changes moves to `pages.c` so neither file passes ~1,500 lines.

## Testing

- **C tests** (`port/tests/`, run by `build.cmd`): AFS and XPR0 readers on synthetic buffers (valid, truncated,
  bogus offsets, huge counts); ink determinism (checksum of a 256 × 144 render); grading maths.
- **Screenshots:** `--shot` for every page and Play state, with and without game art, at 100 % and 150 % DPI.
  Reviewed by eye before the PR. Only original-art shots go into `docs/images/`.
- **Manual:** import from an ISO into a fresh data folder, play, stop, change language, toggle game art,
  updater band with `--update-url` and a local manifest.
- Python tests unchanged (`python -m unittest discover -s tests -v`).

## Out of scope

- General freeze guards for CPU/GPU waits: a separate piece of work after this release.
- Animated page transitions and a resizable window. The 3D viewer is v0.9.0 (roadmap above).

## Release

`chore: release v0.8.0` per [[adr-07-releases-and-updater]]. Notes lead with the new launcher. The v0.7.0 launcher
picks the update up through the updater.
