#include "proc/proc.h"
#include "dev/sbi.h"
#include "fdt/fdt.h"
#include "lib/debug.h"
#include "lib/lock.h"
#include "mem/mem.h"
#include "proc/qtab.h"
#include "trap/csr.h"
#include "trap/plic.h"

namespace proc {
process procs[MAX];           // All process control structures.
process *currents[hart::MAX]; // Currently running process, per hart
process *idles[hart::MAX];    // Per-hart idle process
qent qtab[NQENT];
static_assert(sizeof(qtab) < mem::PAGE_SIZE, "qtab is larger than one frame, should be allocated by alloc_pages");
} // namespace proc

using enum proc::state_t;

namespace {

// We only execute this function at the beginning of a user process.
// So we don't care the 'restored' registers, because they're all placeholders allocated in create_process,
// only saved and loaded to use the same context_switch for process already running.
// Therefore, this function don't have to be naked, like we don't have to zero the callee saved registers for new
// process in create_process
// What's more, "r" already overwrites a register to load USER_BASE.
// The bkl is NOT held here: the switch_context that dispatched us released it in asm.
void user_entry() {
    using enum csr::sstatus_enc;
    asm volatile("csrw sepc, %[sepc]        \n"
                 "csrw sstatus, %[sstatus]  \n"
                 "sret                      \n"
                 :
                 : [sepc] "r"(proc::USER_BASE), [sstatus] "r"(csr::sstatus::read()
                                                                  .unset(SPP) // Not needed, but I like explicit
                                                                  .set(SUM)
                                                                  .raw()));
}

struct [[gnu::packed]] reg_ctx {
    xlen_t ra;
    xlen_t s0;
    xlen_t s1;
    xlen_t s2;
    xlen_t s3;
    xlen_t s4;
    xlen_t s5;
    xlen_t s6;
    xlen_t s7;
    xlen_t s8;
    xlen_t s9;
    xlen_t s10;
    xlen_t s11;
};

} // namespace

namespace proc {
void init() {
    // csr::intr_guard g;

    // Initialize each queue's head/tail pair
    for (id_t head = MAX; head < NQENT; head += 2) {
        id_t tail = head + 1;

        // The MINKEY at tail is used to ensure the descending priority queue can find a place to insert.
        qtab[head] = { {}, tail, {} };
        qtab[tail] = { MINKEY, {}, head };
    }

    init_idle();
    printf("After creating idle proc\n");
}

// Create this hart's idle process and adopt it as the running one. The idle proc's saved
// context is a placeholder: the hart keeps executing on its boot stack until the first
// switch away overwrites idle->sp with the live value.
void init_idle() {
    auto *idle = create({ nullptr, 0 });
    idle->state = state_t::RUNNING;
    chprio(idle, prio_t::N);
    idles[hart::id()] = idle;
    currents[hart::id()] = idle;
}

[[noreturn]]
void idle_loop() {
    for (;;) {
        // printf("switched to idle process\n");
        asm("wfi");
        proc::reschedule();
    }

    printf("I'm going to bed, bye.\n");
    sbi::shutdown();
}

// TODO move to debug.h?
void backtrace() {
    if (auto pid = current().pid; bad_id(pid))
        printf_unlocked("backtrace: fault happened before proc::init\n");
    else
        printf_unlocked("backtrace pid=%d:\n", current().pid);
    auto fp = reinterpret_cast<xlen_t>(__builtin_frame_address(0));
    printf_unlocked("```zsh\n for addr (\n");
    while (fp) {
        // RISC-V Calling Contenvion: The previous frame pointer and return address pair will reside just prior to the
        // current stack address held in fp. This puts the return address at fp - PTRSZ/8, and the previous frame
        // pointer at fp - 2 * PTRSZ/8.
        auto ra = reinterpret_cast<xlen_t *>(fp)[-1];
        auto prev_fp = reinterpret_cast<xlen_t *>(fp)[-2];
        printf_unlocked("    %p\n", ra - 4); // Theoretically I should also take care of compressed inst.
        fp = prev_fp;
    }
    // TODO save elf names for procs
    printf_unlocked(")\n    llvm-symbolizer --obj=result-rv64/kernel.elf $addr\n```\n");
}

process *create(span<const uint8_t> image, bool allocate_heap) {
    // bkl covers the slot scan and every alloc_frame below. Only called at boot today, but
    // uncontended acquisition is cheap and mem::alloc_frame asserts the lock is held.
    csr::intr_guard ig;
    lock::guard g{ lock::bkl };

    // Find an unused process control structure.
    process *proc = nullptr;
    size_t i;
    for (i = 0; i < MAX; ++i)
        if (procs[i].state == UNUSED) {
            proc = &procs[i];
            break;
        }

    if (!proc) [[unlikely]]
        PANIC("no free process slots");

    // Stack callee-saved registers. These register values will be restored in
    // the first context switch in switch_context.
    auto *sp = reinterpret_cast<decltype(proc->sp)>(&proc->stack[sizeof(proc->stack)]); // the stack grows downward
    // Make space for reg_ctx. The number of registers of RV32 and RV64 are the same
    constexpr size_t stack_length = sizeof(reg_ctx) / sizeof(xlen_t);
    static_assert(13 == stack_length, "reg_ctx size donesn't equal to the number of regs");
    sp -= stack_length;

    auto *ctx = reinterpret_cast<reg_ctx *>(sp);
    // *ctx = {};
    ctx->ra = reinterpret_cast<xlen_t>(user_entry);

    using enum mem::pte_t::flag;

    // Identical mapping for kernel pages, prepared for user's syscalls.
    // Map everything from __kernel_base to __free_ram_end.
    // This approach ensures that the kernel can always access both statically allocated areas (like .text), and
    // dynamically allocated areas managed by mem::alloc_frame
    mem::pt_t page_table{ mem::alloc_frame() };
    for (vaddr_t kaddr = reinterpret_cast<vaddr_t>(__kernel_base); kaddr < static_cast<vaddr_t>(free_ram_end);
         kaddr += mem::PAGE_SIZE)
        mem::map_page(page_table, kaddr, kaddr, R | W | X);

    // Map PLIC MMIO TODO skip Hart 0 M-Mode ranges
    for (paddr_t plic_addr = plic::start_addr; plic_addr < plic::end_addr; plic_addr += mem::PAGE_SIZE)
        mem::map_page(page_table, plic_addr, plic_addr, R | W);

    // Map user image pages.
    for (auto offset = 0uz; offset < image.size(); offset += mem::PAGE_SIZE) {
        paddr_t page = mem::alloc_frame();

        // Fill and map the page.
        auto chunk = image.subspan(offset, min(mem::PAGE_SIZE, image.size() - offset));
        memcpy((void *) page, chunk.data(), chunk.size());
        mem::map_page(page_table, USER_BASE + offset, page, U | R | W | X);
    }

    // Map user heap pages 'on demand' to save space
    if (allocate_heap) {
        vaddr_t heap_base = align_up(USER_BASE + image.size(), mem::PAGE_SIZE);
        for (auto offset = 0uz; offset < 8 * 1024 * 1024; offset += mem::PAGE_SIZE)
            mem::map_page(page_table, heap_base + offset, mem::alloc_frame(), U | R | W | X);
    }

    // Initialize fields.
    // Can't construct and swap because the uint8_t stack[];
    proc->pid = i; // TODO pid is always its index in procs. Consider removing?
    // MLFQ rule 3: Put process in the highest queue when created
    // I give it to the second highest because I'll give interactive program (shell) the highest.
    // Although that means only the shell takes the CPU forever.
    proc->prio = proc::prio_t::IV; // default prio
    proc->sp = sp;
    proc->state = SUSPENDED;
    proc->page_table = page_table;
    return proc;
}

} // namespace proc

namespace {

// No matter what's the type of process::sp, we want the pointer to the prev_sp, and the value of next_sp.
// I use template here because I wasn't sure what's the best type for process::sp: xlen_t, xlen_t *, vaddr_t, or vaddr_t
// *?
template <typename T>
[[gnu::naked]]
void switch_context(T *prev_sp, T next_sp, int *bkl_locked, size_t *bkl_holder) {
    asm volatile(
        // clang-format off
        // Save callee-saved registers onto the current process's stack.
        "addi sp, sp, -%[size]\n"
        // This ra is the return address back into the caller (reschedule)
        STORE " ra,  %[ra](sp)\n"
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

        // Switch the stack pointer.
        STORE " sp, (a0)\n" // *prev_sp = sp;

        // Release the bkl: holder = NO_HOLDER, then amoswap.w.rl so the holder write (and
        // everything before the commit) is ordered before the lock release. From here on
        // another hart may pick prev up; we no longer touch prev's stack memory.
        "li t0, -1\n"
        STORE " t0, (a3)\n"
        "amoswap.w.rl x0, x0, (a2)\n"

        "mv sp, a1\n"       // Switch stack pointer (sp) here

        // Restore callee-saved registers from the next process's stack.
        // Loads the return address of next process (its reschedule)
        LOAD " ra,  %[ra](sp)\n"
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
        "addi sp, sp, %[size]\n" // Pop restored registers from stack

        // Return to next process' caller (pc = ra)
        "ret\n"
        // clang-format on
        :
        : [size] "i"(static_cast<xlen_t>(sizeof(reg_ctx))), [ra] "i"(offsetof(reg_ctx, ra)),
          [s0] "i"(offsetof(reg_ctx, s0)), [s1] "i"(offsetof(reg_ctx, s1)), [s2] "i"(offsetof(reg_ctx, s2)),
          [s3] "i"(offsetof(reg_ctx, s3)), [s4] "i"(offsetof(reg_ctx, s4)), [s5] "i"(offsetof(reg_ctx, s5)),
          [s6] "i"(offsetof(reg_ctx, s6)), [s7] "i"(offsetof(reg_ctx, s7)), [s8] "i"(offsetof(reg_ctx, s8)),
          [s9] "i"(offsetof(reg_ctx, s9)), [s10] "i"(offsetof(reg_ctx, s10)), [s11] "i"(offsetof(reg_ctx, s11)));
}

} // namespace

namespace proc {

void ready(process *proc, lock::guard &) {
    proc->state = READY;
    proc::ready_queue().enqueue(proc->pid, static_cast<qkey_t>(proc->prio));

    // Kick a hart running lower prio (probably idle), skipping the quantum for it to reschedule.
    size_t target = hart::MAX;
    auto lowest = proc->prio;
    for (size_t h = 0; h < hart::MAX; h++) {
        if (hart::id() == h or nullptr == currents[h])
            continue;
        if (currents[h]->prio < lowest) {
            lowest = currents[h]->prio;
            target = h;
        }
    }
    if (hart::MAX != target)
        sbi::send_ipi(static_cast<xlen_t>(1) << target, 0);
}

void ready(process *proc) {
    csr::intr_guard ig;
    lock::guard g{ lock::bkl };
    ready(proc, g);
}

void chprio(process *proc, proc::prio_t prio) {
    csr::intr_guard ig;
    lock::guard g{ lock::bkl };
    proc->prio = prio;
    if (READY == proc->state) {
        proc::ready_queue().remove(proc->pid);
        proc::ready_queue().enqueue(proc->pid, static_cast<qkey_t>(proc->prio));
    }
}

void resume(process *proc) {
    csr::intr_guard ig;
    lock::guard g{ lock::bkl };
    if (proc->state != SUSPENDED)
        return;
    ready(proc, g);
}

void sleep_ms(int32_t delay) {
    if (delay < 0)
        return;
    csr::intr_guard ig;
    lock::guard g{ lock::bkl };
    auto &curr = current();
    if (!delay) {
        ready(&curr, g);
        reschedule(g);
        return;
    }
    proc::sleep_queue().enqueue(curr.pid, delay);
    curr.state = proc::state_t::SLEEPING;
    reschedule(g);
}

void timer_tick(int64_t elapsed_ms, bool preempt_due) {
    // Called from the timer trap: SIE is already 0, so the bkl alone suffices.
    lock::guard g{ lock::bkl };

    bool woke = false;
    // Only the primary hart ticks the sleep queue
    if (hart::primary == hart::id()) {
        auto sleepq = proc::sleep_queue();
        if (!sleepq.empty()) {
            sleepq.first_key() -= elapsed_ms;
            while (!sleepq.empty() and sleepq.first_key() <= 0) {
                // Given that the queue is nonempty, dequeue must have value
                // TODO unify the argument/return value: use either pid or procs
                ready(&procs[sleepq.dequeue().value()], g);
                woke = true;
            }
        }
    }

    if (woke or (preempt_due and !proc::ready_queue().empty()))
        reschedule(g);
}

id_t message_passing(span<uint8_t> buffer, id_t rank, bool send) {
    csr::intr_guard ig;
    lock::guard g{ lock::bkl };

    if ((-1 != rank and !send) and bad_id(rank))
        return -1;
    using enum state_t;

    // curr stays valid across reschedule: this code runs on curr's own kernel stack.
    auto &curr = current();
    bool needs_resched = true;

    if (send) {
        auto &recipient = procs[rank];
        if (RECEIVING == recipient.state and (-1 == recipient.sender or curr.pid == recipient.sender)) {
            auto length = min(buffer.size(), recipient.msg_buffer.size());
            mem::cross_proc_memcpy(recipient.page_table, reinterpret_cast<vaddr_t>(recipient.msg_buffer.data()),
                                   curr.page_table, reinterpret_cast<vaddr_t>(buffer.data()), length);
            recipient.sender = curr.pid; // so a recipient with sender=-1 learns the real sender on resume
            ready(&recipient, g);

            // Didn't observe any performance gain. Might delete
            needs_resched = curr.prio < recipient.prio;
        } else {
            curr.state = SENDING;
            curr.msg_buffer = buffer;
            curr.recipient = rank;

            send_queue().enqueue(curr.pid, static_cast<qkey_t>(curr.prio));
        }

    } else {
        bool matched = false;
        auto sq = send_queue();
        for (auto it = sq.begin(); it != sq.end(); ++it) {
            id_t i = it.curr;
            if (auto &sender = procs[i]; curr.pid == sender.recipient and (-1 == rank or rank == i)) {
                auto length = min(buffer.size(), sender.msg_buffer.size());
                mem::cross_proc_memcpy(curr.page_table, reinterpret_cast<vaddr_t>(buffer.data()), sender.page_table,
                                       reinterpret_cast<vaddr_t>(sender.msg_buffer.data()), length);
                curr.sender = i;
                sq.remove(i);
                ready(&sender, g);

                matched = true;
                needs_resched = curr.prio < sender.prio;
                break;
            }
        }
        if (!matched) {
            curr.state = RECEIVING;
            curr.msg_buffer = buffer;
            curr.sender = rank;
        }
    }

    if (needs_resched)
        reschedule(g);

    // If we reach here, we must have found our peer. Reading curr.sender without the bkl
    // (released across the switch) is safe: it's our own field, written by the peer before it
    // readied us, ordered by the peer's lock release and our hart's acquire.
    // rank>=0: peer is the rank we asked for. rank==-1 (ANY_SOURCE recv): peer id was written to
    // curr.sender either by the fast-path match above, or by a late sender's fast-path after we blocked.
    return (-1 == rank) ? curr.sender : rank;
}

void reschedule() {
    csr::intr_guard ig;
    lock::guard g{ lock::bkl };
    reschedule(g);
}

void reschedule(lock::guard &g) {
    auto readyq = proc::ready_queue();
    auto &curr = current();
    auto *idle = idles[hart::id()];

    // The caller is supposed to change the state if needed
    // If it's still running, we only switch if a higher priority process is ready
    if (RUNNING == curr.state) { // MLFQ rule 1: Keep executing if current priority is the highest
        if (readyq.first_key() < static_cast<qkey_t>(curr.prio))
            return;

        curr.state = READY;
        // Idle never enters the shared ready queue: it may only run on its own hart.
        if (&curr != idle)
            readyq.enqueue(curr.pid, static_cast<qkey_t>(curr.prio));
    }

    // Switch to highest priority ready process; fall back to this hart's idle
    auto next_pid = readyq.dequeue();
    auto *next = next_pid ? &procs[next_pid.value()] : idle;

    csr::satp{ next->page_table }.write();

    auto kernel_stack = reinterpret_cast<xlen_t>(&next->stack[sizeof(next->stack)]);

    // Save the kernel stack for the currently executing process for exception handling
    asm volatile("csrw sscratch, %[sscratch]\n"
                 :
                 : [sscratch] "r"(kernel_stack));

    // Skip switch_context if switching to ourselves.
    // This can happen if interrupt woke us before reschedule.
    // i.e. we set the current to SUSPENDED, and then call reschedule,
    // but the interrupt handler calls ready(current, false), before entering reschedule
    if (next == &curr) {
        curr.state = RUNNING;
        return;
    }

    process *prev = &curr;
    currents[hart::id()] = next;
    next->state = RUNNING;

    // Reap an exiting process AFTER satp moved off its page table.
    if (EXITED == prev->state) {
        mem::destroy_page_table(prev->page_table);
        prev->page_table = {};
        prev->state = UNUSED;
    }

    g.disarm();
    switch_context(&prev->sp, next->sp, &lock::bkl.locked, &lock::bkl.holder);
}

} // namespace proc
