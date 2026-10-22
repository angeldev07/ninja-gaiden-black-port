---
tags: [ngb, meta]
updated: 2026-10-22
status: current
---

# LLM Wiki (the pattern this vault follows)

**Source:** Andrej Karpathy's "LLM Wiki" idea file, published as a GitHub gist in April 2026
(https://gist.github.com/karpathy). This page is our own summary; read the original for the full text.

## The idea in short

- Retrieval over raw documents (RAG) rediscovers the same knowledge on every question. Instead, an LLM
  **builds and maintains a persistent wiki**: interlinked Markdown pages that sit between the human and the raw
  sources. Each new source is read once and folded into the existing pages, and contradictions are flagged.
- The human picks the sources, asks the questions and curates. The LLM does the summarizing,
  cross-referencing and bookkeeping. Obsidian is the reader.
- **Three layers:** immutable raw sources, the LLM-owned wiki, and a schema file that tells the LLM how the wiki
  is organized.
- **Three operations:** *ingest* (a new source updates the affected pages, the index and the log), *query*
  (answer from the wiki, and save good syntheses as new pages) and *lint* (look for contradictions, stale
  claims and orphan pages).
- An **index** lists every page and an append-only **log** records what changed and when.

## How this project applies it

- Raw sources: the game data under `NGB_DATA` (never in the repo) and the external sources summarized in
  `sources/`.
- The wiki: `docs/`. The schema: [[CLAUDE]] (`docs/CLAUDE.md`). Entry point: [[GOAL-ngb-port]]. Catalog:
  [[index]]. Record: [[log]].
- On top of the pattern we add reverse-engineering rules: every claim carries an anchor and a
  `VERIFIED` / `INFERRED` / `UNVERIFIED` tag.
