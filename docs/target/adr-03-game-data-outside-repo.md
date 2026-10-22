---
tags: [ngb, target, adr, legal]
updated: 2026-10-20
status: current
---

# ADR-03: game data lives outside the repo (NGB_DATA)

## Decision

The game data is stored in `NGB_DATA` (an environment variable that points to a directory outside the repo):
- `game/`: the disc tree (output of `extract-xiso -x`). Never modified.
- `extracted/<container>/`: content pulled out of `.afs` and other containers.

The tools receive the paths as arguments. Nothing from the game enters the repo, thanks to
two barriers:
- `.gitignore`, by extension: `*.iso`, `*.xiso`, `*.xbe`, `*.afs`, `*.sfd`, `*.xpr`, `*.xwb`,
  `*.xsb`, `*.aix`, `*.adx`, `*.bin`, `*.def`, `*.ins`, `*.ttf`, plus `game/`, `extracted/` and `build/`.
  A freely licensed font that someone wants to version needs an explicit `git add -f`.
- `ngb_tools.hygiene`, which runs in the pre-commit hook and in CI. **It always reads the blob of the git
  index**, never the disk. It ignores `refs/replace` and does not download anything in partial clones. It applies four
  rules:
  - **Extension**, compared case-insensitively and with Unicode folding. It covers the disc formats, the
    AFS-internal ones (`.kmd`, `.spr`, `.sty`, `.wva`, …) and the compressed ones (`.zip`, `.7z`, `.rar`,
    `.gz`, …).
  - **Binary signature** (heuristic): `XBEH`, `AFS\0`, `XPR0`, `KMD\0`, ADX, `WBND`, `SDBK`, `AIXF`, MPEG-PS,
    the Team Ninja chunk header, XDVDFS at sector 32 and the signatures of compressed formats.
  - **Size:** more than 1 MiB.
  - **Path:** `game/`, `extracted/` and `build/` at the root, even if added with `add -f`.

  See [[adr-04-version-control]].

### Exceptions

- **`.hygiene-allow`**: a file at the root with one exact path per line. An allowed path only skips
  the **extension** rule, never the signature, size or path rules. In the hook the `HEAD` version is read,
  so the exception must be committed first, separately and visible in the PR. Cap of 64 KiB.
- **`.ttf`** is not blocked by extension, because a freely licensed font can be legitimate. The game's fonts
  (23 MB) are caught by the size rule. Since `.gitignore` does include `*.ttf`, a free font needs
  a deliberate `add -f`.
- **Known limit:** the guard checks the tree, not the history. That is enough because `main` only accepts squash
  merges ([[adr-04-version-control]]).

## Why

- **Legal:** the repo is public and must contain only original work
  ([[CLAUDE]] § legal).
- **Cloud sync:** if the repo lives in a cloud-synced folder (for example OneDrive), putting 6.3 GB of game
  files there would sync them to the cloud. `NGB_DATA` must be outside any synced folder.
- **Immutability:** if the raw sources are kept separate, no script can
  modify them by accident.
