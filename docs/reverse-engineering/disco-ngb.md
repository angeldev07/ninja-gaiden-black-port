---
tags: [ngb, reverse-engineering, disc]
updated: 2026-10-20
status: current
---

# El disco de NGB (PAL): estructura y contenido

## Imagen

- **Redump** `Ninja Gaiden Black (Europe) (En,Fr,De,Es,It).iso`: 7,825,162,240 bytes. Es un disco
  **XGD2** con la partición de video delante. La magia `MICROSOFT*XBOX*MEDIA` está en `0x18310000`, así
  que la partición del juego empieza en `0x18300000`. `VERIFIED` leyendo los bytes.
- **XISO** generado con `extract-xiso -r -d`: 6,341,263,360 bytes, con la magia en `0x10000`. `VERIFIED`.
  Ver [[herramientas]].
- Árbol extraído en `NGB_DATA\game\`: **249 archivos, 6,340,649,100 bytes**. `VERIFIED` con `extract-xiso -x`.

## Por tipo (`VERIFIED`, conteo con PowerShell sobre `game/`)

| Extensión | Archivos | Bytes | Qué es |
| --- | --- | --- | --- |
| `.sfd` | 26 | 4,530,176,000 | Video Sofdec de CRI (MPEG-1/2 más audio ADX). ffmpeg lo decodifica. `INFERRED` |
| `.afs` | 206 | 1,686,644,736 | Contenedor AFS de CRI (`AFS\0`). Ver [[afs]] |
| `.xbe` | 5 | 72,003,584 | Ejecutables. Ver [[xbe-default]] |
| `.ttf` | 2 | 46,549,144 | `Media\ArialUni.Ttf` y `Media\font.ttf`, **idénticos** (mismo sha256 `58E770F0…`) |
| `.aix` | 1 | 3,102,720 | `movie\ninja.aix`: audio AIX de CRI (ADX multipista). `INFERRED` |
| `.xpr` | 3 | 1,929,216 | Xbox Packed Resource (texturas y recursos del XDK): `font.xpr`, `dlcustom.xpr`, `Fonts\kfont.xpr` |
| `.xwb`/`.xsb` | 1 + 1 | 190,012 + 684 | Banco de ondas y banco de sonidos XACT (`Media\Wave.xwb`, `Sound.xsb`) |
| `.bin` | 2 | 52,984 | `dsstdfx.bin` (15,308; por nombre, imagen DSP de efectos estándar de DirectSound, `INFERRED`) y `load_img.bin` (37,676) |
| `.def` | 1 | 16 | `NinjaLaunchXBEName.def` = `D:\default.xbe\r\n`. `VERIFIED` |
| `.ins` | 1 | 4 | `verchk.ins` = `10 BF 17 43`. `VERIFIED`; significado desconocido |

## Ejecutables

| Archivo | Bytes | Nota |
| --- | --- | --- |
| `default.xbe` | 4,743,168 | Juego principal. sha256 `B61F6EB5…D618` |
| `NinjaTrilogy.xbe` | 3,248,128 | `INFERRED`: los Ninja Gaiden de NES, desbloqueables |
| `NinjaTomoe.xbe` | 3,248,128 | Mismo tamaño que Trilogy pero **otro hash** (`723083BC…` contra `2957569A…`) |
| `update.xbe` | 2,342,912 | `INFERRED`: actualizador |
| `dashupdate.xbe` | 58,421,248 | `INFERRED`: actualización del dashboard (Xbox Live) |

## Los AFS (206), por grupo

Agrupación por nombre de archivo (`VERIFIED`, listado de `game/`). Los conteos de entradas
salen del `u32` en `0x04` de cada AFS (`VERIFIED`, hexdump de `cmn`, `debug`, `chr_ryu` y
`bgm`, y `afs inventory` sobre los 206). El significado de los grupos es `INFERRED`.

- **Globales:** `bgm.afs` (445 MB, 95 entradas: música), `spr.afs` (158 MB), `sse.afs` (58 MB), `cmn.afs`
  (50 MB, 353 entradas), `demo.afs`, `debug.afs` (39 entradas), `live00.afs`.
- **Personajes (`chr_*`, 62):** por ejemplo `chr_ryu`, las variantes `chr_ryua…chr_ryug` (`INFERRED`: trajes),
  `chr_ayane`, `chr_rei`, `chr_doku`, `chr_murai`, `chr_kotei`, `chr_dino`, `chr_dragn` y `chr_tank`.
  `chr_ryu.afs` tiene 2 entradas.
- **Escenarios (17 prefijos, `<zona>_NN` y `<zona>_cmn`):** `boff, cave, chrch, hell, hori, makai, pyrmd,
  rail, ssato, stdim, suiro, tln_a, tln_b, tomb, tower, yasik, zppln`. `INFERRED`: `ssato` = aldea Hayabusa
  (sato), `tln` = Tairon, `zppln` = zeppelin, `yasik` = mansión (yashiki), `chrch` = catedral, `tomb` =
  tumba/monasterio, `suiro` = canal (suiro).
- **`rtm_*` (12):** uno por zona (`rtm_cave`, `rtm_tlna`, `rtm_tlnb`, …). Ojo: Tairon se escribe `tln_a`/`tln_b`
  en los escenarios y `tlna`/`tlnb` en los `rtm_` (`VERIFIED`, nombres de archivo). `INFERRED`: recursos de
  cinemáticas en tiempo real de cada zona.

## Video y audio de CRI (`movie/`: 26 `.sfd` y 1 `.aix`, 27 archivos)

`pre_movie1…14`, varios con su variante `_j` (japonés). También `cre.sfd` (`INFERRED`: créditos), `epi.sfd`,
`ninja_gaiden.sfd`, `d_aruma.sfd`, `2_VI_WIDE_B.sfd` y `4_VI_STD_B.sfd` (`INFERRED`: variantes 16:9 y 4:3),
y `doa4.sfd` (716 MB; `INFERRED`: tráiler de Dead or Alive 4).

## Siguiente

- Contenido de los AFS: [[afs]]. Ejecutable: [[xbe-default]].
