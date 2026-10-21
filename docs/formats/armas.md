---
tags: [ngb, formats, teamninja, weapons]
updated: 2026-10-21
status: current
---

# Armas: modelos y anclaje (`.itm`)

Cada arma está en `cmn.afs` como un par `<arma>.kmd` ([[kmd]]) + `<arma>.itm` (`itm_dat2`,
[[chunk-teamninja]]). Herramienta:
`python -I -m ngb_tools.kmd export <ryu.kmd> --attach <arma.kmd>@<arma.itm>#<estado>` ([[herramientas]]).

## Inventario (`VERIFIED`, por nombre de entrada en `cmn.afs`)

| Arma | `.kmd` | `.itm` |
| --- | --- | --- |
| Dragon Sword | `katana` #70 (mejoras `kata2` #72 y `kata3` #73) | #192 |
| Nunchaku | `nchak` #76 | #193 |
| Flail | `flail` #128 (`fl2` #129) | #208 |
| Lunar | `rod` #291 (`rod2`, `rod3`) | #290 |
| War Hammer | `hammer` #85 (`ham2`) | #196 |
| Dabilahro | `zanba` #89 (`zan2`, `zan3`) | #197 |
| Kitetsu | `yoto` #109 | #249 |
| Unlabored Flawlessness | `boku` #104 | #221 |
| Funda | `sheath` #87 | #245 |
| ? | `skata` #116 | — |

El emparejamiento de nombre a arma del juego es `INFERRED`. En `katana.kmd`, obj0 es la espada (1,724 vértices),
con la hoja sobre +z y la empuñadura en el origen; obj2-17 son efectos del grupo 6.

## Tabla de anclaje: slot 1 del `.itm` (`VERIFIED` estructuralmente; la semántica de los estados es `INFERRED`)

Son registros de `0x20` bytes que terminan en `0xffffff00`:

| Offset | Campo |
| --- | --- |
| `+0x00` | `u32` clave: los 16 bits bajos son el **estado** y los 16 altos la **variante** |
| `+0x04` | `u32` **hueso**, como índice de objeto en el KMD del personaje |
| `+0x08` | `f32×3` posición |
| `+0x14` | `f32×3` rotación en grados (Euler Rz·Ry·Rx) |

- Huesos que aparecen: 11 (mano derecha), 5 (mano izquierda), 0 (pecho) y 2 (cadera).
- **Estado 1 = guardada** (en el pecho o la cadera). **Estados 2 y 3 = en la mano** (hueso 11).
- Katana, estado 2: posición (−0.058, 0, −0.025) y Ry(−13°). En el render, la empuñadura queda en la palma (`INFERRED`, inspección visual de `sword_preview.png` en `NGB_DATA\extracted\models\ryu\`).
- Katana, estado 1: espalda, con rotación (84.2°, 180.3°, 31.0°). El render la coloca con el puño sobre el hombro
  derecho, la pose icónica de Ryu. Es **consistente con el orden Euler Rz·Ry·Rx** con tres ejes (inspección visual, `INFERRED`).
- Las armas del mismo tipo comparten tabla: espadas (katana, yoto, boku, skata), pesadas (hammer, zanba) y
  nunchaku/flail.

## Resultado (`VERIFIED` con `sword_preview.png`, revisado por el líder)

`NGB_DATA\extracted\models\ryu\ryu_sword.glb`: Ryu con la Dragon Sword en la mano. Clips: guardia y tajo con giro
(`rm_kt`), idle y correr (`ryu.mtn`). La espada sigue a la mano en todos.

## Pendiente

- Cambiar entre guardada y en mano según la motion: hoy se elige un único estado por exportación.
- Variante `0x3b` (hueso 16), estados 4-9 y el slot 0 del `.itm` (volúmenes de impacto).
- Renders de las demás armas.
