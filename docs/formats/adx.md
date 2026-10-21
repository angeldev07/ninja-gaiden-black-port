---
tags: [ngb, formats, cri, audio]
updated: 2026-10-20
status: current
---

# ADX: audio de CRI

Hay 412 entradas ADX (480.1 MB): las 95 pistas de música de `bgm.afs` y las 317 voces de `sse.afs`.
Es un formato público, y ffmpeg y vgmstream lo decodifican.

## Cabecera (`VERIFIED` en `bgm.afs#0`, leída por el líder)

| Offset | Valor en `bgm.afs#0` | Campo |
| --- | --- | --- |
| `0x00` | `80 00` | magia |
| `0x02` | `0x0020` (u16 big endian) | offset del copyright. `VERIFIED`. Los datos empiezan en `offset + 4` = `0x24`: `INFERRED` |
| `0x04` | 3 | codificación (ADX estándar). `INFERRED` |
| `0x05` | 18 | tamaño de bloque. `INFERRED` |
| `0x06` | 4 | bits por muestra. `INFERRED` |
| `0x07` | 2 | canales. `VERIFIED` |
| `0x08` | 44,100 (u32 big endian) | frecuencia de muestreo. `VERIFIED` |
| `0x0C` | u32 big endian | total de muestras. `INFERRED` |
| `0x1E` | `(c)CRI` | copyright. `VERIFIED` |

## Decodificación (H4, con `ngb_tools.audio`: validada de forma visual y estadística, sin comparar bit a bit)

- Los 412 ADX del juego son **v3, codificación 3, corte de highpass 500 Hz y sin cifrado** (el byte `0x13` vale 0).
- Los coeficientes del predictor salen de la fórmula pública: con 500 Hz a 44,100 Hz, `(7334, −3283)` en punto
  fijo ×4096.
- Cada bloque de 18 B tiene una escala u16be y 32 nibbles con signo. En v3 la escala se usa como escala+1 y cada
  producto se desplaza por separado, como en vgmstream.
- Lote completo:
  - **bgm 95/95** (unos 150 min de música). El manifiesto marca 65 pistas con loop; falta comprobar el offset
    real del loop en v3 (issue de seguimiento);
  - **sse 317/317** voces.
- La validación fue visual y estadística (forma de onda, espectrograma y RMS). No se comparó bit a bit con otro
  decodificador porque no hay ffmpeg ni vgmstream instalados.

## Notas

- `bgm.afs#66` (`bgm_rtm_dummy.adx`) es una de las 2 únicas entradas de entropía alta del juego.
- El motor enlaza ADXT 9.50 y AIXP 2.32 ([[xbe-default]]). `movie\ninja.aix` es AIX (`AIXF`), ADX multipista.
