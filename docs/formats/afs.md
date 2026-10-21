---
tags: [ngb, formats, afs, cri]
updated: 2026-10-20
status: current
---

# AFS: contenedor de CRI Middleware

Casi todos los assets de NGB están en 206 archivos `.afs` ([[disco-ngb]]). El formato es público.
Hay implementaciones abiertas de referencia: [AFSLib](https://github.com/MaikelChan/AFSLib) y
[Sewer56/AfsLib](https://github.com/Sewer56/AfsLib). Herramienta: `python -m ngb_tools.afs`
([[herramientas]]).

## Estructura (`VERIFIED`: parseo de los 206 archivos con `afs inventory` y hexdump manual de 4)

| Offset | Tipo | Campo |
| --- | --- | --- |
| `0x00` | `char[4]` | magia `AFS\0` (en NGB nunca aparece la variante `AFS ` de AFSLib) |
| `0x04` | `u32` | N, número de entradas |
| `0x08` | `N × (u32 offset, u32 size)` | tabla de contenido (TOC) |
| `0x08 + 8N` | `u32 offset, u32 size` | puntero a la tabla de atributos (nombres y fechas) |
| `align(8 + 8N + 8, 0x800)` | — | primer dato. Los datos van contiguos y alineados a `0x800` |
| `align(fin del último dato)` | `N × 0x30` | tabla de atributos |

Entrada de atributos (`0x30` bytes): `name[32]` (ASCII rellenado con NUL), seis `u16` de fecha
(año, mes, día, hora, minuto, segundo) y un `u32` final.

## Particularidades de NGB

- **Los 206 archivos tienen tabla de nombres**, así que las 3,766 entradas tienen nombre (máximo 23 caracteres).
- **El `u32` final de cada atributo no es el tamaño.** En las 3,766 entradas vale lo mismo que el `u32` del
  offset `4 + 4·i` del archivo: es la cabecera leída como un array de `u32` (`custom[0] = N`,
  `custom[1] = TOC[0].offset`…). Solo coincide con el tamaño por casualidad en 177 casos. **No hay que usarlo
  como tamaño**, y para reempaquetar hay que reproducir esta peculiaridad.
- **Invariantes que valida la herramienta** (`ngb_tools.afs.invariants`), todos válidos en 206/206
  (`VERIFIED`): magia, offsets alineados a `0x800`, entradas dentro del archivo, sin solapes, tamaño de la
  tabla de atributos igual a `N × 0x30` y fechas plausibles.
- **Propiedades observadas con un script exploratorio del analista**, que no está versionado y por tanto la
  herramienta todavía no las valida: el orden de la TOC coincide con el de los offsets, los datos van
  contiguos, la tabla de atributos empieza en `align(fin del último dato)` y el archivo termina alineado.
  Se vieron en 206/206 archivos. `INFERRED` hasta que pasen a `invariants`.
- **No hay AFS anidados**: 0 entradas con `AFS\0` (`VERIFIED`, `summary.nested_afs` de `afs inventory`).
- Las fechas van de 2002 a 2005: 2002 (1), 2003 (771), 2004 (1,416) y 2005 (1,578).

## Qué contienen: principales magias de las entradas (`VERIFIED`, `summary.magic_histogram` de `afs inventory`)

| Magia o etiqueta | Entradas | MiB | Ejemplo | Formato |
| --- | --- | --- | --- | --- |
| `KMD` (chunk) | 724 | 751.8 | `boff_01.afs#0 boff_01.kmd` | modelo o escena → [[kmd]] |
| `XPR0` | 689 | 163.7 | `cmn.afs#253 menu.spr` | Xbox Packed Resource (sprites y UI) → [[xpr0]] |
| `itm_dat2` (chunk) | 600 | 1.4 | `boff_05.afs#1 bf_hpole.sob` | colocación de objetos e ítems (`.sob`, `.itm`) → [[chunk-teamninja]] |
| `ADX` | 412 | 480.1 | `bgm.afs#0 0611_bgm.adx` | audio CRI ADX → [[adx]] |
| `chr_dat` (chunk) | 328 | 116.7 | `boff_cmn.afs#3 eryun.mtn` | banco de animaciones (`.mtn`) |
| `stry_dat` (chunk) | 326 | 35.5 | `boff_cmn.afs#0 s_boff01.sty` | script en bytecode (`.sty`) |
| `.wva` (`u32 N` en la cabecera) | 326 | 43.3 | `boff_cmn.afs#41 bf_wfall.wva` | banco de efectos en Xbox ADPCM → [[wva]] |
| `rtm_dat` (chunk) | 140 | 1.5 | `boff_05.afs#4 chijo_00.rtm` | secuencia de cinemática en tiempo real |
| `chr_dat2` (chunk) | 72 | 1.5 | `chr_army.afs#13 army.chr` | parámetros de personaje |
| resto (`MESSTR`, `JPFONT`, `STG_DAT`, 3 vacías y cabeceras binarias sueltas; 152 tipos distintos en total en el histograma) | 149 | — | — | por clasificar |
| **total** | **3,766** | | | |

En total hay 2,201 entradas con etiqueta de chunk (los tipos `tag:*` del histograma: KMD, `itm_dat2`, `chr_dat`,
`stry_dat`, `rtm_dat`, `chr_dat2`, `MESSTR` 4, `JPFONT` 3, `STG_DAT` 3 y `STG_ETC` 1). XPR0 y ADX no cuentan: son magias, no
etiquetas de chunk. Las "(chunk)" comparten la cabecera genérica de
[[chunk-teamninja]] (`INFERRED`): la regularidad se comprobó con un script exploratorio en 2,198 de ellas. Las 3
restantes son `STG_DAT`, que tiene otra disposición.

## Entropía (`VERIFIED`, `summary.entropy_bands`; ventana de los primeros 64 KiB de cada entrada)

- Baja (< 5): 1,956 entradas (694.6 MiB). Media (5 a 7.5): 1,805 (909.2 MiB). Alta (> 7.5): 2 (0.63 MiB). Vacías: 3. Total: 3,766.
- **Ningún tipo cae mayoritariamente en la banda alta, y no aparecen firmas de zlib ni gzip.** Dentro de la
  ventana medida no hay **compresión de propósito general ni cifrado** que tape los formatos (`INFERRED` para
  el resto de cada entrada). Ojo: los códecs propios de cada medio (ADPCM en audio, DXT en texturas) sí son
  compresión, pero su formato es conocido. Esto contradice la conclusión de [[decomp-ninja]].
- Medias por tipo: ADX 7.1 (ADPCM), `.wva` 6.9 (Xbox ADPCM), KMD 5.5 y XPR0 5.3 (texturas DXT y P8).
  Tablas y scripts quedan por debajo de 4.

## Contenedores destacados

- **`chr_ryu.afs`:** 2 entradas. `ryu.kmd` (1,018,944 bytes; 59 objetos, XPR0 con **19 texturas**: 14 DXT1 y
  5 DXT4/5) y `r_ryu.kmd` (63 objetos; `INFERRED`: variante para cinemáticas). Es el objetivo de H2 y H3
  ([[roadmap]]).
- **`debug.afs`:** 39 entradas.
  - 34 scripts `.sty` de herramientas y pruebas de desarrollo (`s_m_edit`, `s_r_view`, `s_chrput`, `s_test`, y
    24 visores de jefes `s_v<jefe>`).
  - 2 escenarios de prueba (`stage00`, `stage02`), 1 `.cam` y 2 `.rtm` de prueba.
  - No contiene fuentes ni símbolos.
- **`bgm.afs`:** 95 pistas ADX. **`sse.afs`:** 317 voces ADX (`001_Murai.adx`…).
