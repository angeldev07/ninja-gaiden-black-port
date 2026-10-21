---
tags: [ngb, formats, teamninja, model]
updated: 2026-10-20
status: draft
---

# KMD: modelos y escenas del motor Kunai

Es un chunk de [[chunk-teamninja]] con etiqueta `KMD`. Hay 724 KMD en el juego (751.8 MB), que son
personajes y escenarios ([[afs]]).

## Slots fijos (`INFERRED`: script exploratorio del analista, no versionado, sobre los 724 KMD; 709 tienen 10 slots y 15 tienen 9)

| Slot | Subchunk | KMD que lo usan | Hipótesis |
| --- | --- | --- | --- |
| 0 | `OBJINFO` | 724 | un registro por objeto (malla) |
| 1 | `XPR0` | 724 | **texturas, vertex buffers e index buffers en un Xbox Packed Resource** → [[xpr0]] |
| 2 | `HIERARC` | 239 | jerarquía de huesos y nodos |
| 3 | `HITMDL` | 375 | malla de colisión |
| 4 | `TEXANM` | 216 | animación de texturas |
| 5 | `MORPH` | 49 | morph targets |
| 6 | `ACSSRI` | 36 | accesorios |
| 7 | `MISCDATA` | 153 | datos varios |
| 8 | `OBJHIT` | 111 | hitboxes |
| 9 | `EFFSCR` | 55 | scripts de efectos |

- En `0x58` va el nombre y en `0x60` ocho floats (`INFERRED`: bounding box; en escenarios contiene basura).

## `ryu.kmd` (`chr_ryu.afs#0`, 1,018,944 bytes)

- 59 objetos: 59 OBJINFO, 59 vertex buffers y 59 index buffers, 1 pushbuffer y 60 USERDATA.
- XPR0 en `0x3000` con un total de `0xDC000` bytes y una cabecera de `0x6590`. `VERIFIED` por el líder.
- **19 texturas:** 14 DXT1 y 5 DXT4/5 (`VERIFIED` por el analista, contando los formatos D3D de los recursos XPR0).
- HIERARC con 59 slots (57 usados), MORPH, ACSSRI, MISCDATA y OBJHIT con 11 hitboxes.

## Texturas de `ryu.kmd` (H2, `VERIFIED`: exportadas con `ngb_tools.xpr` y revisadas a ojo)

Todas tienen 1 mip. El XPR0 está en el slot 1 (`0x3000`).

| # | Formato | Tamaño | Qué se ve |
| --- | --- | --- | --- |
| 0 | DXT1 | 256x512 | torso del traje negro: correas y hebillas |
| 1 | DXT1 | 128x256 | tela negra tejida |
| 2 | DXT1 | 256x256 | banda de metal dorado y pliegues de tela negra |
| 3 | DXT1 | 256x256 | pliegues de tela negra |
| 4 | DXT1 | 256x256 | adornos de metal, **ojo verde**, párpado y piel |
| 5 | DXT5 | 128x128 | rayos morados (efecto con alfa) |
| 6 | DXT5 | 32x32 | bocanada de humo (partícula) |
| 7 | DXT1 | 256x64 | banda metálica de la frente o la máscara |
| 8 | DXT5 | 128x128 | anillo rojo brillante con venas |
| 9 | DXT1 | 256x128 | hoja o vaina, ornamento y cuero oscuro |
| 10 | DXT1 | 256x256 | piel (músculo) |
| 11 | DXT5 | 256x256 | rayos morados (alfa) |
| 12 | DXT1 | 256x256 | traje negro: correas, remaches y malla |
| 13 | DXT5 | 64x64 | emblema dorado (alfa) |
| 14 | DXT1 | 128x128 | palma y dorso de la mano, guante negro |
| 15 | DXT1 | 128x128 | tela negra tejida con una franja clara |
| 16 | DXT1 | 256x512 | traje negro con correas y hebillas |
| 17 | DXT1 | 16x16 | punto negro redondo (sombra o pupila) |
| 18 | DXT1 | 256x256 | atlas de un traje azul alternativo: ojales y piel |

`r_ryu.kmd` tiene 26 texturas.

## Siguiente (H3)

Decodificar los vertex buffers y los index buffers del XPR0 (la declaración de vértices debería estar en
OBJINFO o en el push buffer) y exportar la malla, con sus UV y estas texturas, a glTF.
