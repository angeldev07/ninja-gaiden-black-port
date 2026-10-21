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
