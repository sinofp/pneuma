#include <stdio.h>
#include <sys/misc.h>

extern "C" void main(void) {
    for (int i = 0; i < 200; ++i) {
        // for (;;) {
        putchar('B');
        sleep_ms(1);
    }
}
