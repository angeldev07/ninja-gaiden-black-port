---
tags: [ngb, target, adr, render]
updated: 2026-10-21
status: current
---

# ADR-06: render híbrido (H7b). PGRAPH emulado como base, HLE solo en sincronización y presentación

## Contexto

[[adr-05-runtime-nativo]] dejó abiertas dos vías para llegar al primer frame: emular el NV2A, al estilo xemu, o
reimplementar D3D8 (HLE), al estilo Cxbx. H5 midió con Ghidra y XbSymbolDatabase cuánto de D3D8 se puede
identificar ([[xbe-default]] § H5). Todos los datos son `VERIFIED` por el informe
`NGB_DATA\ghidra\reports\h5-cobertura.md`.

- **D3D8 es identificable:** 97 puntos de entrada con nombre. Desde `.text` se llama a 86 funciones de la sección
  `D3D` en 1,552 sitios; 78 tienen nombre y cubren 1,420 sitios (91.5 %).
- **Pero LTCG metió estado dentro del juego:**
  - unas 900 escrituras directas a las globales de D3D (`g_RenderState` 286, `g_DeferredRenderState` 135,
    `g_DeferredTextureState` 449);
  - SetTextureStageState casi siempre inline;
  - SetIndices, BeginPush/EndPush, la creación de texturas y el Lock de texturas y VB, todos inline;
  - pushes inline en 23 funciones de `.text`, en 51 sitios que llaman a `MakeRequestedSpace`.
- **Push buffers crudos en los assets:** `RunPushBuffer` (0x3CD680) ejecuta los push buffers NV2A de los KMD
  ([[kmd]]).
- 8 funciones usan convención por registros (261 sitios), y también `Swap_0`, que recibe los flags en EAX.
- `.text` no emite ningún método de dibujo directo: todo pasa por la API de D3D o por `RunPushBuffer`.
- El camino LLE ya funciona: H6 y H7a pasan la init de D3D y llegan al primer `FLIP_STALL`.

## Decisión

**Híbrido.** La base es **emular PGRAPH/PFIFO**: interpretar los métodos NV097 del push buffer y traducirlos a
OpenGL o Vulkan. Ese intérprete hace falta de todos modos por `RunPushBuffer`, y ve todo el estado aunque esté
inline. Encima va **HLE en cuatro puntos**:

| Punto | Dirección | HLE |
| --- | --- | --- |
| `D3DDevice_Swap_0` | `0x3CE820` | frontera de frame: presentar la superficie y medir el tiempo |
| `KickOff` | `0x3CD390` | procesar el push buffer de forma síncrona, en lugar de atrapar `DMA_PUT` con el VEH |
| `BlockOnTime`, `BlockOnResource`, `SetFence` | (ver informe) | resolver al instante con un renderer síncrono |
| vblank | ISR `0x3D2040` → DPC `0x3D2310` → `KeSetEvent(dev+0x1DBC)` | interrupción PCRTC sintética a 60 Hz |

## Alternativas descartadas

- **HLE puro de D3D8:** un hook en la frontera de la API no ve el estado inline (unas 900 escrituras), y
  `RunPushBuffer` exige igualmente un intérprete NV097.
- **LLE puro, sin HLE:** atrapar cada `DMA_PUT` y cada sondeo de fences con el VEH es lento y frágil. La frontera de
  frame en `Swap_0` da un punto natural para presentar.

## Consecuencias

- Las piezas que hay que construir:
  - el intérprete NV097 (un decodificador de métodos ya existe en `feat/nv2a-min`);
  - el traductor de los vertex programs y los register combiners del NV2A a GLSL o SPIR-V;
  - la conversión de texturas, reutilizando la lógica de [[xpr0]] (swizzle, DXT y P8);
  - un backend con SDL3 y OpenGL 4.5, como [[opence]].
- El primer frame visible será un CLEAR seguido de un FLIP presentado en una ventana SDL3, y después las primeras
  primitivas.
