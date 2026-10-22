---
tags: [ngb, team]
updated: 2026-10-20
status: current
---

# Team: roles, coordination and rules

The main session is the **leader**. The subagents are the team. What is
optimized is tangible progress per token spent.

## Roles

| Role | Agent type | Purpose | Deliverable |
| --- | --- | --- | --- |
| **Leader** | main session | Architecture, decisions (ADRs), distributing work, integrating, verifying and writing the vault | Vault pages, ADRs, roadmap |
| **RE analyst** | `general-purpose` | Build a tool or analyze a format or subsystem | Code with tests and a report in `build/reports/` |
| **Investigator** | `caveman:cavecrew-investigator` or `Explore` | Read-only searches (where is X, who calls Y) | `path:line` or `address` table |
| **Builder** | `caveman:cavecrew-builder` | Edits bounded to 1 or 2 existing files | Summarized diff |
| **Reviewer** | `caveman:cavecrew-reviewer` | Review changes before calling something closed | One finding per line, with severity |

## Coordination rules

1. **The leader is the only writer of the vault.** Agents do not touch `docs/`, and the leader
   ingests their reports. That way there are no conflicts and the style is uniform. (Exception: mechanical
   translations explicitly authorized by the leader.)
2. **Each file has a single owner per task.** Two agents in parallel never edit
   the same file. The brief states this explicitly.
3. **Only independent work is parallelized.** Whatever depends on another result waits.
4. **Self-contained briefs** following [[agent-task-template]]: context, task, paths,
   constraints, tests and delivery format.
5. **Evidence before claiming.** A report without an anchor (offset, file#entry or
   command) is a hypothesis. The leader verifies a sample of each report before
   marking it `VERIFIED`.
6. **A single review round per PR** (decision of 2026-10-20, at the project owner's request: "more nitro"). Only
   two things block: a real bug with the game data and a leak of game data into the repo. Hardening against
   hostile files, theoretical risks and nits go to a follow-up issue, and the PR
   is merged with CI green. The inputs are the official disc, not adversarial files.
6b. **Several fronts in parallel:** 3 or 4 agents at once in separate worktrees (assets, audio, native
   loader…). The vault is updated in batches per milestone.
7. **Caveman ultra only in the agent's response to the leader**, to save tokens. Everything
   that is stored in files (code, comments, reports, vault) is in normal prose.
8. **Agents do not commit.** The leader manages branches, commits, PRs, reviews and
   merges ([[adr-04-version-control]]).
9. **Game data:** it is read with `python -I` and never written inside the repo
   ([[adr-03-game-data-outside-repo]]).

## Debugging and verification

- Skills: `superpowers:systematic-debugging` for any failure,
  `superpowers:test-driven-development` for tools and new code, and
  `superpowers:verification-before-completion` before declaring something done.
- **Differential tests** (from H9): a decompiled function is compared against the
  original with thousands of inputs. Precedent: the DX-Ball showcase of [[rea]].
- **xemu as an oracle:** its GDB stub allows setting breakpoints in the real game to
  confirm `UNVERIFIED` hypotheses.

## Deployment history

| Date | Milestone | Agents | Result |
| --- | --- | --- | --- |
| 2026-10-20 | H1 | XBE analyst (`tools/xbe_info.py`) and AFS analyst (`tools/afs.py`) in parallel | see [[log]] |
