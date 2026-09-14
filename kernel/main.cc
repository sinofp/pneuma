#include "dev/sbi.h"
#include "fdt/fdt.h"
#include "lib/symbols.h"
#include "mem/mem.h"
#include "proc/proc.h"
#include "proc/qtab.h"
#include "trap/plic.h"
#include "trap/trap.h"

// forward declaration for sbi::hart_start.
extern "C" void boot(xlen_t, const fdt::header *);

// Cold-boot-once flag, serving two phases of the same "first hart" fact:
//   - relocate_self (pre-GOT) reads it via `lla` to apply relocations exactly once;
//   - kernel_main (post-GOT) reads it normally to route secondaries.
volatile int is_cold_boot = 1;

namespace {

int smp_ready = 0;

// static-PIE self-relocation metadata (see kernel.ld).
// The flake's pieGuard asserts every entry is R_RISCV_RELATIVE, so the loop applies them without checking r_info.
struct Elf64_Rela {
    uint64_t r_offset; // Elf64_Addr
    uint64_t r_info;   // Elf64_Xword: symbol<<32 | type
    int64_t r_addend;  // Elf64_Sxword: signed per the gABI
};

constexpr uint64_t LINK_BASE = 0x80200000; // must match kernel.ld __link_base

[[noreturn]]
void kernel_secondary_main() {
    hart::started[hart::id()] = true;
    while (!__atomic_load_n(&smp_ready, __ATOMIC_ACQUIRE)) {
    }
    trap::init_secondary();
    proc::init_idle();
    trap::enable_timer_intr();
    printf("hart %d up\n", static_cast<int>(hart::id()));
    proc::reschedule();
    proc::idle_loop();
}

} // namespace

extern "C" [[noreturn]]
void kernel_main([[maybe_unused]] const xlen_t hartid, const fdt::header *fdt_ptr) {
    if (!is_cold_boot)
        kernel_secondary_main();
    // relocate_self reads this flag pre-GOT to gate the one-time relocation, so it
    // must stay set until here and be cleared before any hart_start below launches a
    // secondary -- otherwise a secondary would re-run the loop and double-apply the bias.
    is_cold_boot = 0;

    memset(__bss, 0, __bss_end - __bss);
    // Whichever hart won OpenSBI's boot lottery becomes primary: it ticks the sleep queue
    // and owns the PLIC S-context for external interrupts.
    hart::primary = hart::id();
    // After the BSS memset above, or the mark would be wiped.
    hart::started[hart::id()] = true;
    printf("hart %d is the primary hart\n", hart::primary);

    fdt::init(fdt_ptr);
    printf("dtb:\n  ptr=%p, plic_base=0x%lx, __free_ram=%p, free_ram_end=0x%lx\n", fdt_ptr,
           static_cast<unsigned long>(plic::start_addr), reinterpret_cast<paddr_t>(__free_ram), free_ram_end);

    trap::init();

    mem::init();

    printf("After mem::init\n");

    proc::init();
    printf("Process created: idle (%d)\n", proc::current().pid);

    // Timer interrupts only after the scheduler exists; a tick before proc::init would
    // preempt into uninitialised queues.
    trap::enable_timer_intr();

    printf("USER_BASE is %p\n", reinterpret_cast<void *>(proc::USER_BASE));

    // Create user servers and apps relying on fixed pid.
    //   PID 1 = fs
    //   PID 2 = blk
    //   PID 3 = virtio-input
    //   PID 4 = virtio-gpu
    //   PID 5 = console
    //   PID 6 = proc-a (both in code and smoke.exp)
    //   PID 7 = proc-b
    auto *fs = proc::create({ _binary_fs_bin_start, reinterpret_cast<size_t>(_binary_fs_bin_size) }, true);
    proc::chprio(fs, proc::prio_t::V);
    printf("Process created: fs (%d)\n", fs->pid);

    auto *blk = proc::create({ _binary_blk_bin_start, reinterpret_cast<size_t>(_binary_blk_bin_size) });
    proc::chprio(blk, proc::prio_t::V);
    printf("Process created: blk (%d)\n", blk->pid);

    auto *virtio_input =
        proc::create({ _binary_virtio_input_bin_start, reinterpret_cast<size_t>(_binary_virtio_input_bin_size) });
    proc::chprio(virtio_input, proc::prio_t::V);
    printf("Process created: virtio-input (%d)\n", virtio_input->pid);

    const bool has_gpu = fdt::has_gpu;
    // Create virtio-gpu regardless has gpu or not, so later apps have fixed pid
    auto *virtio_gpu =
        proc::create({ _binary_virtio_gpu_bin_start, reinterpret_cast<size_t>(_binary_virtio_gpu_bin_size) });
    proc::chprio(virtio_gpu, proc::prio_t::V);
    printf("Process created: virtio-gpu (%d)\n", virtio_gpu->pid);

    auto *console = proc::create({ _binary_console_bin_start, reinterpret_cast<size_t>(_binary_console_bin_size) });
    printf("Process created: console (%d)\n", console->pid);

    auto *proc_a = proc::create({ _binary_a_bin_start, reinterpret_cast<size_t>(_binary_a_bin_size) });
    printf("Process created: a (%d)\n", proc_a->pid);

    auto *proc_b = proc::create({ _binary_b_bin_start, reinterpret_cast<size_t>(_binary_b_bin_size) });
    printf("Process created: b (%d)\n", proc_b->pid);

    auto *libc_test =
        proc::create({ _binary_libc_test_bin_start, reinterpret_cast<size_t>(_binary_libc_test_bin_size) }, true);
    printf("Process created: libc-test (%d)\n", libc_test->pid);

    auto *shell = proc::create({ _binary_shell_bin_start, reinterpret_cast<size_t>(_binary_shell_bin_size) });
    printf("Process created: shell (%d)\n", shell->pid);

    proc::process *doom = nullptr;
    if (has_gpu) {
        doom = proc::create({ _binary_doom_bin_start, reinterpret_cast<size_t>(_binary_doom_bin_size) }, true);
        printf("Process created: doom (%d)\n", doom->pid);
    }

    proc::process *peanut = nullptr;
    if (has_gpu) {
        peanut = proc::create({ _binary_peanut_bin_start, reinterpret_cast<size_t>(_binary_peanut_bin_size) }, true);
        printf("Process created: peanut-gb (%d)\n", peanut->pid);
    }

    // Wake the secondary harts now that every proc slot exists but before the resumes
    for (size_t h = 0; h < hart::MAX; h++) {
        if (hart::id() == h)
            continue;
        if (!hart::startable[h])
            printf(
                "hart %d not startable because it's SiFive S7, or lack of MMU, or has been disabled in device tree\n",
                static_cast<int>(h));
        else if (auto err = sbi::hart_start(h, reinterpret_cast<xlen_t>(&boot), reinterpret_cast<xlen_t>(fdt_ptr));
                 0 != err)
            printf("hart %d not started (SBI error %ld)\n", static_cast<int>(h), err);
    }
    __atomic_store_n(&smp_ready, 1, __ATOMIC_RELEASE);

    // Resume console proc first so user space can use printf
    proc::resume(console);
    proc::resume(blk);
    proc::resume(virtio_input);
    proc::resume(fs);
    proc::resume(libc_test);
    proc::resume(shell);
    if (has_gpu) {
        proc::resume(virtio_gpu);
        proc::resume(doom);
        // proc::resume(peanut);
    } else {
        proc::resume(proc_b);
        proc::resume(proc_a);
    }
    proc::reschedule();

    proc::idle_loop();
}

extern "C" [[gnu::used, noreturn]]
void relocate_self(xlen_t hartid, const fdt::header *fdt) {
    if constexpr (64 == __riscv_xlen) {
        uintptr_t self, rela_start, rela_end, flag;
        asm volatile("lla %0, is_cold_boot"
                     : "=r"(flag));
        if (*reinterpret_cast<volatile int *>(flag)) { // cold hart only
            asm volatile("lla %0, __kernel_base"
                         : "=r"(self));
            const intptr_t delta = self - LINK_BASE; // load bias
            asm volatile("lla %0, __rela_start"
                         : "=r"(rela_start));
            asm volatile("lla %0, __rela_end"
                         : "=r"(rela_end));
            for (auto *r = reinterpret_cast<const Elf64_Rela *>(rela_start);
                 r < reinterpret_cast<const Elf64_Rela *>(rela_end); ++r)
                *reinterpret_cast<uint64_t *>(r->r_offset + delta) = r->r_addend + delta;
        }
    }
    kernel_main(hartid, fdt);
}

[[using gnu: section(".text.boot"), naked]]
void boot([[maybe_unused]] xlen_t hartid, [[maybe_unused]] const fdt::header *fdt_ptr) {
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

        // Actual boot code. Secondary harts enter here too, via sbi::hart_start(&boot, ...):
        // HSM gives them a0 = hartid, a1 = opaque (fdt), satp = 0 -- same as a cold boot.
        "__after_image_header:\n"
        "mv tp, a0\n" // Save the hart id as the first thing!
        "li s0, 0\n"  // RISC-V Calling Convention: The end of the frame record chain is indicated by a NULL pointer
                      // appearing as the next link in the chain
        // lla (PC-relative), not la (GOT-relative): the GOT is not yet relocated here.
        "lla sp, __stack_top\n" // Load stack top address
        "slli t0, a0, 17\n"     // hartid * 128 KiB: each hart gets its own boot-stack slot
        "sub sp, sp, t0\n"
        // a0=hartid, a1=fdt flow in as relocate_self's params.
        // It applies the static-PIE R_RISCV_RELATIVE fixups (rv64; no-op on rv32) then tail-calls kernel_main.
        "j relocate_self\n");
}
