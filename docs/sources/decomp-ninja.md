---
tags: [ngb, source, prior-art]
updated: 2026-10-20
status: current
---

# Trabajo previo sobre NGB

## DECOMP__Ninja (abandonado)

- Repo: https://github.com/mehmetalinya/DECOMP__Ninja. Último push el 2022-07-02, 6 estrellas. `VERIFIED` vía la API de GitHub.
- Casi solo es una estructura de carpetas: `Ninja Gaiden Black Decomp`, `Ninja Gaiden Black
  Prototype Decomp`, `SDK/` y equivalentes para NG2. Las carpetas de NGB están prácticamente vacías.
- El autor se detuvo porque consideró los formatos de assets "UNKNOWN" (compresión o cifrado propios).
- **Lo que vimos nosotros:** los contenedores del disco son **AFS de CRI Middleware**, con la magia
  `AFS\0` (`VERIFIED`, [[disco-ngb]]). El formato del contenedor está documentado en implementaciones
  abiertas como [AFSLib](https://github.com/MaikelChan/AFSLib) y
  [Sewer56/AfsLib](https://github.com/Sewer56/AfsLib). Lo desconocido, si lo hay, es el contenido de cada
  entrada ([[afs]]).
- El nombre "Prototype Decomp" sugiere que existe un prototipo de NG. `UNVERIFIED`: un prototipo con
  símbolos cambiaría mucho la viabilidad de descompilar, como pasó con [[opence]].

## Contexto oficial

- Team Ninja (Fumihiko Yasuda, 2021) dijo que NGB quedó fuera de *Master Collection* porque ya no tienen
  el código fuente completo. Fuente:
  https://gameranx.com/updates/id/219898/article/ninja-gaiden-black-left-out-of-master-collection-thanks-to-lost-code/
