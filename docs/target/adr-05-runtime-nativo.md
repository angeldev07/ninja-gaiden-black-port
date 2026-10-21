---
tags: [ngb, target, adr, runtime]
updated: 2026-10-21
status: current
---

# ADR-05: runtime nativo (cargador y kernel HLE)

## Contexto

[[adr-01-enfoque-port]] eligió *rehosting*: ejecutar el código x86 del XBE tal cual en un proceso Win32 de 32 bits.
H6 lo hizo realidad ([[arranque-nativo]]). Este ADR recoge cómo, y las decisiones no obvias. El código está en
`port/` y su README tiene el detalle.

## Decisiones

| Problema | Decisión | Por qué |
| --- | --- | --- |
| El XBE va en `0x10000`, pero esa zona se llena antes de `main` | El exe `ngb-loader.exe` se enlaza en `0x10000` con `/FIXED` y una BSS enorme (`.xbox`), para que Windows reserve el rango al crear el proceso. La lógica vive en `ngb-core.dll` | Es la técnica de `cxbxr-ldr` de Cxbx-Reloaded (`INFERRED`: de memoria, sin contrastar con la fuente). Reservar con `VirtualAlloc` al empezar no basta (lo probó el analista) |
| Al pisar la cabecera PE con la del XBE, `CreateThread` falla (error 193) | **Cabecera PE "sombra"**: las NT headers se copian a `0x01000000`, se escribe `MZ` en `0x10000` y `e_lfanew` en `0x1003C` | Esos campos caen en el magic y en la firma RSA del XBE, que el juego no lee en ejecución (`VERIFIED`: el juego arranca) |
| Memoria contigua y direcciones físicas para la GPU | Arena de 64 MB en `0x80000000` (`/LARGEADDRESSAWARE`), con `MmGetPhysicalAddress = va − 0x80000000` | Reproduce el mapeo de la Xbox, con su RAM de 64 MB |
| El código Xbox lee la KPCR por `fs:`, pero en Windows `fs:` es el TEB | **Superposición en el TEB**: `fs:[0x1C]` → KPCR falsa, `[0x20]` → Prcb, `[0x24]` → `tid<<8` (IRQL 0), `[0x28]` → ETHREAD falso. `fs:[0..8]` (SEH y límites de pila) significan lo mismo en los dos sistemas | WOW64 no tiene LDT, así que no hay otro selector posible. Coste: `GetCurrentThreadId` devuelve basura en los hilos Xbox |
| TLS de XAPI (`_tls_index` negativo bajo StackBase) | Cada hilo Xbox corre sobre su propia pila (≥ 1 MB) con el bloque TLS en la cima, y el TEB apunta a ella. La cadena SEH se reinicia con el registro final de ntdll | Compatible con SEHOP |
| Hilos, IRQL, DPC | Un hilo Xbox es un hilo Win32 con afinidad a 1 CPU. IRQL ≥ DPC toma un lock global recursivo. DPC y timers corren en un hilo interno, y no hay interrupciones | Lo mínimo para arrancar. Es deuda para H7+ |
| Sistema de archivos | `D:` es la carpeta del juego en solo lectura. `PartitionN\` se mapea a `NGB_DATA\runs\hdd\partitionN\`, y el volumen en crudo a `partitionN.raw`. Se simula una geometría FATX (clúster de 16 KB) con la tabla de particiones de un disco retail de 8 GB | Sin geometría FATX, XAPI pide formatear y llama a `HalReturnToFirmware(2)` |
| Stubs del kernel | Se **generan** desde `src/ngb_tools/data/xboxkrnl_exports.tsv` con `tools/gen_kernel_stubs.py`, respetando la convención (stdcall/fastcall/cdecl variádica) y el número de argumentos | Una sola fuente de verdad. `--check` en los tests detecta si `stubs.c` está desactualizado |
| MMIO | El NV2A **detiene** la ejecución: es el final de H6. USB, APU, AC97, NIC y flash se tratan como "RAM trazada": la página NOACCESS dispara el log, se ejecuta paso a paso y se vuelve a cerrar | Avanzar todo lo posible sin modelar hardware que todavía no hace falta |

## Riesgos

- **Confinamiento de rutas** (corregido en la revisión de la PR #14): `port/src/kernel/hostpath.c` rechaza `..`, `.`, `:`, `/`, comodines y el punto o espacio final, y exige que tras `GetFullPathNameA` la ruta quede bajo su raíz. Lo prueban las 22 comprobaciones de `hostpath_test.exe`. `NtSetInformationFile` solo admite una lista blanca de clases, sin borrado ni renombrado.

- La superposición en el TEB y la cabecera sombra dependen de detalles internos de Windows: una actualización
  podría romperlas.
- La imagen es RWX y el proceso corre sin DEP. Es aceptable porque solo ejecuta la copia del juego del propio
  usuario.

## Próxima decisión: H7, cómo llegar al primer frame

- **A. Emular un mínimo de registros del NV2A** (PMC, PBUS, PFB, PGRAPH y el FIFO de push buffers), al estilo
  xemu. Es fiel al hardware, pero hay que interpretar el push buffer.
- **B. HLE de D3D8**, al estilo Cxbx: identificar las funciones de D3D8 en el XBE y sustituirlas por
  implementaciones sobre OpenGL o Vulkan. Es la vía de [[opence]] (`d3d8_gl.c`), pero con **D3D8LTCG** las funciones
  pueden estar inlineadas y cuesta encontrarlas ([[xbe-default]]).

Se decide en H7 con datos. Antes conviene tener el proyecto Ghidra (H5) para ver cuánto de D3D8 es identificable.

- **Corrección (H7a):** `fs:[0x24]` NO se superpone. En Windows es `ClientId.UniqueThread`, y pisarlo hacía perder despertares de CRITICAL_SECTION/SRW. Las lecturas de IRQL del XBE (`movzx r32, byte fs:[24h]`) se parchean al cargar ([[gpu-nv2a]]).
