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

## Geometría (H3, `VERIFIED` en los 59 objetos de `ryu.kmd` y 63 de `r_ryu.kmd`)

**Correspondencias.** El objeto *k* es a la vez la entrada *k* de OBJINFO, el nodo *k* de HIERARC, el USERDATA
*k+1*, y el *k*-ésimo VB y el *k*-ésimo IB del [[xpr0]], en orden de recurso. El USERDATA 0 es una cabecera:
63, **19 texturas**, **59 objetos**, …

**Descriptor de malla** (payload del USERDATA *k+1*):
- Cabecera de `0x28` B: `+4` n_sub, `+8` **FVF de D3D**, `+0x14` nv, `+0x18` stride, `+0x24` n_idx.
- En los 59 objetos de ryu, nv·stride = tamaño del VB y ceil4(n_idx·2) = tamaño del IB (`VERIFIED`, script del
  spike). La herramienta solo exige que quepan: tamaño ≥ lo necesario.
- Después, un registro por submalla:
  - `0x60` B fijos, más `0x50` de material D3DMATERIAL8 si `+0x20 = 1`;
  - `n_tex` etapas de `0x40` B: cola del nombre, **índice de textura** (el mismo de H2) y 14 `u32` de estados;
  - en `+0x10` el origen de los índices: `2` = IB, `0` = push buffer;
  - en `+0x14` y `+0x18` el inicio y el tamaño en bytes.

| FVF | Stride | Campos |
| --- | --- | --- |
| `0x112` (XYZ\|NORMAL\|TEX1) | 32 | pos f32×3, normal f32×3, uv f32×2 |
| `0x116` (XYZB1\|NORMAL\|TEX1) | 36 | pos, peso f32, normal, uv (piezas con skin) |
| `0x042` (XYZ\|DIFFUSE) | 16 | pos, D3DCOLOR (sombras) |

También aparecen `0x142` y un grupo 6 en otros personajes; los cubre el decodificador genérico de FVF.

- **No hay declaración de vértices NV2A en el push buffer.** El formato sale del FVF.
- Normales en float de magnitud 1, sin NORMPACKED3.
- UV con wrap: el glTF las conserva y el OBJ exporta `1−v`.

**Índices.**
- u16 en **triangle strip** sin reinicio, unidos con degenerados.
- Winding: pares `abc`, impares `bac`. Coincide con las normales en 14,673 de 14,683 triángulos.
- La cabeza de Ryu (submalla 1 del obj1) se dibuja desde el **push buffer**:
  - `BEGIN TRIANGLE_STRIP` y `ARRAY_ELEMENT16` × 1085 palabras = 2,170 índices, y `END`;
  - usa exactamente los 732 vértices que su IB no toca (`VERIFIED`, script `pbcheck.py` del spike, guardado en
    `NGB_DATA\extracted\h3\spike\`).
  - `INFERRED`: es la cara, con MORPH para las expresiones.

**Jerarquía (HIERARC).**
- Cabecera con la raíz en `+0x28`.
- Entradas `u16 self, parent (0xFFFF = raíz), n_children, subtree_size, children[n]`.
- Los nodos son los propios objetos.
- TRS en OBJINFO (`+0x30` T, `+0x40` R, `+0x50` S). En Ryu R = 0 y S = 1, así que la posición en el mundo es la
  suma de las T a lo largo de la cadena.
- **Los vértices vienen en el espacio local de su hueso**, por eso hay que aplicar la jerarquía.
- Las piezas con skin van en su hueso padre e ignoran su propia T. La herramienta las reconoce por el FVF con
  peso (XYZB*), no por el grupo; en ryu coinciden con el grupo 4.
- Esqueleto:
  - cadera 2 → pecho 0 → cabeza 1;
  - hombros 8/14 → codos 4/10 → manos 5/11;
  - muslos 6/12 → rodillas 7/13 → pies 3/9.
  - Pose T, 1.78 m de alto.

**Grupos de objeto** (OBJINFO `+0xC`):
- 1: cuerpo de detalle.
- 4: articulaciones con skin.
- 5: LOD bajo.
- 2: sombra, `INFERRED`.
- 3: efecto con escala 0, `INFERRED`.
- El glTF exporta 1 y 4 por defecto.

## Exportado a glTF (`VERIFIED`: las cifras son la salida de `ngb_tools.kmd export`; renderizado desde el glTF releído y revisado a ojo)

| Modelo | Resultado |
| --- | --- |
| `ryu.kmd` | 27 objetos, 9,143 vértices, 13,431 triángulos, 0 incidencias |
| `r_ryu.kmd` | 39 objetos, 20,623 triángulos (variante de cinemáticas) |
| Ayane (`chr_ayane#0`, `aya_a`) | completa; la incidencia es una rotación pequeña en los obj24/25, que se ignora |
| Murai (`murai`, `r_mur`) | completo |
| Doku: katana (`do_sw`) y máscara (`do_face`) | completos |
| **Doku: cuerpo** (`chr_doku#0`) | **no**: tiene 88 VB para 87 objetos y un descriptor variante (origen de submalla 24) |

La espada de Ryu no está en `ryu.kmd`.

## Pendiente

- La variante de Doku.
- R ≠ 0 (orden de rotación desconocido).
- Semántica de los pesos del skin.
- Estados de material: blend aditivo, alfa de las DXT5 y etapas extra.
- MORPH y animaciones (`chr_dat`/`.mtn`, [[chunk-teamninja]]).
