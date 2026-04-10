#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct input_event {
    uint16_t type;
    uint16_t code;
    int32_t value;
};

int sleep_ms(unsigned ms);

#ifdef __cplusplus
}
#endif
