# Ninja Gaiden Black: native PC port

**Before anything else, read `docs/GOAL-ngb-port.md`.** Then read `docs/index.md` and the
latest entries of `docs/log.md` (`grep "^## \[" docs/log.md | tail -5`). `docs/` is the
project's context brain: an Obsidian vault that follows the LLM Wiki pattern. Its schema is in
`docs/CLAUDE.md`.

## Repository rules

- **Nothing from the game goes in the repo.** Game data lives in `%USERPROFILE%\ngb-data\`: `game/` (immutable)
  and `extracted/`. `.gitignore` and the `ngb-hygiene` check block game files. See
  `docs/target/adr-03-datos-fuera-del-repo.md`.
- **Never use the leaked Microsoft XDK** or anything derived from it.
- **No official artwork, logos or trademarks** as assets: launcher art and icons are original.
- Tools that read game data run with `python -I`.
- Python 3.13, stdlib only. Run the tests with `unittest`: `python -m unittest discover -s tests -v`.
- **Everything is in English:** code identifiers, comments, log and UI strings, and docs. The
  launcher also ships a Spanish UI translation.
- The project is going open source. Write for outside contributors, and never call it a
  "pedagogical" project.
- Every session that learns something updates the vault: the affected pages, `docs/index.md`
  when a page is added, and an entry in `docs/log.md`.
- **Agent team:**
  - The leader is the only writer in `docs/`, except for mechanical translations the leader
    authorizes.
  - Subagents report in caveman ultra. See `docs/team/equipo.md`.
- **Version control:**
  - The leader manages branches, commits, PRs, reviews and merges (authorized by the user on
    2026-10-20). Flow in `docs/target/adr-04-control-de-versiones.md`.
  - Subagents never commit.
