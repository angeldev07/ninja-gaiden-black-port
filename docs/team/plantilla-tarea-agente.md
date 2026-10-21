---
tags: [ngb, team, template]
updated: 2026-10-20
status: current
---

# Plantilla de brief para un subagente

Se copia y se rellena. Un buen brief permite que el agente trabaje sin preguntar.

```markdown
Rol: <analista RE | investigador | constructor | revisor> del equipo "port nativo Ninja Gaiden Black (Xbox) a PC" (pedagógico, sin fines comerciales).

## Contexto
- Repo: <repo> (git; NO hagas commits).
- Datos del juego (fuera del repo, inmutables): %USERPROFILE%\ngb-data\game\ ; extractos en %USERPROFILE%\ngb-data\extracted\
- Entorno: Windows 11, Python 3.13 (solo stdlib, unittest), Bash o PowerShell.
- Lo que ya se sabe: <enlaces o resumen de las páginas del vault relevantes>
- Archivos de otros agentes en paralelo (NO tocar): <lista>. NO escribas en docs/.

## Tarea
<qué construir o analizar; detalles de formato conocidos; qué verificar contra la doc pública>

## Restricciones
- Prohibido usar o leer el XDK de Microsoft o algo derivado.
- Ejecutar sobre datos del juego con `python -I`.
- Identificadores en inglés; comentarios en español con tildes.

## Tests
<qué probar, con datos sintéticos; comando para ejecutar>

## Entrega (respuesta final)
Caveman ultra: fragmentos, sin artículos ni relleno, flechas para causalidad, números y rutas exactas.
- archivos creados o modificados
- resultado de los tests
- <datos específicos pedidos>
- dudas, discrepancias y URLs consultadas
```
