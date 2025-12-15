#include "user.h"

extern "C" void main(void) {
    for (int i = 0; i < 50; ++i) {
        putchar('B');
        // yield();
    }
}
