---
tags: [ngb, formats, teamninja, audio]
updated: 2026-10-20
status: draft
---

# WVA: banco de efectos de sonido

Hay 326 archivos `.wva` (43.3 MB), uno o varios por escenario (`bf_wfall.wva`, `bf_head.wva`…). No llevan una
magia ASCII: la cabecera empieza por `u32 N`, el número de sonidos. Por eso el inventario los agrupa como
`hex:NN000000`.

## Estructura (`INFERRED`: script exploratorio del analista, no versionado; con esta lectura, los 326 archivos quedan dentro de sus límites)

```
u32 N
N × u32   (bit31 | offset)     // bit31=1 → registro de sonido
registro:
  u32 (flags << 24 | data_size)
  WAVEFORMATEX (20 B)          // wFormatTag 0x0069 = Xbox ADPCM
  ¿loops?
  3 × float (-1.0)
  datos ADPCM
```

- 2,966 de 3,336 registros llevan `wFormatTag 0x0069` (Xbox ADPCM): alineación de bloque 36 en mono y 72 en
  estéreo, 4 bits y 64 muestras por bloque.
- Frecuencias: 22,050 Hz (2,657), 32,000 Hz (169), 16,000, 18,900 y 44,100.
- Hay 446 offsets con bit31 = 0 que apuntan a otra estructura, todavía desconocida. `UNVERIFIED`.

## Siguiente

Xbox ADPCM es una variante documentada de IMA ADPCM. Con un decodificador ADPCM → WAV saldrían los efectos (H4).
