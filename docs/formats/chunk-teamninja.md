---
tags: [ngb, formats, teamninja]
updated: 2026-10-20
status: draft
---

# Chunk de Team Ninja: cabecera común de los formatos del motor Kunai

Los formatos propios del motor (KMD, `itm_dat2`, `chr_dat`, `stry_dat`, `rtm_dat`, `chr_dat2`, `MESSTR`,
`JPFONT`, `STG_ETC`, y los subchunks `OBJINFO`, `HIERARC`, `MORPH`…) comparten esta cabecera.

## Cabecera (`INFERRED`; regularidad comprobada en 2,198 de 2,198 entradas con etiqueta)

| Offset | Tipo | Campo |
| --- | --- | --- |
| `0x00` | `char[12]` | etiqueta ASCII rellenada con NUL (`KMD`, `chr_dat`…) |
| `0x0C` | `u32` | tamaño del chunk. Coincide con el tamaño de la entrada salvo en KMD, donde su semántica es desconocida |
| `0x10` | `u32` | `n_slots` |
| `0x14` | `u32` | `n_used`: cuántos offsets son distintos de 0 |
| `0x18` | `u32` | `table_pos` (`0x20`; en KMD `0x80` y en MESSTR `0x40`) |
| `0x1C` | `u32` | `0x20` (tamaño de la cabecera) |
| `0x20 … table_pos` | — | cabecera propia de cada tipo |
| `table_pos` | `u32[n_slots]` | offsets de los subchunks, relativos al chunk. 0 = vacío; los usados van en orden ascendente |

`VERIFIED` en `ryu.kmd` (leído por el líder): tamaño `1018672`, 10 slots, 7 usados, tabla en `0x80`, offsets
`0xb0` (OBJINFO), `0x3000` (XPR0), `0xdf000` (HIERARC), `0xdf4f0` (MORPH), `0xf7120` (ACSSRI), `0xf8490`
(MISCDATA) y `0xf8a40` (OBJHIT).

## Excepciones

- `STG_DAT` tiene otra disposición: etiqueta, `0x10`, la ruta `d:\nocare.stg` y relleno `Stak`.

## Hipótesis por tipo (`INFERRED`)

- `chr_dat` (`.mtn`): 3 slots. Una tabla de offsets por motion con keyframes, una segunda tabla y una cola pequeña.
- `itm_dat2` (`.sob`, `.itm`): 4 slots, 1 usado. Registros de colocación que terminan en `0x0FFFFFFF`.
- `stry_dat` (`.sty`): el slot 0 es un flujo de `u32`. Los tokens `0x80`/`0x81`/`0x86xxxxxx` son opcodes (id en el
  `u16` bajo) y el resto son argumentos int o float. Encaja con las 479 cadenas `STRY_*` de [[xbe-default]].

Detalle de KMD: [[kmd]].
