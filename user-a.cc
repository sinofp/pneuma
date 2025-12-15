#include "user.h"

extern "C" void main(void) {
    for (int i = 0; i < 10; ++i) {
        putchar('A');
        yield();
    }
}
