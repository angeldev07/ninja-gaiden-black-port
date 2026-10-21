---
tags: [ngb, index]
updated: 2026-10-20
status: current
---

# NGB Port Wiki: índice

Catálogo de todas las páginas. Se empieza por [[GOAL-ngb-port]]. Las convenciones están
en [[CLAUDE]] y la línea de tiempo en [[log]].

## Raíz

- [[GOAL-ngb-port]]: qué, por qué, dónde está todo, reglas y decisiones vigentes. **Punto de entrada.**
- [[CLAUDE]]: esquema del vault, reglas de RE (anclas y etiquetas de confianza) y reglas legales.
- [[log]]: registro cronológico, solo se agrega.
- [[wiki-llm]]: el patrón LLM Wiki que seguimos.

## Observación: ingeniería inversa (`reverse-engineering/`)

- [[disco-ngb]]: imagen Redump y XISO, 249 archivos por tipo, grupos de AFS, ejecutables y videos.
- [[arranque-nativo]]: qué hace `default.xbe` al arrancar de forma nativa (H6): hilos, XAPI, unidades, caché en el disco duro, init de D3D hasta la primera MMIO del NV2A.
- [[xbe-default]]: anatomía de `default.xbe`. Motor "Kunai", MSVC 13.10 con el XDK 5849, D3D8LTCG, 16 secciones, 145 imports del kernel, middleware de CRI y los otros 4 XBE del disco.

## Observación: formatos (`formats/`)

- [[afs]]: contenedor AFS de CRI. Estructura, peculiaridad del campo custom, inventario de las 3,766 entradas, magias y entropía.
- [[chunk-teamninja]]: cabecera común de los formatos del motor Kunai (KMD, `chr_dat`, `stry_dat`…).
- [[kmd]]: modelos y escenas. Slots fijos (OBJINFO, XPR0, HIERARC…); `ryu.kmd` tiene 59 objetos y 19 texturas.
- [[xpr0]]: Xbox Packed Resource (texturas, paletas, VB e IB). Campo `Format` y swizzle.
- [[mtn]]: animaciones `.mtn`/`.mot`: 44 pistas (raíz, tronco e IK de 4 miembros), claves de 20 bits, 258 motions de Ryu.
- [[escenarios]]: KMD de zona (descriptor multiparte, vértices en mundo), colisión HITMDL y las 17 zonas exportadas a glb.
- [[armas]]: las 10 armas de Ryu (KMD + `.itm`) y la tabla de anclaje a los huesos (estado, hueso, posición y rotación).
- [[adx]]: audio de CRI (95 pistas de música y 317 voces).
- [[wva]]: banco de efectos de sonido en Xbox ADPCM.

## Observación: hallazgos (`findings/`)

_(todavía vacío)_

## Propuesta: diseño y decisiones (`target/`)

- [[roadmap]]: hitos tangibles H0 a H9+, con criterio de terminado y estado.
- [[adr-01-enfoque-port]]: rehosting más capa de plataforma más descompilación incremental.
- [[adr-02-herramientas-re]]: Ghidra con ghidra-xbe, herramientas propias, REA auxiliar, xemu como oráculo.
- [[adr-03-datos-fuera-del-repo]]: los datos del juego en `NGB_DATA`, fuera del repo y de OneDrive.
- [[adr-05-runtime-nativo]]: cargador con base fija, cabecera PE sombra, KPCR superpuesta en el TEB, TLS, archivos y MMIO. Opciones para H7.
- [[adr-04-control-de-versiones]]: trunk-based, Conventional Commits, PRs con squash, revisión, puertas de CI y tags por hito.

## Equipo (`team/`)

- [[equipo]]: roles, reglas de coordinación, depuración e historial de despliegues.
- [[plantilla-tarea-agente]]: brief estándar para subagentes.

## Herramientas (`tools/`)

- [[herramientas]]: extract-xiso y las herramientas propias (`xbe_info.py`, `afs.py`).

## Fuentes externas (`sources/`)

- [[opence]]: Halo CE nativo. Descompilación con símbolos y D3D8 sobre GL. Es el precedente.
- [[rea]]: MCP de RE. Evaluación y límites (no acepta XBE; en Windows es de solo lectura).
- [[decomp-ninja]]: intento previo abandonado y contexto oficial (código fuente perdido).
