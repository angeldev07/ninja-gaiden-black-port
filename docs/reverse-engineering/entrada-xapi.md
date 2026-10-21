---
tags: [ngb, reverse-engineering, runtime, input, h8]
updated: 2026-10-22
status: current
---

# Entrada del mando: HLE de XAPI (H8)

El juego lee el mando con las funciones de XAPI que lleva enlazadas estáticamente en `default.xbe`. En la Xbox
real, por debajo, está el driver USB (OHCI) del kernel. El runtime nativo no emula el USB: **sustituye esas 8
funciones** por implementaciones propias que leen el mando de PC o el teclado. El código está en
`port/src/input/`.

## Cómo se engancha (`VERIFIED`: 8 de 8 en el juego real)

- Las VA vienen del análisis de H5 ([[xbe-default]], `symbols.csv` en NGB_DATA). A cada una se le pone un
  `JMP rel32` a nuestra implementación.
- **Solo se parchea si la función es la esperada:**
  - los 8 primeros bytes del prólogo coinciden;
  - el epílogo termina en `C2 N 00`, con el mismo N;
  - con `--symbols`, la VA coincide con el CSV.
- Si algo no cuadra, se deja la función original y se anota en el log. Así, otro XBE no se corrompe.

| Función | VA | `ret` |
| --- | --- | --- |
| `XGetDevices` | `0x3DCC99` | 4 |
| `XGetDeviceChanges` | `0x3DCCBB` | 12 |
| `XPeekDevices` | `0x3DCC58` | 12 |
| `XInputOpen` | `0x3DDFF1` | 16 |
| `XInputClose` | `0x3DE047` | 4 |
| `XInputGetCapabilities` | `0x3DE053` | 8 |
| `XInputGetState` | `0x3DE22B` | 8 |
| `XInputSetState` | `0x3DE29E` | 8 |

## Semántica

- El puerto 0 siempre tiene un mando conectado. `XGetDeviceChanges` notifica la inserción una sola vez.
- **`XINPUT_STATE` (22 bytes, empaquetado):**
  - `dwPacketNumber`, que sube con cada cambio;
  - `wButtons`: D-pad, START, BACK y los clics de los sticks;
  - `bAnalogButtons[8]`: A, B, X, Y, BLACK, WHITE, LT y RT, de 0 a 255;
  - los 4 ejes de los sticks.
- **Comprobado en la memoria del juego:** el estado se copia en `0xD4CD80 + puerto*0x80`. La tabla
  `0x46DA38` convierte los botones en flags, y un botón analógico por encima de `0x20` cuenta como pulsado.
- `XInputSetState` (vibración) devuelve `ERROR_IO_PENDING`, como la real, y se ignora.

## Fuentes de entrada en el PC

- **Mando XInput** (`xinput1_4`, que se carga en tiempo de ejecución): LB es WHITE y RB es BLACK.
- **Teclado:**
  - Enter = START + A, Esc = BACK + B;
  - flechas = D-pad, WASD = stick izquierdo;
  - J K L I = A B X Y.
- **Guion determinista:** `--input-script "t=42s:START,t=45s:A@300ms"`. Con `--no-host-input` se ignoran el mando y
  el teclado reales.

## Lo que se descubrió: el bloqueo después de los avisos no es el mando

- **El juego recibe START y A** (comprobado en su memoria y con 8 pulsaciones por guion en 80 s), y aun así la
  pantalla negra que sigue a los avisos no cambia.
- **Hipótesis principal (`INFERRED`): un vídeo de Sofdec.** Al arrancar, el juego abre todos los `.sfd` de
  `movie\` (CRI Sofdec: vídeo MPEG más audio ADX). Tras los avisos, lo esperable es el vídeo de introducción.
  Sofdec sincroniza el vídeo con el reloj del audio, y como la APU no está emulada (`DirectSound` no consume),
  ese reloj no avanza y la imagen se queda en negro.
- **Siguiente paso:** confirmarlo en Ghidra con la ruta de reproducción de Sofdec y su dependencia de
  DirectSound. Después, o se emula un mínimo de DirectSound o se hace HLE del reloj.
