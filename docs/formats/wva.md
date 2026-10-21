---
tags: [ngb, formats, teamninja, audio]
updated: 2026-10-20
status: current
---

# WVA: banco de efectos de sonido

Hay 326 archivos `.wva` (43.3 MB), uno o varios por escenario (`bf_wfall.wva`, `bf_head.wva`…). No llevan una
magia ASCII: la cabecera empieza por `u32 N`, el número de sonidos. Por eso el inventario los agrupa como
`hex:NN000000`.

## Estructura (`INFERRED`, con evidencia fuerte: con este layout se decodifican los 2,966 registros de los 326 `.wva`, y el audio se ve correcto en el espectrograma; el significado de los floats de parámetros es desconocido)

```
u32 N
N × u32 puntero           // 0 = hueco vacío (370 en el juego)
                          // bit31 = 1 → bloque de parámetros de 24 B; bit31 = 0 → de 8 B
registro:
  u32 (flags << 24 | data_size)     // flags vistos: 0x00, 0x01, 0x04
  WAVEFORMATEX (20 B)               // wFormatTag 0x0069 = Xbox ADPCM
  bloque de parámetros (24 u 8 B, floats)
  datos Xbox ADPCM
```

- Esto corrige la hipótesis anterior: el bit31 **no** es un flag de validez, sino que marca el tamaño del bloque
  de parámetros.
- Xbox ADPCM: 64 muestras por bloque y canal (la de cabecera más 63 nibbles; el último se descarta), distinto
  de las 65 de MS-IMA. Hay 63 registros en estéreo, y las frecuencias van de 11,000 a 44,100 Hz.
- 93 registros son silencio puro (sobre todo huecos de relleno).
- `Media\Wave.xwb` (WBND v3) contiene 7 sonidos en **PCM16**, no en Xbox ADPCM.

## Siguiente

Mapear qué evento del juego dispara cada sonido (`.sty` y tablas de `chr_dat`).
