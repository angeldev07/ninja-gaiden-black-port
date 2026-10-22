# Contributing

Thanks for your interest in the Ninja Gaiden Black native PC port. This is an unofficial,
non-commercial fan project. Contributions of every size are welcome, from a one-line compatibility
report to a renderer fix.

By taking part you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md). Read the
[Legal rules](#legal-rules-mandatory) before you open anything: they are not optional.

## Ways to help

- **Test and report compatibility.** Play chapters on your own hardware and open a
  *Compatibility report* issue: how far you got, fps, GPU and driver.
- **Report bugs.** Use the *Bug report* issue form. The log from the launcher
  (*Advanced > Debugging*) and *Copy session info* make a report actionable.
- **Write code.** The native runtime lives in `port/` (C), the reverse-engineering tools in
  `src/ngb_tools/` (Python). The [roadmap](docs/target/roadmap.md) lists what is open.
- **Document reverse engineering.** Formats, findings and notes live in `docs/`. New, verified
  facts are as valuable as code.

For anything bigger than a small fix, open an issue or a discussion first so we can agree on the
approach before you spend time on it.

## Build and test

Requirements:

- Visual Studio 2022 with the C++ x86 toolset (for `port/`);
- Python 3.13. The tools and tests use the standard library only; `ruff` and `mypy` are the only
  dev dependencies.

```bat
port\build.cmd
python -m unittest discover -s tests -v
```

`build.cmd` compiles with `/W4 /WX` (warnings are errors) and runs the C unit tests. If Visual
Studio is not in the default path, set `VCVARSALL` to your `vcvarsall.bat`.

Lint and type checks, which CI also runs:

```bat
pip install -e ".[dev]"
ruff check .
ruff format --check .
mypy
```

Enable the pre-commit hook once per clone. It runs the game-data guard on what you staged:

```bat
git config core.hooksPath .githooks
```

## Workflow

1. Fork the repository and clone your fork.
2. Create a short-lived branch from `main`, named `<type>/<slug>`, where the type is one of
   `feat`, `fix`, `docs`, `ci`, `refactor`, `test` or `chore` (for example `fix/apu-underrun`).
3. Commit with [Conventional Commits](https://www.conventionalcommits.org/), in English:
   `feat(afs): container inventory`. Sign off every commit (see
   [Developer Certificate of Origin](#developer-certificate-of-origin)).
4. Keep one pull request per coherent unit: code, tests and the matching docs update together.
5. Open a pull request against `main` and fill in the template. Describe what changed, why, and
   what you ran to check it.
6. CI must be green: game-data guard, ruff + mypy, tests on Ubuntu and Windows, and the DCO
   sign-off check.
7. Review is a single round. Only real bugs block a merge. The maintainer squash-merges, and the
   PR title becomes the commit on `main`, so make it a good Conventional Commit title.

## Documentation vault (`docs/`)

`docs/` is the project's knowledge base (an Obsidian vault). A contributor pull request may add
or update pages there. The schema and conventions are in [`docs/CLAUDE.md`](docs/CLAUDE.md).
The short version:

- Every reverse-engineering claim carries an **anchor**: `default.xbe @ 0x0001A2B0`,
  `cmn.afs#12`, or the tool and function that produced it.
- Every claim not read directly from bytes or tool output carries a confidence tag:
  `VERIFIED`, `INFERRED` or `UNVERIFIED`.
- Observation (`reverse-engineering/`, `formats/`, `findings/`) is kept apart from proposals
  (`target/`).
- Page names are unique across `docs/`; new pages get a line in `docs/index.md`.
- Docs are written in English.

## Legal rules (mandatory)

This project only works if it stays clean. These rules apply to code, docs, issues, pull requests
and attachments.

- **No game data of any kind.** No binaries, assets, extracted files, screenshots of game
  content committed to the repo, memory dumps, or decompiled game code pasted into the repository.
  Game data lives outside the repo, under `%NGB_DATA%`.
- **Never the leaked Microsoft XDK**, or anything derived from it: headers, libraries,
  declarations, comments. Xbox kernel declarations come from public sources (nxdk, CC0) or from
  your own analysis.
- **No Microsoft or Xbox keys** of any kind.
- **No links to ROM or ISO sites.** Everyone uses their own copy of the game.
- **No official artwork or logos.** Launcher art and icons must be original.
- **Third-party code only if its license is GPL-3.0-compatible**, and it gets an entry in
  [`THIRD_PARTY.md`](THIRD_PARTY.md) with its origin and license.
- **The hygiene guard must pass:** `python -m ngb_tools.hygiene`. It runs in the pre-commit hook
  and in CI.

Pull requests and issues that violate these rules are closed without merge.

## Developer Certificate of Origin

Every commit must carry a `Signed-off-by: Name <email>` trailer. Add it with:

```bat
git commit -s
```

The sign-off means you certify the [Developer Certificate of Origin 1.1](https://developercertificate.org/)
for that commit, and that your work does not come from the game or from the Microsoft XDK.
A CI check ("DCO sign-off") fails the pull request if any commit is missing the trailer.

Forgot it? Fix the last commit with `git commit --amend -s --no-edit`, or sign off a whole
branch with `git rebase --signoff main` and force-push to your branch.

Pseudonyms and GitHub noreply emails (`<id>+<user>@users.noreply.github.com`) are fine.

## Money

The project accepts no donations, sponsorship or payments of any kind. It is, and will stay,
non-commercial.

## License

By contributing you agree that your contribution is licensed under
[GPL-3.0-or-later](LICENSE), the license of the project.
