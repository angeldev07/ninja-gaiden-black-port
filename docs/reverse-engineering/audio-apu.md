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

## Voces libres: la trampa SE2FE_IDLE_VOICE y la interrupción de la APU

- **Síntoma:** la demo del título se colgaba al entrar la primera cinemática, en el frame 4690. El hilo principal
  daba vueltas en `IDirectSoundBuffer_GetStatus` (160 millones de `RtlEnterCriticalSection`).
- **Qué espera DirectSound:**
  - el juego (función `0x322F70`) llama a `StopEx` y espera a que `GetStatus` deje de dar PLAYING;
  - `CMcpxBuffer_GetStatus` lee los flags del búfer (+0x12), y DirectSound solo los limpia cuando la APU avisa
    de que la voz quedó libre.
- **El aviso en el hardware:**
  - tras `VOICE_OFF`, `VOICE_RELEASE` o el fin de un búfer sin bucle, el procesador de voces envía al front
    end el método `SE2FE_IDLE_VOICE` (`0x8000`), con la voz como parámetro;
  - el front end lo atrapa (`FECTL` bits 8-11 = `0xF`, "requested"), deja el método y el parámetro en
    `FEDECMETH` (`0xFE801300`) y `FEDECPARAM` (`0xFE801304`), y levanta `ISTS` (`0xFE801000`) con
    `GINTSTS | FETINTSTS` (`0x11`).
- **La ISR de DirectSound** (`0x381092`, conectada al nivel 5 de interrupción):
  - lee `ISTS`; con el bit 4 activo, si `FECTL & 0xF00 == 0xF00` despacha el método;
  - `0x8000` libera la voz (`0x3809D7`).
- **Emulación:**
  - el procesador de voces encola cada voz libre (`vp_take_idle`);
  - el hilo de DPC entrega una por llamada a la ISR, a su IRQL;
  - mientras dura la llamada, las lecturas de esos registros devuelven la trampa (`crash.c`, `mmio_access`);
    fuera de ella, `ISTS` lee 0.
- **Resultado:**
  - la demo sigue con su montaje de cinemáticas y llega al menos al frame 10500;
  - en 200 s, 993 `VOICE_ON` frente a 46, porque DirectSound ya recupera las voces libres;
  - 986 avisos entregados y ninguno perdido.

## Música a trozos: el hilo vsync de CRI sin su prioridad (`VERIFIED`)

- **Síntoma:** con sonido, la música se oía a trozos aunque la salida no se vaciaba nunca (0 vaciados y 0 frames
  descartados), y el gameplay caía a 40-58 fps. Los efectos de sonido iban bien.
- **Medida:**
  - La música es la voz 68: PCM de 16 bits estéreo a 44,1 kHz (pitch `0xFE0B`), en un anillo de 16 384 muestras
    (unos 0,37 s) que CRI rellena.
  - Se compararon los hashes de cada tramo de 1 024 muestras con los de la vuelta anterior. Entre el 20 y el 30 %
    de los tramos sonaban sin haberse reescrito, o sea repetidos.
  - CRI escribía unas 2 560 muestras cada 80 ms (unas 32 000 por segundo) para una voz que consume 44 100.
- **Cadena (Ghidra):**
  - La capa `mwSnd` de CRI tiene una tabla de funciones en `0x433A00`. La posición la da `+0x38`
    (`IDirectSoundBuffer_GetCurrentPosition`) y la escritura `+0x6C` (`Lock`, copia y `Unlock`).
  - El servidor ADX lo mueve el hilo vsync de ADXM (`0x335710`): `D3DDevice_BlockUntilVerticalBlank`, servidores
    y `ResumeThread` del hilo principal de CRI (`0x3353B0`), que decodifica y se suspende.
  - Ese hilo daba unas 13 vueltas por segundo en vez de 60 (contador del propio CRI en `0x489E38`).
- **Causas:**
  1. **Prioridades ignoradas.** `KeSetBasePriorityThread` era un stub. CRI pide prioridad alta para su hilo vsync
     y TIME_CRITICAL para el principal; el hilo principal del juego la reaplica cientos de veces. Todos los hilos
     Xbox comparten un núcleo, como en la consola. Sin prioridades, el hilo vsync esperaba un cuanto entero
     detrás de otros hilos y se saltaba vblanks.
  2. **El vblank no iba a 60 Hz.** El hilo de DPC usaba `GetTickCount64`, que avanza a saltos de ~15,6 ms. Un
     vblank "cada 16 ms" caía cada 31 ms: entre 26 y 37 por segundo. Los `KTIMER` periódicos se retrasaban igual.
  3. **El hardware compartía el núcleo del juego.** El proceso entero estaba fijado a un núcleo, así que el hilo de
     render (la GPU) y el de mezcla (la APU, en TIME_CRITICAL) le quitaban tiempo al juego. En cuanto el juego tuvo
     sus prioridades, el hilo de audio se quedó a veces sin CPU (11 vaciados en 150 s).
- **Arreglo:**
  - `KeSetBasePriorityThread`, `KeQueryBasePriorityThread` y `KeSetDisableBoostThread` se aplican al hilo del
    host. Los valores de XAPI (-2..2 y ±16) son los mismos `THREAD_PRIORITY_*`. El hilo de DPC va en
    TIME_CRITICAL, porque las interrupciones se adelantan a todo.
  - El hilo de DPC usa un reloj QPC de 1 ms. El vblank se programa en microsegundos y sin deriva, igual que los
    timers periódicos.
  - Los hilos Xbox se fijan al núcleo 0 y los hilos que emulan hardware (render y mezcla) van al resto
    (`ngb_pin_hardware_thread`), como la GPU y la APU, que en la Xbox trabajan en paralelo a la CPU.
- **Resultado** (demo determinista, 150 s, con sonido):
  - el hilo vsync de CRI da 60 vueltas por segundo y ningún tramo de la música se repite;
  - 0 vaciados;
  - el gameplay va a 60 fps en todas las ventanas (antes, 40-58), con el dibujo en 1,4-2,6 ms por frame;
  - 0 errores del push buffer.
- **Lección:** los contadores de diagnóstico alteraban el reparto de CPU. Un muestreador de EIP en el hilo de DPC
  bajaba el vblank a 30 por segundo y la música dejaba de fallar. Para medir esto, mejor contadores baratos en
  el propio juego (`0x489E38`) que hilos que paren a otros.

