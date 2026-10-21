---
tags: [ngb, reverse-engineering, runtime, audio, apu, h8]
updated: 2026-10-22
status: current
---

# Audio: la APU del MCPX en el runtime nativo

DirectSound está enlazado de forma estática en `default.xbe` (118 funciones identificadas en H5, librería
`DSOUND`; [[xbe-default]]) y habla directamente con el hardware de audio, la APU del MCPX. El runtime nativo no
la emula entera: tiene un DSP falso (H7a, [[gpu-nv2a]]) y un **procesador de voces (VP) mínimo, sin sonido**.
El código está en `port/src/apu/vp.c`.

## Cómo sabe DirectSound por dónde va un sonido (`VERIFIED` en Ghidra)

- `CMcpxBuffer_GetCurrentPosition` (`0x38344E`) llama a `0x38262C`, que lee la posición de la **tabla de voces
  en la RAM**:
  - cada voz ocupa 0x80 bytes, y la base de la tabla está en `NV_PAPU_VPVADDR`;
  - la posición (CBO, en muestras) está en `+0x58`;
  - solo se lee si el bit `NEW_VOICE` (20) de `+0x54` está apagado.
- `0x3811C7` pasa las muestras a bytes: `(cbo >> 6) · blockAlign` en ADPCM (formato `0x69`), y `cbo · blockAlign`
  en PCM.
- La capa de sonido de Sofdec (`mwSnd`, de CRI) reproduce el audio del vídeo en un buffer de DirectSound, y **avanza
  el vídeo según esa posición**. Si la posición no se mueve, el vídeo se queda congelado en su primer fotograma.
- DirectSound gobierna las voces con **métodos PIO** escritos en la página MMIO `0xFE820000`: `VOICE_ON` `0x124`,
  `VOICE_OFF` `0x128`, `VOICE_PAUSE` `0x140`, `SET_CURRENT_VOICE` `0x2F8` y `SET_VOICE_*` `0x300-0x3DC`. Los
  nombres y offsets salen de `apu_regs.h` de xemu, usado solo como referencia.

## El VP emulado

- Las escrituras PIO (que ya se trazaban como RAM, `crash.c`) se pasan a `vp_method`, que mantiene el conjunto
  de voces activas y escribe en la tabla los campos `SET_VOICE_*` (formato, pitch, LBO, CBO y EBO).
- El hilo de DPC llama a `vp_advance` con el tiempo real transcurrido. Para cada voz activa y no pausada:
  - limpia `NEW_VOICE`;
  - avanza la CBO a `48000 · 2^(pitch/4096)` muestras por segundo;
  - en EBO, vuelve a LBO si la voz tiene `LOOP`; si no, la apaga.
- Prueba: `port/tests/vp_test.c`, con 16 comprobaciones sobre una tabla de voces sintética.

## Resultado (`VERIFIED`)

**El vídeo de introducción se reproduce entero** (el mar tormentoso y después las montañas al amanecer) y al
terminar el juego sigue solo. El líder revisó las capturas `vp2-pgraph-shot-09900.png` y `-10200.png`.

## Pendiente

- **Sonido real:** leer las muestras de cada voz (PCM o ADPCM de Xbox; ver [[adx]] y [[wva]] para los formatos
  de los datos), mezclarlas con su volumen y sacarlas por WASAPI.
- Las notificaciones de fin de voz que DirectSound espera para los sonidos de una sola vez.
- Los avisos se repiten en bucle unos 2 minutos antes del vídeo. **No depende del VP**: el vídeo sigue empezando
  hacia los 3,4 M de llamadas al kernel.
