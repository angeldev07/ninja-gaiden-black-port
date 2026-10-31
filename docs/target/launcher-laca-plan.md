---
tags: [ngb, target, launcher, plan]
updated: 2026-10-31
status: current
---

# Launcher "lacquer and ink": implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the launcher redesign of [[launcher-laca-design]] as v0.8.0.

**Architecture:** The launcher stays an immediate-mode Win32 + GDI app (`ui.c` draws into one DIB). New pure-C
units produce pixels (`ink.c`, `kamon.c`) or read the user's game copy (`gamedata.c` readers, `gameart.c` grading
and composition on a worker thread). `art.c` composes the cached backdrop from those. `launcher.c` keeps state and
actions; page painting moves to `pages.c`.

**Tech Stack:** C (MSVC, `port/build.cmd`), Win32/GDI, `port/src/gl/texconv.c` (linked, no GL), Python 3.13 stdlib
for `make_icon.py`.

**Spec:** `docs/target/launcher-laca-design.md`. Prototypes (reference behaviour for the C ports):
`%TEMP%\...\scratchpad\inkland.py` and `gameart_proto.py` are copied into `tools/proto/` in Task 2 so they travel
with the plan.

## Global Constraints

- Win32 + GDI only; no third-party libraries. The exe grows by less than 400 KB.
- Nothing from the game in the repo, the zip or any file the launcher writes outside `NGB_DATA`.
- Window 1240 × 700 logical, scaled with `S()`; all coordinates in logical pixels.
- Tokens: `bg #13110f`, `cream #efe7d6`, `muted` cream 56 %, `dim` cream 36 %, `sep` cream 10 %, `verm #d23f2f`,
  `ok #8fb08a`, `warn #d9a648`, `bad #d8584c`. One accent only.
- Fonts: Josefin Sans 100/300/600 embedded (OFL); kanji Yu Mincho Demibold, fallback MS Mincho, else omit kanji;
  Latin fallback Segoe UI Light/Semibold.
- English and Spanish for every new string (`strings.h`). Code, comments and logs in English.
- C tests run from `build.cmd` and must use synthetic data only.
- `python -m unittest discover -s tests -v` stays green. Subagents never commit; the leader commits.

## Review Focus

1. **150 % and 200 % DPI:** layout, tracked text and the ink render must hold; ink under 400 ms at 200 %.
   Test: Task 2 timing check at 2480 × 1400; Task 9 shots at `--dpi 144`.
2. **Another region's disc or a modded copy:** a missing `demo.spr`, wrong ordinal or size must disable that asset
   only, never crash. Test: Task 4 bogus-input cases; Task 5 "missing asset falls back" case.
3. **Data folder change or re-import while game art is decoding:** the worker must be cancelled/joined and the art
   rebuilt for the new game. Test: Task 5 `gameart_restart` case.
4. **Long Spanish labels** in tracked micro labels, tabs and the primary button: must fit or degrade to a smaller
   tracking, never overflow. Test: Task 6 `ui_fit_tracked` case; Task 9 Spanish shots.
5. **Missing fonts** (embedded load fails, no Yu Mincho): Latin falls back, kanji are skipped, layout unchanged.
   Test: Task 1 `--no-embedded-fonts` shot and the face check.

---

### Task 1: Tokens and fonts

**Files:**
- Create: `port/res/fonts/JosefinSans-Thin.ttf`, `JosefinSans-Light.ttf`, `JosefinSans-SemiBold.ttf`, `OFL.txt`
- Modify: `port/launcher/ui.h` (palette, `font_id`), `port/launcher/ui.c` (`make_fonts`, tracked text),
  `port/launcher/launcher.rc` (RCDATA 101..103), `THIRD_PARTY.md`

**Interfaces:**
- Produces: colour macros `C_BG C_CREAM C_MUTED C_DIM C_SEP C_VERM C_OK C_WARN C_BAD` (muted/dim/sep as
  pre-blended over `bg`: `0x8f8a80`, `0x625d56`, `0x2a2724`); `enum font_id { F_BODY /*Josefin 300 14*/,
  F_SMALL /*300 12.5*/, F_LABEL /*600 10.5*/, F_TAB /*600 11*/, F_VALUE /*300 16*/, F_H1 /*100 88*/,
  F_H2 /*100 46*/, F_H3 /*100 54*/, F_BTN /*600 15*/, F_KANJI /*Yu Mincho 600 16*/, F_KANJI_L /*Yu Mincho 600
  128*/, F_MONO /*Consolas 12*/, F_COUNT }`; `void txt_tracked(int font, rgb_t col, int x, int y, int h,
  float em, const wchar_t *s)`; `int tracked_w(int font, float em, const wchar_t *s)`;
  `int ui_has_kanji(void)`.

- [ ] **Step 1:** Fetch the static TTFs from the upstream `googlefonts/josefinsans` repository (OFL 1.1); keep
  `OFL.txt` beside them. Verify each is under 100 KB. `git add -f` them (`.gitignore` ignores `*.ttf`).
- [ ] **Step 2:** Add `101 RCDATA "..\\res\\fonts\\JosefinSans-Thin.ttf"` (102 Light, 103 SemiBold) to
  `launcher.rc`. In `ui_init`, load each with `FindResource`/`LoadResource`/`AddFontMemResourceEx`.
- [ ] **Step 3:** Rewrite `make_fonts` with the table above (face `"Josefin Sans"`, weights 100/300/600; kanji
  face `"Yu Mincho"` 600). After creation, `GetTextFaceW`: Josefin missing → `"Segoe UI Light"`/`"Segoe UI
  Semibold"`; Yu Mincho missing → `"MS Mincho"`; both kanji faces missing → `ui_has_kanji()` returns 0.
  `--no-embedded-fonts` skips Step 2 (test switch, documented in the `launcher.c` header).
- [ ] **Step 4:** `txt_tracked`: draw glyph by glyph advancing by `GetTextExtentPoint32` of each character plus
  `em * font_px`; grayscale AA (`ANTIALIASED_QUALITY`) for F_H1..F_H3, ClearType otherwise.
- [ ] **Step 5:** Build (`port\build.cmd`), then `ngb-launcher.exe --shot a.bmp` and
  `--no-embedded-fonts --shot b.bmp`. Expected: both render; old pages still draw (colours change only).
- [ ] **Step 6:** `THIRD_PARTY.md`: Josefin Sans, Santiago Orozco, OFL 1.1, embedded in `ngb-launcher.exe`.
- [ ] **Step 7:** Commit `feat(launcher): lacquer palette and embedded Josefin Sans`.

### Task 2: Procedural ink landscape (`ink.c`)

**Files:**
- Create: `port/launcher/ink.c`, `port/launcher/ink.h`, `port/tests/ink_test.c`,
  `tools/proto/inkland.py`, `tools/proto/gameart_proto.py`
- Modify: `port/build.cmd` (test 5c and launcher sources)

**Interfaces:**
- Consumes: `canvas_t` (`art.h`).
- Produces: `void ink_landscape(canvas_t *c, uint32_t seed);` fills the whole canvas; `#define INK_SEED 7u`.

- [ ] **Step 1:** Copy the two prototypes into `tools/proto/` with a header naming them reference prototypes.
- [ ] **Step 2:** Write `ink_test.c`: renders 256 × 144 (`k = 256/1240.f`) twice with seed 7 → identical buffers;
  seed 8 → different; FNV-1a of the seed-7 buffer equals a constant recorded on the first passing run (comment
  says so); mean luminance of the top 10 % rows < mean of rows 45–55 % (sky glow), bottom 10 % rows darkest band.
  Timing: render 2480 × 1400 and print ms (fail above 400 ms).
- [ ] **Step 3:** Run `build.cmd`; expected FAIL (no `ink.c`).
- [ ] **Step 4:** Port `inkland.py` to `ink.c` with the same constants (bands, palette, moon at (0.76 W, 0.25 H),
  r = 0.062 H, mist bands at 0.66 and 0.79). Integer hash for noise (no `rand()`, no globals mutated between
  calls). Work in logical coordinates (`x / k`) so the picture is DPI-independent. If over budget: render at half
  resolution and upscale bilinearly.
- [ ] **Step 5:** Run `build.cmd`; expected PASS, timing printed. Record the checksum constant.
- [ ] **Step 6:** Commit `feat(launcher): procedural ink-wash landscape`.

### Task 3: Kamon and icon

**Files:**
- Create: `port/launcher/kamon.c`, `port/launcher/kamon.h`
- Modify: `port/res/make_icon.py` (rewrite), `port/res/ngb.ico` (regenerated)

**Interfaces:**
- Produces: `void kamon_draw(int cx, int cy, int r, rgb_t col);` (logical coords, uses `ui.c` shape primitives:
  ring stroke 5 % of r·2, two straight swords at ±45° — blade 4.8 % wide with a pointed tip, guard 18 % × 4.5 %,
  grip 4.4 % × 17 %, matching the SVG symbol in the spec mockup, viewBox 100).
- [ ] **Step 1:** `kamon.c` from the geometry above (`capsule` and `fill_rect` rotated via polygon fill; add a
  `fill_quad` to `ui.c` if needed).
- [ ] **Step 2:** `make_icon.py`: same geometry in vermilion on a `bg` rounded square; sizes unchanged. Run
  `python port/res/make_icon.py --png %TEMP%\icon.png`; inspect 16, 32 and 256 px; legible at 16 px (thicken ring
  and blades below 32 px).
- [ ] **Step 3:** Commit `feat(launcher): original kamon emblem and icon`.

### Task 4: Game data readers (`gamedata.c`)

**Files:**
- Create: `port/launcher/gamedata.c`, `port/launcher/gamedata.h`, `port/tests/gamedata_test.c`
- Modify: `port/build.cmd` (test 5d)

**Interfaces:**
- Produces:
  - `int afs_find(const wchar_t *afs_path, const char *name, uint64_t *off, uint32_t *size);` 1 if found. Reads
    the TOC and the attribute table ([[afs]]): magic `AFS\0`, N ≤ 65535, attribute pointer at `8 + 8N`, entries
    of 0x30 bytes, name ≤ 32 bytes NUL-padded, case-sensitive match.
  - `int xpr_texture(const uint8_t *xpr, size_t len, int ordinal, xpr_tex_t *out);` walks resource headers per
    [[xpr0]] (texture 20 B, palette 12 B, push buffer 24 B, USERDATA 8 B + payload, stop at `0xFFFFFFFF` or
    header_size); `ordinal` counts textures only. `xpr_tex_t { uint32_t format; const uint8_t *data; size_t
    data_len; const uint8_t *palette; size_t palette_len; }` — palette = the next header if it is a palette.
  - `int spr_sprite(const uint8_t *xpr, size_t len, int index, spr_rect_t *out);` reads USERDATA records
    `{f32 u0,v0,u1,v1; u32 tex; u16 w,h}` (24 B); `spr_rect_t { int tex, x0, y0, x1, y1; }` in pixels.
  - `uint8_t *gamedata_load(const wchar_t *afs_path, const char *name, size_t *len);` (malloc; caps at 16 MB).
- [ ] **Step 1:** `gamedata_test.c` builds synthetic AFS files in `%TEMP%` and XPR0 buffers in memory. Cases:
  find by name; missing name → 0; truncated TOC; attribute pointer past EOF; N = 0x7fffffff; texture ordinal 1
  with a palette after it; ordinal past the end → 0; palette missing for P8 → `palette == NULL`; USERDATA with
  2 records → rects in pixels from a 1024 × 512 texture (`u0 = 74/1024` → `x0 = 74`); header walk with a resource
  header pointing past `len` → 0; push buffer header skipped with 24 bytes.
- [ ] **Step 2:** Run; expected FAIL. **Step 3:** Implement. **Step 4:** Run; expected PASS.
- [ ] **Step 5:** Commit `feat(launcher): AFS, XPR0 and sprite-table readers`.

### Task 5: Game art (`gameart.c`)

**Files:**
- Create: `port/launcher/gameart.c`, `port/launcher/gameart.h`, `port/tests/gameart_test.c`
- Modify: `port/build.cmd` (link `%SRC%\gl\texconv.c` into the launcher and the test)

**Interfaces:**
- Consumes: Task 4 readers; `texconv_describe(format, 0, 0, 0, &d)` + `texconv_convert` (RGBA8 out).
- Produces:
  - `uint32_t ga_grade(uint8_t r, uint8_t g, uint8_t b);` → `0xRRGGBB`, the spec's grade (smoothstep 0.04–0.92,
    power 1.15, duotone `bg` → 86 % cream, 14 % chroma, reds `(r − max(g,b) − 0.14)·3` toward vermilion at 80 %).
  - `typedef struct ga_image { int w, h; uint32_t *px; uint8_t *alpha; } ga_image_t;`
  - `void gameart_start(const wchar_t *game_dir, int hero_index);` (worker thread; cancels and joins a previous
    one); `int gameart_ready(void);` `const ga_image_t *gameart_get(int which);` with
    `enum { GA_HERO, GA_LOGO, GA_LOGO_BLACK, GA_TITLEBG, GA_RING, GA_COUNT }`; `void gameart_stop(void);`
  - Asset table (name, afs, texture ordinal, format, w, h, sprite index): `demo.spr` 0 P8 1024×512 sprite 0
    (hero Ryu), 1 (hero Ayane), 2 P8 512×256 (logo), 3 P8 512×128 (BLACK); `titlebg.spr` 1 P8 512×512 sprite 0
    (title bg), 3 A8 256×256 (ring). AFS file `demo.afs`.
- [ ] **Step 1:** `gameart_test.c`: `ga_grade(0,0,0) == 0x13110f`; `ga_grade(255,255,255)` within ±2 of
  `0xd2cbbc` (86 % cream); a mid grey has |r−g| ≤ 8; pure red `(255,0,0)` has r > 2·g. Logo mask: alpha 255 on a
  dark pixel → mask ≥ 250; alpha 120 → 0; red glow pixel (r=200,g=40,a=255) → ≤ 40. Restart: start on a temp dir
  without `demo.afs`, then on another → no crash, `gameart_ready()` true with every image NULL (fallback).
- [ ] **Step 2:** Run; FAIL. **Step 3:** Implement: load, check format/size, convert, crop by sprite rect, keep
  the hero ungraded at decode size (graded in `art.c` at composition so DPI scaling samples the source once),
  logos as alpha masks. Log one line per disabled asset (`launcher.log`).
- [ ] **Step 4:** Run; PASS. **Step 5:** Commit `feat(launcher): game art read from the user's copy`.

### Task 5b: Title music (`music.c`)

**Files:**
- Create: `port/launcher/music.c`, `port/launcher/music.h`, `port/tests/music_test.c`
- Modify: `port/build.cmd` (test 5e; `winmm.lib` for the launcher)

**Interfaces:**
- Consumes: `afs_find`, `gamedata_load` (Task 4).
- Produces: `int adx_parse(const uint8_t *p, size_t n, adx_info_t *out);` (`adx_info_t { int channels, rate;
  uint32_t frames, loop_start, loop_end; int has_loop; size_t data_offset; int block; int c1, c2; }`);
  `int adx_decode(const adx_info_t *h, const uint8_t *p, size_t n, uint32_t frame0, uint32_t count, int16_t *out,
  adx_state_t *st);` (interleaved, keeps history per channel); `void music_start(const wchar_t *game_dir);`
  `void music_fade_to(float volume, int ms);` `void music_stop(void);`
- [ ] **Step 1:** `music_test.c`: a synthetic ADX built in the test (header 0x80, version 3, one 18-byte frame per
  channel with scale 1 and nibbles 0..15) decodes to the samples `ngb_tools.audio.adx.decode` gives for the same
  bytes (expected values generated once with the Python tool, pasted with a comment naming the command);
  truncated header → error; encrypted flag → error; loop parse from a version-4 header with AINF.
- [ ] **Step 2:** Run; FAIL. **Step 3:** Port `adx.py` (`parse_header`, `parse_loop`, `compute_coefficients`,
  `decode_channel`). Streaming: worker thread, 3 × 8192-frame `waveOut` buffers, wraps at the loop points; volume
  applied per sample with a linear ramp. Entry name search: first containing `bgm_title`, else `bgm_startmenu`.
- [ ] **Step 4:** Run; PASS. Manual: the theme plays, loops cleanly, fades on PLAY.
- [ ] **Step 5:** Commit `feat(launcher): title theme from the user's copy`.

### Task 6: Widgets

**Files:**
- Modify: `port/launcher/ui.c`, `port/launcher/ui.h`

**Interfaces:**
- Produces (replacing `ui_plate`, `txt_chrome`, `ui_card`, `ui_checkbox`, `ui_chevron`):
  `void ui_tab(int id, int x, int y, int h, const wchar_t *label, int active);` (F_TAB 0.24 em; active cream with a
  1 px vermilion rule at the bottom; hover muted);
  `void ui_primary(int id, int x, int y, int w, int h, const wchar_t *label, const wchar_t *kanji, int enabled);`
  (vermilion fill, 1 px cream-28 % inner frame at inset 4, F_BTN 0.52 em; hover lighter `#dc4a3a`; pressed
  `#b8362a`; disabled `#4a2a26` with dim text);
  `void ui_secondary(int id, int x, int y, int w, int h, const wchar_t *label);` (1 px cream-32 % outline);
  `void ui_link(int id, int x, int y, const wchar_t *label);` (F_LABEL vermilion);
  `void ui_switch(int id, int x, int y, int on);` (44 × 22 square);
  `void ui_hair(int x, int y, int w);` `void ui_label(int x, int y, const wchar_t *s, rgb_t col);`
  (F_LABEL 0.26 em uppercase); `int ui_fit_tracked(int font, float em, const wchar_t *s, int max_w, float *em_out);`
  (reduces em down to 0.08, then returns 0 so callers switch font).
- [ ] **Step 1:** Implement; keep hit registration as today (`add_hit` inside each widget).
- [ ] **Step 2:** Add a `--demo widgets` state to `launcher.c` that paints every widget state in a grid; shot it at
  96 and 144 dpi; check by eye against the mockup (`.superpowers/brainstorm/*/content/laca-v5.html`).
- [ ] **Step 3:** `ui_fit_tracked` check inside the `--demo widgets` grid: the Spanish primary label
  "IMAGEN DE DISCO (.ISO)" in a 230 px button fits.
- [ ] **Step 4:** Commit `feat(launcher): lacquer widgets`.

### Task 7: Backdrop composition (`art.c`)

**Files:**
- Modify: `port/launcher/art.c`, `port/launcher/art.h` (rewrite), `port/launcher/ui.c` (`ui_begin` cache key)

**Interfaces:**
- Consumes: `ink_landscape`, `gameart_get`, `ga_grade`.
- Produces: `enum art_kind { ART_HERO, ART_INNER };` `void art_backdrop(canvas_t *c, int kind, int use_game_art);`
  and `void ui_invalidate_backdrop(void);` (called when game art becomes ready or the switch changes).
- [ ] **Step 1:** ART_HERO: source = graded hero (scale to 660 px tall, subject x 870, top 40, crop edge
  softened over 160 px) or the ink landscape; then darken left (smoothstep x 30 %→66 %), top 0→16 %, bottom
  80→100 % at 65 %; top bar band (0–64) `bg` at 78 %, bottom bar band (588–700) `#0e0c0b` at 88 %, hairlines.
- [ ] **Step 2:** ART_INNER: flat `bg`; with game art, the title bg graded at 7 % opacity in the left column
  (0–310) and the ring tinted cream 5 % behind the page number; hairlines (top bar, column divider).
- [ ] **Step 3:** Shots: Play with `--no-game-art` and with game art (owner's data), Graphics both ways.
- [ ] **Step 4:** Commit `feat(launcher): ink or game-art backdrop`.

### Task 7b: Living backdrop

**Files:**
- Modify: `port/launcher/art.c`, `port/launcher/ui.c`, `port/launcher/launcher.c`

**Interfaces:**
- Produces: `void art_animate(canvas_t *frame, const canvas_t *cached, int kind, double t, float mx, float my);`
  (composes the moving layers over the cached backdrop into the frame); `ui_begin` calls it instead of `memcpy`
  when animation is on.
- [ ] **Step 1:** Timer `ID_TIMER_ANIM` at 33 ms; on only if visible, not minimized, game not running and
  `SPI_GETCLIENTAREAANIMATION` is TRUE; `InvalidateRect` without erase.
- [ ] **Step 2:** Layers per spec (mist strips 6 and 10 px/s ≤ 10 %; parallax ±8 px eased 0.12 per frame; moon
  halo ±3 % over 8 s; ring 1 turn / 120 s). Mist strips precomputed once per DPI (1D noise × vertical profile).
- [ ] **Step 3:** Measure: log the mean composition time over 300 frames with `--perf-anim`; expected < 6 ms at
  96 dpi. Task Manager CPU < 3 % of one core.
- [ ] **Step 4:** Commit `feat(launcher): living backdrop`.

### Task 8: Pages and states (`pages.c`, `launcher.c`)

**Files:**
- Create: `port/launcher/pages.c`, `port/launcher/pages.h`
- Modify: `port/launcher/launcher.c` (top bar replaces sidebar; footer replaced by the Play bottom bar; settings
  `game_art`, `hero_index`; flags `--no-game-art`), `port/launcher/strings.h`

**Interfaces:**
- Produces: `pages.h` exposes `void pages_paint(void);` and the shared state struct `launcher_view_t` that
  `launcher.c` fills each frame (page, language, import status, game info, settings, update band, note); actions
  stay in `launcher.c::activate`.
- [ ] **Step 1:** Move page painting into `pages.c` unchanged (pure move); build; `--shot` per page identical to
  before. Commit `refactor(launcher): move page painting to pages.c`.
- [ ] **Step 2:** Top bar per spec (kamon 28 px at x 44; "NINJA GAIDEN" F_TAB 0.34 em + "BLACK" vermilion; tabs
  from x 236 gap 32; `ES / EN` right; legal line moves to About and the first-run screen).
- [ ] **Step 3:** Play ready: vertical `忍者外伝` (if `ui_has_kanji`), kicker, title (F_H1 0.1 em + "BLACK"
  F_VALUE 1.0 em) or the game logos (cream / vermilion masks, 560 px and 290 px wide), status line, bottom bar
  spec columns (renderer, resolution, pace, version), `ui_primary` `PLAY 始` / `JUGAR 始`, hint
  "ENTER TO PLAY" / "INTRO PARA JUGAR" (and make Enter start the game).
- [ ] **Step 4:** Play first run (steps `一 二 三`, drop hint box, primary + secondary import buttons, data folder
  with CHANGE / OPEN links), importing (2 px vermilion progress rule + count + Cancel), error note, running
  (primary becomes STOP), update band (thin band under the top bar: version, WHAT'S NEW link, UPDATE secondary).
- [ ] **Step 5:** Inner pages: left column (ghost number `0N`, F_H2 title, kanji 画 音 操 設 記 per page, one
  sentence), right column hairline rows with `ui_switch` / combo text + caret; open combo overlay restyled flat.
  New Graphics row "Game art in the launcher" (`[launcher] game_art`, default 1; disabled while no game). Top bar `MUSIC` / `MÚSICA` toggle (`[launcher] music`, default 1) shown only in game art mode; `music_fade_to` on PLAY, minimize and game exit.
- [ ] **Step 6:** `strings.h`: new keys in English and Spanish for every label above; remove unused ones.
- [ ] **Step 7:** Shots of every page/state (`--page`, `--demo progress|error`, `--lang es`) at 96 and 144 dpi.
- [ ] **Step 8:** Commit `feat(launcher): lacquer layout for every page and state`.

### Task 9: Review, docs and release

**Files:**
- Modify: `docs/images/launcher-*.png` (original art only), `README.md`, `docs/guide/getting-started.md`,
  `docs/log.md`, `docs/target/launcher-laca-design.md` (status current), `port/res/version.h`

- [ ] **Step 1:** Full `build.cmd` + `python -m unittest discover -s tests -v`; both green.
- [ ] **Step 2:** Manual pass: fresh data folder, import ISO, play, stop, change language, toggle game art, change
  data folder during decode, updater band with `--update-url` and a local manifest.
- [ ] **Step 3:** One whole-branch code review; fix real bugs only.
- [ ] **Step 4:** New README/guide screenshots with `--no-game-art`; vault log entry; PR, merge.
- [ ] **Step 5:** `chore: release v0.8.0` per [[adr-07-releases-and-updater]]; tag; owner publishes the draft;
  then `0.9.0-dev`.
