---
tags: [ngb, formats, xdk, texture]
updated: 2026-10-20
status: current
---

# XPR0: Xbox Packed Resource

Es el formato estándar del ecosistema Xbox para empaquetar recursos de GPU: texturas, paletas, vertex
buffers, index buffers y push buffers. En NGB aparece suelto, en las 689 entradas de AFS con la magia `XPR0`
(sobre todo los `.spr` de sprites y UI) y en `Media\*.xpr`, y también dentro de cada [[kmd]]. Herramienta:
`python -I -m ngb_tools.xpr` ([[herramientas]]).

## Estructura

| Offset | Campo | Confianza |
| --- | --- | --- |
| `0x00` | magia `XPR0` | `VERIFIED` |
| `0x04` | `u32` tamaño total | `VERIFIED` (ryu: `0xDC000`) |
| `0x08` | `u32` `header_size`: los datos de GPU empiezan ahí | `VERIFIED` (ryu: `0x6590`) |
| `0x0C…` | cabeceras de recurso, hasta `0xFFFFFFFF` | `VERIFIED` |

- **`Data` de cada recurso es relativo a `header_size`.** En ryu, el primer índice cae en `header+0` y la
  textura 0 en `header+0xBA70`, es decir en el offset `0x12000` del XPR, alineado a `0x1000`. `VERIFIED`: el
  sha256 del manifiesto coincide con un hash independiente de esos bytes.
- **`header_size` no tiene por qué ir alineado a `0x800`**, aunque xboxdevwiki dice que sí: en ryu vale `0x6590`.
  `VERIFIED`.

### Cabeceras de recurso, según el campo `Common` (`VERIFIED` al recorrer los archivos reales)

| `Common` | Recurso | Tamaño de la cabecera |
| --- | --- | --- |
| `0x00040001` | textura: `Common`, `Data`, `Lock`, `Format`, `Size` | 20 B |
| `0x00830001` | paleta | 12 B |
| `0x00800001` | vertex buffer | — |
| `0x00010001` | index buffer | — |
| push buffer | `Size`, `AllocationSize` y **un `u32` extra que siempre vale 0** | **24 B** (Cxbx documenta 20; con 20, el recorrido de las cabeceras se desalinea) |
| `0x80000000` exacto | USERDATA: `u32 size` y el payload (en los `.spr`, una tabla de atlas UV) | 8 B + payload |

Campo `Format` de la textura: dimensiones en los bits 4 a 7, `D3DFORMAT` en los bits 8 a 15, número de mips
en los 16 a 19, `log2 U` en los 20 a 23 y `log2 V` en los 24 a 27.

## Formatos vistos en los datos (`VERIFIED`: exportados y revisados a ojo)

| `D3DFORMAT` | Nombre | Dónde | Swizzle |
| --- | --- | --- | --- |
| `0x04` | A4R4G4B4 | `font.xpr` (atlas de glifos 512x256) y `kfont.xpr` | sí (Morton) |
| `0x0B` | P8 con paleta D3DCOLOR | 89 texturas de `menu.spr` | sí |
| `0x0C` | DXT1 / BC1 | 14 texturas de ryu | **no**: bloques 4×4 lineales |
| `0x0E` | DXT2/3 / BC2 | `dlcustom.xpr` (3 texturas de 1024x512) | no |
| `0x0F` | DXT4/5 / BC3 | 5 texturas de ryu (efectos con alfa) | no |
| `0x19` | A8 | 8 texturas de `menu.spr` | sí |
| `0x28` | V8U8 (relieve con signo; alias G8B8) | 326 texturas (agua y superficies de escenarios) | sí |
| `0x27` / `0x37` | L6V5U5 (relieve con signo más luminancia; alias R6G5B5) | 32 texturas (por ejemplo `chr_kotei`) | sí / no |

- La tabla de `ngb_tools.xpr.formats` conoce 54 códigos `D3DFORMAT` de Xbox. Los de profundidad y YUV se
  reconocen, pero no se decodifican, porque no aparecen en los datos.
- **Relieve con signo** (V8U8 y L6V5U5): en la NV2A son formatos de color sin signo, y el signo lo pone el estado
  de la etapa de textura, que el XPR0 no guarda. Se usa la semántica D3D de los nombres; leídos con signo, la
  variación entre píxeles vecinos es 74 veces menor (`VERIFIED`, spike del analista). En el PNG, R y G llevan
  U y V en binario desplazado, B la z de la normal reconstruida y A la luminancia. El manifiesto los marca con
  `encoding: signed-bump`.
- **DXT va sin swizzle.** `VERIFIED`, por dos vías:
  - en xemu, `pgraph/texture.c` solo des-swizzlea los formatos marcados como swizzled y los compressed los
    pasa a `pgraph/s3tc.c` en bloques lineales;
  - las 22 texturas DXT exportadas (19 de ryu y 3 de `dlcustom.xpr`) se ven correctas sin swizzle.
  Los formatos con swizzle usan Morton, con x en el bit bajo (`pgraph/swizzle.c`). `VERIFIED` con `menu.spr` y
  `font.xpr`.
- **P8:** se toma la paleta de la cabecera siguiente, si es una paleta, y si no la única del archivo. En
  `menu.spr` cada P8 va seguida de su paleta tanto en las cabeceras como en los datos, y las imágenes salen
  bien. `INFERRED`.
- **A8** se exporta como blanco con alfa, igual que hace Cxbx. Sobre fondo blanco parece vacía.
- La alfa de DXT5 se decodifica siguiendo a xemu (`pgraph/s3tc.c`) y la especificación BC3 de Microsoft, no a
  Cxbx. `INFERRED`: el analista vio en el decodificador de Cxbx (`XbConvert.cpp`) una precedencia de operadores
  dudosa, pero no se fijó la línea exacta.

## Fuentes (no se usó el XDK)

- xemu: [`nv2a_regs.h`](https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/nv2a_regs.h),
  [`pgraph/texture.c`](https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/texture.c),
  [`pgraph/swizzle.c`](https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/swizzle.c) y
  [`pgraph/s3tc.c`](https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/s3tc.c).
- Cxbx-Reloaded: [`XbD3D8Types.h`](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/core/hle/D3D8/XbD3D8Types.h)
  y [`XbConvert.cpp`](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/core/hle/D3D8/XbConvert.cpp).
- [xboxdevwiki/XPR](https://xboxdevwiki.net/XPR).
- [Block compression (Microsoft)](https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression).
- [Texture tiling and swizzling (F. Giesen)](https://fgiesen.wordpress.com/2011/01/17/texture-tiling-and-swizzling/).

## Gotcha para el port

En PC, las texturas con swizzle hay que des-swizzlearlas al cargarlas, o resolverlo en un shader. Las DXT se
pueden subir tal cual a la GPU sin decodificarlas en la CPU. BC1, BC2 y BC3 son obligatorios en Direct3D 11/12.
En Vulkan dependen de la feature `textureCompressionBC` y en OpenGL de `EXT_texture_compression_s3tc`; ambas
están en todas las GPU de escritorio, pero no en móviles.
