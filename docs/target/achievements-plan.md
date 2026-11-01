---
tags: [ngb, target, achievements, plan]
updated: 2026-10-31
status: draft
---

# Achievements: implementation plan (phase 1)

> PROPOSAL. Spec: [[achievements-design]]. One branch, `feat/achievements`, started from the spike branch; one PR.

## Tasks

1. **Catalog** (`port/src/ach/catalog.c`, `catalog.h`): the 27 rows of the spec with both languages, category, icon
   glyph, points, secret flag and goal. Test `port/tests/ach_catalog_test.c`: unique ids, 1,680 points, every string
   filled.
2. **Store** (`port/src/ach/store.c`, `store.h`): load v1 and v2, unlock, set and add progress, save by temp file and
   rename. Pure C over a path, no globals of the loader. Test `ach_store_test.c`: round trip, v1 migration, a
   truncated file.
3. **Rank formula** (`ach_rank()` in `store.c`'s neighbour `rank.c`): chapter karma, `{max, min}`, difficulty → rank.
   Test with the owner's chapter 1 numbers (405,327 → 0) and the edges of each third and the Ninja Dog map.
4. **Detectors** (`port/src/ach/ach.c`): `find_vars` also returns the run's karma, the karma at the last chapter end
   and the rank table pointer, each sanity-checked; chapter, story, rank and port detectors; queue toasts through
   `toast_show()`. Signature test with synthetic code bytes (`ach_sig_test.c`): the PAL and NTSC-U instruction
   shapes, a missing push, a duplicate push.
5. **Fonts in the core:** regenerate the Shippori subset with 誉 極 試 数 蒐 挑 灯 added (fontTools, a dev tool; the
   command goes in `port/res/fonts/README.md`); embed Josefin Light, Josefin SemiBold and the subset in
   `ngb-core.dll` (`port/res/ngb.rc`, `NGB_RC_CORE`).
6. **Toast** (`port/src/gfx/toast.c`): variants A and D, a queue, the motion of the spec, `--lang`, `--no-ach-toast`;
   the drum hit synthesized at start into a WAV in memory and played with `PlaySoundW(SND_MEMORY | SND_ASYNC)`.
7. **Loader:** options `--lang en|es` and `--no-ach-toast`; F12 tells `ach` a shot was saved; the play time counter
   (game running with its window open) saved every minute and at exit.
8. **Launcher:** the catalog linked in; page `PG_ACH` with filters, rows, scroll and the two switches; strings in
   `strings.h`; `[achievements] toast` in `ngb.ini`; PLAY passes `--lang` and `--no-ach-toast`; the Play page column
   becomes "n / 27"; a `--shot` of the new page for review.
9. **Docs:** [[game-state]], [[getting-started]] (a short "Achievements" section), [[log]], the index; PR with the
   test evidence.

## Checks before the PR

- `port\build.cmd` builds with `/W4 /WX` and every test passes, the new ones included.
- `python -m unittest discover -s tests -v` passes (hygiene).
- A run with `--ach-demo` on PAL and on the modified build shows variant A, then D, in the window back buffer
  (`NGB_GL_SHOT_WINDOW`), and writes the file.
- Loading a chapter 2 save unlocks nothing; the launcher's `--shot --page 4` renders the page with the test file.

## Needs the owner (after the PR)

- Clear the last chapter once, to confirm the story detector (`INFERRED`).
- Phase 1b: done without dumps from the owner (missions, game over, Golden Scarabs; see the design's "Detectors
  (phase 1b)"). Still needed from the owner: which mission is "the stadium" (#12), and which weapons and levels count
  for #20 and #21.
