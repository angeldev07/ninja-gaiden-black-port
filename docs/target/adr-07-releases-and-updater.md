---
tags: [ngb, target, adr, release]
updated: 2026-10-22
status: current
---

# ADR-07: tag-driven releases and a launcher updater

## Context

The repository is about to go public, and players expect to download a launcher and stay current. Until v0.2.0 the
release zip was built by hand, `port/res/version.h` was not kept in step with the tags (it said 0.1.0 at v0.2.0),
and a player had no way to learn that a new version existed.

Constraints: no game data in any release ([[adr-03-game-data-outside-repo]]); the launcher stays Win32 + GDI with no
third-party libraries; Python tools are stdlib only; the owner reviews what goes out.

## Decision

### Releases (`.github/workflows/release.yml`)

- **Version:** between releases `version.h` carries the next version with `-dev` (`0.3.0-dev`). A
  `chore: release vX.Y.Z` PR sets it to `X.Y.Z`. Pushing the tag `vX.Y.Z` runs the workflow, which fails if
  `version.h` does not match the tag.
- **Build:** a Windows runner finds Visual Studio with `vswhere`, passes it as `VCVARSALL`, and runs `port\build.cmd`
  (C tests included) and the Python tests.
- **Package** (`tools/package_release.py`, stdlib, unit-tested):
  - `ngb-port-vX.Y.Z-win32.zip` holds `ngb-launcher.exe`, `ngb-loader.exe`, `ngb-core.dll`, `LICENSE`,
    `THIRD_PARTY.md` and a short `README.txt` for players. Nothing else; the hygiene guard checks the staged files.
  - `ngb-update.txt` is the update manifest (`key=value` lines): `version`, `zip`, `size`, `sha256`, `notes`.
- **Publication:** the workflow creates a **draft** release with the fixed legal note and the changelog. The owner
  publishes it by hand. Only a published release is "latest", which is what launchers see.
- **Rehearsal:** running the workflow by hand (`workflow_dispatch`) builds and packages `version.h` as it is
  (`python -m ngb_tools.release --dry-run`) and uploads the zip as an artifact, with no tag and no release.

### How to cut a release

1. A PR `chore: release vX.Y.Z` sets `port/res/version.h` to `X.Y.Z` (string and numbers) and is merged.
2. `git tag vX.Y.Z` on that commit of `main`, then `git push origin vX.Y.Z`.
3. Review the draft release (notes, the zip, `ngb-update.txt`) and click **Publish**.
4. A PR sets `version.h` to the next `-dev` version.

### Updater (`port/launcher/update.c`)

- **Check:** at launcher start, at most once per 24 h, a WinHTTP GET of
  `https://github.com/angeldev07/ninja-gaiden-black-port/releases/latest/download/ngb-update.txt`.
  - A fixed GitHub URL means no JSON parsing and no API rate limit.
  - Offline or any error is silent.
  - The *Check for updates* switch (Advanced) is on by default.
- **Notice:** if the manifest's version is newer than `NGB_VERSION_STR` (semantic order, `0.3.0-dev < 0.3.0 < 0.3.1`),
  the Play page shows a band "Version X available" with *What's new* (opens the release) and *Update*.
- **Update:**
  - Refused while the game runs.
  - The zip is downloaded to `NGB_DATA\updates\` with a progress bar.
  - Its size and SHA-256 (Windows BCrypt) must match the manifest; otherwise it is deleted and nothing changes.
  - It is extracted with the `tar.exe` that Windows ships since 10 1803, so there is no zip reader of our own.
- **Apply:**
  - Windows lets a running exe be renamed, so each of the three binaries is renamed to `*.old` and the new one copied
    in. On any failure the `.old` files are restored.
  - The launcher then restarts itself with `--updated`, which deletes the `.old` files and says "Updated to X".
  - `ngb.ini` and the data folder are never touched.
- **Testing hook:** `--update-url URL` replaces the manifest URL and checks at once. A URL without `://` is a local
  file, so the whole flow can be checked without a real release.
  - `port/tests/update_test.c` (run by `build.cmd`) covers the manifest, the version order, SHA-256 vectors and a
    full local update with a tampered-zip case.
  - The test exe is called `upd_test.exe`: Windows asks for elevation for any unmanifested exe whose name contains
    "update".

## Consequences

- One manual step per release: publish the draft. Nothing reaches players unreviewed.
- **Integrity, not authenticity:** HTTPS plus the SHA-256 in the manifest catch a corrupt or truncated download.
  They do not protect against a compromised GitHub account, since the manifest comes from the same place. Signing
  the manifest with an owner key is a future improvement.
- **Unsigned binaries:** Windows SmartScreen warns on first run. The player guide explains it.
- `tar.exe` limits updates to Windows 10 1803 or later, which the port already needs (OpenGL 4.6 drivers).
