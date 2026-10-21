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

## Sonido real (`port/src/apu/mix.c` y `audio_out.c`)

- **Lectura de muestras:**
  - la dirección es `BA + CBO · tamaño_de_bloque`, en un espacio lineal de páginas de 4 KB;
  - cada página se traduce con la tabla SGE (`NV_PAPU_VPSGEADDR`, entradas de 8 bytes con la página física);
  - formatos: U8, S16, S24 y S32, y ADPCM de Xbox (IMA, 36 bytes y 64 muestras por canal y bloque).
- **La tabla SGE no la escribe DirectSound en RAM:** la programa por PIO, con `SET_CURRENT_INBUF_SGE` (`0x804`)
  para elegir la entrada y `SET_CURRENT_INBUF_SGE_OFFSET` (`0x808`) para escribir la página. Sin esto se leía
  basura.
- **Direcciones físicas fuera de la RAM contigua:** el búfer de audio de Sofdec vive en memoria normal del proceso,
  y `MmGetPhysicalAddress` devuelve la identidad para esa memoria (issue #31). El lector de la APU acepta esas
  direcciones si están mapeadas.
- **Volúmenes:**
  - DirectSound los fija por PIO (`SET_VOICE_TAR_VOLA/B/C`, `0x360-0x368`): 8 atenuaciones de 12 bits, de 1/64 dB
    por paso, y `0xFFF` es silencio;
  - el slot b lleva el canal `b % canales` al mixbin que diga `CFG_VBIN`/`CFG_FMT`;
  - mixbins 0 y 1, frontales; 2, central; 4 y 5, traseros (se mezclan a estéreo).
- **Voces internas de DirectSound** (en NGB, las 64-67, en bucle sobre 32 muestras): en la Xbox las rellena el DSP
  con la mezcla de efectos. Sin DSP serían ruido a volumen máximo, así que avanzan pero no se oyen.
- **Salida:**
  - `waveOut` a 48 kHz, estéreo y 16 bits;
  - el hilo de DPC mezcla en cada tick lo que corresponde al tiempo real, y se descarta lo que pase de 150 ms de
    cola;
  - `--mute` quita el sonido;
  - `--audio-wav` guarda la mezcla en `<log>-audio.wav`.
- **Verificado:** el audio del vídeo de introducción suena entre los 40 y los 58 s, con un nivel que varía segundo a
  segundo (RMS de 600 a 4 500) y una autocorrelación de 0,93-0,99, propia de una señal real. El ruido daría ~0. El
  WAV es `NGB_DATA\runs\aud9.log-audio.wav`.

## Pendiente

- La reverberación y los efectos (el DSP GP/EP no se emula).
- Las voces de stream (SSL) y las notificaciones de fin de voz.
- Las voces 3D (HRTF).
- Comprobar de oído la música del menú y los efectos del juego.
