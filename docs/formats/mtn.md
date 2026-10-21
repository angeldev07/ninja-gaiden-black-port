---
tags: [ngb, formats, teamninja, animation]
updated: 2026-10-21
status: current
---

# MTN / MOT: animaciones del motor Kunai

Los bancos de animación de los personajes. El de Ryu es `cmn.afs#22 ryu.mtn`, y los de sus armas
`cmn.afs#6-21` (`rm_*`, `rs_*`, `rn_*.mot`). Herramienta: `python -I -m ngb_tools.kmd export <kmd> --motions <banco>`
([[herramientas]]). Fuente: `ngb_tools.kmd.motion` y el spike de animación (renders revisados a ojo).

## Contenedor (`VERIFIED`)

- **`.mtn`** es un chunk `chr_dat` ([[chunk-teamninja]]) con 3 slots:
  - slot 0: el banco;
  - slot 1: otra tabla de offsets, sin interpretar;
  - slot 2: `08 0 100 0`, sin interpretar.
- **`.mot`** es el cuerpo del slot 0 suelto, sin cabecera de chunk.
- **Tabla de ids:** un `u32` por id de motion, hasta encontrar un 0, con offsets relativos al cuerpo. Muchos ids
  comparten una motion de relleno de 4 frames. `ryu.mtn` tiene 1,890 ids y **258 motions distintas**; `rm_kt.mot`
  tiene 167. `INFERRED`: los ids forman un espacio global que completan los bancos de armas.
- **Motion:** una cabecera de `u32` terminada en 0, con un offset por pista. Los datos de cada pista van
  **justo antes** de la cabecera. **Siempre hay 44 pistas**, todas de la misma duración (0 discrepancias en 425
  motions).

## Claves (`VERIFIED` por la suma de duraciones; la interpolación es `INFERRED`)

- Un `u16` de control:
  - bits 0-1: tipo (1 = valor, 2 = valor más 1 tangente, 3 = valor más 2 tangentes, 0 = fin);
  - bits 2-11: frames hasta la clave siguiente;
  - bits 12-15: bits altos de un **valor de 20 bits con signo**.
- Después vienen el `u16` bajo del valor y las tangentes `s16`.
- El 97 % son claves de tipo 1 a 1 frame: la animación viene ya muestreada.
- Interpolación lineal en el tipo 1 y Hermite en los tipos 2 y 3. La tangente es la pendiente por frame ×16.
  Se asume 60 fps.
- **Unidades:** 1e-4 m y 1e-4 rad. Hay saltos exactos de 62,831 = 2π·1e4, y en la pose estática la cadera queda a
  0.99 m.

## Pistas (44 = 3 + 9 + 4×8, `VERIFIED` visualmente)

| Pistas | Qué |
| --- | --- |
| 0-2 | posición de la raíz (cadera, obj2) en espacio de modelo; el suelo está en y = 0 |
| 3-11 | Euler de cadera, pecho y cabeza (locales; R = Rz·Ry·Rx, `INFERRED`) |
| 12-43 | 4 miembros × 8: **objetivo de IK** (xyz), Euler de mano o pie y dos parámetros G y H (H = polo de la rodilla o el codo; G sin descifrar) |

Los miembros van por **IK de 2 huesos**: el motor guarda dónde va la mano o el pie, no el ángulo de cada hueso.
`ngb_tools.kmd.rig` deduce el esqueleto humanoide desde HIERARC ([[kmd]]) y resuelve la FK del tronco y la IK de los
miembros para cada frame.

## Motions identificadas (`VERIFIED` con `anim_preview.png`)

| Banco | Id | Qué es |
| --- | --- | --- |
| `ryu.mtn` | 1 | idle (guardia) |
| `ryu.mtn` | 3 | andar |
| `ryu.mtn` | 90 | correr |
| `ryu.mtn` | 36 | salto hacia delante (aterriza 1.28 m más abajo) |
| `rm_kt.mot` | 141 | guardia con katana |
| `rm_kt.mot` | 151 | tajo con giro |

## Pendiente

- El parámetro G de los miembros y los slots 1 y 2 del `.mtn`.
- Confirmar contra el juego el orden de los Euler, los 60 fps y la tangente ×16.
- El rig solo cubre humanoides de 44 pistas: probar con otros personajes.
- El arma es otro modelo y aún no se monta en la mano.
