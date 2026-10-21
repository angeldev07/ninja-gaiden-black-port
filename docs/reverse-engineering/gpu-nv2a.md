---
tags: [ngb, reverse-engineering, runtime, gpu, h7]
updated: 2026-10-21
status: current
---

# GPU NV2A en el runtime nativo (H7): la pantalla de avisos en una ventana

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

## H7b: el primer frame visible (`VERIFIED`)

**La pantalla de avisos se ve en una ventana del PC a 60 fps**: el rótulo "Warnings" con su brillo y el texto
desplazándose y fundiéndose. Es el código del juego ejecutándose de forma nativa.

- Comando: `build\port\ngb-loader.exe <default.xbe> --window`.
- Capturas: el loader guarda una cada 300 frames (`<log>-pgraph-shot-NNNNN.png`); el líder revisó la de
  `h7b-int-pgraph-shot-00600.png`.

### Cómo se dibuja

- **Rasterizador por software** (`port/src/gpu/raster.c`, código puro con 23 pruebas hechas a mano):
  - triángulos con la posición ya en pantalla (XYZRHW), color difuso y texcoord0;
  - muestreo por punto de A8R8G8B8, X8R8G8B8, R5G6B5, A8 e I8 con paleta, con o sin swizzle;
  - textura × difuso, alpha test, blend con los factores GL del NV2A y corrección de perspectiva;
  - las primitivas TRIANGLES, TRIANGLE_STRIP, TRIANGLE_FAN, QUADS, QUAD_STRIP y POLYGON.
- **Métodos NV097 nuevos:**
  - `SET_VERTEX_DATA_ARRAY_FORMAT`;
  - `SET_BEGIN_END`: se dibuja al cerrar;
  - `INLINE_ARRAY` y `SET_VERTEX_DATA4UB`;
  - alpha test y blend.
- **Presentador** (`port/src/gfx/present.c`, Win32/GDI): en cada `FLIP_STALL` copia el búfer de color de 720x480
  a la ventana. Después espera al siguiente 1/60 s, lo que fija el ritmo (*pacing*). Con `--no-pacing` vuelve a
  ir sin límite: unos 1 300 fps.
- **Para el render acelerado** ya hay traductores de shaders ([[shaders-nv2a]]) y un backend OpenGL 4.6 en
  `feat/gl-backend`. El rasterizador por software basta para los avisos, que son 2D.

### Lo que hubo que arreglar en el kernel emulado

- **`NtSuspendThread` aplazado.** El juego reparte trabajo entre hilos con Suspend/Resume, y podía congelar un
  hilo que tenía tomado un lock del loader (kernel emulado, VEH del NV2A o IRQL ≥ DISPATCH). Ahora la suspensión
  espera a que el hilo salga de ese código.
- **Eventos limpiados en memoria.** D3D pone a 0 el `SignalState` de su evento de vblank directamente, sin
  pasar por el kernel. `KeWaitFor*` ahora sincroniza el evento de Windows con la cabecera. Antes,
  `BlockUntilVerticalBlank` volvía al instante unas 50 000 veces por segundo.
- **Vigilante de inactividad:** una carrera hacía saltar "15 s sin llamadas" a los 5 s.

### Hasta dónde llega

1. La pantalla de avisos completa, unos 40 s.
2. **Se reproduce el vídeo de introducción** (Sofdec, un mar tormentoso), decodificado por el propio juego y
   dibujado con el rasterizador por software. El líder revisó la captura del frame 3600 de `exit-fix.log`.
   - Antes, el juego se quedaba en una pantalla negra. **La causa era un bug del kernel emulado**, no el audio
     ni el mando.
   - `GetExitCodeThread` de XAPI (`0x32BE72`) lee `Header.SignalState` (+4) y `ExitStatus` (+0x120) del ETHREAD.
     El hilo que enumera las partidas guardadas termina pronto, pero `ngb_thread_exit` nunca escribía esos
     campos.
   - Por eso la tarea de fibra del hilo principal (`0x168030`) esperaba en un bucle `while (code == 0x103)`
     para siempre.
   - **Arreglo:** al terminar, un hilo escribe ExitStatus y pone SignalState a 1. Lo cubre un test con un XBE
     sintético.
3. Para reproducir el vídeo, el juego reinicia D3D dos veces (`AvSetDisplayMode` con pitch `0xB40`) y el pusher
   lo soporta.
4. **El vídeo se queda en su primer fotograma** (`INFERRED`: Sofdec avanza con el reloj del audio, y la APU no
   está emulada). Con START se salta.
5. **Pantalla de título** ("PRESS START BUTTON", con Ryu de fondo; captura `title4-pgraph-shot-15300.png`,
   revisada por el líder). Antes de mostrarla, el juego verifica sus datos con `XcSHAInit/Update/Final`, que
   ahora están implementadas (SHA-1 y HMAC propios en `port/src/kernel/sha1.c`, con los vectores de FIPS 180 y
   RFC 2202).

**Los avisos "en bucle" van a cámara lenta (`VERIFIED` con `--watch`):**
- La tarea de los avisos (`0x168030`) hace, por orden:
  1. espera a T8;
  2. ejecuta 5 fases (tabla `0x443B1C`);
  3. espera a la caché de `z:\demo.afs` (`FUN_00304f10`, flag `0xD7DCF9`), que termina en 1,5 s;
  4. dibuja los avisos (`FUN_001f73b0`, estado en `0x906180`).
- En PAL (región 4), los avisos esperan 3·60 frames (`0xE130DA = 60`) y luego desplazan el texto 0,3 px por frame
  (`0x43018C`) hasta recorrer 640 px: unos 2 300 frames, **38 s a 60 fps**. START o A los saltan.
- **No hay ningún bucle:** el juego corre a 42 fps o menos, porque el rasterizador por software no llega a 60, y
  los avisos se alargan a 1-3 minutos. Los fundidos que se ven son los del propio desplazamiento.
- La solución es el render por GPU (H7c).
- Por el camino:
  - el loader pide al sistema resolución de reloj de 1 ms (`timeBeginPeriod(1)`), porque sin ella cada `Sleep`
    corto de la Xbox duraba 15,6 ms;
  - se añadió `--watch` para vigilar variables del juego.

## H7c: render por GPU con OpenGL (`VERIFIED`)

`--renderer gl` es el modo por defecto si hay ventana y contexto; `--renderer soft` queda como respaldo. Código en
`port/src/gpu/pgraph_gl.c` y `rcache.c`; usa [[shaders-nv2a]], `texconv` y `glstate`.

| Métrica (líneas `perf:` del log) | Software | OpenGL (RX 6600) |
| --- | --- | --- |
| Título y menú | 16 fps (raster 59 ms/frame) | **60 fps** (pusher 0,7-2 ms) |
| Avisos | 38-53 fps | **60 fps**: terminan a los 40,0 s, que es su duración de diseño |

- **Hilos:**
  - el kick llega en cualquier hilo Xbox, pero el contexto GL va atado a un solo hilo. Por eso hay un hilo de
    render propio, y `pgraph_kick` le pasa el trabajo de forma síncrona;
  - el pacing se hace fuera de los locks (issue #35).
- **Superficies:**
  - las de color y profundidad son texturas GL, indexadas por dirección física;
  - render-to-texture sin pasar por la CPU;
  - una firma dispersa de la RAM detecta qué superficies ha pisado la CPU.
- **Dibujo:**
  - INLINE_ARRAY, DRAW_ARRAYS y ARRAY_ELEMENT;
  - vertex programs (`0x0B00` + LOAD/START) o fixed function;
  - cachés de programas y texturas por hash.
- **Bug del FIFO que causaba el "parpadeo" tras los avisos** (presente también en soft):
  - al reiniciar D3D para el vídeo, el juego para el pusher (`CACHE1_PUSH0 = 0`), libera el push buffer viejo y
    pone PUT a 0;
  - el NV2A emulado hacía kick con cualquier escritura de PUT, aunque el pusher estuviera parado, y reproducía
    unos 4 M de palabras del búfer liberado. Salían FLIP de los avisos viejos alternando con negro durante unos 2
    minutos, más 121 errores del pb;
  - ahora, con el pusher parado PUT solo se guarda, y el kick llega al reactivarlo.
  - Resultado: los avisos dan paso al vídeo (mar, montañas, logo de Team NINJA) y al título, a 60 fps.
- **3D tras New Game** (visto una vez):
  - 1 091 primitivas por frame con vertex programs y 0 errores de shader, pero a 6,6 fps, y la captura muestra un
    búfer intermedio;
  - falta el formato `0x2E` (profundidad lineal como textura), bajar el coste por draw y elegir la superficie que
    se presenta.

## H8b: gameplay en 3D (la demo del título)

Si se deja la pantalla de título sin pulsar nada, el juego reproduce una **demo de gameplay con su propio motor**:
- es una escena 3D determinista que no necesita input, así que es el mejor caso de prueba del render 3D;
- el usuario avisó de ella, y llega sola: avisos (40 s), vídeo (~15 s), título y espera.

**Resultado (`VERIFIED`):**
- Ryu pelea contra demonios con la Dragon Sword y un Ninpo de fuego, con modelos, animaciones, texturas y efectos.
  El líder revisó las capturas `demo1-pgraph-shot-05400.png` y `-06600.png`.
- Va a 35-60 fps (antes, 6,6).
- Suenan 46 voces, 38 de ellas efectos de una sola vez, sin cortes.

**Qué se cambió:**
- **Rendimiento:** estado GL incremental, menos hashing de texturas, constantes solo si están sucias y cachés nuevas
  en `rcache.c`.
- **EEPROM** (`kernel/eeprom.c`):
  - XAPI pide la EEPROM entera (índice `0xFFFF`) para firmar los archivos del disco duro cuando `XboxHDKey` es
    cero;
  - antes solo se escribían 4 bytes y el resto era basura de la pila, así que la firma de `T:\system.dat` cambiaba
    entre ejecuciones y el juego decía "Unable to load game settings";
  - ahora hay una imagen completa de 256 bytes.
- **Input determinista:** `--input-script` acepta la unidad `p` (lecturas del mando), por ejemplo
  `t=2400p:START@10p`. Así no depende de lo que tarden las cargas.

**Pendiente:**
- el cielo y el fondo salen de un azul plano (skybox o niebla) y falta parte del suelo;
- las caídas a ~30 fps en escenas cargadas;
- el resto de los menores del issue #43.

**Lección de proceso:** el agente gastó 1 h 30 min intentando llegar al nivel por New Game, cuyo vídeo inicial no se
puede saltar sin partida guardada, y llenó `NGB_DATA` de capturas. La demo del título daba la misma escena 3D sin
input. Los agentes tienen que limpiar sus capturas y buscar antes el caso de prueba más barato.

## Siguiente

1. Pasar de la pantalla de título al menú principal, y averiguar qué esperan los avisos y el vídeo (audio).
2. Geometría 3D: vertex programs ([[shaders-nv2a]]) y dibujo desde arrays de vértices (`DRAW_ARRAYS`/`ARRAY_ELEMENT`)
   sobre el backend OpenGL.
