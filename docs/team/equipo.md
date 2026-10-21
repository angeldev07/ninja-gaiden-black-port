---
tags: [ngb, team]
updated: 2026-10-20
status: current
---

# Equipo: roles, coordinación y reglas

La sesión principal es el **líder**. Los subagentes son el equipo. Lo que se
optimiza es el avance tangible por token gastado.

## Roles

| Rol | Tipo de agente | Para qué | Entrega |
| --- | --- | --- | --- |
| **Líder** | sesión principal | Arquitectura, decisiones (ADRs), repartir trabajo, integrar, verificar y escribir el vault | Páginas del vault, ADRs, roadmap |
| **Analista RE** | `general-purpose` | Construir una herramienta o analizar un formato o subsistema | Código con tests y un reporte en `build/reports/` |
| **Investigador** | `caveman:cavecrew-investigator` o `Explore` | Búsquedas de solo lectura (dónde está X, quién llama a Y) | Tabla `ruta:línea` o `dirección` |
| **Constructor** | `caveman:cavecrew-builder` | Ediciones acotadas a 1 o 2 archivos existentes | Diff resumido |
| **Revisor** | `caveman:cavecrew-reviewer` | Revisar cambios antes de dar algo por cerrado | Un hallazgo por línea, con severidad |

## Reglas de coordinación

1. **El líder es el único escritor del vault.** Los agentes no tocan `docs/`, y el líder
   ingiere sus reportes. Así no hay conflictos y el estilo es uno solo.
2. **Cada archivo tiene un solo dueño por tarea.** Dos agentes en paralelo nunca editan
   el mismo archivo. El brief lo dice explícitamente.
3. **Solo se paraleliza lo independiente.** Lo que depende de otro resultado espera.
4. **Briefs autocontenidos** según [[plantilla-tarea-agente]]: contexto, tarea, rutas,
   restricciones, tests y formato de entrega.
5. **Evidencia antes de afirmar.** Un reporte sin ancla (offset, archivo#entrada o
   comando) es una hipótesis. El líder verifica una muestra de cada reporte antes de
   marcarlo `VERIFIED`.
6. **Revisión al cierre de cada hito:** el revisor pasa por el código nuevo.
7. **Caveman ultra solo en la respuesta del agente al líder**, para ahorrar tokens. Todo
   lo que se guarda en archivos (código, comentarios, reportes, vault) va en prosa
   normal.
8. **Los agentes no hacen commits.** El líder gestiona ramas, commits, PRs, revisiones y
   merges ([[adr-04-control-de-versiones]]).
9. **Datos del juego:** se leen con `python -I` y nunca se escriben dentro del repo
   ([[adr-03-datos-fuera-del-repo]]).

## Depuración y verificación

- Skills: `superpowers:systematic-debugging` ante cualquier fallo,
  `superpowers:test-driven-development` para herramientas y código nuevo, y
  `superpowers:verification-before-completion` antes de declarar algo terminado.
- **Tests diferenciales** (desde H9): una función descompilada se compara contra la
  original con miles de entradas. Precedente: el showcase DX-Ball de [[rea]].
- **xemu como oráculo:** su stub de GDB permite poner breakpoints en el juego real para
  confirmar hipótesis `UNVERIFIED`.

## Historial de despliegues

| Fecha | Hito | Agentes | Resultado |
| --- | --- | --- | --- |
| 2026-10-20 | H1 | Analista XBE (`tools/xbe_info.py`) y analista AFS (`tools/afs.py`) en paralelo | ver [[log]] |
