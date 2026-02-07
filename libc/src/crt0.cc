#include <stdlib.h>

extern char __stack_top[];
extern "C" void main(void);

[[using gnu: section(".text.start"), naked]]
void start(void) {
    asm volatile("mv sp, %[stack_top] \n"
                 "call main           \n"
                 "call exit           \n" ::[stack_top] "r"(__stack_top));
}
