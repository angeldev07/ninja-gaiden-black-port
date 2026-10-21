---
tags: [ngb, source, precedent]
updated: 2026-10-20
status: current
---

# OpenCE (Halo CE nativo): el precedente a seguir

- Repo: https://github.com/OpenCommunityEdition/OpenCE. Licencia AGPL-3.0-or-later (antes CC0), creado el 2026-09-26.
- Instalado localmente en `%USERPROFILE%\xemu\halo-windows-release\`
  (`halo.exe`, SDL3, OpenGL 4.5). El usuario lo ha jugado online. `VERIFIED` por `halo.log`.

## Cómo se hizo (`VERIFIED`, leído en README.md y AGENTS.md del repo)

- **Es una descompilación**, no un emulador. Linaje:
  [punpckhdq/halo](https://github.com/punpckhdq/halo) (descompilación *matching*, con su
  avance publicado en decomp.dev), luego bnunu/halo-1 y luego OpenCE.
- **El objetivo de la descompilación es un build de desarrollo:** Xbox build 01.01.14.2342,
  `cachebeta.exe` (sha256 `4cc87b45…9520`).
- **Los tipos y nombres salen de los símbolos de depuración de juegos posteriores:** el PDB
  de la beta de Halo CEA, pasado por `pdb-decompiler`. **Ese es el atajo que NGB no tiene.**
- Para la descompilación *matching* original hace falta el XDK de agosto de 2001 (filtrado).
  OpenCE **prohíbe leer los headers del SDK**: escribió `port/include/xdk` desde la
  información de depuración y la documentación pública.

## Arquitectura del port (`VERIFIED`, leído en README.md y AGENTS.md del repo, 2026-10-20)

- `source/`: 466 archivos C de la descompilación, compilados casi sin cambios. Cada cambio va marcado con `port:`.
- Capa de plataforma (`port/linux/src/`): **Direct3D 8 sobre OpenGL (`d3d8_gl.c`)**, DirectSound y
  XInput sobre SDL3, archivos, hilos, memoria y sockets.
- Build: x86 de 32 bits con clang en el ABI de MSVC (`-fms-extensions -fshort-wchar -malign-double`), C89.
- **Matemática determinista:** musl incluido en el repo y sin FMA, para que todas las máquinas calculen lo mismo en red.
- **Simulación a 30 ticks como en la Xbox. El render corre a la frecuencia del monitor e
  interpola entre ticks ("Smooth Motion").** Es la técnica a usar en NGB para subir los fps sin romper la física.
- El usuario aporta su propio ISO, y el juego extrae `maps/` en el primer arranque.
- Tiene `AGENTS.md`, señal de que el desarrollo se hace con agentes de IA.

## Qué tomamos para NGB

- Modelo de capas: código del juego más capa de plataforma ([[adr-01-enfoque-port]]).
- `d3d8_gl.c` como referencia de D3D8 sobre GL. Si se reutiliza código, el proyecto pasa a AGPL.
- La regla de no tocar el XDK y la convención de marcar los cambios con `port:`.
