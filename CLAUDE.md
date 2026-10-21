# Ninja Gaiden Black: port nativo a PC (pedagógico)

**Antes de hacer nada, lee `docs/GOAL-ngb-port.md`.** Después lee `docs/index.md` y las
últimas entradas de `docs/log.md` (`grep "^## \[" docs/log.md | tail -5`). `docs/` es el
cerebro de contexto (vault de Obsidian, patrón LLM Wiki). Su esquema está en `docs/CLAUDE.md`.

## Reglas del repo

- **Nada del juego en el repo.** Los datos viven en `%USERPROFILE%\ngb-data\` (`game/` inmutable
  y `extracted/`). `.gitignore` bloquea las extensiones del juego. Ver `docs/target/adr-03-datos-fuera-del-repo.md`.
- **Prohibido usar el XDK filtrado de Microsoft** o algo derivado de él.
- Las herramientas que leen datos del juego se ejecutan con `python -I`.
- Python 3.13, solo stdlib. Tests con `unittest`: `python -m unittest discover -s tests -v`.
- Código: identificadores en inglés y comentarios en español con tildes.
- Toda sesión que aprenda algo actualiza el vault: las páginas afectadas, `docs/index.md` si
  hay página nueva y una entrada en `docs/log.md`.
- Equipo de subagentes: el líder es el único que escribe en `docs/`, y los subagentes reportan
  en caveman ultra. Ver `docs/team/equipo.md`.
- Control de versiones: el líder gestiona las ramas, los commits, las PRs, las revisiones y los
  merges (autorizado por el usuario el 2026-10-20). Flujo en `docs/target/adr-04-control-de-versiones.md`.
  Los subagentes nunca hacen commits.
