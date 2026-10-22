---
tags: [ngb, reverse-engineering, mods]
updated: 2026-10-22
status: draft
---

# Mods: trajes e Intercept

Objetivo del usuario: opciones del port, como parches en memoria (nunca tocando la copia del juego), que habiliten
el selector de trajes con todos desbloqueados, incluido el "futurista", y que recuperen la técnica Intercept.

## Lo que hay en el disco y en el XBE (`VERIFIED`)

- **Trajes de Ryu:** 7 contenedores alternativos, `chr_ryua.afs` … `chr_ryug.afs`, además de `chr_ryu.afs`. Se ven
  en la hoja de [[personajes]].
  - `load_img.bin` los lista seguidos en su tabla de archivos.
  - El juego abre los 8 al arrancar (log de la partida del 2026-10-21, líneas 7668-7822).
  - En partida copia el traje elegido a la caché de Z: como `z:\chr_ryu.afs`.
- **Las guías públicas dan 5 trajes desbloqueables** (Comic, normal, Red Muffler, DOA y Ryuken), así que sobran
  archivos. Candidatos al "futurista": `ryue` (armadura roja y negra) y `ryub` (blanco con armadura).
  `UNVERIFIED` hasta cargarlos en el juego.
- **Selector:**
  - mensajes "A new costume has been added. Hold the **Right** Trigger when selecting NEW GAME…" (tabla de
    mensajes `0x4685a8`, 6 idiomas) y otro igual con el **Left** Trigger (`0x468590`);
  - en el modo Misiones, "@BY Costume" (mensajes `0x46`/`0x47` en `0x44d568`). Las entradas son `{u32 id, 6 punteros
    por idioma}` y se usan por ID, no por puntero: Ghidra no ve referencias.
- **Intercept:** la técnica de los Hurricane Packs del Ninja Gaiden de 2004, retirada en Black. Su nombre sigue
  en la página "Ultimate Techniques" de la lista de técnicas (`0x44d908`, 6 idiomas). `UNVERIFIED` si el código de
  la técnica sigue.
- **Datos de sistema:**
  - `TDATA/5443000d/system.dat`: 0x550 bytes, cifrado (entropía alta), con firma XcSHA sobre `+0x14` (0x53C
    bytes);
  - se lee en un buffer del heap que se libera enseguida, y lo descifrado vive en otra parte;
  - los desbloqueos deberían estar ahí. Las partidas (`UDATA/.../save000.dat`, 95 400 bytes) son del usuario:
    solo lectura.
- **Restos de desarrollo:** el AFS `debug` trae escenarios de prueba (`s_m_edit`, `s_chrput`, `s_r_view`…).

## Herramienta: `--dump-ram FRAMES` y F10

- Vuelca la RAM escribible del juego: la imagen del XBE, el heap y la RAM contigua, unos 150 MB, junto al log,
  como `{u32 va, u32 len, bytes}`.
- Para comparar estados: dos volcados separados por un cambio conocido.
- **Primer intento:** cambiar el idioma en OPTIONS → General Settings, frames 3300 y 3600. Hay demasiado ruido; ni
  el idioma ni el reloj de juego (2:29 → 2:30) se aíslan con un solo par.

## Siguiente paso

- Escaneo iterativo, al estilo Cheat Engine: varios volcados cambiando el idioma ida y vuelta, y quedarse con los
  bytes que siguen el patrón. Eso localiza la estructura de opciones, que en Team Ninja suele compartir bloque con
  los desbloqueos.
- Alternativa estática: la rutina que descifra `system.dat`. Está tras las llamadas a XcSHA del hilo T2
  (`0x32D0F4`…); hay que subir a su llamador del juego.
