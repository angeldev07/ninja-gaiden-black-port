---
tags: [ngb, reverse-engineering, gpu, shaders, h7]
updated: 2026-10-21
status: current
---

# Shaders del NV2A: vertex programs y register combiners → GLSL

Es una pieza de H7b ([[adr-06-render-hibrido]]): traduce el código de shader que el juego carga en la GPU a GLSL
4.00, para el backend de OpenGL. Implementación propia en `port/src/shaders/` (`vsh.c`, `psh.c` y `emit.c`). Se
estudió el formato en xemu, Cxbx-Reloaded, envytools, GL_NV_register_combiners y la especificación vs_1_1, sin
copiar código GPL y sin tocar el XDK.

## Vertex programs (`VERIFIED`: tests más los 16 programas reales)

- Cada instrucción ocupa 128 bits, con **MAC e ILU en paralelo**:
  - MAC: MOV MUL ADD MAD DP3 DPH DP4 DST MIN MAX SLT SGE ARL;
  - ILU: MOV RCP RCC RSQ EXP LOG LIT.
- Registros:
  - R0-R11; R12 es alias de oPos;
  - entradas v0-v15 y constantes c[0-191], con direccionamiento relativo por A0;
  - swizzles, negación y máscaras de escritura.
  - Si el ILU va emparejado con el MAC, escribe en R1.
- **Epílogo de screen space de la Xbox:** la posición sale en píxeles y se convierte a clip space con
  `surfaceSize`/`clipRange`, multiplicando xyz por w.
- **Datos reales:** en `.data` de `default.xbe` hay **16 binarios de vertex shader** (cabecera `0x2078`, de 13 a 134
  instrucciones), y se decodifican los 16. Confirman el formato:
  - `DP4 oPos` con la matriz en c[96-99];
  - epílogo `MUL c[58]` + `RCC R1.x` + `MAD c[59]`.
  - Los GLSL generados están en `NGB_DATA\runs\shaders\` (son derivados del juego, no van al repo).

## Register combiners (pixel shaders)

- 1 a 8 etapas más la final.
  - Entradas A-D, con los 8 mapeos (unsigned, expand, signed…).
  - AB/CD como producto o dot, y SUM o MUX.
  - Escala y sesgo, y blue-to-alpha.
  - Constantes por etapa.
- Etapa final: A·B + (1−A)·C + D, alfa = G, más EF_PROD y V1R0_SUM.
- Modos de textura emulados:
  - 2D proyectiva, 3D y CUBE;
  - PASSTHRU;
  - CLIPPLANE;
  - BUMPENVMAP y BUMPENVMAP_LUM.
- **Datos reales:** se encontraron 2 pixel shaders reales, de 2 etapas con bump por producto escalar
  (`DOTPRODUCT` + `DOT_ST`). Se traducen, pero **los modos `DOT_*` aún no se emulan**, así que su resultado todavía
  no es correcto. `INFERRED`: la mayoría de los materiales usarán el pipeline fijo (texture stage states), no
  pixel shaders.

## Pruebas

- `port/tests/shaders_test.c`: 219 comprobaciones, con instrucciones codificadas a mano palabra a palabra.
- **El GLSL todavía no se ha compilado nunca**: no hay glslangValidator, así que de momento solo se valida el texto.
  Se validará al compilarlo en el backend de OpenGL.

## Pendiente

- Los modos `DOT_*`, BRDF y DPNDNT.
- Niebla no lineal, oB0/oB1 según la cara y el offset de medio píxel.
- Alpha test, color key y alpha kill.
- Juntar los programas que llegan por push buffer (`0x0B00` + LOAD/START) en el intérprete PGRAPH.
