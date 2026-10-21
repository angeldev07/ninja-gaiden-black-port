---
tags: [ngb, target, adr]
updated: 2026-10-20
status: current
---

# ADR-01: enfoque del port (rehosting más descompilación incremental)

## Contexto

- La CPU de la Xbox es un Pentium III a 733 MHz (x86 de 32 bits; especificación pública
  de la consola, https://xboxdevwiki.net/Xbox_Hardware). El código de `default.xbe` puede
  ejecutarse **tal cual** en un proceso Win32 de 32 bits, porque no hace falta traducir
  instrucciones, a diferencia de la 360 (PowerPC) o la N64 (MIPS).
- El SDK de Xbox (D3D8, DirectSound, XAPI) va **enlazado de forma estática** dentro del
  XBE: hay secciones `D3D`, `DSOUND`, `XGRPH` y otras, y 9 librerías del XDK 5849 en su
  cabecera. El juego llama al kernel `xboxkrnl` por ordinales, con 145 imports en el thunk.
  `VERIFIED`: [[xbe-default]].
- Team Ninja ya no tiene el código fuente completo de NGB ([[decomp-ninja]] § contexto).
- Precedente directo: [[opence]] (Halo CE) partió de una descompilación con símbolos de
  depuración de un build de desarrollo. **NGB no tiene ese atajo** (`UNVERIFIED` si
  existe algún prototipo con símbolos).

## Decisión

Avanzar en tres capas que pueden crecer en paralelo:

1. **Rehosting.** Un cargador Win32 de 32 bits coloca las secciones del XBE en sus
   direcciones virtuales (base `0x10000`, a confirmar con [[xbe-default]]), resuelve los
   imports del kernel con un **kernel HLE propio** y salta al entry point.
2. **Capa de plataforma.** Las funciones del SDK, identificadas por firmas, se
   reemplazan por implementaciones de PC: D3D8 a OpenGL/Vulkan, DirectSound y XInput
   sobre SDL3, archivos, hilos y memoria. OpenCE tiene una capa equivalente
   (`d3d8_gl.c`) que sirve de referencia.
3. **Descompilación incremental.** Las funciones del juego se pasan a C una a una (al
   estilo re3), cada una validada con **tests diferenciales** contra la original. Al
   final queda código fuente compilable, como en OpenCE.

## Alternativas descartadas

- **Descompilación "matching" desde el día 1** (estilo SM64): exige el mismo
  compilador. La cadena `MSC1310 XTL5849` de `default.xbe` indica MSVC 13.10 (Visual
  Studio .NET 2003) con el XDK 5849 (`VERIFIED`, [[xbe-default]]). Ese toolchain viene con
  el XDK, que no podemos usar ([[CLAUDE]] § legal), y el camino supone años sin nada tangible.
- **Emulación completa** (como xemu): ya existe y no enseña a portar. Se usa como oráculo.
- **Recompilación estática** (estilo XenonRecomp): no aporta nada si la CPU de origen ya
  es x86. El problema está en el hardware y el SDK, no en el ISA.

## Consecuencias

- El primer resultado tangible ejecutable es el arranque nativo hasta la primera
  llamada al kernel sin implementar ([[roadmap]] H6), mucho antes de tener C.
- Hay que construir la identificación de librerías del SDK (firmas) sin usar el XDK.
- Proceso de 32 bits: hay que reservar el rango bajo de direcciones al arrancar.
  `INFERRED`: es el mismo problema que resuelve Cxbx-Reloaded.
- Licencia: si reutilizamos código de OpenCE, el proyecto pasa a AGPL-3.0, lo cual es
  compatible con el carácter no comercial.
