---
tags: [ngb, reverse-engineering, characters]
updated: 2026-10-21
status: current
---

# Game characters: gallery

There are 62 `chr_*.afs` containers ([[ngb-disc]]). `python -I -m ngb_tools.gallery build` exports the main KMD
of each one to GLB ([[kmd]]), adds its idle if it finds the motion bank ([[mtn]]) and composes a contact
sheet. Source: the run of 2026-10-21. The output is in `NGB_DATA/extracted/gallery/` (`gallery.png`,
`gallery.md`, `gallery.json`). The figures are `VERIFIED`, outputs of that run; the leader reviewed the sheet.

## Result

- **With multipart support (2026-10-21), 59 of the 60 main KMD export**; before it was 49. Only unagi fails, because of its HIERARC variant. The contact sheet below predates that change.
- **49 of 62 exported, 20 with an animated idle:** Ryu and his 7 outfits, cat, daina, genin, hanin, kage, m_arm, murai,
  rider, s_kag, swat, taike and yonin. The humanoid rig generalizes beyond Ryu (checked on murai, genin, cat and
  kage).
- Recognized on the sheet: Ayane, Rachel (`rei`), Ryu and his outfits, Murai, genin, jonin, Garra, Haiki (spider),
  Kasek (skeleton), Daina, the horse and the rider, the rat and Gamof with a trench coat, among others.

## Failures (13)

| Cause | Characters |
| --- | --- |
| Multipart descriptor (more VBs than objects, see [[stages]]) | army, aruma, doku, heri, s_aru, sohei, tank, truck, ufo, whip |
| HIERARC with another layout (root out of range) | unagi |
| No KMD in the AFS | dragn (its model is `dragn.kmd` inside `chr_s_mur`), e_ryu (only carries motions) |

## Pending

- Multipart support in `kmd` (in progress), which should make the first 10 work.
- A rig per species (crow, bat, fish, horse…) and motion banks with different names (ayane, jonin…).
- The "main" KMD chosen by name is not always the body: `s_dok`, `sumau` and `s_kot`.
