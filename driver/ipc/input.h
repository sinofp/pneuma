#pragma once

#include <stdint.h>

#define INPUT_SERVER 3 // PID; matches spawn order in kernel/main.cc

#define INPUT_OP_GET 1 // non-blocking: reply has_event=0 if the buffer is empty

typedef struct {
    int op;
} input_req_t;

typedef struct {
    int has_event; // 1 = valid, 0 = no event this poll
    uint16_t type; // linux/virtio event type; EV_KEY = 1
    uint16_t code; // scancode
    int32_t value; // 1 = press, 0 = release
} input_resp_t;
