/*
 * input.h: gamepad input for the XAPI HLE (H8).
 *
 * Two layers:
 *   - input.c (pure, no Windows or loader): Xbox structures, mapping of a PC gamepad
 *     (Windows XInput) and the keyboard to Xbox XINPUT_GAMEPAD, and the automatic
 *     button-press script (--input-script). Tested by port/tests/input_test.c.
 *   - xapi_hle.c (loader): patches the game's XAPI functions with a JMP to ours
 *     and reads the real input (xinput1_4.dll / xinput9_1_0.dll and GetAsyncKeyState).
 *
 * Xbox layout (public XDK; nxdk lib/usb/libusbohci_xbox/xid_driver.h for the order of
 * the analog buttons and the sticks; XboxDevWiki "Xbox Input Devices" for the bits):
 *
 *   XINPUT_GAMEPAD (18 bytes)            XINPUT_STATE (22 bytes)
 *     +0  WORD  wButtons                   +0  DWORD dwPacketNumber
 *     +2  BYTE  bAnalogButtons[8]          +4  XINPUT_GAMEPAD Gamepad
 *           A B X Y BLACK WHITE LT RT
 *     +10 SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY (signed; positive Y = up)
 *
 *   wButtons: D-pad up 0x01, down 0x02, left 0x04, right 0x08, START 0x10,
 *             BACK 0x20, left stick pressed 0x40, right 0x80. A, B, X, Y, the black and
 *             white buttons and the triggers are analog (0..255).
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

/* The game copies these bytes as they are: the size is checked at compile time. */
typedef char xpad_check_gamepad_size[sizeof(xbox_gamepad_t) == 18 ? 1 : -1];
typedef char xpad_check_state_size[sizeof(xbox_input_state_t) == 22 ? 1 : -1];

/* Windows XINPUT_GAMEPAD (xinput1_4), with its bits; here, without including <xinput.h>. */
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

/* Xbox One/360 gamepad -> Xbox: A/B/X/Y digital at 255, LB -> WHITE, RB -> BLACK. */
void xpad_from_pc(const pc_gamepad_t *pc, xbox_gamepad_t *out);

/* Keyboard -> Xbox. `down(vk)` != 0 if the virtual key `vk` is pressed.
 *   Enter = START + A, Esc = BACK + B, arrows = D-pad, WASD = left stick,
 *   J/K/L/I = A/B/X/Y, Q/E = WHITE/BLACK, U/O = left/right trigger. */
typedef int (*key_down_fn)(int vk);
void xpad_from_keys(key_down_fn down, xbox_gamepad_t *out);

/* Merges two sources: OR of the buttons, maximum of the analogs and the most deflected stick. */
void xpad_merge(xbox_gamepad_t *dst, const xbox_gamepad_t *src);
int xpad_equal(const xbox_gamepad_t *a, const xbox_gamepad_t *b);

/* Button-press script: "t=3s:START,t=5s:A,t=6500ms:UP+A@1s" (default hold 200 ms).
 * Buttons: START BACK A B X Y BLACK WHITE LT RT UP DOWN LEFT RIGHT LTHUMB RTHUMB.
 *
 * With the "p" unit the time is counted in gamepad reads (XInputGetState of port 0)
 * instead of milliseconds: "t=2400p:START@10p". The game reads the gamepad once per frame
 * of its loop and does not read it while loading, so a script in reads does not depend on how
 * long I/O or rendering take (with milliseconds, a slower load made the press
 * land on another screen). An entry uses the same unit for the instant and the duration
 * (default hold: 12 reads). */
#define INPUT_SCRIPT_MAX 64
#define INPUT_SCRIPT_HOLD_MS 200u
#define INPUT_SCRIPT_HOLD_POLLS 12u

typedef struct input_event {
    uint32_t at_ms, hold_ms; /* in reads if `polls` */
    uint8_t polls;
    uint16_t buttons;
    uint8_t analog_mask; /* bit i -> analog[i] = 255 */
} input_event_t;

typedef struct input_script {
    int count;
    input_event_t events[INPUT_SCRIPT_MAX];
} input_script_t;

/* 0 if the text is valid; otherwise -1 and the reason in `err`. */
int input_script_parse(const char *text, input_script_t *out, char *err, size_t errlen);
/* Adds to `out` the presses active at `now_ms` or at read number `now_polls`. */
void input_script_apply(const input_script_t *script, uint32_t now_ms, uint32_t now_polls,
                        xbox_gamepad_t *out);

/* xapi_hle.c (loader) --------------------------------------------------------------- */

/* Patches XAPI if the bytes of the loaded XBE are the expected ones (and match the symbols
 * CSV, if there is one). `host_input` = 0: neither PC gamepad nor keyboard, only the script
 * (deterministic tests). Returns the patched functions; -1 if the script is invalid. */
int ngb_input_install(const char *symbols_csv, const char *script, int host_input);
/* Summary of calls, to the log. */
void ngb_input_summary(void);
