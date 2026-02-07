#include <stdio.h>
#include <sys/misc.h>

extern "C" void main(void) {
    for (int i = 0; i < 10; ++i) {
        // for (;;) {
        for (int j = 0; j < 10; ++j)
            putchar('A');
        sleep_ms(100);
    }
}
