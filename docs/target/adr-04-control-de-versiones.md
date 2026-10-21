---
tags: [ngb, target, adr, git]
updated: 2026-10-20
status: current
---

# ADR-04: control de versiones, PRs y calidad

## Contexto

El usuario delegó al líder la gestión completa de git: ramas, commits, PRs, revisión
y merge (2026-10-20), con la exigencia de que el código sea de **alto nivel de
ingeniería**. El remoto es `github.com/angeldev07/ninja-gaiden-black-port`, **privado**.

## Decisión

- **Trunk-based:** `main` siempre está en verde. Las ramas son cortas, con nombre
  `<tipo>/<slug>`, donde el tipo es `feat`, `fix`, `docs`, `ci`, `refactor`, `test` o `chore`.
- **Conventional Commits.** El tipo va en inglés y la descripción en español:
  `feat(afs): inventario de contenedores`. Cada commit lleva al final
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- **Una PR por unidad coherente.** Cada una incluye código, tests y la actualización del
  vault que le corresponde: un cambio sin documentar no se considera terminado.
- **Merge:** squash y borrado de la rama. El título de la PR pasa a ser el commit en `main`.
- **Revisión:** antes de mergear, el revisor (`caveman:cavecrew-reviewer`) y el líder
  revisan el diff, **en una sola ronda** (ver [[equipo]] § reglas). GitHub **no permite aprobar una PR propia** (la cuenta autora es la
  misma), así que la revisión se publica como comentario (`gh pr review --comment`) con
  veredicto `APROBADO` o `BLOQUEANTE`. Solo se mergea si CI está en verde y no quedan
  bloqueantes.
- **Puertas de calidad (CI en GitHub Actions):**
  - `unittest` en Ubuntu y Windows con Python 3.13;
  - `ruff check` y `ruff format --check`;
  - `mypy`;
  - **guardia de datos del juego**: falla si se versiona alguna extensión del juego o
    un binario de más de 1 MB.
- **Hitos:** al cerrar cada hito del [[roadmap]] se crea un tag anotado
  `milestone/hN` en `main`.

## Configuración del repo en GitHub (aplicada el 2026-10-20 vía `gh api`, `VERIFIED` por la respuesta)

- Merge: **solo squash** (`allow_merge_commit=false`, `allow_rebase_merge=false`). El título del
  commit es el de la PR y el cuerpo, el de la PR. La rama se borra al mergear.
- Protección de `main`:
  - checks obligatorios: `guardia de datos del juego`, `ruff + mypy`, `tests (ubuntu-latest)` y
    `tests (windows-latest)`;
  - `strict`: la rama tiene que estar al día con `main` antes de mergear;
  - historia lineal; sin force-push ni borrado;
  - **también para los admins** (`enforce_admins`).
- Como solo entra a `main` el árbol final de cada PR (squash), basta con que la guardia revise el
  árbol y no la historia: los commits intermedios de una PR nunca llegan a `main`.

## Gotchas

- **Red:** el sandbox de la sesión bloquea `github.com` (git por https). `api.github.com`
  responde de forma intermitente. Los comandos `git push/fetch` y `gh` se ejecutan fuera
  del sandbox, con el helper de credenciales de `gh`
  (`git -c credential.helper= -c "credential.helper=!gh auth git-credential" push`).
- Fin de línea: `.gitattributes` normaliza a LF (`* text=auto eol=lf`).
