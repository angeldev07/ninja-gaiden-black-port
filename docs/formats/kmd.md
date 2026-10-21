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

## Siguiente (H2 y H3)

1. Exportar una textura DXT1 de `ryu.kmd` a PNG. En el NV2A, las texturas DXT se guardan como bloques 4×4
   lineales, sin swizzle; las que no están comprimidas (P8, A8, ARGB) sí van swizzled (Morton), ver [[xpr0]].
   `INFERRED`, por el formato Xbox: hay que confirmarlo exportando una de cada tipo.
2. Decodificar los vertex buffers (`D3DVERTEXDECL` en OBJINFO o en el pushbuffer) y exportar la malla a glTF.
