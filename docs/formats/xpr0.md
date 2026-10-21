---
tags: [ngb, formats, xdk, texture]
updated: 2026-10-20
status: draft
---

# XPR0: Xbox Packed Resource

Es el formato estándar del ecosistema Xbox para empaquetar recursos de GPU (texturas, paletas, vertex
buffers e index buffers). En NGB aparece suelto (689 entradas de AFS con la magia `XPR0`, sobre todo los `.spr` de sprites y UI,
y `Media\*.xpr`) y dentro de cada [[kmd]].

## Estructura (`INFERRED`: viene del conocimiento público del formato y no se ha contrastado con una fuente en línea)

| Offset | Campo |
| --- | --- |
| `0x00` | magia `XPR0` |
| `0x04` | `u32` tamaño total |
| `0x08` | `u32` tamaño de la cabecera. Los datos de GPU empiezan ahí |
| `0x0C…` | cabeceras de recurso, hasta encontrar `0xFFFFFFFF` |

Cabeceras de recurso, según el campo `Common`:
- **Textura (20 bytes):** `Common 0x00040001`, `Data`, `Lock`, `Format` y `Size`.
- **Paleta (12 bytes):** `0x00830001`.
- **Vertex buffer:** `0x00800001`. **Index buffer:** `0x00010001`.
- **USERDATA:** `u32 0x80000000`, `u32 size` y el payload. En los `.spr`, es una tabla de atlas UV.

Campo `Format` de la textura: dimensiones en los bits 4 a 7, `D3DFORMAT` en los bits 8 a 15, número de mips en
los 16 a 19, `log2 U` en los 20 a 23 y `log2 V` en los 24 a 27.

## Formatos vistos (`VERIFIED` por el analista)

- `0x0C` DXT1 y `0x0F` DXT4/5 en los modelos (ryu: 14 y 5 texturas).
- `0x0B` P8, con paleta, y `0x19` A8 en `menu.spr`.

## Gotcha para el port

Las texturas P8 y A8 (y en general las que no son DXT) van **swizzled** (Morton). En PC hay que des-swizzlear
al cargar, o subirlas tal cual a un shader que lo resuelva. `INFERRED`.
