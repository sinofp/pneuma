#include "user.h"

extern char __stack_top[];

unsigned long syscall(unsigned long sysno, unsigned long arg0, unsigned long arg1, unsigned long arg2) {
    register unsigned long a0 __asm__("a0") = arg0;
    register unsigned long a1 __asm__("a1") = arg1;
    register unsigned long a2 __asm__("a2") = arg2;
    register unsigned long a3 __asm__("a3") = sysno;

    __asm__ __volatile__("ecall" : "=r"(a0) : "r"(a0), "r"(a1), "r"(a2), "r"(a3) : "memory");

    return a0;
}

__attribute__((noreturn)) void exit() {
    syscall(SYS_EXIT, 0, 0, 0);
    for (;;)
        ; // Just in case!
}

int putchar(int ch) { return syscall(SYS_PUTCHAR, ch, 0, 0); }

int getchar() { return syscall(SYS_GETCHAR, 0, 0, 0); }

__attribute__((section(".text.start"))) __attribute__((naked)) void start(void) {
    __asm__ __volatile__("mv sp, %[stack_top] \n"
                         "call main           \n"
                         "call exit           \n" ::[stack_top] "r"(__stack_top));
}
