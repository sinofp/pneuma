#include "dev/virtio-blk.h"
#include "dev/virtio-gpu.h"
#include "dev/virtio-input.h"
#include "fdt/fdt.h"
#include "lib/symbols.h"
#include "mem/mem.h"
#include "proc/proc.h"
#include "proc/qtab.h"
#include "trap/plic.h"
#include "trap/trap.h"

extern "C" void kernel_main([[maybe_unused]] const xlen_t hartid, const fdt::header *fdt_ptr) {
    memset(__bss, 0, __bss_end - __bss);
    fdt::init(fdt_ptr);
    printf("dtb:\n  ptr=%p, plic_base=0x%lx, __free_ram=%p, free_ram_end=0x%lx\n", fdt_ptr,
           static_cast<unsigned long>(plic::start_addr), reinterpret_cast<paddr_t>(__free_ram), free_ram_end);
    for (size_t i = 0; i < fdt::virtio_device_count; i++)
        printf("  virtio device %d: base=0x%lx, irq=%d\n", i, fdt::virtio_devices[i].mmio_base,
               fdt::virtio_devices[i].irq);

    trap::init();

    mem::init();

    printf("After mem::init\n");

    proc::init();
    printf("Process created: idle (%d)\n", proc::current->pid);

    // This will trigger yield when dealing PLIC interrupt, which access idle_proc->table
    // Therefore it must be placed after idle_proc = create_process, otherwise we'll have
    // Store/AMO Access Fault: scause=0x0000000000000007, stval=0x0000000000000008, sepc=0x0000000080200f2e
    virtio::blk::init();
    // Idle was created before blk::init() set base, so its page table lacks the MMIO mapping.
    // Add it now so interrupt handlers can access virtio MMIO while idle is current.
    mem::map_page(proc::current->page_table, virtio::blk::base, virtio::blk::base,
                  mem::pte_t::flag::R | mem::pte_t::flag::W);

    virtio::input::init();
    mem::map_page(proc::current->page_table, virtio::input::base, virtio::input::base,
                  mem::pte_t::flag::R | mem::pte_t::flag::W);

    virtio::gpu::init();
    if (fdt::uninitialised != virtio::gpu::base) {
        mem::map_page(proc::current->page_table, virtio::gpu::base, virtio::gpu::base,
                      mem::pte_t::flag::R | mem::pte_t::flag::W);

        // static uint32_t fb[320 * 200];
        // for (auto &px : fb)
        //     px = 0xFF; // BGRA: blue
        // virtio::gpu::attach(reinterpret_cast<vaddr_t>(fb));
        // virtio::gpu::flush();
    }

    printf("USER_BASE is %p\n", reinterpret_cast<void *>(proc::USER_BASE));

    // Create user processes
    // FS must be PID 1 for libc to work
    auto *fs = proc::create({ _binary_fs_bin_start, reinterpret_cast<size_t>(_binary_fs_bin_size) });
    proc::chprio(fs, proc::prio_t::V, false);
    printf("Process created: fs (%d)\n", fs->pid);

    auto *libc_test =
        proc::create({ _binary_libc_test_bin_start, reinterpret_cast<size_t>(_binary_libc_test_bin_size) });
    printf("Process created: libc-test (%d)\n", libc_test->pid);

    auto *proc_a = proc::create({ _binary_a_bin_start, reinterpret_cast<size_t>(_binary_a_bin_size) });
    printf("Process created: a (%d)\n", proc_a->pid);

    auto *proc_b = proc::create({ _binary_b_bin_start, reinterpret_cast<size_t>(_binary_b_bin_size) });
    printf("Process created: b (%d)\n", proc_b->pid);

    auto *shell = proc::create({ _binary_shell_bin_start, reinterpret_cast<size_t>(_binary_shell_bin_size) });
    printf("Process created: shell (%d)\n", shell->pid);

    proc::process *doom;
    if (fdt::uninitialised != virtio::gpu::base) {
        doom = proc::create({ _binary_doom_bin_start, reinterpret_cast<size_t>(_binary_doom_bin_size) });
        printf("Process created: doom (%d)\n", doom->pid);
    }

    proc::resume(fs, false);
    proc::resume(libc_test, false);
    proc::resume(shell, false);
    if (fdt::uninitialised != virtio::gpu::base) {
        proc::resume(doom, false);
    } else {
        proc::resume(proc_b, false);
        proc::resume(proc_a, false);
    }
    proc::reschedule();

    proc::idle_loop();
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
        "li s0, 0\n" // RISC-V Calling Convention: The end of the frame record chain is indicated by a NULL pointer
                     // appearing as the next link in the chain
        "la sp, __stack_top\n" // Load stack top address
        "j kernel_main\n"      // Jump to the kernel main function
    );
}
