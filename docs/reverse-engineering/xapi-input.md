---
tags: [ngb, reverse-engineering, runtime, input, h8]
updated: 2026-10-22
status: current
---

# Gamepad input: XAPI HLE (H8)

The game reads the gamepad with the XAPI functions statically linked into `default.xbe`. On the real Xbox,
underneath, there is the kernel's USB (OHCI) driver. The native runtime does not emulate USB: it **replaces those 8
functions** with its own implementations that read the PC gamepad or the keyboard. The code is in
`port/src/input/`.

## How it hooks in (`VERIFIED`: 8 of 8 in the real game)

- The VAs come from the H5 analysis ([[xbe-default]], `symbols.csv` in NGB_DATA). Each one gets a
  `JMP rel32` to our implementation.
- **A function is patched only if it is the expected one:**
  - the first 8 bytes of the prologue match;
  - the epilogue ends in `C2 N 00`, with the same N;
  - with `--symbols`, the VA matches the CSV.
- If anything does not add up, the original function is left alone and a note goes to the log. That way another XBE is not corrupted.

| Function | VA | `ret` |
| --- | --- | --- |
| `XGetDevices` | `0x3DCC99` | 4 |
| `XGetDeviceChanges` | `0x3DCCBB` | 12 |
| `XPeekDevices` | `0x3DCC58` | 12 |
| `XInputOpen` | `0x3DDFF1` | 16 |
| `XInputClose` | `0x3DE047` | 4 |
| `XInputGetCapabilities` | `0x3DE053` | 8 |
| `XInputGetState` | `0x3DE22B` | 8 |
| `XInputSetState` | `0x3DE29E` | 8 |

## Semantics

- Port 0 always has a gamepad connected. `XGetDeviceChanges` reports the insertion only once.
- **`XINPUT_STATE` (22 bytes, packed):**
  - `dwPacketNumber`, which rises with every change;
  - `wButtons`: D-pad, START, BACK and the stick clicks;
  - `bAnalogButtons[8]`: A, B, X, Y, BLACK, WHITE, LT and RT, from 0 to 255;
  - the 4 stick axes.
- **Checked in the game's memory:** the state is copied to `0xD4CD80 + port*0x80`. The table
  `0x46DA38` converts the buttons into flags, and an analog button above `0x20` counts as pressed.
- `XInputSetState` (rumble) returns `ERROR_IO_PENDING`, like the real one, and is ignored.

## Input sources on the PC

- **XInput gamepad** (`xinput1_4`, loaded at run time): LB is WHITE and RB is BLACK.
- **Keyboard:**
  - Enter = START + A, Esc = BACK + B;
  - arrows = D-pad, WASD = left stick;
  - J K L I = A B X Y.
- **Deterministic script:** `--input-script "t=42s:START,t=45s:A@300ms"`. With `--no-host-input` the real gamepad and
  keyboard are ignored.

## What was found: the hang after the warnings was not the gamepad

- **The game receives START and A** (checked in its memory), and yet it stayed on the black screen.
- The hypothesis of the Sofdec video waiting for the audio clock **turned out to be false**. The real cause was a bug in the
  emulated kernel, described in [[gpu-nv2a]] ("How far it gets"). With that fixed, the intro video
  plays.
