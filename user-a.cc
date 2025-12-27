#include "user.h"

extern "C" void main(void) {
    // for (int i = 0; i < 200; ++i) {
    for (;;) {
        for (int j = 0; j < 10; ++j)
            putchar('A');
        sleep_ms(500);
    }
}
