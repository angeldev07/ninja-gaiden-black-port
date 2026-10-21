/*
 * input.h: entrada del mando para el HLE de XAPI (H8).
 *
 * Dos capas:
 *   - input.c (pura, sin Windows ni el loader): estructuras de Xbox, mapeo de un mando de PC
 *     (XInput de Windows) y del teclado a XINPUT_GAMEPAD de Xbox, y el guion de pulsaciones
 *     automáticas (--input-script). La prueban port/tests/input_test.c.
 *   - xapi_hle.c (loader): parchea las funciones de XAPI del juego con un JMP a las nuestras
 *     y lee la entrada real (xinput1_4.dll / xinput9_1_0.dll y GetAsyncKeyState).
 *
 * Layout de Xbox (XDK público; nxdk lib/usb/libusbohci_xbox/xid_driver.h para el orden de
 * los botones analógicos y los sticks; XboxDevWiki "Xbox Input Devices" para los bits):
 *
 *   XINPUT_GAMEPAD (18 bytes)            XINPUT_STATE (22 bytes)
 *     +0  WORD  wButtons                   +0  DWORD dwPacketNumber
 *     +2  BYTE  bAnalogButtons[8]          +4  XINPUT_GAMEPAD Gamepad
 *           A B X Y BLACK WHITE LT RT
 *     +10 SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY (con signo; Y positivo = arriba)
 *
 *   wButtons: D-pad arriba 0x01, abajo 0x02, izquierda 0x04, derecha 0x08, START 0x10,
 *             BACK 0x20, stick izquierdo pulsado 0x40, derecho 0x80. A, B, X, Y, los
 *             botones negro y blanco y los gatillos son analógicos (0..255).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define XPAD_DPAD_UP 0x0001u
#define XPAD_DPAD_DOWN 0x0002u
#define XPAD_DPAD_LEFT 0x0004u
#define XPAD_DPAD_RIGHT 0x0008u
#define XPAD_START 0x0010u
#define XPAD_BACK 0x0020u
#define XPAD_LEFT_THUMB 0x0040u
#define XPAD_RIGHT_THUMB 0x0080u

enum xpad_analog {
    XPAD_A = 0,
    XPAD_B = 1,
    XPAD_X = 2,
    XPAD_Y = 3,
    XPAD_BLACK = 4,
    XPAD_WHITE = 5,
    XPAD_LEFT_TRIGGER = 6,
    XPAD_RIGHT_TRIGGER = 7,
    XPAD_ANALOG_COUNT = 8,
};

#pragma pack(push, 1)
typedef struct xbox_gamepad {
    uint16_t buttons;
    uint8_t analog[XPAD_ANALOG_COUNT];
    int16_t thumb_lx, thumb_ly, thumb_rx, thumb_ry;
} xbox_gamepad_t;

typedef struct xbox_input_state {
    uint32_t packet;
    xbox_gamepad_t gamepad;
} xbox_input_state_t;
#pragma pack(pop)

/* El juego copia estos bytes tal cual: el tamaño se comprueba al compilar. */
typedef char xpad_check_gamepad_size[sizeof(xbox_gamepad_t) == 18 ? 1 : -1];
typedef char xpad_check_state_size[sizeof(xbox_input_state_t) == 22 ? 1 : -1];

/* XINPUT_GAMEPAD de Windows (xinput1_4), con sus bits; aquí, sin incluir <xinput.h>. */
typedef struct pc_gamepad {
    uint16_t buttons;
    uint8_t left_trigger, right_trigger;
    int16_t thumb_lx, thumb_ly, thumb_rx, thumb_ry;
} pc_gamepad_t;

#define PC_DPAD_UP 0x0001u
#define PC_DPAD_DOWN 0x0002u
#define PC_DPAD_LEFT 0x0004u
#define PC_DPAD_RIGHT 0x0008u
#define PC_START 0x0010u
#define PC_BACK 0x0020u
#define PC_LEFT_THUMB 0x0040u
#define PC_RIGHT_THUMB 0x0080u
#define PC_LEFT_SHOULDER 0x0100u
#define PC_RIGHT_SHOULDER 0x0200u
#define PC_A 0x1000u
#define PC_B 0x2000u
#define PC_X 0x4000u
#define PC_Y 0x8000u

/* Mando de Xbox One/360 -> Xbox: A/B/X/Y digitales a 255, LB -> WHITE, RB -> BLACK. */
void xpad_from_pc(const pc_gamepad_t *pc, xbox_gamepad_t *out);

/* Teclado -> Xbox. `down(vk)` != 0 si la tecla virtual `vk` está pulsada.
 *   Enter = START + A, Esc = BACK + B, flechas = D-pad, WASD = stick izquierdo,
 *   J/K/L/I = A/B/X/Y, Q/E = WHITE/BLACK, U/O = gatillos izquierdo/derecho. */
typedef int (*key_down_fn)(int vk);
void xpad_from_keys(key_down_fn down, xbox_gamepad_t *out);

/* Combina dos fuentes: OR de botones, máximo de los analógicos y el stick más desviado. */
void xpad_merge(xbox_gamepad_t *dst, const xbox_gamepad_t *src);
int xpad_equal(const xbox_gamepad_t *a, const xbox_gamepad_t *b);

/* Guion de pulsaciones: "t=3s:START,t=5s:A,t=6500ms:UP+A@1s" (hold por defecto 200 ms).
 * Botones: START BACK A B X Y BLACK WHITE LT RT UP DOWN LEFT RIGHT LTHUMB RTHUMB.
 *
 * Con la unidad "p" el tiempo se cuenta en lecturas del mando (XInputGetState del puerto 0)
 * en vez de en milisegundos: "t=2400p:START@10p". El juego lee el mando una vez por frame
 * de su bucle y no lo lee mientras carga, así que un guion en lecturas no depende de cuánto
 * tarden la E/S o el render (con milisegundos, una carga más lenta hacía que la pulsación
 * cayera en otra pantalla). Una entrada usa la misma unidad en el instante y en la duración
 * (hold por defecto: 12 lecturas). */
#define INPUT_SCRIPT_MAX 64
#define INPUT_SCRIPT_HOLD_MS 200u
#define INPUT_SCRIPT_HOLD_POLLS 12u

typedef struct input_event {
    uint32_t at_ms, hold_ms; /* en lecturas si `polls` */
    uint8_t polls;
    uint16_t buttons;
    uint8_t analog_mask; /* bit i -> analog[i] = 255 */
} input_event_t;

typedef struct input_script {
    int count;
    input_event_t events[INPUT_SCRIPT_MAX];
} input_script_t;

/* 0 si el texto es válido; si no, -1 y el motivo en `err`. */
int input_script_parse(const char *text, input_script_t *out, char *err, size_t errlen);
/* Añade a `out` las pulsaciones activas en `now_ms` o en la lectura número `now_polls`. */
void input_script_apply(const input_script_t *script, uint32_t now_ms, uint32_t now_polls,
                        xbox_gamepad_t *out);

/* xapi_hle.c (loader) --------------------------------------------------------------- */

/* Parchea XAPI si los bytes del XBE cargado son los esperados (y cuadran con el CSV de
 * símbolos, si lo hay). `host_input` = 0: ni mando de PC ni teclado, solo el guion (pruebas
 * deterministas). Devuelve las funciones parcheadas; -1 si el guion es inválido. */
int ngb_input_install(const char *symbols_csv, const char *script, int host_input);
/* Resumen de llamadas al log. */
void ngb_input_summary(void);
