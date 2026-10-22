---
tags: [ngb, target, adr, git]
updated: 2026-10-20
status: current
---

# ADR-04: version control, PRs and quality

## Context

The project owner delegated the complete management of git to the leader: branches, commits, PRs, review
and merge (2026-10-20), with the requirement that the code be of a **high engineering
standard**. The remote is the GitHub repo `github.com/angeldev07/ninja-gaiden-black-port`.

## Decision

- **Trunk-based:** `main` is always green. Branches are short-lived, named
  `<type>/<slug>`, where the type is `feat`, `fix`, `docs`, `ci`, `refactor`, `test` or `chore`.
- **Conventional Commits.** The type and the description are in English:
  `feat(afs): container inventory`. Each commit ends with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- **One PR per coherent unit.** Each includes code, tests and the vault update that
  corresponds to it: an undocumented change is not considered finished.
- **Merge:** squash and delete the branch. The PR title becomes the commit on `main`.
- **Review:** before merging, the reviewer (`caveman:cavecrew-reviewer`) and the leader
  review the diff, **in a single round** (see [[team]] § rules). GitHub **does not allow approving your own PR** (the
  author account is the same), so the review is posted as a comment (`gh pr review --comment`) with
  a verdict of `APPROVED` or `BLOCKING`. It is merged only if CI is green and no
  blockers remain.
- **Quality gates (CI in GitHub Actions):**
  - `unittest` on Ubuntu and Windows with Python 3.13;
  - `ruff check` and `ruff format --check`;
  - `mypy`;
  - **game-data guard**: fails if any game extension or
    a binary larger than 1 MB is versioned.
- **Milestones:** when each milestone of the [[roadmap]] closes, an annotated tag
  `milestone/hN` is created on `main`.

## GitHub repo configuration (applied on 2026-10-20 via `gh api`, `VERIFIED` by the response)

- Merge: **squash only** (`allow_merge_commit=false`, `allow_rebase_merge=false`). The commit
  title is the PR's and the body is the PR's. The branch is deleted on merge.
- Protection of `main`:
  - required checks: `game data guard`, `ruff + mypy`, `tests (ubuntu-latest)` and
    `tests (windows-latest)`;
  - `strict`: the branch must be up to date with `main` before merging;
  - linear history; no force-push or deletion;
  - **also for admins** (`enforce_admins`).
- Since only the final tree of each PR (squash) enters `main`, it is enough for the guard to check the
  tree and not the history: the intermediate commits of a PR never reach `main`.

## Gotchas

- **Network:** the session sandbox blocks `github.com` (git over https). `api.github.com`
  responds intermittently. The `git push/fetch` and `gh` commands are run outside
  the sandbox, with `gh`'s credential helper
  (`git -c credential.helper= -c "credential.helper=!gh auth git-credential" push`).
- Line endings: `.gitattributes` normalizes to LF (`* text=auto eol=lf`).
