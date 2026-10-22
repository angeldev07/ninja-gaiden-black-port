# NGB Port Wiki: log

Solo se agrega, y lo más nuevo va abajo. Cada entrada empieza con `## [fecha] tipo | título`.

## [2026-10-20] ingest | ISO Redump convertido a XISO
La imagen Redump (XGD2, partición del juego en `0x18300000`) se convirtió a XISO con
extract-xiso v2.7.1, descargado de XboxDev porque la herramienta anterior ya no estaba en
disco. El XISO pesa 6.34 GB y tiene 249 archivos. El original quedó intacto. Ver [[herramientas]].

## [2026-10-20] query | Viabilidad del port y precedentes
Se investigó [[opence]] (descompilación de Halo CE con símbolos de depuración y D3D8 sobre GL), el
intento abandonado [[decomp-ninja]] y los términos de uso de Anthropic. Las Consumer Terms (§3,
vigentes desde 2025-10-08, https://www.anthropic.com/legal/consumer-terms) solo prohíben hacer
ingeniería inversa de los servicios de Anthropic. La Usage Policy (https://www.anthropic.com/legal/aup)
no menciona la ingeniería inversa de terceros y prohíbe infringir la propiedad intelectual. Es una
lectura de los textos, no asesoría legal: la frontera real la marca la ley local. Se evaluó
[[rea]]: no acepta XBE y en Windows es de solo lectura, así que queda como auxiliar.

## [2026-10-20] decision | Enfoque, herramientas y datos
[[adr-01-enfoque-port]], [[adr-02-herramientas-re]] y [[adr-03-datos-fuera-del-repo]]. El usuario
delegó las decisiones técnicas al líder, con dos condiciones: documentarlo todo en este vault y
mostrar avances tangibles por hitos ([[roadmap]]).

## [2026-10-20] milestone | H0: cerebro y base
Se inicializó el repo git (sin commits) con `.gitignore`, que bloquea los datos del juego. El vault
se creó siguiendo el estándar del vault de Rithm: [[CLAUDE]], [[GOAL-ngb-port]], [[index]], este log
y la configuración de `.obsidian` copiada. El XISO se extrajo a `%USERPROFILE%\ngb-data\game\`.
Se definió el equipo en [[equipo]] y [[plantilla-tarea-agente]].

## [2026-10-20] ingest | Estructura del disco
[[disco-ngb]]: 206 AFS, 26 SFD, 5 XBE, 3 XPR y XACT. **Los 206 `.afs` llevan la magia `AFS\0`
(CRI Middleware)**, en contra de lo que reportaba [[decomp-ninja]] ("formatos desconocidos").
`VERIFIED`: hexdump de 4 cabeceras y parseo de los 206 con `afs inventory` (206/206 válidos).
Se lanzaron dos analistas en paralelo para H1: `tools/xbe_info.py` y `tools/afs.py`.

## [2026-10-20] decision | Control de versiones delegado al líder
[[adr-04-control-de-versiones]]: el repo privado `angeldev07/ninja-gaiden-black-port` es trunk-based,
con Conventional Commits, PRs con squash, revisión publicada como comentario (GitHub no deja
aprobar una PR propia), puertas de CI y tags por hito. Gotcha: git y gh se ejecutan fuera del sandbox.

## [2026-10-20] ingest | Anatomía de default.xbe (H1)
`ngb_tools.xbe` analiza los 5 XBE del disco ([[xbe-default]]). Lo esencial: el motor se llamaba **"Kunai"**
(`c:\Kunai\~output\release_ltcg\Kunai.exe`, PDB con GUID `BAAFA588…`, que no está en el disco). Se compiló con
**MSVC 13.10 (VS.NET 2003) y el XDK 5849**, en un build retail LTCG sin asserts ni RTTI propio. Usa
**D3D8LTCG**, lo que complicará la identificación por firmas en H5. Importa 145 símbolos del kernel (128 funciones
y 17 variables de datos), que son el alcance del kernel HLE de H6. Enlaza middleware de CRI (ADX, AIX y Sofdec), de mayo y junio de 2005.
`NinjaTrilogy.xbe` y `NinjaTomoe.xbe` son el emulador de los Ninja Gaiden de NES (US y JP).
## [2026-10-20] milestone | PR #2 y PR #1 mergeadas: puertas de calidad y vault
`main` incorpora el paquete `ngb_tools` con ruff, mypy strict, CI en Ubuntu y Windows, y la guardia
`ngb_tools.hygiene`, que pasó tres rondas de revisión y cerró dos bypass ([[adr-03-datos-fuera-del-repo]]).
Los menores pendientes están en el issue #3. El repo solo admite squash y `main` está protegida
([[adr-04-control-de-versiones]]).

## [2026-10-20] ingest | Formato AFS y contenido de los assets (H1)
`ngb_tools.afs` inventaría los 206 AFS: 3,766 entradas, todas con nombre, e invariantes válidos en 206/206.
**No hay compresión de propósito general ni cifrado** en la ventana de 64 KiB que se midió: solo 2 entradas de
entropía alta y ninguna firma de zlib o gzip. ADPCM y DXT son códecs conocidos. Se crearon
[[afs]], [[chunk-teamninja]], [[kmd]], [[xpr0]], [[adx]] y [[wva]]. Peculiaridad: el `u32` final de cada
atributo AFS no es el tamaño, sino la cabecera leída como array. `ryu.kmd` lleva 19 texturas (DXT1 y
DXT4/5), que son el objetivo de H2.

## [2026-10-20] milestone | H1 cerrado (tag milestone/h1) y H2: primeras texturas del juego en PNG
Las PRs #4 (AFS y `_paths`) y #5 (XBE) entraron después de 4 y 2 rondas de revisión. Los menores pendientes
están en los issues #6 y #7, y el tag `milestone/h1` está en `14fd808`.
**H2: `ngb_tools.xpr` exporta las texturas del juego a PNG.** `ryu.kmd` da 19 texturas (traje, ojo verde, piel,
metal y efectos), `menu.spr` 97 (Dragon Sword, marcos y kanji) y `font.xpr` 1 (atlas de glifos). El líder las
revisó a ojo. Hallazgos en [[xpr0]]:
- `Data` es relativo a `header_size`;
- el push buffer ocupa 24 B y no 20;
- `header_size` no va alineado a `0x800`;
- DXT va sin swizzle.
En [[chunk-teamninja]]: el campo de tamaño no acota los slots. La tabla de las texturas de Ryu está en [[kmd]].

## [2026-10-20] milestone | H3: Ryu en 3D (glTF)
Un spike resolvió la geometría de `ryu.kmd`, validado con renders hechos por un rasterizador propio:
- vértices FVF de D3D (`0x112`/`0x116`/`0x042`);
- triangle strips u16;
- la cara se dibuja desde un push buffer;
- HIERARC: los vértices vienen en el espacio de su hueso.
`ngb_tools.kmd` lo exporta a glTF 2.0 y OBJ. Ryu, Ayane, Murai y las armas de Doku salen completos; el cuerpo de
Doku usa una variante del descriptor que todavía no se soporta. Detalle en [[kmd]].
Además se adoptó una política nueva, una sola ronda de revisión por PR ([[equipo]]), y la PR #8 (H2) entró
en `main` con el tag `milestone/h2`.

## [2026-10-20] milestone | H3 cerrado (tag milestone/h3) y H4: todo el audio del juego en WAV
La PR #10 (`ngb_tools.kmd`, glTF) entró con una sola ronda de revisión; los menores quedan en el issue #11.
**H4: `ngb_tools.audio`** decodifica bgm 95/95, sse 317/317, 2,966 registros `.wva` y los 7 de `Wave.xwb`. Ningún
ADX va cifrado. Se corrigió el layout de [[wva]]: el bit31 marca el tamaño del bloque de parámetros. Se validó
con espectrogramas (música, voz y efectos reconocibles) y estadísticas. Detalle en [[adx]].

## [2026-10-21] decision | Sesión 2: estado al retomar
- La **PR #14** (H6, cargador nativo) salió BLOQUEANTE en su revisión por seguridad del host. El mapeo de
  rutas del kernel HLE no validaba `..`, así que podía escribir, crear o borrar fuera de `NGB_DATA`. Con la
  política de [[equipo]], eso sí bloquea. Se está corrigiendo.
- Antes del límite de la sesión 1, las tres ramas en curso se guardaron como commits `wip` en origin:
  `feat/nv2a-min` (H7a: la init de D3D pasa y llegan el primer CLEAR y el primer FLIP; se cuelga esperando el
  vblank), `feat/kmd-anim` (`--format glb` y el parser de `.mtn`: 258 motions de Ryu) y `feat/stage-export`
  (parser de escenarios: descriptor multiparte y HITMDL). Las notas de traspaso están en `NGB_DATA\handoff\`.
- Se instaló **Ghidra 12.1.4 + ghidra-xbe** con aprobación del usuario ([[herramientas]]) y se lanzó H5.
## [2026-10-21] milestone | H6: el código de NGB corre de forma nativa en PC
`ngb-loader.exe` (C, MSVC x86, `/W4 /WX`) carga `default.xbe` en `0x10000` dentro de un proceso Win32 y lo ejecuta
**de forma nativa**, con un kernel HLE propio: 118 exports, 105 de las 128 funciones que importa el juego. El juego
crea su hilo, inicializa XAPI, monta las unidades, escribe metadatos en U:, formatea la caché Z:, inicializa los
dispositivos USB y arranca D3D8, hasta la primera lectura de la GPU (`0xFD001804`, NV_PBUS, en `EIP 0x003D171B`).
Son 285 llamadas al kernel. El líder lo reprodujo de forma independiente. Decisiones en [[adr-05-runtime-nativo]];
observación en [[arranque-nativo]]. Próxima decisión, H7: emulación mínima del NV2A frente a HLE de D3D8.

## [2026-10-21] ingest | Ryu animado: formato MTN descifrado
`ngb_tools.kmd` exporta animaciones: `--motions banco[:ids]` sobre `--format glb`, con un rig humanoide que
resuelve la FK del tronco y la IK de 2 huesos de los miembros. `ryu_anim.glb` incluye idle, andar, correr, salto,
guardia con katana y tajo con giro, todo revisado en `anim_preview.png`. El formato está en [[mtn]]: 44 pistas,
claves de 20 bits con signo, unidades de 1e-4 y los miembros por objetivo de IK.
## [2026-10-21] ingest | Los 17 escenarios del juego en 3D
`ngb_tools.stage` exporta las 17 zonas a `.glb`. Los KMD de escenario usan un **descriptor multiparte**, con los
vértices ya en mundo y sin jerarquía; la misma variante explica que no se exportara el cuerpo de Doku. La colisión
HITMDL también está descifrada. Renders revisados: Boff (ciudad de Tairon y fortaleza), la aldea Hayabusa y la
catedral. La colocación de props está en el bytecode `.sty`, pendiente. Detalle en [[escenarios]].

## [2026-10-21] ingest | Ryu con la Dragon Sword
`--attach` monta un arma en un hueso con la tabla de anclaje del `.itm` (registros de estado, hueso, posición y
rotación). Se encontraron las 10 armas de Ryu en `cmn.afs`. La pose de la katana guardada en la espalda confirma
el orden Euler Rz·Ry·Rx. Detalle en [[armas]].
## [2026-10-21] ingest | Texturas de relieve V8U8 y L6V5U5
`ngb_tools.xpr` decodifica las 358 texturas de relieve con signo del juego, 0 fallos ([[xpr0]]). Con esto
desaparecen los avisos de "textura sin PNG" de 8 zonas de [[escenarios]]. Pendiente: que los glTF las usen como
`normalTexture` en lugar de color base.

## [2026-10-21] milestone | H5: Ghidra y firmas del XDK. Decisión de render (ADR-06)
Hay un proyecto Ghidra persistente de `default.xbe`: 548 de sus 9,118 funciones tienen nombre gracias a
XbSymbolDatabase y FID. D3D8 es identificable (97 entradas, que cubren el 91.5 % de las llamadas desde `.text`).
Pero LTCG metió unas 900 escrituras de estado inline, y `RunPushBuffer` ejecuta push buffers crudos de los assets.
Por eso se decide un **render híbrido**: PGRAPH emulado, más HLE en Swap, KickOff, fences y vblank
([[adr-06-render-hibrido]]). Además apareció la receta exacta del vblank que bloqueaba H7a (ISR `0x3D2040` → DPC
`0x3D2310`). Detalle en [[xbe-default]].
## [2026-10-21] ingest | Galería de personajes
`ngb_tools.gallery`: 49 de 62 personajes en GLB, 20 de ellos con idle animado, más una hoja de contactos. La
mayoría de los fallos son el descriptor multiparte. Detalle en [[personajes]].

## [2026-10-21] ingest | Descriptor multiparte en kmd: 59/60 personajes
`ngb_tools.kmd` ya lee el descriptor multiparte que usan los escenarios y 10 personajes, y `stage` usa ahora el
mismo parser. Exportan Doku, army, aruma, sohei, tank, truck, ufo, whip, heli y s_aru. Ryu y Ayane salen
idénticos byte a byte. Detalle en [[kmd]].
## [2026-10-21] milestone | H7a: el juego llega a la pantalla de avisos
Con un mínimo de registros del NV2A emulados, más el vblank sintético y el push buffer, `default.xbe` pasa la
inicialización de D3D8 y envía 37,337 frames con 808,728 primitivas en 30 s, todavía sin rasterizar. Las texturas
que carga son el rótulo "Warnings" y el atlas de la fuente, revisados por el líder.
El bloqueo real era un bug de H6: la superposición del TEB pisaba `ClientId.UniqueThread` (`fs:[0x24]`), y con
eso Windows perdía despertares de hilos. Detalle en [[gpu-nv2a]].

## [2026-10-21] ingest | Shaders del NV2A a GLSL
`port/src/shaders/` traduce los vertex programs (MAC+ILU de 128 bits, epílogo de screen space) y los register
combiners a GLSL 4.00. Los 16 vertex programs que hay en `default.xbe` se decodifican. Falta emular los modos
de textura `DOT_*`, que usan los 2 pixel shaders reales. Detalle en [[shaders-nv2a]].

## [2026-10-22] milestone | H7: primer frame del juego en una ventana
La pantalla de avisos de NGB se ve en una ventana del PC a 60 fps: el código del juego ejecutándose de forma
nativa, con el NV2A emulado, un rasterizador por software y el presentador Win32. Hubo que aplazar
`NtSuspendThread` mientras el hilo tiene un lock del loader, y sincronizar los eventos que D3D limpia en
memoria. Tras unos 40 s el juego pasa a una pantalla negra que espera algo. Además, `.gitattributes` fuerza
CRLF en los `.cmd`: con LF y UTF-8, cmd.exe entraba en bucle al volver de `call`. Detalle en [[gpu-nv2a]].

## [2026-10-22] ingest | H8: el mando llega al juego, pero el bloqueo es otro
HLE de las 8 funciones de entrada de XAPI, con un guion determinista (`--input-script`), mando XInput y
teclado. START y A llegan al juego (comprobado en su memoria), pero la pantalla negra que sigue a los avisos no
cambia. Hipótesis: es un vídeo de Sofdec (`movie\*.sfd`) esperando al reloj del audio, que no avanza sin APU.
Detalle en [[entrada-xapi]].

## [2026-10-22] ingest | Backend OpenGL y validación de los shaders
Con un backend OpenGL 4.6 propio (WGL, sin dependencias), los 18 shaders reales del juego traducidos por
`port/src/shaders/` compilan y enlazan en el driver. `texconv` convierte las texturas del NV2A (swizzle, P8,
DXT, con signo, cube) igual que `ngb_tools.xpr`, y `glstate` traduce el estado NV097. Aún no está enganchado al
runtime. Detalle en [[shaders-nv2a]].

## [2026-10-22] milestone | El vídeo de introducción se reproduce
La pantalla negra que seguía a los avisos no la causaban el audio ni el mando. `GetExitCodeThread` lee
SignalState y ExitStatus del ETHREAD, y el kernel emulado no los escribía al terminar un hilo, así que el
juego esperaba para siempre al hilo de las partidas guardadas. Con el arreglo, el juego reinicia D3D y
reproduce su vídeo de introducción (Sofdec) en la ventana. Lo encontró un agente de investigación en Ghidra.
Lección: ante un bloqueo, buscar primero qué espera el hilo principal antes de suponer una causa. Detalle en
[[gpu-nv2a]].

## [2026-10-22] milestone | Pantalla de título del juego
Tras los avisos y el vídeo de introducción (congelado en su primer fotograma, se salta con START), el juego
llega a la pantalla de título "PRESS START BUTTON". Antes hubo que implementar `XcSHAInit/Update/Final` y
`XcHMAC`: SHA-1 propio, que el juego usa para verificar sus datos. Quedan abiertos el audio, que probablemente
explica el vídeo congelado y los avisos repetidos, y pasar del título al menú. Detalle en [[gpu-nv2a]].

## [2026-10-22] ingest | El vídeo de introducción se reproduce entero
DirectSound lee la posición de cada voz en una tabla en RAM que en la Xbox avanza el hardware (el procesador de
voces de la APU), y Sofdec sincroniza el vídeo con ella. Un VP mínimo (`port/src/apu/vp.c`) que interpreta los
métodos PIO y avanza las voces activas con el tiempo real basta para que el vídeo avance y, al terminar, el juego
siga solo. Todavía sin sonido. El líder hizo este trabajo en paralelo al agente de render GL. Detalle en
[[audio-apu]].

## [2026-10-22] ingest | Los avisos "en bucle" son el juego a cámara lenta
Con `--watch` (vigilancia nueva de variables del juego) se ve que no hay ninguna espera bloqueada: la caché
termina en 1,5 s y los avisos se desplazan 0,3 px por frame, unos 38 s a 60 fps. Como el juego va a 42 fps o
menos con el rasterizador por software, duran de 1 a 3 minutos. Se corrige con el render por GPU (H7c). Además,
el loader pide al sistema un reloj de 1 ms (`timeBeginPeriod`). Lección: medir con `--watch` antes de suponer
una espera. Detalle en [[gpu-nv2a]].

## [2026-10-22] milestone | H7c: render por GPU a 60 fps
PGRAPH dibuja con OpenGL (RX 6600): el título y el menú pasan de 16 a 60 fps, y los avisos duran 40 s, que es su
diseño. El "parpadeo" tras los avisos era el NV2A emulado haciendo kick con el pusher parado: reproducía el push
buffer que D3D acababa de liberar. Arreglado, aparecen el vídeo, el logo de Team NINJA y el título. Detalle en
[[gpu-nv2a]].

## [2026-10-22] milestone | El juego suena
Hay mezcla real de las voces de la APU y salida por `waveOut`: el audio del vídeo de introducción suena. Faltaban
tres piezas:
- la tabla SGE, que DirectSound programa por PIO (`0x804`/`0x808`);
- los volúmenes, que también llegan por PIO (`0x360-0x368`);
- aceptar las direcciones "físicas" identidad que da nuestro `MmGetPhysicalAddress`.
Las 4 voces internas que alimenta el DSP se silencian, porque sin DSP serían ruido. Detalle en [[audio-apu]].

## [2026-10-22] milestone | Gameplay en 3D: la demo del título
La demo de gameplay del título, que el juego reproduce con su propio motor, se ve en 3D a 35-60 fps y con sonido:
Ryu pelea con la Dragon Sword. También se corrigió el aviso "Unable to load game settings": la EEPROM emulada
devolvía basura de la pila, y la firma de `system.dat` cambiaba en cada ejecución. Pendientes el cielo (azul
plano) y parte del suelo. Detalle en [[gpu-nv2a]].

## [2026-10-22] ingest | El cuelgue de la demo: fixups de push buffers de D3D
D3D marca con un `NO_OPERATION` de parámetro especial los push buffers estáticos cuyo JUMP de vuelta todavía no
puede reescribir; en la Xbox, la GPU se detiene y la rutina de interrupción lo parchea. Sin emular eso, la GPU
emulada daba vueltas por push buffers viejos (hasta 400 M palabras), pisaba el fence y colgaba el juego al
empezar la demo. Con el HLE del parcheo, la demo va a 55-60 fps sin cuelgues. Lo diagnosticó el líder con
`--watch`, trazas de kicks y Ghidra. Detalle en [[gpu-nv2a]].

## [2026-10-22] ingest | Pipeline fijo con luces y mapas de sombra
La mitad de los dibujos del 3D usan el pipeline fijo con iluminación, que el render GL no implementaba. Ahora hay
luces, color material, texgen y matrices de textura, y el escenario de la demo aparece iluminado. Los mapas de
sombra (texturas de profundidad `0x2E`) se enlazan como render-to-texture y se comparan en el shader: los 46
dibujos que se descartaban ya se ejecutan. Siguen pendientes el cielo y el suelo. Detalle en [[gpu-nv2a]].

## [2026-10-22] ingest | RenderDoc: el suelo faltaba por el culling invertido
Se integró RenderDoc (`--rdoc-frame N`) y se analizaron las capturas con su API de Python. El *pixel history* de un
píxel del suelo mostró 43 draws descartados por culling: `glstate` invertía el sentido de la cara frontal, cuando
nuestra proyección ya lo compensa. Sin la inversión aparece el escenario de la demo. Lección: con estado de GPU,
un depurador de frames responde en minutos lo que las trazas a ciegas no respondían en horas. Detalle en
[[gpu-nv2a]].

## [2026-10-22] ingest | El templo de la demo: culling invertido y skinning del pipeline fijo
- **El diagnóstico de #52 era incorrecto:**
  - su cambio en `glstate` no tenía efecto, porque `pgraph_gl` fijaba `glFrontFace` justo después;
  - la mejora que se vio venía de comparar escenas distintas.
- **Lo que lo resolvió:**
  - entrada determinista (`t=…p`);
  - capturas de xemu del usuario como referencia;
  - RenderDoc: el destino tras cada evento y los vértices de entrada.
- **Arreglo:**
  - se invierte la cara frontal, como en xemu;
  - se implementa el skinning del pipeline fijo (`SET_SKIN_MODE`), que dejaba una pieza de Ryu pegada a la
    cámara tapándolo todo.
- **Lección:** no se compara antes y después sin entrada determinista; un mismo frame en dos builds evita
  conclusiones falsas.

Detalle en [[gpu-nv2a]].

## [2026-10-22] fix | Personajes completos: la proyección del skinning está en la matriz compuesta
- **Síntoma:** el usuario vio a los personajes con partes que faltaban, como si fueran transparentes.
- **Diagnóstico:**
  - el *pixel history* mostró que ningún draw del personaje tocaba esos píxeles;
  - un log temporal de matrices mostró que el registro de proyección `0x0440` está a cero, y que con skinning
    (`SET_SKIN_MODE` 1) la matriz compuesta contiene solo proyección y viewport.
- **Arreglo:** la posición con skinning es la compuesta por la suma de las model-view ponderadas.
- **Lección:** volcar los registros reales antes de suponer qué escribe D3D en ellos.

Detalle en [[gpu-nv2a]].
