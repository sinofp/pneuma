#include "kernel.h"
#include "common.h"
#include "csr.h"
#include "file.h"
#include "mem.h"
#include "proc.h"
#include "sbi.h"
#include "trap.h"
#include "virtio.h"

extern uint8_t __bss[], __bss_end[], __stack_top[];

// shell.bin.o
extern uint8_t _binary_shell_bin_start[], _binary_shell_bin_size[];

// proc.cc
extern process *idle_proc, *current_proc;

file files[FILES_MAX];

extern "C" void kernel_main() {
    memset(__bss, 0, __bss_end - __bss);

    interrupt_init();

    printf("\n\nhello %s\n", "world!");
    // %lx should be able to output 0x12345678'ffffffff on RV64 (LP64)
    printf("1 + 2 = %d, 0x%lx, 0x%lx\n", 1 + 2, 0x1234abcd, 0x12345678);

    auto paddr0 = alloc_pages(2);
    auto paddr1 = alloc_pages(1);
    printf("alloc_pages test: paddr0=0x%lx\n", paddr0);
    printf("alloc_pages test: paddr1=0x%lx\n", paddr1);

    idle_proc = create_process({ nullptr, 0 });
    idle_proc->pid = 0; // idle
    current_proc = idle_proc;

    // This will trigger yield when dealing PLIC interrupt, which access idle_proc->table
    // Therefore it must be placed after idel_proc = create_process, otherwise we'll have
    // Store/AMO Access Fault: scause=0x0000000000000007, stval=0x0000000000000008, sepc=0x0000000080200f2e
    virtio_blk_init();
    fs_init();

    printf("USER_BASE is %p\n", reinterpret_cast<void *>(USER_BASE));
    printf("_binary_shell_bin_start is 0x%lx\n", _binary_shell_bin_start);
    printf("_binary_shell_bin_size is 0x%lx\n", _binary_shell_bin_size);
    printf("About to create shell\n");
    create_process({ _binary_shell_bin_start, reinterpret_cast<size_t>(_binary_shell_bin_size) });

    yield();

    for (;;) {
        printf("switched to idle process\n");
        asm("wfi");
        yield();
    }
}

[[using gnu: section(".text.boot"), naked]]
void boot() {
    asm volatile("mv sp, %[stack_top]\n" // Set the stack pointer
                 "j kernel_main\n"       // Jump to the kernel main function
                 :
                 : [stack_top] "r"(__stack_top) // Pass the stack top address as %[stack_top]
    );
}
