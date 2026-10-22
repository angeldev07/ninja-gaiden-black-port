---
tags: [ngb, team, template]
updated: 2026-10-20
status: current
---

# Brief template for a subagent

Copy it and fill it in. A good brief lets the agent work without asking questions.

```markdown
Role: <RE analyst | investigator | builder | reviewer> of the "native Ninja Gaiden Black (Xbox) to PC port" team (unofficial, non-commercial fan project).

## Context
- Repo: <repo root> (git; do NOT commit).
- Game data (outside the repo, immutable): %NGB_DATA%\game\ ; extracts in %NGB_DATA%\extracted\
- Environment: Windows 11, Python 3.13 (stdlib only, unittest), Bash or PowerShell.
- What is already known: <links or a summary of the relevant vault pages>
- Files owned by other agents in parallel (do NOT touch): <list>. Do NOT write to docs/.

## Task
<what to build or analyze; known format details; what to verify against the public docs>

## Constraints
- Using or reading Microsoft's XDK or anything derived from it is forbidden.
- Run against game data with `python -I`.
- Identifiers and comments in English.

## Tests
<what to test, with synthetic data; command to run>

## Delivery (final response)
Caveman ultra: fragments, no articles or filler, arrows for causality, exact numbers and paths.
- files created or modified
- test results
- <specific data requested>
- doubts, discrepancies and URLs consulted
```
