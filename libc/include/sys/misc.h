#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int sleep_ms(unsigned);
unsigned get_ticks_ms();
void reboot();

#ifdef __cplusplus
}
#endif
