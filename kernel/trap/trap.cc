#include "trap/trap.h"
#include "dev/sbi.h"
#include "lib/debug.h"
#include "lib/hart.h"
#include "proc/proc.h"
#include "proc/qtab.h"
#include "trap/csr.h"
#include "trap/driver.h"
#include "trap/plic.h"

namespace {
constexpr uint64_t timer_interrupt_ms = 5;
constexpr int64_t quantum = 1;
int64_t preempts[hart::MAX]; // per-hart quantum countdown; zero-init preempts on the first tick
} // namespace

namespace trap {
// User code is not compiled with -ffixed-x4, so we use template to save the hart id.
template <size_t HARTID>
[[using gnu: naked, aligned(4)]]
void kernel_entry() {
    asm volatile(
        // clang-format off
        // When handling an exception in kernel, we can't use sscratch as our kernel stack because that's not written.
        // I check Supervisor Previous Privilege (SPP) on wether to use sscratch (saves the kernel_stack for user):
        // 'When a trap is taken, SPP is set to 0 if the trap originated from user mode, or 1 otherwise.'
        "addi sp, sp, -%[xlen_size]\n"
        STORE " t0, (sp)\n"
        "csrr t0, sstatus\n"
        // Get SPP: Supervisor Previous Privilege
        "andi t0, t0, 1 << 8\n"
        "bnez t0, __from_kernel%=\n"

        // We must not trust the stack pointer at the time of exception.
        // Retrieve the kernel stack of the running process from sscratch.
        "csrrw sp, sscratch, sp\n"
        // sp now points to the kernel (not user) stack of the currently running process.
        // sscratch now holds the original value of sp (user stack) at the time of the exception
        "j __save%=\n"

        "__from_kernel%=:\n"
        "csrw sscratch, sp\n"
        // sscratch now holds the original value of sp (kernel stack) at the time of the exception

        "__save%=:\n"
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

        // The user tp is in the frame now; from here the kernel owns tp.
        "li tp, %[hartid]\n"

        "__call_handle_trap%=:\n"
        // Retrieve and save the sp at the time of exception.
        "csrr t2, sscratch\n"
        STORE " t2, %[sp](sp)\n"

        // Reset the kernel stack.
        "addi t3, sp, %[size]\n"
        "csrw sscratch, t3\n"

        // Pass the trap::frame* to handle_trap
        "mv a0, sp\n"

        // Setup a frame pointer and return address pair that links to the panic-ed frame
        "addi sp, sp, -%[xlen_size]-%[xlen_size]\n"
        // Save the previous fp
        STORE " fp, (sp)\n"
        "csrr t0, sepc\n"
        // Point fp to 'the stack pointer value on entry to the current procedure', which was saved to a0 earlier
        "mv fp, a0\n"
        // Save 'return address' as-if there's no exception.
        // Should I do this? There must be something wrong if we have an exception.
        // However, I need do this to link the backtrace.
        STORE " t0, %[xlen_size](sp)\n"

        "call handle_trap\n"

        "__restore%=:\n"
        // Pop the prev fp and ra
        "addi sp, sp, %[xlen_size]+%[xlen_size]\n"
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
          [sstatus] "i"(offsetof(frame, sstatus)), [xlen_size] "i"(static_cast<xlen_t>(sizeof(xlen_t))),
          [hartid] "i"(HARTID));
}
} // namespace trap

namespace {

void handle_syscall(trap::frame *f) {
    switch (f->a3) {
    case SYS_PUTCHAR: {
        // TODO create a SYS_PUTS.
        // Currently the console log only make sure kernel printf don't interleave with user
        lock::guard g{ lock::console };
        putchar(f->a0);
        break;
    }
    case SYS_GETCHAR:
        for (;;) {
            long ch = getchar();
            if (ch != EOF) {
                f->a0 = ch;
                break;
            }

            // Let other ready procs run for a brief moment
            proc::sleep_ms(1);
        }
        break;
    case SYS_EXIT: {
        printf("process %d exited\n", proc::current().pid);
        // Park as EXITED and wait until we change satp to recyle the PCB
        csr::intr_guard ig;
        lock::guard g{ lock::bkl };
        proc::current().state = proc::state_t::EXITED;
        proc::reschedule(g);
        PANIC("unreachable");
    }
    case SYS_SLEEP:
        proc::sleep_ms(f->a0);
        break;
    case SYS_GET_TICKS:
        f->a0 = csr::time::read_ms();
        break;
    case SYS_MP_SSEND:
        // printf("\n\n\nSsend(addr=%p, addr[0]=%d, recipient=%ld)\n", f->a0, *reinterpret_cast<uint8_t *>(f->a0),
        // f->a2);
        f->a0 = static_cast<xlen_t>(proc::message_passing({ reinterpret_cast<uint8_t *>(f->a0), f->a1 }, f->a2, true));
        break;
    case SYS_MP_RECV:
        // printf("\n\n\nRecv(addr=%p, addr[0]=%d, sender=%ld)\n", f->a0, *reinterpret_cast<uint8_t *>(f->a0), f->a2);
        f->a0 = static_cast<xlen_t>(proc::message_passing({ reinterpret_cast<uint8_t *>(f->a0), f->a1 }, f->a2, false));
        break;
    case SYS_FDT_VIRTIO:
        f->a0 = driver::sys_fdt_virtio(f->a0, f->a1);
        break;
    case SYS_MMIO_MAP:
        f->a0 = driver::sys_mmio_map(f->a0, f->a1);
        break;
    case SYS_DMA_ALLOC:
        f->a0 = driver::sys_dma_alloc(f->a0);
        break;
    case SYS_IRQ_CLAIM:
        f->a0 = driver::sys_irq_claim(static_cast<uint32_t>(f->a0));
        break;
    case SYS_IRQ_WAIT:
        f->a0 = driver::sys_irq_wait(static_cast<uint32_t>(f->a0));
        break;
    case SYS_PAGE_WALK:
        f->a0 = driver::sys_page_walk(f->a0, f->a1, f->a2);
        break;
    case SYS_REBOOT:
        printf("system reset requested by process %d\n", proc::current().pid);
        sbi::system_reset(1);
        PANIC("unreachable");
    default:
        PANIC("unexpected syscall a3=0x%lx\n", f->a3);
    }
}

inline void set_next_timer_interrupt() { sbi::set_timer(csr::time::read() + timer_interrupt_ms * fdt::clk_freq_ms); }

// Fatal-trap report: everything a serial-only board can give us. Registers come
// from the saved frame; the backtrace walks the *trapped* context's fp (f->s0),
// which the bounds checks reject when it's a user-space fp.
[[noreturn]]
void fatal_trap(trap::frame *f, csr::scause scause, sxlen_t stval, const char *what) {
    panic::prologue();
    xlen_t satp;
    asm volatile("csrr %0, satp"
                 : "=r"(satp));
    printf_unlocked("FATAL TRAP: %s\n", what);
    printf_unlocked("  scause=0x%lx stval=0x%lx sepc=0x%lx sstatus=0x%lx satp=0x%lx\n", static_cast<sxlen_t>(scause),
                    stval, f->sepc, f->sstatus, satp);
    printf_unlocked("  ra=0x%lx sp=0x%lx gp=0x%lx tp=0x%lx\n", f->ra, f->sp, f->gp, f->tp);
    printf_unlocked("  t0=0x%lx t1=0x%lx t2=0x%lx t3=0x%lx t4=0x%lx t5=0x%lx t6=0x%lx\n", f->t0, f->t1, f->t2, f->t3,
                    f->t4, f->t5, f->t6);
    printf_unlocked("  a0=0x%lx a1=0x%lx a2=0x%lx a3=0x%lx a4=0x%lx a5=0x%lx a6=0x%lx a7=0x%lx\n", f->a0, f->a1, f->a2,
                    f->a3, f->a4, f->a5, f->a6, f->a7);
    printf_unlocked("  s0=0x%lx s1=0x%lx s2=0x%lx s3=0x%lx s4=0x%lx s5=0x%lx\n", f->s0, f->s1, f->s2, f->s3, f->s4,
                    f->s5);
    printf_unlocked("  s6=0x%lx s7=0x%lx s8=0x%lx s9=0x%lx s10=0x%lx s11=0x%lx\n", f->s6, f->s7, f->s8, f->s9, f->s10,
                    f->s11);
    proc::backtrace(f->s0);
    sbi::system_reset();
}

} // namespace

extern "C" void handle_trap(trap::frame *f) {
    // Maybe the panicking hart's send_ipi take us here, or maybe it's just our own timer interrupt.
    // So it's better to be before the switch-case
    panic::park_if_panicking();

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
        bool preempt_due = --preempts[hart::id()] <= 0;
        if (preempt_due)
            preempts[hart::id()] = quantum;
        proc::timer_tick(timer_interrupt_ms, preempt_due);
    } break;
    case SSoftwareIntr: // IPI
        csr::sip::clear_ssip();
        proc::reschedule();
        break;
    case SExternalIntr: {
        // External interrupt from PLIC. All virtio IRQs are owned by user-space drivers now:
        // mark pending, wake the owner if it's blocked, defer plic::complete to the owner's next wait call.
        lock::guard g{ lock::bkl }; // SIE already 0 in trap context
        if (const auto irq = plic::claim()) {
            if (auto pid = driver::owner(irq); 0 <= pid) {
                driver::mark_pending(irq);
                // Switch to the (typically higher-priority) driver now instead of waiting for
                // the next 5 ms preempt tick — only when we actually made it ready.
                if (driver::wake_if_waiting(&proc::procs[pid], irq, g))
                    proc::reschedule(g);
            } else {
                printf_unlocked("Unexpected IRQ: %d\n", irq); // bkl held: printf's console lock nests fine, but
                                                              // keep the rare error path lock-free
                plic::complete(irq);
            }
        }
        break;
    }
    case IllegalInst:
        fatal_trap(f, scause, stval, "Illegal Instruction");
    case LoadAddrMisaligned:
    case StoreAmoAddrMisaligned:
        fatal_trap(f, scause, stval, "Address Misaligned");
    case LoadAccessFault:
        fatal_trap(f, scause, stval, "Load Access Fault");
    case StoreAmoAccessFault:
        fatal_trap(f, scause, stval, "Store/AMO Access Fault");
    case LoadPageFault:
    case StoreAmoPageFault:
        fatal_trap(f, scause, stval, "Load/Store Page Fault (check memory mapping!)");
    case InstPageFault:
        fatal_trap(f, scause, stval, "Instruction Page Fault (check memory mapping/context switching!)");
    default:
        fatal_trap(f, scause, stval, "Unexpected trap");
    }
}

namespace trap {

constexpr void (*kernel_entries[])(){ kernel_entry<0>, kernel_entry<1>, kernel_entry<2>, kernel_entry<3>,
                                      kernel_entry<4> };
static_assert(hart::MAX == sizeof(kernel_entries) / sizeof(kernel_entries[0]),
              "one kernel_entry instantiation per hart");

void init() {
    using namespace csr;

    // This is super important, without setting stvec, sret won't take us to sepc.
    // Each hart gets its own instantiation so trap entry can re-materialise its hartid.
    stvec{ kernel_entries[hart::id()], stvec::Mode::Direct }.write();

    // Initialize PLIC for external interrupts: this (boot) hart's S-mode context takes them
    // all; OpenSBI's lottery may pick a hart != 0, so the context is computed, not assumed.
    plic::init(hart::id());

    // Enable supervisor software (IPI), timer, and external interrupt sources; delivery stays
    // off until enable_timer_intr() sets sstatus.SIE, after the scheduler structures exist.
    sie::read().set(sie_enc::SSIE).set(sie_enc::STIE).set(sie_enc::SEIE).write();
}

void init_secondary() {
    using namespace csr;

    stvec{ kernel_entries[hart::id()], stvec::Mode::Direct }.write();

    // IPIs and our own timer only. No SEIE and no plic::init: external interrupts are the
    // primary hart's business.
    sie::read().set(sie_enc::SSIE).set(sie_enc::STIE).write();
}

void enable_timer_intr() {
    using namespace csr;

    // Arm the one-shot timer before turning interrupt delivery on. Doing this before
    // proc::init would let a tick preempt into scheduler structures that don't exist yet.
    set_next_timer_interrupt();
    sstatus::read().set(sstatus_enc::SIE).write();
}

} // namespace trap
