#include "doomgeneric.h"
#include "doomkeys.h"
#include "m_fixed.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/misc.h>
#include <sys/syscall.h>
#include <ui/fb.h>
#include <ui/keys.h>

// Use double to fake Doom 16.16 fixed-point to replace 64-bit int divison on RV32
fixed_t FixedMul(fixed_t a, fixed_t b) { return (fixed_t) ((double) a * b / FRACUNIT); }

fixed_t FixedDiv(fixed_t a, fixed_t b) {
    if ((abs(a) >> 14) >= abs(b))
        return (a ^ b) < 0 ? INT_MIN : INT_MAX;
    return (fixed_t) ((double) a / b * FRACUNIT);
}

void DG_Init(void) {
    // DG_ScreenBuffer is defined and allocated in doomgeneric.c
    fb_open(DG_ScreenBuffer, DOOMGENERIC_RESX, DOOMGENERIC_RESY);
}

void DG_DrawFrame(void) { fb_update(); }

void DG_SleepMs(uint32_t ms) { sleep_ms(ms); }

uint32_t DG_GetTicksMs(void) { return get_ticks_ms(); }

// Linux/virtio key code ranges mapped to ASCII
static const unsigned char keyboard_nums[10] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0' };
static const unsigned char keyboard_qwerty[12] = { 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']' };
static const unsigned char keyboard_asdf[11] = { 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'' };
static const unsigned char keyboard_zxcv[11] = { '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/' };

static unsigned char toDoomKey(uint_fast16_t code) {
    // Arrow keys
    if (code == SCANCODE_UP)
        return KEY_UPARROW;
    if (code == SCANCODE_DOWN)
        return KEY_DOWNARROW;
    if (code == SCANCODE_LEFT)
        return KEY_LEFTARROW;
    if (code == SCANCODE_RIGHT)
        return KEY_RIGHTARROW;

    // F Keys
    if ((SCANCODE_F1 <= code && code <= SCANCODE_F10) || code == SCANCODE_F11 || code == SCANCODE_F12)
        return 0x80 + code;

    // Number row
    if (SCANCODE_1 <= code && code <= SCANCODE_0)
        return keyboard_nums[code - SCANCODE_1];

    // QWERTY row
    if (SCANCODE_Q <= code && code <= SCANCODE_RIGHTBRACE)
        return keyboard_qwerty[code - SCANCODE_Q];

    // ASDF row
    if (SCANCODE_A <= code && code <= SCANCODE_APOSTROPHE)
        return keyboard_asdf[code - SCANCODE_A];

    // ZXCV row
    if (SCANCODE_BACKSLASH <= code && code <= SCANCODE_SLASH)
        return keyboard_zxcv[code - SCANCODE_BACKSLASH];

    // Misc keys
    switch (code) {
    case SCANCODE_ESC:
        return KEY_ESCAPE;
    case SCANCODE_BACKSPACE:
        return KEY_BACKSPACE;
    case SCANCODE_TAB:
        return KEY_TAB;
    case SCANCODE_ENTER:
        return KEY_ENTER;
    case SCANCODE_SPACE:
        return KEY_USE;
    case SCANCODE_MINUS:
        return KEY_MINUS;
    case SCANCODE_EQUAL:
        return KEY_EQUALS;
    case SCANCODE_LEFTCTRL:
    case SCANCODE_RIGHTCTRL:
        return KEY_FIRE; // left/right ctrl
    case SCANCODE_LEFTSHIFT:
    case SCANCODE_RIGHTSHIFT:
        return KEY_RSHIFT; // left/right shift
    case SCANCODE_LEFTALT:
    case SCANCODE_RIGHTALT:
        return KEY_LALT; // left/right alt
    default:
        return 0;
    }
}

int DG_GetKey(int *pressed, unsigned char *doomKey) {
    virtio_input_event ev;
    if (poll_event(&ev)) {
        *pressed = ev.value;
        *doomKey = toDoomKey(ev.code);
        return 1;
    }

    return 0;
}

void DG_SetWindowTitle(const char *title) { printf("DG_SetWindowTitle: %s", title); }

int main() {
    doomgeneric_Create(0, NULL);
    for (;;)
        doomgeneric_Tick();
}
