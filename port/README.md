# port/: loader nativo de XBE (H6, arranque nativo)

Ejecuta el código x86 de un `default.xbe` dentro de un proceso Win32 de 32 bits, con un
kernel Xbox propio que registra cada llamada. No emula la CPU: el código del juego corre
tal cual. El hito H6 termina cuando el juego llega a la inicialización de D3D (primer
acceso MMIO al NV2A en `0xFD000000`).

Proyecto pedagógico. No incluye nada del juego ni del XDK de Microsoft. Las estructuras y
los prototipos del kernel salen de nxdk (`lib/xboxkrnl/xboxkrnl.h`, CC0) y la tabla de
exports de `src/ngb_tools/data/xboxkrnl_exports.tsv`.

## Compilar y ejecutar

```bat
port\build.cmd
build\port\ngb-loader.exe %USERPROFILE%\ngb-data\game\default.xbe
```

- `build.cmd` llama a `vcvarsall.bat x86` (VS 2022; se puede cambiar con `VCVARSALL`) y
  compila con `/W4 /WX /O2 /Zi`. Deja `ngb-loader.exe`, `ngb-core.dll` y los `.pdb` en
  `build\port\` (ignorado por git).
- El log va por defecto a `%NGB_DATA%\runs\h6-<fecha>.log` (fuera del repo); `--log`
  lo cambia. El disco duro virtual vive en `%NGB_DATA%\runs\hdd\`.
- Opciones: `--max-calls N` (200000), `--timeout S` (60), `--idle S` (10 s sin llamadas al
  kernel), `--lenient` (una llamada sin implementar devuelve `STATUS_SUCCESS`), `--echo`,
  `--mmio-strict` (para en cualquier MMIO o E/S de puertos), `--mmio-gpu-ram` (el NV2A
  también se trata como RAM trazada: sirve para explorar más allá de H6).
- Código de salida: 0 todo terminó, 1 uso/XBE inválido, 2 llamada sin implementar,
  3 excepción (incluido MMIO), 4 `--max-calls`, 5 timeout/inactividad, 6 el juego pidió
  apagar (`HalReturnToFirmware`, `KeBugCheck`).

Regenerar los stubs tras tocar el TSV: `python tools/gen_kernel_stubs.py` (o
`--check` para verificar). Tests: `python -m unittest discover -s tests` con
`PYTHONPATH=src`; `tests/port/test_loader.py` ejecuta el loader compilado sobre XBE
sintéticos y se salta si no existe `build\port\ngb-loader.exe` (o `NGB_LOADER`).

## Archivos

| Archivo | Propósito |
|---|---|
| `build.cmd` | Compilación x86 con MSVC: `ngb-core.dll` y el exe stub. |
| `src/loader_stub.c` | `ngb-loader.exe`: base fija `0x10000` y BSS `.xbox` que reserva la imagen del XBE; carga `ngb-core.dll`. |
| `launcher/` | `ngb-launcher.exe`: launcher gráfico (Win32 + GDI, sin dependencias), inglés y español. `launcher.c` (páginas y lógica; ajustes en `ngb.ini` junto al exe; PLAY lanza el loader con `NGB_DATA`), `ui.c` (widgets), `art.c` (arte original procedural), `import.c` (importar el juego del usuario en un hilo), `xdvdfs.c` (lector/extractor XDVDFS de imágenes `.iso`), `strings.h` (tabla EN/ES). |
| `res/` | Recursos: `ngb.ico` (shuriken cromado con tajo rojo, original; lo genera `make_icon.py` con solo la stdlib), `ngb.rc` (icono y versión de loader y core) y `version.h`. |
| `src/main.c` | `ngb_main`: argumentos, carga, thunk, hilo de arranque y vigilante (timeout, inactividad, fin). |
| `src/xbe.c`, `xbe.h` | Parseo mínimo: cabecera, secciones, TLS y des-XOR (retail/debug) del entry y del thunk. |
| `src/memory.c` | Mapa de la imagen, cabecera PE sombra, arena contigua en `0x80000000`, kernel PE falso, thunk. |
| `src/thread.c` | Hilos Xbox: KPCR/ETHREAD falsos, superposición en el TEB, pila propia con TLS, cadena SEH. |
| `src/crash.c` | Vectored exception handler: informe del crash, MMIO trazado, E/S de puertos. |
| `src/log.c` | Log thread-safe, resumen y parada (`ngb_stop`). |
| `src/kernel/stubs.c` | GENERADO: un stub por export con su convención y número de argumentos. |
| `src/kernel/dispatch.c` | `ngb_kcall`: línea de log por llamada y despacho a la implementación. |
| `src/kernel/data.c` | Las variables exportadas (`KeTickCount`, `XboxKrnlVersion`, `LaunchDataPage`...). |
| `src/kernel/impl_*.c` | Implementaciones: memoria (`mm`), `Rtl`, planificador (`ke`), hilos (`ps`), archivos (`io`), varios (`misc`). |
| `../tools/gen_kernel_stubs.py` | Envoltorio de `python -m ngb_tools.port.kernel_stubs` (genera `stubs.c` desde el TSV). |

Formato del log: `[n] T<hilo> @<dirección de retorno> <ordinal> <nombre>(args) -> ret`.
La línea se escribe al volver de la llamada, así que los detalles que añade la
implementación (`    · ...`) quedan justo encima. Al final van el motivo de la parada,
el estado de cada hilo Xbox (EIP, pila, llamada en curso) y las llamadas por función.

## Decisiones

**Memoria de la imagen.** El XBE no es reubicable y vive en `0x10000`, una zona que Windows
llena enseguida (heaps, pilas, NLS). Igual que el `cxbxr-ldr.exe` de Cxbx-Reloaded, el exe
se enlaza en `0x10000` con `/FIXED` y una BSS enorme: el cargador de Windows reserva
`[0x10000, 0x01010000)` al crear el proceso. El exe no tiene CRT; todo el loader está en
`ngb-core.dll`, que vive en otra dirección. `ngb_main` pisa el código del stub (no vuelve
nunca) y copia cabeceras y secciones (todas, también las no PRELOAD; `XeLoadSection`
solo cuenta referencias). La imagen queda RWX y el proceso sin DEP, como en Xbox.

**Cabecera PE sombra.** Tras pisar la cabecera PE del exe con la del XBE, `CreateThread`
falla (`ERROR_BAD_EXE_FORMAT`) y los hilos nuevos mueren en ntdll: Windows relee la
cabecera desde la base de la imagen (cambiar `PEB.ImageBaseAddress` no basta). Se copian
las NT headers a `0x01000000` (cola de la BSS), se escribe `MZ` en `0x10000` y en `0x1003C`
(`e_lfanew`) el offset a la copia. Los dos campos caen en el magic y en la firma RSA del
XBE, que el código del juego no lee en ejecución (solo el kernel al cargar).

**FS/KPCR (atajo de H6).** En WOW64 no hay LDT (`NtSetLdtEntries` no existe en x64), así
que `fs:` sigue apuntando al TEB de Windows. El código Xbox solo usa unos pocos campos y se
superponen en el TEB de cada hilo Xbox: `fs:[0x1C]` SelfPcr (EnvironmentPointer),
`fs:[0x20]` Prcb (ClientId.UniqueProcess) y `fs:[0x28]` CurrentThread (ActiveRpcHandle).
`fs:[0x24]` (Irql) NO se superpone: es `ClientId.UniqueThread`, que Windows usa para
despertar a quien espera un CRITICAL_SECTION (cambiarlo perdía despertares y colgaba D3D
en H7a). Las 7 lecturas `movzx r32, byte ptr fs:[24h]` del juego se parchean al cargar
a `mov r32, 0` (PASSIVE_LEVEL). `fs:[0]`, `fs:[4]` y `fs:[8]` (ExceptionList, StackBase,
StackLimit) significan lo mismo en ambos y se usan tal cual. Consecuencia:
`GetCurrentProcessId` devuelve basura en hilos Xbox; el loader no lo usa. Alternativa futura: parchear los accesos `fs:` como hace Cxbx-Reloaded.

**TLS.** El arranque de XAPI pone `_tls_index` negativo y accede con
`[fs:[4] + _tls_index*4]`: el bloque TLS vive justo debajo de StackBase y
`KTHREAD.TlsData` (offset 0x28) apunta a él. Cada hilo Xbox corre en una pila propia
(VirtualAlloc, mínimo 1 MB) con el bloque en la cima; el TEB se apunta a ella y la cadena
SEH se reinicia con una copia del registro final de ntdll dentro de la pila nueva, para que
la validación de SEH/SEHOP la acepte.

**Hilos y planificador.** Un hilo Xbox es un hilo Win32 (`PsCreateSystemThreadEx` llama a
la SystemRoutine de XAPI igual que el kernel real). Proceso con afinidad a un núcleo. Subir
a DISPATCH_LEVEL toma un lock global recursivo. Los timers y DPC los ejecuta un hilo Xbox
interno. Las interrupciones se aceptan pero nunca se disparan.

**Kernel.** Cada slot del thunk (`0x80000000|ordinal`) se reescribe con el stub (función)
o con la dirección de la variable (data). Los stubs se generan del TSV con la convención
real (stdcall/fastcall/cdecl) y `arg_bytes/4` argumentos, devuelven EDX:EAX y llaman a
`ngb_kcall`. Una llamada a un export sin implementación detiene la ejecución (salvo con `--lenient`). `XAPI` lee la cabecera PE de
`xboxkrnl.exe` en `0x80010000` (busca su sección INIT): hay una cabecera PE falsa sin INIT.

**Archivos.** `\Device\CdRom0` (D:) es la carpeta del XBE y es de solo lectura: nunca se
escribe ahí. `\Device\Harddisk0\PartitionN\...` va a `%NGB_DATA%\runs\hdd\partitionN\`; el
volumen en crudo (`...\PartitionN` sin barra final) es `partitionN.raw`. El tamaño de
volumen imita FATX (clúster de 16 KB): con el de NTFS, XAPI pide formatear y reinicia.

**MMIO.** En H6 el primer acceso al NV2A detenía la ejecución; desde H7a lo atiende el
modelo de `src/gpu/` (ver abajo; `--mmio-gpu-ram` vuelve al modo de exploración). El resto
de periféricos (USB, APU, AC97, NIC, flash) se traza con RAM a cero: cada acceso se
registra (los 400 primeros), se abre la página, se ejecuta la instrucción en paso a paso y
se vuelve a cerrar. Unos pocos registros que el arranque sondea se leen fijos (APU
`PIO_FREE` = 0x80, AC97 `GLOB_STA` = códecs listos) y el bit de reset de los `x_CR` del
AC97 se limpia solo. `IN`/`OUT`, `CLI`/`STI` y `WBINVD` se registran y se saltan.

## H7a: modelo mínimo del NV2A y captura del push buffer

Archivos en `src/gpu/`: `nv2a.c` (registros), `pushbuf.c` (decodificador del push buffer,
puro y probado en `tests/pushbuf_test.c`), `pgraph.c` (métodos, log, clear, semáforos,
PNG), `nv097_names.c` (nombres de los métodos 3D) y `png.c`.

- **Registros.** `[0xFD000000, 0xFE000000)` queda reservado sin acceso; cada acceso falla, el
  VEH escribe en la página el valor modelado, ejecuta la instrucción en paso a paso y aplica
  los efectos de la escritura. Modelados: `PMC_BOOT_0` (0x02A000A3), interrupciones W1C
  (PMC, PFIFO, PGRAPH, PCRTC, PTIMER; escribir 1 en `PCRTC_INTR_0` baja también el bit 24
  de `PMC_INTR_0`), ID PCI de PBUS, PFIFO vacío, `DMA_CTL` válido, `DMA_PUT`/`DMA_GET`
  (PFIFO y USER canal 0), PTIMER que avanza, `PFB_CSTATUS` = 64 MB, flush de `PFB_WBC` que
  termina al instante, PGRAPH ocioso, `PCRTC_RASTER`, PLL de PRAMDAC enganchados. Lo
  desconocido es RAM y se anota una vez en el log. Dos excepciones por acceso: en la
  inicialización son miles y por frame D3D solo toca PUT/GET, así que no compensa mapear
  como RAM los registros sin efectos. Fuente de offsets y valores: xemu `hw/xbox/nv2a`.
- **PRAMIN** (0xFD700000) es RAM: una sección de 1 MB mapeada a la vez ahí y en 0x83F00000
  (el último MB de la RAM física, como en el hardware).
- **FIFO.** Al escribir `DMA_PUT` se decodifica de GET a PUT dentro del objeto DMA del canal
  (RAMIN + `CACHE1_DMA_INSTANCE`·16) y GET pasa a valer PUT. `SET_OBJECT` y los
  `SET_CONTEXT_DMA_*` se resuelven por RAMHT (hash de xemu). Efectos que D3D espera de
  vuelta: `BACK_END_WRITE_SEMAPHORE_RELEASE` escribe en memoria y `CLEAR_SURFACE` de color
  rellena la superficie. No se rasteriza nada.
- **Log de PGRAPH**: `<log>-pgraph.log`, con un método por línea y las rachas agrupadas
  (`--pgraph-frames N` frames completos, 3 por defecto, más el primer frame que dibuja). Al
  final del log principal va el resumen: frames (FLIP_STALL), BEGIN_END por primitiva,
  texturas y métodos por tipo.
- **PNG**: `<log>-pgraph-NNN.png` es el búfer de color en cada FLIP de los 10 primeros frames
  y cada 100; `<log>-pgraph-texNN.png`, cada textura nueva que se usa al dibujar (formatos
  de 32 y 16 bits, A8 e I8 con paleta; lineales o swizzle).
- **Vblank** sintético a ~60 Hz en el hilo de DPC: marca `PCRTC_INTR_0`/`PMC_INTR_0` y llama
  a la ISR de D3D, que encola su DPC y señala `m_VerticalBlankEvent`.
- **Memoria "física"** (`MmGetPhysicalAddress`): arena contigua = 0..64 MB; la imagen del
  XBE se ve en la ventana `NGB_PHYS_XBE` (96 MB + VA); el resto, identidad.
- **DSP falso** de la APU: DSOUND espera que el GP DSP ponga a 0 un buzón (offset 0x810 de
  la primera página de la lista de `NV_PAPU_GPSADDR`); el hilo de DPC lo hace por él.
- **`fs:[0x24]`** ya no se superpone (ver FS/KPCR): era la causa del cuelgue de D3D en H7a.
- `--symbols CSV` (de Ghidra, en NGB_DATA) añade `<función+off>` a las líneas del log y a los
  crashes. `--log-cap N` limita las líneas por función del kernel (2000 por defecto).

Resultado (30 s de `default.xbe`): D3D y DSOUND inicializan, el juego llega a la pantalla de
avisos ("Warnings") y envía ~40 000 frames (sin pacing: FLIP_STALL no espera al vblank),
~800 000 BEGIN_END (TRIANGLE_STRIP con INLINE_ARRAY) y 5 texturas (fuente I8 128x128, rótulo
A8 256x64, dos búferes de 720x480). Siguiente paso: rasterizar (HLE de D3D o PGRAPH sobre
D3D11/Vulkan), pacing de FLIP_STALL contra el vblank y entrada del mando (XInput) para pasar
de la pantalla de avisos.

## H7b: primer frame en una ventana de PC

`--window` abre la ventana del presentador (`src/gfx/present.c`: Win32 + GDI en su hilo,
escalado entero, F12 guarda captura en `%NGB_DATA%\runs\shots`, FPS en el título).

- **Enganche** (`gpu/pgraph.c`): en cada `FLIP_STALL` se presenta el búfer de color de 720 de
  ancho que el juego limpió o dibujó en ese frame (A8R8G8B8, pitch de `SET_SURFACE_PITCH`).
  Cada 300 frames presentados se guarda una captura automática (`<log>-pgraph-shot-NNNNN.png`).
- **Pacing**: `FLIP_STALL` espera al siguiente 1/60 s de reloj (un vblank de 60 Hz virtual)
  antes de devolver GET; `--no-pacing` lo quita. Desde H7c la espera se hace al volver del
  kick, ya sin el lock del NV2A ni el de PGRAPH (issue #35); solo un kick con dos
  `FLIP_STALL` seguidos (el push buffer en bucle del vídeo) espera dentro.
- **Rasterizador de referencia** (`gpu/raster.c`, puro, probado en `tests/raster_test.c`):
  triángulos con posición ya en pantalla (XYZRHW: 4 floats en el atributo 0), difuso
  (atributo 3, `UB_D3D`/`UB_OGL`/float, o la constante de `SET_VERTEX_DATA4UB`), coordenadas
  de la etapa 0 (atributo 9), muestreo por punto (A8R8G8B8/X8R8G8B8/R5G6B5, A8, I8 con paleta;
  swizzle o lineales con coordenadas en texels), combinador mínimo textura x difuso, prueba de
  alfa y mezcla con los factores GL del NV2A, interpolación con 1/w. Recorta al clip de la
  superficie. Primitivas: TRIANGLES, TRIANGLE_STRIP, TRIANGLE_FAN, QUADS, QUAD_STRIP, POLYGON.
- **Métodos NV097 nuevos**: `SET_VERTEX_DATA_ARRAY_FORMAT[16]`, `SET_BEGIN_END` (dibuja al
  cerrar), `INLINE_ARRAY`, `SET_VERTEX_DATA4UB` (difuso), `SET_ALPHA_TEST_ENABLE`,
  `SET_ALPHA_FUNC`, `SET_ALPHA_REF`, `SET_BLEND_ENABLE`, `SET_BLEND_FUNC_SFACTOR/DFACTOR`,
  `SET_TRANSFORM_EXECUTION_MODE` (registrado), y la etapa de textura 0 ya leída en H7a
  (`OFFSET`, `FORMAT`, `CONTROL0/1`, `IMAGE_RECT`, `PALETTE`).
- Lo que no se dibuja se cuenta en el resumen: posiciones de 3 floats (geometría 3D, que
  necesita el intérprete de vertex programs), puntos y líneas, `DRAW_ARRAYS`/`ARRAY_ELEMENT*`
  (vértices en memoria) y superficies swizzle.

Además, para pasar de la pantalla de avisos sin colgarse:

- **Suspensiones seguras**: el juego planifica sus hilos con SuspendThread/ResumeThread.
  Suspender un hilo con un lock del loader tomado (kernel emulado, VEH del NV2A o IRQL >=
  DISPATCH) colgaba al resto: ahora NtSuspendThread se aplaza hasta que el hilo sale de
  ese código (`ngb_kernel_enter`/`ngb_kernel_leave`), como la APC de suspensión del kernel real.
- **Eventos limpiados en memoria**: D3D pone a 0 el `SignalState` de su evento de vblank sin
  llamar al kernel; `KeWaitFor*` alinea antes el objeto de Windows con la cabecera.
- `--pgraph-at N` vuelca al log de PGRAPH los frames N..N+2; tras `--log-cap`, el log del
  kernel guarda una muestra cada 100 000 llamadas; el vigilante de inactividad ya no confunde
  una carrera entre hilos con 15 s sin llamadas.

Resultado (`--window`, 75 s de `default.xbe`): la ventana muestra la pantalla de avisos
completa ("Warnings" y el texto que se desplaza y se funde) a 60 fps; capturas automáticas en
`%NGB_DATA%\runs\<log>-pgraph-shot-NNNNN.png`. Tras ~40 s el juego deja los avisos, abre
`spr.afs` y pasa a una pantalla que solo limpia a negro (no hay geometría nueva que dibujar).
Siguiente paso: averiguar qué espera ahí (mando conectado, carga de datos o vídeo) e
interpretar los vertex programs para la geometría 3D.

## H7c: render con OpenGL en el runtime

`--renderer gl|soft` elige quién dibuja. Por defecto (`gl` sin pedirlo) se usa OpenGL 4.x si
hay `--window` y el driver da un contexto; si no, el rasterizador por software de H7b.

- **Hilo de render** (`gpu/pgraph_gl.c`): el contexto GL vive en un hilo propio, sobre la
  ventana del presentador (`gfx_set_external`: GDI deja de pintar). `pgraph_kick` le pasa el
  kick entero y espera (sigue siendo síncrono). Motivo: el kick llega desde el VEH en
  cualquier hilo Xbox, con el TEB parcheado y una pila propia, y un contexto GL solo puede
  estar actual en un hilo.
- **Superficies**: cada render target (color y zeta) es una textura GL indexada por su
  dirección física (`gpu/rcache.c`). Una textura cuya dirección es la de una superficie se
  muestrea directamente de la GPU (render-to-texture); si además es el destino actual, se
  copia antes (GL no deja leer y escribir a la vez). Se dibuja con la fila 0 arriba, como en
  la Xbox, y el present la voltea.
- **Dibujo**: `INLINE_ARRAY`, `DRAW_ARRAYS` y `ARRAY_ELEMENT16/32` con los formatos de
  `SET_VERTEX_DATA_ARRAY_FORMAT` y `SET_VERTEX_DATA_ARRAY_OFFSET`; atributos constantes de
  `SET_VERTEX_DATA*`. Shaders: vertex programs (`0x0B00` + LOAD/START) con `nv2a_vsh_to_glsl`
  o, en modo fijo, un shader con la matriz compuesta; combiners con `nv2a_psh_to_glsl`;
  estado con `glstate`. Programas cacheados por hash del microcódigo y de los combiners.
- **Texturas**: `texconv`, con caché por dirección + formato y hash del contenido, que se
  comprueba la primera vez que se usa cada textura en un frame.
- **Present**: en `FLIP_STALL`, blit de la superficie de 720 al back buffer y SwapBuffers
  (sin vsync: el ritmo lo pone el pacing). Las capturas automáticas leen la textura de la GPU.
- **Medidas**: cada 300 frames el log escribe una línea `perf:` con los fps reales y el
  tiempo por frame de cada fase (pusher, dibujo, clear, present, pacing, log), más llamadas al
  kernel, accesos MMIO y kicks por frame.
- **Pusher parado** (`gpu/nv2a.c`): con el bit 0 de `CACHE1_PUSH0` o de `CACHE1_DMA_PUSH` a 0,
  una escritura de PUT solo se guarda, y el kick se hace al reactivarlo. Al reiniciar D3D para
  el vídeo, el juego para el pusher, libera el push buffer viejo y escribe PUT = 0 antes que
  GET. Antes ese PUT se ejecutaba: el pusher recorría el búfer liberado durante 4 M de
  palabras, con FLIP_STALL de los avisos viejos y unos 2 minutos de pantalla negra, en los dos
  renders. El log anota las escrituras del FIFO que no son un PUT normal (`NV2A: FIFO`).

## Deuda conocida

- 106 de las 128 funciones que importa `default.xbe` tienen implementación (119 exports
  en total). Faltan 22: la pila de E/S por IRP (`IoBuildSynchronousFsdRequest`,
  `IoInvalidDeviceRequest`, `IoStartNextPacket`, `IoStartPacket`,
  `IoSynchronousDeviceIoControlRequest`, `IoSynchronousFsdRequest`, `IofCallDriver`,
  `IofCompleteRequest`, `IoMarkIrpMustComplete`), `NtDeleteFile`, `ObReferenceObjectByName` y la criptografía `Xc*` (SHA-1, RC4, HMAC, RSA, DES). Además,
  `ObReferenceObjectByHandle` solo resuelve hilos y renombrar archivos no está hecho.
- E/S síncrona: las rutinas APC de `NtReadFile` no se llaman.
- Interrupciones: solo el vblank del NV2A (sintético, H7a); la de USB no existe. Los timers
  tienen ~16 ms de granularidad.
- H7a: sin rasterizado ni pacing de FLIP_STALL; DSP de la APU falso; la traducción de
  direcciones "físicas" es aproximada (arena, ventana del XBE, identidad).
- Las tablas de secciones críticas y objetos crecen sin liberar; las pilas Xbox no se liberan.
- Atajos frágiles documentados arriba: superposición en el TEB y cabecera PE sombra.

## Jugar

```
portuild.cmd      compila (una vez)
port\play.cmd       abre el juego en una ventana: sonido, mando de PC y teclado
port\play.cmd --mute
```

- Con `--window` (lo que usa `play.cmd`) no hay límites de tiempo ni de llamadas, salvo que se pidan con
  `--timeout`, `--max-calls` o `--idle`.
- El teclado solo cuenta con la ventana del juego activa:
  - Enter = START + A, Esc = BACK + B;
  - flechas = cruceta, WASD = stick izquierdo;
  - J K L I = A B X Y.
