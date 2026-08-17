#pragma once

#include <stdint.h>

#define INPUT_SERVER 3 // PID; matches spawn order in kernel/main.cc

#define INPUT_OP_GET 1 // non-blocking: reply has_event=0 if the buffer is empty

typedef struct {
    int op;
} input_req_t;

// deliberately reuse virtio_input_event
typedef struct __attribute__((packed)) {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} virtio_input_event;

typedef struct {
    int has_event; // 1 = valid, 0 = no event this poll. Added because our recv can't express it by ret value
    virtio_input_event ev;
} input_resp_t;
