---
tags: [ngb, reverse-engineering, characters]
updated: 2026-10-21
status: current
---

# Personajes del juego: galería

Hay 62 contenedores `chr_*.afs` ([[disco-ngb]]). `python -I -m ngb_tools.gallery build` exporta el KMD principal
de cada uno a GLB ([[kmd]]), le añade su idle si encuentra el banco de motions ([[mtn]]) y compone una hoja de
contactos. Fuente: la ejecución del 2026-10-21. La salida está en `NGB_DATA/extracted/gallery/` (`gallery.png`,
`gallery.md`, `gallery.json`). Las cifras son `VERIFIED`, salidas de esa ejecución; la hoja la revisó el líder.

## Resultado

- **49 de 62 exportan; 20 con idle animado:** Ryu y sus 7 trajes, cat, daina, genin, hanin, kage, m_arm, murai,
  rider, s_kag, swat, taike y yonin. El rig humanoide generaliza más allá de Ryu (revisado en murai, genin, cat y
  kage).
- Se reconocen en la hoja: Ayane, Rachel (`rei`), Ryu y sus trajes, Murai, genin, jonin, Garra, Haiki (araña),
  Kasek (esqueleto), Daina, el caballo y el jinete, la rata y Gamof con gabardina, entre otros.

## Fallos (13)

| Causa | Personajes |
| --- | --- |
| Descriptor multiparte (más VB que objetos, ver [[escenarios]]) | army, aruma, doku, heri, s_aru, sohei, tank, truck, ufo, whip |
| HIERARC con otra disposición (raíz fuera de rango) | unagi |
| Sin KMD en el AFS | dragn (su modelo es `dragn.kmd` dentro de `chr_s_mur`), e_ryu (solo trae motions) |

## Pendiente

- Soporte multiparte en `kmd` (en curso), con el que deberían salir los 10 primeros.
- Un rig por especie (crow, bat, fish, horse…) y bancos de motion con nombres distintos (ayane, jonin…).
- El KMD "principal" elegido por nombre no siempre es el cuerpo: `s_dok`, `sumau` y `s_kot`.
