---
tags: [ngb, schema]
updated: 2026-10-22
status: current
---

# NGB Port Wiki: schema and conventions

**LLM-maintained** knowledge base for the native PC port of Ninja Gaiden
Black (Xbox, PAL). It follows the [[wiki-llm]] pattern. Obsidian is the reader. The LLM
writes and maintains all the pages. The human directs, asks questions and curates.

**It is the project's context brain.** A new session starts by reading
[[GOAL-ngb-port]]. Then it reads [[index]] and then whichever pages it needs. That is
cheaper than re-analyzing binaries or redoing searches.

## Layers

- **Raw sources (immutable, outside the repo):** the game data, extracted from the
  user's own copy and stored under `NGB_DATA`:
  - `game/`: the disc tree (extracted XISO). Never modified.
  - `extracted/`: content pulled out of the `.afs` files and other containers.
  External sources (repos, wikis, public docs) are summarized in `sources/`.
- **The wiki:** this directory (`docs/`). Markdown owned by the LLM.
- **The schema:** this file.
- **The tools:** `tools/` and `src/` at the repo root. They produce reports in `build/reports/`
  (ignored by git). The wiki cites those reports and summarizes them. It does not copy them whole.

## Directory structure

```
docs/
  CLAUDE.md             ← this file (schema)
  GOAL-ngb-port.md      ← entry point of every new session
  index.md              ← catalog of all pages
  log.md                ← chronological record, append-only
  wiki-llm.md           ← the pattern we follow
  reverse-engineering/  ← OBSERVATION: what is on the disc and in the binaries
  formats/              ← OBSERVATION: one page per file format
  findings/             ← OBSERVATION: individual findings F1…Fn (fact → evidence → impact)
  target/               ← PROPOSAL: ADRs, roadmap, target architecture
  team/                 ← how the agent team works
  tools/                ← how to use our tools
  sources/              ← summaries of external sources (projects, docs)
```

Obsidian resolves `[[links]]` by file name across the whole vault. That is why
**page names are unique** across `docs/`.

## Reverse-engineering rules (inherited from the audit standard)

- **Every claim carries an anchor.** Examples:
  - Binary: `default.xbe @ 0x0001A2B0 (FUN_0001a2b0)`, with virtual address and name if one exists.
  - File: `cmn.afs#12`, with file and entry number.
  - Tool: `tools/afs.py → parse_toc`.
  A claim without an anchor is a hypothesis and is marked as such.
- **Confidence tags** on everything that was not read directly:
  - `VERIFIED`: read in the bytes, in tool output or in the official docs.
  - `INFERRED`: deduced without running or confirming it.
  - `UNVERIFIED`: needs checking, for example in xemu or in a debugger.
- **Observation separated from proposal.** `reverse-engineering/`, `formats/` and
  `findings/` describe only what exists. `target/` holds design and decisions,
  and is never presented as the current state.
- **Every number says where it came from:** the command, the report or the offset.

## Legal rules (non-negotiable)

- No byte of the game ever goes into the repo: binaries, assets, extracts or large
  memory dumps. Two barriers enforce this: `.gitignore` (by extension) and the
  `ngb_tools.hygiene` guard (by extension, binary signature and size), which runs in the
  pre-commit hook and in CI ([[adr-04-version-control]]).
- Using Microsoft's leaked XDK or anything derived from it (headers,
  libraries) is forbidden. OpenCE rewrote the SDK declarations without the XDK, starting from the
  debug information of Halo builds and from public documentation ([[opence]]).
  NGB has no debug information, so here the declarations come only from public
  documentation and from our own analysis.
- This is an unofficial, non-commercial fan project. Each person uses their
  own copy of the game.

## Page conventions

- **Frontmatter** YAML on every page. There are two exceptions: [[log]], which is an
  append-only record, and [[wiki-llm]], which is a verbatim copy of an external source:
  ```yaml
  ---
  tags: [ngb, <area>]
  updated: YYYY-MM-DD
  status: current | draft | superseded
  ---
  ```
- **Wikilinks** `[[page-name]]`; link freely. A link to a page that does not exist
  yet works as a TODO marker.
- **Density and facts.** The preferred shape is "What it is → How it works → Evidence →
  Gotchas → Next step".
- **Language:** English. Technical terms, identifiers and commands are kept as they are.
- **Contradictions:** if a new fact invalidates another, the old page is corrected and
  the change is noted in [[log]].

## Workflows

**Ingest** (something is learned): update the affected pages and their links,
add one line to [[index]] for each new page and record the entry in [[log]].

**Query** (a question): read [[index]], drill down to the pages and answer with citations.
If the answer is a synthesis worth keeping, save it as a page.

**Lint** (health check): look for contradictions, stale claims, orphan pages,
concepts without their own page and broken anchors.

## Log format

Append-only at the end, one header line per entry, so that
`grep "^## \[" log.md` works:

```
## [YYYY-MM-DD] <ingest|query|lint|decision|milestone> | <short title>
```

## Agent team

The leader (the main session) is the **only writer of the vault**. Subagents
deliver reports in caveman ultra format and the leader ingests them into the vault in
normal prose. The roles, templates and rules are in [[team]].
