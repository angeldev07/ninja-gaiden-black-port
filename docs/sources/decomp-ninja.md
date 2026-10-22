---
tags: [ngb, source, prior-art]
updated: 2026-10-20
status: current
---

# Prior work on NGB

## DECOMP__Ninja (abandoned)

- Repo: https://github.com/mehmetalinya/DECOMP__Ninja. Last push on 2022-07-02, 6 stars. `VERIFIED` via the GitHub API.
- It is almost only a folder structure: `Ninja Gaiden Black Decomp`, `Ninja Gaiden Black
  Prototype Decomp`, `SDK/` and equivalents for NG2. The NGB folders are practically empty.
- The author stopped because they considered the asset formats "UNKNOWN" (custom compression or encryption).
- **What we saw ourselves:** the containers on the disc are **CRI Middleware AFS**, with the magic
  `AFS\0` (`VERIFIED`, [[ngb-disc]]). The container format is documented in open
  implementations such as [AFSLib](https://github.com/MaikelChan/AFSLib) and
  [Sewer56/AfsLib](https://github.com/Sewer56/AfsLib). What is unknown, if anything, is the content of each
  entry ([[afs]]).
- The name "Prototype Decomp" suggests that an NG prototype exists. `UNVERIFIED`: a prototype with
  symbols would greatly change the feasibility of decompiling, as happened with [[opence]].

## Official context

- Team Ninja (Fumihiko Yasuda, 2021) said NGB was left out of the *Master Collection* because they no longer have
  the complete source code. Source:
  https://gameranx.com/updates/id/219898/article/ninja-gaiden-black-left-out-of-master-collection-thanks-to-lost-code/
