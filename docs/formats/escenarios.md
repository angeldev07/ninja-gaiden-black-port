---
tags: [ngb, formats, teamninja, stage]
updated: 2026-10-21
status: current
---

# Escenarios: KMD de zona y colisión HITMDL

Cada una de las 17 zonas del juego ([[disco-ngb]]) se reparte en `<zona>_NN.afs` y `<zona>_cmn.afs`. La geometría
está en los KMD de cada parte (`boff_01.kmd`…). Herramienta: `python -I -m ngb_tools.stage export <zona>`
([[herramientas]]). Fuente: `ngb_tools.stage`, más un spike que leyó los 418 KMD de zona y de `cmn`
(6.78 M triángulos, push buffers 1886/1886).

## En qué difiere un KMD de escenario de uno de personaje (`VERIFIED` en 111 KMD)

| Aspecto | Personaje ([[kmd]]) | Escenario |
| --- | --- | --- |
| Descriptor de malla | `u32` = 1 | **`u32 n_parts`** (1-4) más 8 `u32` por parte; la cabecera ocupa `8 + 0x20·n`. El campo `+0x04` de cada submalla indica su parte |
| VB por objeto | 1 | uno **por parte**: `USERDATA0[2]` cuenta VB, no objetos |
| Transformación | jerarquía con T por hueso | **sin HIERARC**. T = R = 0 y S = 0: los vértices ya vienen **en coordenadas de mundo** |
| Grupos | 1-5 | 0 y 6 (grupo 6: billboards en el origen) |
| FVF | `0x112` / `0x116` / `0x042` | `0x142`, `0x152`, `0x012`… (con color de vértice; sin TEX2) |
| Cabecera `0x60` | bbox | centinela ±99999 |
| Push buffer | limpio | 64 KMD traen basura en el `u32` de relleno que sigue a `END`; se tolera |

El **cuerpo de Doku** usa la misma variante multiparte, y por eso `ngb_tools.kmd` no lo exporta. Hay que adaptar
`kmd` (issue de seguimiento).

## Colisión HITMDL (`VERIFIED` en 323 KMD)

Un slot por objeto, con la etiqueta `VHD` y bloques de triángulos (4 × `u16`), planos y vértices `vec4` en
coordenadas de mundo, con winding CCW. `--collision` la exporta como un nodo translúcido. Se comprobó que cae
justo sobre la geometría visible de Boff; el fondo (`boff_99`) no tiene colisión.

## Colocación de objetos

Los `.sob` y `.itm` (`itm_dat2`) **no colocan nada**: contienen estados e hitboxes locales. La colocación de los
props está en el bytecode de los scripts `.sty`, por ejemplo `s_boff01.sty @0x300`, con el opcode `0x80000006`
y 3 `f32`. Está sin descifrar. `INFERRED`.

## Zonas exportadas (2026-10-21, `VERIFIED` con el validador glTF y renders)

- **Las 17 zonas** generan su `.glb` en `NGB_DATA\extracted\stages\<zona>\`, y los 17 pasan el validador.
- Renders revisados a ojo:
  - Boff: ciudad de Tairon, murallas, catedral, bosque y fortaleza;
  - Ssato: aldea Hayabusa con casas, río y acantilados;
  - Chrch: planta de la catedral.
- 8 zonas avisan de "textura sin PNG". Son texturas bump V8U8 / L6V5U5 que [[xpr0]] todavía no decodifica.
- Las variantes idénticas byte a byte (`tomb_04/05/06`, `tln_b_05`) se exportan una sola vez.
- Las variantes que solo se parecen se solapan con `--parts all`: `tln_a_0N`/`1N`, `ssato_0N`/`1N` y `chrch`
  01≈03, 02≈06, 04≈05. `INFERRED`: son estados de la zona en distintos capítulos.

## Pendiente

- Colocación de props desde `.sty`.
- Estados de material (alpha test, MODULATE2X) y TEXANM.
- Texturas bump.
- Los billboards del grupo 6.
