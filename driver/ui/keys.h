#pragma once

#include <ipc/input.h>
#include <stdbool.h>
#include <sys/ipc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EV_KEY           1

#define KEY_PRESS   1
#define KEY_RELEASE 0

static inline bool poll_event(virtio_input_event *event) {
    input_req_t req = { .op = INPUT_OP_GET };
    ssend(&req, sizeof req, INPUT_SERVER);
    input_resp_t resp;
    recv(&resp, sizeof resp, INPUT_SERVER);
    if (!resp.has_event || resp.ev.type != EV_KEY)
        return false;

    *event = resp.ev;
    return true;
}

// matches https://github.com/torvalds/linux/blob/master/include/uapi/linux/input-event-codes.h for virtio-input
// uses SDL lingo for later scancodes/keycodes split on strange keyboards
enum scancodes : uint16_t {
    SCANCODE_ESC = 1,
    SCANCODE_1,
    SCANCODE_2,
    SCANCODE_3,
    SCANCODE_4,
    SCANCODE_5,
    SCANCODE_6,
    SCANCODE_7,
    SCANCODE_8,
    SCANCODE_9,
    SCANCODE_0,
    SCANCODE_MINUS,
    SCANCODE_EQUAL,
    SCANCODE_BACKSPACE,

    SCANCODE_TAB,
    SCANCODE_Q,
    SCANCODE_W,
    SCANCODE_E,
    SCANCODE_R,
    SCANCODE_T,
    SCANCODE_Y,
    SCANCODE_U,
    SCANCODE_I,
    SCANCODE_O,
    SCANCODE_P,
    SCANCODE_LEFTBRACE,
    SCANCODE_RIGHTBRACE,
    SCANCODE_ENTER,

    SCANCODE_LEFTCTRL, // I think that's why many people swap CapsLock with LeftCtrl
    SCANCODE_A,
    SCANCODE_S,
    SCANCODE_D,
    SCANCODE_F,
    SCANCODE_G,
    SCANCODE_H,
    SCANCODE_J,
    SCANCODE_K,
    SCANCODE_L,
    SCANCODE_SEMICOLON,
    SCANCODE_APOSTROPHE,

    SCANCODE_LEFTSHIFT = 42,
    SCANCODE_BACKSLASH,
    SCANCODE_Z,
    SCANCODE_X,
    SCANCODE_C,
    SCANCODE_V,
    SCANCODE_B,
    SCANCODE_N,
    SCANCODE_M,
    SCANCODE_COMMA,
    SCANCODE_DOT,
    SCANCODE_SLASH,
    SCANCODE_RIGHTSHIFT,

    SCANCODE_LEFTALT = 56,
    SCANCODE_SPACE,
    SCANCODE_CAPSLOCK,

    SCANCODE_F1,
    SCANCODE_F2,
    SCANCODE_F3,
    SCANCODE_F4,
    SCANCODE_F5,
    SCANCODE_F6,
    SCANCODE_F7,
    SCANCODE_F8,
    SCANCODE_F9,
    SCANCODE_F10,
    SCANCODE_F11 = 87,
    SCANCODE_F12,

    SCANCODE_RIGHTCTRL = 97,
    SCANCODE_RIGHTALT = 100,

    SCANCODE_UP = 103,
    SCANCODE_LEFT = 105,
    SCANCODE_RIGHT,
    SCANCODE_DOWN = 108,
};

#ifdef __cplusplus
}
#endif
