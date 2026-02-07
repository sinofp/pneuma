#include "dev/sbi.h"
#include "dev/virtio.h"
#include "fdt/fdt.h"
#include "fs/file.h"
#include "lib/symbols.h"
#include "mem/mem.h"
#include "proc/proc.h"
#include "proc/qtab.h"
#include "trap/plic.h"
#include "trap/trap.h"

extern "C" void kernel_main([[maybe_unused]] const xlen_t hartid, const fdt::header *fdt_ptr) {
    memset(__bss, 0, __bss_end - __bss);
    fdt::init(fdt_ptr);
    printf("\n\ndtb:\n  ptr=%p, virtio_base=0x%lx, virtio_irq=%d, plic_base=0x%lx, __free_ram=%p, free_ram_end=0x%lx\n",
           fdt_ptr, static_cast<unsigned long>(virtio::blk::base), static_cast<int>(plic::VIRTIO_BLK_IRQ),
           static_cast<unsigned long>(plic::start_addr), reinterpret_cast<paddr_t>(__free_ram), free_ram_end);

    queue::init();
    trap::init();

    mem::init();

    printf("hello %s\n", "world!");
    // %lx should be able to output 0x12345678'ffffffff on RV64 (LP64)
    printf("1 + 2 = %d, 0x%lx, 0x%lx\n", 1 + 2, 0x1234abcd, 0x12345678);

    proc::idle = proc::create({ nullptr, 0 });
    // Set the idle process running manually, so it can be saved to readyq in reschedule
    proc::idle->state = proc::state_t::RUNNING;
    proc::chprio(proc::idle, proc::prio_t::N, false);
    proc::current = proc::idle;
    printf("Process created: idle (%d)\n", proc::idle->pid);

    // This will trigger yield when dealing PLIC interrupt, which access idle_proc->table
    // Therefore it must be placed after idle_proc = create_process, otherwise we'll have
    // Store/AMO Access Fault: scause=0x0000000000000007, stval=0x0000000000000008, sepc=0x0000000080200f2e
    virtio::blk::init();
    fs::ustar::init();

    printf("USER_BASE is %p\n", reinterpret_cast<void *>(proc::USER_BASE));

    // Create user processes
    auto *proc_a = proc::create({ _binary_a_bin_start, reinterpret_cast<size_t>(_binary_a_bin_size) });
    printf("Process created: a (%d)\n", proc_a->pid);

    auto *proc_b = proc::create({ _binary_b_bin_start, reinterpret_cast<size_t>(_binary_b_bin_size) });
    printf("Process created: b (%d)\n", proc_b->pid);

    auto *shell = proc::create({ _binary_shell_bin_start, reinterpret_cast<size_t>(_binary_shell_bin_size) });
    printf("Process created: shell (%d)\n", shell->pid);

    proc::resume(proc_b, false);
    proc::resume(proc_a, false);
    proc::resume(shell, false);
    proc::reschedule();

    // for (int i = 0; i < 5; ++i) {
    for (;;) {
        // printf("switched to idle process\n");
        asm("wfi");
        proc::reschedule();
    }

    printf("I'm going to bed, bye.\n");
    sbi::shutdown();
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
