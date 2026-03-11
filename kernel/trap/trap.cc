#include "trap/trap.h"
#include "dev/sbi.h"
#include "dev/virtio-blk.h"
#include "dev/virtio-gpu.h"
#include "lib/debug.h"
#include "proc/proc.h"
#include "proc/qtab.h"
#include "trap/csr.h"
#include "trap/plic.h"

namespace {
constexpr uint64_t timer_interrupt_ms = 5;
constexpr int64_t quantum = 1;
int64_t preempt = quantum;
} // namespace

namespace trap {
[[using gnu: naked, aligned(4)]]
void kernel_entry() {
    asm volatile(
        // clang-format off
        // When handling an exception in kernel, we can't use sscratch as our kernel stack because that's not written.
        // I check Supervisor Previous Privilege (SPP) on wether to use sscrach (saves the kernel_stack for user):
        // 'When a trap is taken, SPP is set to 0 if the trap originated from user mode, or 1 otherwise.'
        "addi sp, sp, -%[xlen_size]\n"
        STORE " t0, (sp)\n"
        "csrr t0, sstatus\n"
        // Get SPP: Supervisor Previous Privilege
        "andi t0, t0, 1 << 8\n"
        "bnez t0, __from_kernel\n"

        // We must not trust the stack pointer at the time of exception.
        // Retrieve the kernel stack of the running process from sscratch.
        "csrrw sp, sscratch, sp\n"
        // sp now points to the kernel (not user) stack of the currently running process.
        // sscratch now holds the original value of sp (user stack) at the time of the exception
        "j __save\n"

        "__from_kernel:\n"
        "csrw sscratch, sp\n"
        // sscratch now holds the original value of sp (kernel stack) at the time of the exception

        "__save:\n"
        "addi sp, sp, -%[size]\n"
        STORE " ra,  %[ra](sp)\n"
        STORE " gp,  %[gp](sp)\n"
        STORE " tp,  %[tp](sp)\n"
        // Remember not to do anything dependent on user t0 when handling trap---I can't see anything requires user t0
        // STORE " t0,  %[t0](sp)\n"
        STORE " t1,  %[t1](sp)\n"
        STORE " t2,  %[t2](sp)\n"
        STORE " t3,  %[t3](sp)\n"
        STORE " t4,  %[t4](sp)\n"
        STORE " t5,  %[t5](sp)\n"
        STORE " t6,  %[t6](sp)\n"
        STORE " a0,  %[a0](sp)\n"
        STORE " a1,  %[a1](sp)\n"
        STORE " a2,  %[a2](sp)\n"
        STORE " a3,  %[a3](sp)\n"
        STORE " a4,  %[a4](sp)\n"
        STORE " a5,  %[a5](sp)\n"
        STORE " a6,  %[a6](sp)\n"
        STORE " a7,  %[a7](sp)\n"
        STORE " s0,  %[s0](sp)\n"
        STORE " s1,  %[s1](sp)\n"
        STORE " s2,  %[s2](sp)\n"
        STORE " s3,  %[s3](sp)\n"
        STORE " s4,  %[s4](sp)\n"
        STORE " s5,  %[s5](sp)\n"
        STORE " s6,  %[s6](sp)\n"
        STORE " s7,  %[s7](sp)\n"
        STORE " s8,  %[s8](sp)\n"
        STORE " s9,  %[s9](sp)\n"
        STORE " s10, %[s10](sp)\n"
        STORE " s11, %[s11](sp)\n"

        // All the GP registers are saved, so we can use them now.
        // I try to use different reg as dst to help pipelining.
        // sepc is very important, because we may switch to the next proc when handling an timer interrupt.
        // If we don't save & restore sepc for current proc, the program might sret to invalid places, leading to random page fault/unkown instruction/unexcepted syscall after 'resuming' current proc.
        "csrr t0, sepc\n"
        STORE " t0,  %[sepc](sp)\n"
        // sstatus is less important than sepc, because it stays the same for most procs.
        // However, I still save & restore it for correctness, as it also affects sret.
        // stval and scause are ignored because they don't affect the program flow for context switching.
        "csrr t1, sstatus\n"
        STORE " t1,  %[sstatus](sp)\n"

        "__call_handle_trap:\n"
        // Retrieve and save the sp at the time of exception.
        "csrr t2, sscratch\n"
        STORE " t2, %[sp](sp)\n"

        // Reset the kernel stack.
        "addi t3, sp, %[size]\n"
        "csrw sscratch, t3\n"

        "mv a0, sp\n"
        "call handle_trap\n"

        "__restore:\n"
        LOAD " t0, %[sepc](sp)\n"
        "csrw sepc, t0\n"
        LOAD " t1, %[sstatus](sp)\n"
        "csrw sstatus, t1\n"
        LOAD " ra,  %[ra](sp)\n"
        LOAD " gp,  %[gp](sp)\n"
        LOAD " tp,  %[tp](sp)\n"
        // LOAD " t0,  %[t0](sp)\n"
        LOAD " t1,  %[t1](sp)\n"
        LOAD " t2,  %[t2](sp)\n"
        LOAD " t3,  %[t3](sp)\n"
        LOAD " t4,  %[t4](sp)\n"
        LOAD " t5,  %[t5](sp)\n"
        LOAD " t6,  %[t6](sp)\n"
        LOAD " a0,  %[a0](sp)\n"
        LOAD " a1,  %[a1](sp)\n"
        LOAD " a2,  %[a2](sp)\n"
        LOAD " a3,  %[a3](sp)\n"
        LOAD " a4,  %[a4](sp)\n"
        LOAD " a5,  %[a5](sp)\n"
        LOAD " a6,  %[a6](sp)\n"
        LOAD " a7,  %[a7](sp)\n"
        LOAD " s0,  %[s0](sp)\n"
        LOAD " s1,  %[s1](sp)\n"
        LOAD " s2,  %[s2](sp)\n"
        LOAD " s3,  %[s3](sp)\n"
        LOAD " s4,  %[s4](sp)\n"
        LOAD " s5,  %[s5](sp)\n"
        LOAD " s6,  %[s6](sp)\n"
        LOAD " s7,  %[s7](sp)\n"
        LOAD " s8,  %[s8](sp)\n"
        LOAD " s9,  %[s9](sp)\n"
        LOAD " s10, %[s10](sp)\n"
        LOAD " s11, %[s11](sp)\n"
        LOAD " sp,  %[sp](sp)\n"

        // Restore the real user t0, overwritten because we wanted to load sstatus.SPP before saving all registers
        LOAD " t0, (sp)\n"
        STORE " zero, (sp)\n"
        "addi sp, sp, %[xlen_size]\n"

        // pc = sepc
        // sstatus.SIE = sstatus.SPIE
        // Mode = sstatus.SPP
        "sret\n"
        // clang-format on
        :
        : [size] "i"(static_cast<xlen_t>(sizeof(frame))), [ra] "i"(offsetof(frame, ra)), [gp] "i"(offsetof(frame, gp)),
          [tp] "i"(offsetof(frame, tp)), [t0] "i"(offsetof(frame, t0)), [t1] "i"(offsetof(frame, t1)),
          [t2] "i"(offsetof(frame, t2)), [t3] "i"(offsetof(frame, t3)), [t4] "i"(offsetof(frame, t4)),
          [t5] "i"(offsetof(frame, t5)), [t6] "i"(offsetof(frame, t6)), [a0] "i"(offsetof(frame, a0)),
          [a1] "i"(offsetof(frame, a1)), [a2] "i"(offsetof(frame, a2)), [a3] "i"(offsetof(frame, a3)),
          [a4] "i"(offsetof(frame, a4)), [a5] "i"(offsetof(frame, a5)), [a6] "i"(offsetof(frame, a6)),
          [a7] "i"(offsetof(frame, a7)), [s0] "i"(offsetof(frame, s0)), [s1] "i"(offsetof(frame, s1)),
          [s2] "i"(offsetof(frame, s2)), [s3] "i"(offsetof(frame, s3)), [s4] "i"(offsetof(frame, s4)),
          [s5] "i"(offsetof(frame, s5)), [s6] "i"(offsetof(frame, s6)), [s7] "i"(offsetof(frame, s7)),
          [s8] "i"(offsetof(frame, s8)), [s9] "i"(offsetof(frame, s9)), [s10] "i"(offsetof(frame, s10)),
          [s11] "i"(offsetof(frame, s11)), [sp] "i"(offsetof(frame, sp)), [sepc] "i"(offsetof(frame, sepc)),
          [sstatus] "i"(offsetof(frame, sstatus)), [xlen_size] "i"(static_cast<xlen_t>(sizeof(xlen_t))));
}
} // namespace trap

namespace {

void handle_syscall(trap::frame *f) {
    switch (f->a3) {
    case SYS_PUTCHAR:
        putchar(f->a0);
        break;
    case SYS_GETCHAR:
        for (;;) {
            long ch = getchar();
            if (ch != EOF) {
                f->a0 = ch;
                break;
            }
            // Give interact process higher priority
            // chprio(current_proc, ProcPrio::V);
        }
        break;
    case SYS_EXIT:
        printf("process %d exited\n", proc::current->pid);
        proc::destroy(proc::current);
        proc::current->state = proc::state_t::UNUSED;
        proc::reschedule();
        PANIC("unreachable");
    case SYS_DISK_READ:
    case SYS_DISK_WRITE: {
        size_t sector = f->a0;
        auto *buf = reinterpret_cast<char *>(f->a1);

        // Validate sector is within disk capacity
        if (sector >= virtio::blk::capacity) {
            printf("disk: sector %lld out of range (capacity: %lld)\n", sector, virtio::blk::capacity);
            f->a0 = static_cast<xlen_t>(-1);
            break;
        }

        // Validate user buffer address
        if (reinterpret_cast<vaddr_t>(buf) < proc::USER_BASE) {
            printf("disk: invalid buffer address 0x%lx\n", f->a1);
            f->a0 = static_cast<xlen_t>(-1);
            break;
        }

        int is_write = (SYS_DISK_WRITE == f->a3) ? 1 : 0;
        virtio::blk::read_write(buf, sector, is_write);
        f->a0 = 0; // Success
    } break;
    case SYS_SLEEP:
        proc::sleep_ms(f->a0);
        break;
    case SYS_DISK_IOCTL: {
        switch (f->a0) {
        case DISK_IOCTL_GET_SECTOR_COUNT:
            f->a0 = static_cast<xlen_t>(virtio::blk::capacity);
            break;
        case DISK_IOCTL_GET_SECTOR_SIZE:
            f->a0 = 512; // Standard sector size
            break;
        case DISK_IOCTL_GET_BLOCK_SIZE:
            f->a0 = 1; // Block erase granularity in sectors
            break;
        default:
            f->a0 = static_cast<xlen_t>(-1);
            break;
        }
    } break;
    case SYS_GET_TICKS:
        f->a0 = csr::time::read_ms();
        break;
    case SYS_GPU_ATTACH:
        virtio::gpu::attach(f->a0);
        break;
    case SYS_GPU_FLUSH:
        virtio::gpu::flush();
        break;
    default:
        PANIC("unexpected syscall a3=0x%lx\n", f->a3);
    }
}

inline void set_next_timer_interrupt() { sbi::set_timer(csr::time::read() + timer_interrupt_ms * fdt::clk_freq_ms); }

} // namespace

extern "C" void handle_trap(trap::frame *f) {
    auto scause = csr::scause::read();
    auto stval = csr::stval::read();

    using enum csr::scause_enc;
    switch (scause.encoding()) {
    case UEcall:
        handle_syscall(f);
        // Advance an instruction to skip the ecall
        f->sepc += 4; // Instructions are always 4 bytes, even on RV64
        break;
    case STimerIntr: {
        set_next_timer_interrupt();
        // printf("Time: %ldms!\n", static_cast<long>(csr::time::read_ms()));
        auto sleepq = sleep_queue();
        if (!sleepq.empty()) {
            // printf("Adjusting sleeping delta list\n");
            sleepq.first_key() -= timer_interrupt_ms;
            if (sleepq.first_key() <= 0)
                proc::wakeup();
        }
        if (--preempt <= 0 and !ready_queue().empty()) {
            // printf("Preempt scheduling\n");
            preempt = quantum;
            proc::reschedule();
        }
    } break;
    case SExternalIntr: {
        // External interrupt from PLIC
        if (auto irq = plic::claim(0)) {
            if (irq == virtio::blk::irq)
                virtio::blk::handle_io_completion();
            else
                printf("Unexpected IRQ: %d\n", irq);
            plic::complete(0, irq);
        }
        break;
    }
    case IllegalInst:
        PANIC("Illegal Instruction: stval=0x%lx, sepc=0x%lx\n", stval, f->sepc);
        break;
    case LoadAddrMisaligned:
    case StoreAmoAddrMisaligned:
        PANIC("Address Misaligned: scause=0x%lx, stval=0x%lx, sepc=0x%lx\n", scause, stval, f->sepc);
        break;
    case LoadAccessFault:
        PANIC("Load Access Fault: scause=0x%lx, stval=0x%lx, sepc=0x%lx\n", scause, stval, f->sepc);
        break;
    case StoreAmoAccessFault:
        PANIC("Store/AMO Access Fault: scause=0x%lx, stval=0x%lx, sepc=0x%lx\n", scause, stval, f->sepc);
        break;
    case LoadPageFault:
    case StoreAmoPageFault:
        PANIC("Load/Store Page Fault (check memory mapping!): scause=0x%lx, stval=0x%lx, sepc=0x%lx\n", scause, stval,
              f->sepc);
        break;
    case InstPageFault:
        PANIC(
            "Instruction Page Fault (check memory mapping/context switching!): scause=0x%lx, stval=0x%lx, sepc=0x%lx\n",
            scause, stval, f->sepc);
        break;
    default:
        PANIC("Unexpected trap: scause=0x%lx, stval=0x%lx, sepc=0x%lx\n", scause, stval, f->sepc);
    }
}

namespace trap {

void init() {
    using namespace csr;

    // This is super important, without setting stvec, sret won't take us to sepc.
    stvec{ kernel_entry, stvec::Mode::Direct }.write();

    // Set next timer interrupt (mtimecmp) before enabling timer interrupt
    set_next_timer_interrupt();

    // Initialize PLIC for external interrupts
    plic::init();

    // Enable supervisor timer and external interrupts
    sie::read().set(sie_enc::STIE).set(sie_enc::SEIE).write();
    sstatus::read().set(sstatus_enc::SIE).write();
}

} // namespace trap
