#include "user.h"

extern "C" void main(void) {
    for (int i = 0; i < 5; ++i) {
        putchar('B');
        yield();
    }
}
