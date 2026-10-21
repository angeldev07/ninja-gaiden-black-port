---
tags: [ngb, source, tooling]
updated: 2026-10-20
status: current
---

# REA (Reverse Engineer Anything): evaluación

- Repo: https://github.com/morluto/rea. Licencia MIT. Unas 348k estrellas según la página del repo el
  2026-10-20. Paquete npm `rea-agents` v6.7.0 (2026-10-18, según `npm view`). Saca versiones casi a diario
  (CHANGELOG).
- Es un MCP más una CLI que da a los agentes acceso a Ghidra, IDA o Hopper en modo
  headless: pseudocódigo, ensamblador, strings, xrefs, llamadas entrantes y salientes y
  dossiers de funciones, todo con evidencia. Instala skills de flujo de trabajo en el agente.

## Encaje con NGB (`VERIFIED` en docs/windows-ghidra-p0.md, raw-binary.md y native-investigation.md)

- **No acepta XBE.** Solo admite PE, ELF, Mach-O o binarios sin cabecera (`raw-binary`).
- **En Windows es experimental, de solo lectura y solo admite PE.** Los PE x86 de 32 bits
  entraron en la v6.0.0. Los `raw-binary` x86 no se admiten en Windows.
- **Renombrar y comentar** (`annotate_native_function`, `set_address_name`) **solo funciona en Linux o macOS**,
  y sobre una base de datos **efímera**. No sirve como proyecto persistente.
- Requisitos: Node ≥ 22.19 (aquí hay 22.15.1), Ghidra 12.1.x (no instalado) y JDK 21 (instalado).
- Hay WSL con Ubuntu 24.04 disponible, que es la vía para usarlo sin las limitaciones de Windows.
- Showcase relevante: **DX-Ball**, una función x86 reconstruida en C que pasa 3,205
  casos y reproduce los 63 bytes compilados. Es el modelo de los tests diferenciales.

## Postura

Sirve como **auxiliar para investigaciones puntuales**, no como base del proyecto
([[adr-02-herramientas-re]]). Si algún día se usa en Windows, un conversor propio
`xbe2pe` lo habilitaría, y escribirlo también tiene valor pedagógico.

## Legal y seguridad

- El disclaimer permite educación, interoperabilidad, preservación y analizar software
  propio. No ayuda a "reproducing its protected code and assets".
- El README advierte de clones. Instalar solo desde npm `rea-agents`, con la versión fija,
  y revisar el diff que propone `setup`.
