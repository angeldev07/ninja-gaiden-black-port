---
tags: [ngb, target, roadmap]
updated: 2026-10-20
status: current
---

# Roadmap: hitos tangibles

Cada hito termina con algo que **se ve, se oye o se ejecuta**. El estado se
actualiza al cerrar cada sesión, y el detalle de cada cierre queda en [[log]].

| Hito | Resultado tangible | Criterio de terminado | Estado |
| --- | --- | --- | --- |
| **H0** Cerebro y base | Vault, repo, datos extraídos, equipo definido | Una sesión nueva puede retomar el proyecto con [[GOAL-ngb-port]] | **hecho** (2026-10-20) |
| **H1** Mapa del disco y del binario | Reporte del XBE (versión del XDK, librerías, imports del kernel, secciones) e inventario de los `.afs` | [[xbe-default]] y [[afs]] con datos `VERIFIED`; herramientas con tests en verde | en curso |
| **H2** Primera textura | Una textura del juego exportada a PNG, por ejemplo la de Ryu | Formato de textura documentado en `formats/`; exportador con test | pendiente |
| **H3** Primer modelo | Modelo de Ryu exportado a glTF/OBJ y visible en un visor | Formato de modelo documentado; exportador | pendiente |
| **H4** Primer sonido | Una pista de `bgm.afs` reproducida como WAV | Formato de audio documentado | pendiente |
| **H5** Proyecto Ghidra y firmas del SDK | Proyecto Ghidra persistente; librerías del XDK identificadas por firmas; % de funciones con nombre | Métrica de cobertura en [[xbe-default]] | pendiente |
| **H6** Arranque nativo | `default.xbe` corriendo en un proceso Win32 de 32 bits hasta la primera llamada al kernel sin implementar, con log de las llamadas | Log reproducible; kernel HLE mínimo | pendiente |
| **H7** Primer frame | Un frame del juego (logo o pantalla de carga) en una ventana de PC | D3D8 mínimo sobre GL/Vulkan | pendiente |
| **H8** Menú jugable | Menú principal con mando, audio y video | Entrada SDL3, audio, `.sfd` | pendiente |
| **H9+** Descompilación y mejoras | Funciones en C con tests diferenciales; panorámico, 60+ fps interpolados, resolución alta | Por función o subsistema | pendiente |

H2, H3 y H4 (assets) no dependen de H5, H6 y H7 (ejecución), así que pueden avanzar
en paralelo. Los assets dan resultados visibles pronto y además revelan las
estructuras que luego usa el código.

## Siguiente paso concreto

Cerrar H1: ingerir los reportes de `tools/xbe_info.py` y `tools/afs.py` en
[[xbe-default]] y [[afs]], y elegir la textura de H2 según los formatos más
frecuentes del inventario.
