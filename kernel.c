#include "kernel.h"
#include "common.h"

typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef uint32_t size_t;

extern char __bss[], __bss_end[], __stack_top[];

struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4,
                       long arg5, long fid, long eid) {
    register long a0 __asm__("a0") = arg0;
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    register long a3 __asm__("a3") = arg3;
    register long a4 __asm__("a4") = arg4;
    register long a5 __asm__("a5") = arg5;
    register long a6 __asm__("a6") = fid;
    register long a7 __asm__("a7") = eid;

    __asm__ __volatile__("ecall"
                         : "=r"(a0), "=r"(a1)
                         : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
                           "r"(a6), "r"(a7)
                         : "memory");
    return (struct sbiret){.error = a0, .value = a1};
}

void putchar(char ch) {
    // DBCN: Console Write Byte (FID #2), struct sbiret sbi_debug_console_write_byte(uint8_t byte)
    sbi_call((uint8_t) ch, 0, 0, 0, 0, 0, 2, 0x4442434E);
}

void puts(const char *s) {
    // DBCN: Console Write (FID #0) struct sbiret sbi_debug_console_write(unsigned long num_bytes, unsigned long base_addr_lo, unsigned long base_addr_hi)
    unsigned long num_bytes;
    for (num_bytes = 0; s[num_bytes] != '\0'; ++num_bytes) {}
    long base_addr = (long) s;
    sbi_call(num_bytes, base_addr, 0, 0, 0, 0, 0, 0x4442434E);
}

void *memset(void *buf, char c, size_t n) {
    uint8_t *p = (uint8_t *) buf;
    while (n--)
        *p++ = c;
    return buf;
}

void kernel_main() {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    const char *s = "\n\nHello World!\n";
    for (int i = 0; s[i] != '\0'; i++) {
        putchar(s[i]);
    }
    puts(s);

    printf("\n\nHello %s\n", "World!");
    printf("1 + 2 = %d, %x\n", 1 + 2, 0x1234abcd);

    for (;;) {
        __asm__ __volatile__("wfi");
    }
}

__attribute__((section(".text.boot")))
__attribute__((naked))
void boot() {
    __asm__ __volatile__(
        "mv sp, %[stack_top]\n" // Set the stack pointer
        "j kernel_main\n"       // Jump to the kernel main function
        :
        : [stack_top] "r" (__stack_top) // Pass the stack top address as %[stack_top]
    );
}

