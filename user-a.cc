#include "user.h"

extern "C" void main(void) {
    for (int i = 0; i < 100; ++i) {
        putchar('A');
        // yield();
    }
}
