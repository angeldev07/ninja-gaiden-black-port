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
