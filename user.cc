#include "user.h"

extern char __stack_top[];

unsigned long syscall(unsigned long sysno, unsigned long arg0, unsigned long arg1, unsigned long arg2) {
    register unsigned long a0 asm("a0") = arg0;
    register unsigned long a1 asm("a1") = arg1;
    register unsigned long a2 asm("a2") = arg2;
    register unsigned long a3 asm("a3") = sysno;

    asm volatile("ecall"
                 : "=r"(a0)
                 : "r"(a0), "r"(a1), "r"(a2), "r"(a3)
                 : "memory");

    return a0;
}

[[noreturn]]
void exit() {
    syscall(SYS_EXIT, 0, 0, 0);
    for (;;)
        ; // Just in case!
}

int putchar(int ch) { return syscall(SYS_PUTCHAR, ch, 0, 0); }

int getchar() { return syscall(SYS_GETCHAR, 0, 0, 0); }

[[using gnu: section(".text.start"), naked]]
void start(void) {
    asm volatile("mv sp, %[stack_top] \n"
                 "call main           \n"
                 "call exit           \n" ::[stack_top] "r"(__stack_top));
}
