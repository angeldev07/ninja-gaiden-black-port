---
tags: [ngb, target, adr, legal]
updated: 2026-10-20
status: current
---

# ADR-03: los datos del juego viven fuera del repo (NGB_DATA)

## Decisión

Los datos del juego se guardan en `NGB_DATA`, por defecto `%USERPROFILE%\ngb-data\`:
- `game/`: árbol del disco (salida de `extract-xiso -x`). Nunca se modifica.
- `extracted/<contenedor>/`: contenido sacado de `.afs` y otros contenedores.

Las herramientas reciben las rutas por argumento. Nada del juego entra al repo, gracias a
dos barreras:
- `.gitignore`, por extensión: `*.iso`, `*.xiso`, `*.xbe`, `*.afs`, `*.sfd`, `*.xpr`, `*.xwb`,
  `*.xsb`, `*.aix`, `*.adx`, `*.bin`, `*.def`, `*.ins`, `*.ttf`, más `game/`, `extracted/` y `build/`.
  Una fuente con licencia abierta que se quiera versionar necesita un `git add -f` explícito.
- `ngb_tools.hygiene`, que corre en el hook pre-commit y en CI. **Lee siempre el blob del índice de
  git**, nunca el disco. Ignora `refs/replace` y no hace descargas en clones parciales. Aplica cuatro
  reglas:
  - **Extensión**, comparada sin mayúsculas y con plegado Unicode. Cubre los formatos del disco, los
    internos de los AFS (`.kmd`, `.spr`, `.sty`, `.wva`, …) y los comprimidos (`.zip`, `.7z`, `.rar`,
    `.gz`, …).
  - **Firma binaria** (heurística): `XBEH`, `AFS\0`, `XPR0`, `KMD\0`, ADX, `WBND`, `SDBK`, `AIXF`, MPEG-PS,
    la cabecera de chunk de Team Ninja, XDVDFS en el sector 32 y las firmas de los comprimidos.
  - **Tamaño:** más de 1 MiB.
  - **Ruta:** `game/`, `extracted/` y `build/` en la raíz, aunque se añadan con `add -f`.

  Ver [[adr-04-control-de-versiones]].

### Excepciones

- **`.hygiene-allow`**: archivo en la raíz con una ruta exacta por línea. Una ruta permitida solo se salta
  la regla de **extensión**, nunca la de firma, tamaño o ruta. En el hook se lee la versión de `HEAD`,
  así que la excepción hay que commitearla antes, por separado y a la vista en la PR. Tope de 64 KiB.
- **`.ttf`** no se bloquea por extensión, porque una fuente libre puede ser legítima. Las fuentes del juego
  (23 MB) las detecta la regla de tamaño. Como `.gitignore` sí incluye `*.ttf`, una fuente libre necesita
  un `add -f` deliberado.
- **Límite conocido:** la guardia revisa el árbol, no la historia. Basta porque `main` solo admite squash
  merges ([[adr-04-control-de-versiones]]).

## Por qué

- **Legal:** el repo puede hacerse público algún día y solo debe contener trabajo propio
  ([[CLAUDE]] § legal).
- **OneDrive:** el repo está en `OneDrive\Escritorio`, y meter ahí 6.3 GB de archivos
  del juego los sincronizaría a la nube. `%USERPROFILE%\` no está sincronizado.
- **Inmutabilidad:** si las fuentes crudas están separadas, ningún script puede
  modificarlas por accidente.
