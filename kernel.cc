#include "kernel.h"
#include "common.h"
#include "csr.h"
#include "fdt.h"
#include "file.h"
#include "mem.h"
#include "plic.h"
#include "proc.h"
#include "qtab.h"
#include "sbi.h"
#include "trap.h"
#include "virtio.h"

extern uint8_t __bss[], __bss_end[], __stack_top[];

// Embedded user program binaries (from *.bin.o files)
extern uint8_t _binary_shell_bin_start[], _binary_shell_bin_size[];
extern uint8_t _binary_a_bin_start[], _binary_a_bin_size[];
extern uint8_t _binary_b_bin_start[], _binary_b_bin_size[];

file files[FILES_MAX];

extern "C" void kernel_main([[maybe_unused]] const xlen_t hartid, const fdt::header *fdt_ptr) {
    memset(__bss, 0, __bss_end - __bss);
    fdt::init(fdt_ptr);
    printf("\n\ndtb:\n  ptr=%p, virtio_base=0x%lx, virtio_irq=%d, plic_base=0x%lx, __free_ram=%p, free_ram_end=0x%lx\n",
           fdt_ptr, static_cast<unsigned long>(virtio_blk_base), static_cast<int>(plic::VIRTIO_BLK_IRQ),
           static_cast<unsigned long>(plic::start_addr), reinterpret_cast<paddr_t>(__free_ram), free_ram_end);

    // TODO make xxx_int xxx::init
    queue_init();
    interrupt_init();

    mem::init();

    printf("hello %s\n", "world!");
    // %lx should be able to output 0x12345678'ffffffff on RV64 (LP64)
    printf("1 + 2 = %d, 0x%lx, 0x%lx\n", 1 + 2, 0x1234abcd, 0x12345678);

    idle_proc = create_process({ nullptr, 0 });
    // Set the idle process running manually, so it can be saved to readyq in reschedule
    idle_proc->state = ProcState::RUNNING;
    chprio(idle_proc, ProcPrio::N, false);
    current_proc = idle_proc;
    printf("Process created: idle (%d)\n", idle_proc->pid);

    // This will trigger yield when dealing PLIC interrupt, which access idle_proc->table
    // Therefore it must be placed after idle_proc = create_process, otherwise we'll have
    // Store/AMO Access Fault: scause=0x0000000000000007, stval=0x0000000000000008, sepc=0x0000000080200f2e
    virtio_blk_init();
    fs_init();

    printf("USER_BASE is %p\n", reinterpret_cast<void *>(USER_BASE));

    // Create user processes
    auto *proc_a = create_process({ _binary_a_bin_start, reinterpret_cast<size_t>(_binary_a_bin_size) });
    printf("Process created: a (%d)\n", proc_a->pid);

    auto *proc_b = create_process({ _binary_b_bin_start, reinterpret_cast<size_t>(_binary_b_bin_size) });
    printf("Process created: b (%d)\n", proc_b->pid);

    auto *shell = create_process({ _binary_shell_bin_start, reinterpret_cast<size_t>(_binary_shell_bin_size) });
    printf("Process created: shell (%d)\n", shell->pid);

    resume(proc_b, false);
    resume(proc_a, false);
    resume(shell, false);
    reschedule();

    // for (int i = 0; i < 5; ++i) {
    for (;;) {
        // printf("switched to idle process\n");
        asm("wfi");
        reschedule();
    }

    printf("I'm going to bed, bye.\n");
    shutdown();
}

[[using gnu: section(".text.boot"), naked]]
void boot() {
    // Don't use inline asm constraints because the compiler generates preambles before the header maigc
    asm volatile(
        // Boot image header for U-Boot: https://www.kernel.org/doc/html/latest/arch/riscv/boot-image-header.html
        ".option push\n"
        ".option norvc\n"                      // Disable compressed instructions
        "j __after_image_header\n"             // u32 Executable code
        ".option pop\n"                        //
        ".word 0\n"                            // u32 Executable code
        ".dword 0x200000\n"                    // u64 Image load offset (+2MB = 0x80200000)
        ".dword __image_end - __kernel_base\n" // u64 effective Image size (text + rodata + data + bss)
        ".dword 0\n"                           // u64 kernel flags: 0 for LE kernel endian
        ".word 2\n"                            // u32 Version of this header
        ".word 0\n"                            // u32 Reserved
        ".dword 0\n"                           // u64 Reserved
        ".ascii \"RISCV\\x00\\x00\\x00\"\n"    // u64 (8-char) Magic number, little endian, "RISCV"
        ".word 0x05435352\n"                   // u32 Magic number 2, little endian, "RSC\x05"
        ".word 0\n"                            // u32 Reserved for PE COFF offset

        // Actual boot code
        "__after_image_header:\n"
        "la sp, __stack_top\n" // Load stack top address
        "j kernel_main\n"      // Jump to the kernel main function
    );
}
