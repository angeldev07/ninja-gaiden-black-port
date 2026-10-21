---
tags: [ngb, reverse-engineering, runtime, gpu, h7]
updated: 2026-10-21
status: current
---

# GPU NV2A en el runtime nativo (H7a): hasta la pantalla de avisos

El cargador nativo ([[arranque-nativo]], [[adr-05-runtime-nativo]]) ya **emula un mínimo de registros del NV2A** y
decodifica el push buffer. Con eso, `default.xbe` pasa la inicialización de D3D8, envía frames y llega a la
**pantalla de avisos** ("Warnings"). El código está en `port/src/gpu/` y la decisión de diseño en
[[adr-06-render-hibrido]].

Fuentes:
- `NGB_DATA\runs\h7a-run.log` y `h7a-run-pgraph.log`: ejecución de 30 s del analista;
- las texturas volcadas, revisadas por el líder.

Todo lo que sigue es `VERIFIED` por esos logs.

## Qué destrabó cada paso

1. **El bloqueo no era el vblank, sino un bug de H6.** La superposición del TEB escribía `fs:[0x24] = tid<<8`
   como IRQL. En Windows ese campo es `ClientId.UniqueThread`, y el sistema lo usa para despertar a quien espera en
   un CRITICAL_SECTION o un SRW. Se perdían despertares y el hilo DPC quedaba bloqueado para siempre en
   `NtWaitForAlertByThreadId`.
   - **Arreglo:** ya no se toca `fs:[0x24]`. Las 7 lecturas `movzx r32, byte fs:[24h]` que hace el XBE se parchean
     al cargar a `mov r32, 0` (IRQL 0).
   - **Lección:** cada campo del TEB que se superpone hay que contrastarlo con su uso en Windows.
2. **vblank sintético a ~60 Hz**, con la receta de H5 ([[xbe-default]]): se pone el bit 24 de `PMC_INTR_0` y el
   bit 0 de `PCRTC_INTR_0`, se llama a la ISR `0x3D2040`, que pasa por la DPC `0x3D2310` y acaba en
   `KeSetEvent(m_VerticalBlankEvent)`. Escribir `PCRTC_INTR_0` limpia los dos bits (W1C).
3. **Audio sin bloquear:**
   - APU `PIO_FREE = 0x80`;
   - AC97 `GLOB_STA` con los códecs listos y un bit de reset que se limpia solo;
   - un buzón del DSP GP que se vacía solo.
4. `NtQueryDirectoryFile` implementado.

## Registros modelados

- `PMC_BOOT_0 = 0x02A000A3` (id del NV2A) e interrupciones W1C en PMC, PFIFO, PGRAPH, PCRTC y PTIMER.
- PFIFO: `DMA_PUT`/`GET`, canal 0 de USER y `DMA_CTL`. Cada cambio de PUT se decodifica y GET avanza.
- PTIMER avanza; `PFB_CSTATUS` = 64 MB; el flush de PFB WBC se limpia solo; PGRAPH ocioso; `PCRTC_RASTER`; los PLL
  de PRAMDAC están enganchados.
- PRAMIN es un alias de RAM. RAMHT resuelve los objetos (clases `0x97` Kelvin/3D, `0x39`, `0x9F`, `0x62` y `0x44`).
- Lo que no está modelado se comporta como RAM y se anota una sola vez.

## Lo que el juego envía (30 s, sin pacing)

| Métrica | Valor |
| --- | --- |
| Frames (`FLIP_STALL`) | 37,337. Todavía no se sincronizan con el vblank |
| `BEGIN_END` | 808,728, todos `TRIANGLE_STRIP` con `INLINE_ARRAY` |
| Primer frame con primitivas | el 202 |
| `CLEAR` | 74,473 |
| Kicks del push buffer | 117,874; semáforos 117,870 |
| Errores de decodificación del push buffer | 0 |
| vblanks | 972 |
| Llamadas al kernel | 3.99 M |

- **Superficie de color:** 720x480 A8R8G8B8, pitch 3072, en `0x02878000`.
- **Texturas que carga el juego**, volcadas desde la memoria de la GPU y revisadas por el líder:
  - el **rótulo "Warnings"** (A8 256x64);
  - el **atlas de la fuente** del juego (I8 con paleta, 128x128 con swizzle);
  - los dos búferes de 720x480 que se usan como textura.
- El búfer de color está **negro**: todavía no se rasteriza, y el CLEAR es 0.

## Siguiente

1. **Rasterizar (H7b):** interpretar los métodos NV097 sobre OpenGL, con el traductor de shaders
   (`feat/nv2a-shaders`) y el presentador en ventana (`feat/gfx-present`).
2. Hacer que `FLIP_STALL` espere al vblank (pacing).
3. Entrada del mando (XInput/XPP): sin ella no se sale de la pantalla de avisos.
