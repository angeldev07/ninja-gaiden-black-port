---
tags: [ngb, target, adr, tooling]
updated: 2026-10-20
status: current
---

# ADR-02: herramientas de ingeniería inversa

## Decisión

| Necesidad | Herramienta | Por qué |
| --- | --- | --- |
| Base de análisis persistente (miles de nombres y tipos) | **Ghidra con la extensión ghidra-xbe** | Carga el XBE de forma nativa (cabecera, thunk del kernel, secciones) y el proyecto se guarda entre sesiones. |
| Que Claude opere sobre ese proyecto | **MCP conectado a Ghidra abierto** (GhidraMCP u otro). `UNVERIFIED`: evaluar. | Renombrar y tipar en el proyecto real. |
| Investigaciones puntuales asistidas | **REA** en WSL Ubuntu 24.04 ([[rea]]) | Buen flujo de evidencia. En Windows es de solo lectura y no acepta XBE. |
| Formatos y reportes reproducibles | **Herramientas Python propias** en `tools/` ([[herramientas]]) | Solo stdlib, con tests y pedagógicas. Son la fuente de los números del wiki. |
| Oráculo dinámico | **xemu**, basado en QEMU, cuyo stub de GDB (`-s -S`) es `UNVERIFIED` en esta instalación | Breakpoints y lectura de memoria del juego real para validar hipótesis. |
| Imagen de disco | **extract-xiso** v2.7.1 | Redump a XISO y extracción de archivos. |
| Depuración y método | Skills `systematic-debugging`, `test-driven-development` y `verification-before-completion` | Disciplina: evidencia antes de afirmar. |

## Alternativas descartadas

- **Solo REA:** no carga XBE, en Windows es de solo lectura y usa una base de datos
  efímera ([[rea]]). No sirve como proyecto de meses.
- **IDA:** tiene licencia comercial y no aporta ventaja clara sobre Ghidra con ghidra-xbe
  para x86 de 32 bits.

## Pendiente (requiere instalar, se confirma con el usuario)

- Ghidra 12.1.x y ghidra-xbe (JDK 21 ya está instalado).
- Node ≥ 22.19 para REA (hay 22.15.1).
- El MCP para Ghidra.
