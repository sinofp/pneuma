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

int sleep_ms(unsigned);

typedef long int_pid; // Register-width signed; matches proc::id_t (intptr_t); < 0: error / ANY_SOURCE
#define ANY_SOURCE ((int_pid)-1)

// < 0: Error. >= 0: the other end's PID
int_pid ssend(void *, size_t, int_pid);
int_pid recv(void *, size_t, int_pid);

#ifdef __cplusplus
}
#endif
