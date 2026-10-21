---
tags: [ngb, goal]
updated: 2026-10-20
status: current
---

# GOAL: port nativo de Ninja Gaiden Black a PC

> **Una sesión nueva lee esta página primero.** Después lee [[index]] y luego
> [[log]] (las últimas entradas: `grep "^## \[" docs/log.md | tail -5`).

## Qué

Correr Ninja Gaiden Black (Xbox original, versión PAL "Europe (En,Fr,De,Es,It)")
**de forma nativa en Windows**, sin emulador. El código del juego se ejecuta
sobre una capa propia que reemplaza al hardware y al SDK de Xbox. El modelo es
[[opence]] (Halo CE nativo).

## Por qué

Es un proyecto **pedagógico y personal**. Lo que importa es aprender y documentar
el proceso de ingeniería: ingeniería inversa, cargadores, kernels, gráficos y
descompilación. No tiene fines comerciales y no se distribuye nada del juego.
El juego es el favorito del usuario.

**Criterio de avance:** cada hito deja algo **tangible**: un reporte, una imagen
o un sonido extraídos, o un binario que arranca. Se avanza poco a poco, con ayuda
de la IA, así que el plan se cuenta en hitos, no en meses.

## Dónde está todo

| Qué | Dónde |
| --- | --- |
| Repo (código, herramientas, vault) | `<repo>` |
| Vault (este cerebro) | `docs/`. Se abre como vault en Obsidian. |
| Datos del juego extraídos (inmutables) | `%USERPROFILE%\ngb-data\game\`. Fuera del repo y de OneDrive: [[adr-03-datos-fuera-del-repo]] |
| Contenido extraído de contenedores | `%USERPROFILE%\ngb-data\extracted\` |
| Imagen XISO del juego | `%USERPROFILE%\xemu\Juegos\Ninja Gaiden Black (Europe) (En,Fr,De,Es,It).xiso.iso` |
| Emulador de referencia (oráculo) | xemu 0.8.136 en `%USERPROFILE%\xemu\` |
| extract-xiso | `...\xemu\tools\extract-xiso\artifacts\extract-xiso.exe` ([[herramientas]]) |
| Reportes generados | `build/reports/` (ignorado por git; se regenera con las herramientas) |

## Reglas (versión corta)

- No va al repo ningún byte del juego. Prohibido usar el XDK filtrado. Ver [[CLAUDE]] § Reglas legales.
- Toda afirmación lleva un ancla y una etiqueta `VERIFIED` / `INFERRED` / `UNVERIFIED`.
- El líder es el único que escribe en el vault. Los subagentes reportan en caveman ultra. Ver [[equipo]].
- Las herramientas que leen datos del juego se ejecutan con `python -I` (aislado).
- Código: identificadores en inglés y comentarios en español.

## Decisiones vigentes

- [[adr-01-enfoque-port]]: rehosting del XBE más descompilación incremental. El estado final es como OpenCE.
- [[adr-02-herramientas-re]]: proyecto persistente en Ghidra con ghidra-xbe, herramientas Python propias, REA como auxiliar en WSL y xemu como oráculo.
- [[adr-03-datos-fuera-del-repo]]: los datos del juego viven en `NGB_DATA`.
- [[adr-04-control-de-versiones]]: el líder gestiona git y las PRs. Repo privado `angeldev07/ninja-gaiden-black-port`.

## Estado y siguiente paso

Ver [[roadmap]]: hitos, criterio de terminado y estado de cada uno.
