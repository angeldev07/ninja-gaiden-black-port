---
tags: [ngb, schema]
updated: 2026-10-20
status: current
---

# NGB Port Wiki: esquema y convenciones

Base de conocimiento **mantenida por el LLM** para el port nativo de Ninja Gaiden
Black (Xbox, PAL) a PC. Sigue el patrón de [[wiki-llm]], el mismo estándar del
vault de Rithm. Obsidian es el lector. El LLM escribe y mantiene todas las
páginas. El humano dirige, pregunta y cura.

**Es el cerebro de contexto del proyecto.** Una sesión nueva empieza leyendo
[[GOAL-ngb-port]]. Después lee [[index]] y luego las páginas que necesite. Es más
barato que volver a analizar binarios o rehacer búsquedas.

## Capas

- **Fuentes crudas (inmutables, fuera del repo):** los datos del juego, extraídos de
  la copia del usuario en `NGB_DATA` (por defecto `%USERPROFILE%\ngb-data\`):
  - `game/`: árbol del disco (XISO extraído). Nunca se modifica.
  - `extracted/`: contenido sacado de los `.afs` y otros contenedores.
  Las fuentes externas (repos, wikis, docs públicas) se resumen en `sources/`.
- **El wiki:** este directorio (`docs/`). Markdown que pertenece al LLM.
- **El esquema:** este archivo.
- **Las herramientas:** `tools/` en la raíz del repo. Producen reportes en `build/reports/`
  (ignorado por git). El wiki cita esos reportes y los resume. No los copia enteros.

## Estructura de directorios

```
docs/
  CLAUDE.md             ← este archivo (esquema)
  GOAL-ngb-port.md      ← punto de entrada de toda sesión nueva
  index.md              ← catálogo de todas las páginas
  log.md                ← registro cronológico, solo se agrega
  wiki-llm.md           ← el patrón que seguimos
  reverse-engineering/  ← OBSERVACIÓN: qué hay en el disco y en los binarios
  formats/              ← OBSERVACIÓN: una página por formato de archivo
  findings/             ← OBSERVACIÓN: hallazgos puntuales F1…Fn (dato → evidencia → impacto)
  target/               ← PROPUESTA: ADRs, roadmap, arquitectura destino
  team/                 ← cómo trabaja el equipo de agentes
  tools/                ← cómo usar nuestras herramientas
  sources/              ← resúmenes de fuentes externas (proyectos, docs)
```

Obsidian resuelve `[[enlaces]]` por nombre de archivo en todo el vault. Por eso
**los nombres de página son únicos** en todo `docs/`.

## Reglas de ingeniería inversa (heredadas del estándar de auditoría)

- **Toda afirmación lleva un ancla.** Ejemplos:
  - Binario: `default.xbe @ 0x0001A2B0 (FUN_0001a2b0)`, con dirección virtual y nombre si existe.
  - Archivo: `cmn.afs#12`, con archivo y número de entrada.
  - Herramienta: `tools/afs.py → parse_toc`.
  Si una afirmación no tiene ancla es una hipótesis y se marca como tal.
- **Etiquetas de confianza** en todo lo que no se leyó directamente:
  - `VERIFIED`: leído en los bytes, en la salida de una herramienta o en la doc oficial.
  - `INFERRED`: deducido sin ejecutar ni confirmar.
  - `UNVERIFIED`: requiere comprobarlo, por ejemplo en xemu o en un depurador.
- **Observación separada de propuesta.** `reverse-engineering/`, `formats/` y
  `findings/` describen solo lo que existe. `target/` contiene diseño y decisiones,
  y nunca se presenta como estado actual.
- **Cada número dice de dónde salió:** el comando, el reporte o el offset.

## Reglas legales (no negociables)

- Nunca va al repo ningún byte del juego: binarios, assets, extractos ni dumps
  grandes de memoria. Lo impiden dos barreras: `.gitignore` (por extensión) y la
  guardia `ngb_tools.hygiene` (por extensión, firma binaria y tamaño), que corre en
  el hook pre-commit y en CI ([[adr-04-control-de-versiones]]).
- Prohibido usar el XDK filtrado de Microsoft ni nada derivado de él (headers,
  librerías). OpenCE reescribió las declaraciones del SDK sin el XDK, partiendo de la
  información de depuración de builds de Halo y de documentación pública ([[opence]]).
  NGB no tiene información de depuración, así que aquí las declaraciones salen solo
  de la documentación pública y del análisis propio.
- El proyecto es pedagógico, personal y sin fines comerciales. Cada persona usa su
  propia copia del juego.

## Convenciones de página

- **Frontmatter** YAML en todas las páginas. Hay dos excepciones: [[log]], que es un
  registro append-only igual que en el vault de Rithm, y [[wiki-llm]], que es una copia
  literal de una fuente externa:
  ```yaml
  ---
  tags: [ngb, <área>]
  updated: YYYY-MM-DD
  status: current | draft | superseded
  ---
  ```
- **Wikilinks** `[[nombre-de-pagina]]`, enlazando sin miedo. Un enlace a una página
  que aún no existe sirve como marcador de TODO.
- **Densidad y hechos.** La forma preferida es "Qué es → Cómo funciona → Evidencia →
  Gotchas → Siguiente paso".
- **Idioma:** español con tildes. Los términos técnicos, identificadores y comandos
  van tal cual.
- **Contradicciones:** si un hecho nuevo invalida otro, se corrige la página vieja y
  se anota en [[log]].

## Flujos

**Ingest** (se aprende algo): actualizar las páginas afectadas y sus enlaces,
añadir a [[index]] una línea por cada página nueva y registrar la entrada en [[log]].

**Query** (una pregunta): leer [[index]], bajar a las páginas y responder citando.
Si la respuesta es una síntesis que vale la pena conservar, guardarla como página.

**Lint** (chequeo de salud): buscar contradicciones, afirmaciones viejas, páginas
huérfanas, conceptos sin página propia y anclas rotas.

## Formato del log

Solo se agrega al final, una línea de encabezado por entrada, para que
`grep "^## \[" log.md` funcione:

```
## [YYYY-MM-DD] <ingest|query|lint|decision|milestone> | <título corto>
```

## Equipo de agentes

El líder (la sesión principal) es el **único escritor del vault**. Los subagentes
entregan reportes en formato caveman ultra y el líder los ingiere al vault en prosa
normal. Los roles, las plantillas y las reglas están en [[equipo]].
