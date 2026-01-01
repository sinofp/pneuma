#include "proc.h"
#include "csr.h"
#include "debug.h"
#include "mem.h"
#include "plic.h"
#include "qtab.h"
#include "virtio.h"

process procs[PROCS_MAX]; // All process control structures.

process *current_proc; // Currently running process
process *idle_proc;    // Idle process

using enum ProcState;

// We only execute this function at the beginning of a user process.
// So we don't care the 'restored' registers, because they're all placeholders allocated in create_process,
// only saved and loaded to use the same context_switch for process already running.
// Therefore, this function don't have to be naked, like we don't have to zero the callee saved registers for new
// process in create_process
// What's more, "r" already overwrites a register to load USER_BASE.
void user_entry() {
    using enum csr::sstatus_enc;
    asm volatile("csrw sepc, %[sepc]        \n"
                 "csrw sstatus, %[sstatus]  \n"
                 "sret                      \n"
                 :
                 : [sepc] "r"(USER_BASE), [sstatus] "r"(csr::sstatus::read()
                                                            .unset(SPP) // Not needed, but I like explicit
                                                            .set(SUM)
                                                            .raw()));
}

process *create_process(span<const uint8_t> image) {
    // Find an unused process control structure.
    process *proc = nullptr;
    size_t i;
    for (i = 0; i < PROCS_MAX; ++i)
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

    // Identical mapping for kernel pages, prepared for user's syscalls.
    // Map everything from __kernel_base to __free_ram_end.
    // This approach ensures that the kernel can always access both statically allocated areas (like .text), and
    // dynamically allocated areas managed by alloc_pages
    auto *page_table = reinterpret_cast<paddr_t *>(alloc_pages(1));
    for (vaddr_t kaddr = reinterpret_cast<vaddr_t>(__kernel_base); kaddr < static_cast<vaddr_t>(free_ram_end);
         kaddr += PAGE_SIZE)
        map_page(page_table, kaddr, kaddr, PAGE_R | PAGE_W | PAGE_X);

    // Map virtio-block MMIO
    map_page(page_table, virtio_blk_base, virtio_blk_base, PAGE_R | PAGE_W);

    // Map PLIC MMIO TODO skip Hart 0 M-Mode ranges
    for (paddr_t plic_addr = plic::start_addr; plic_addr < plic::end_addr; plic_addr += PAGE_SIZE)
        map_page(page_table, plic_addr, plic_addr, PAGE_R | PAGE_W);

    // Map user pages.
    for (auto offset = 0uz; offset < image.size(); offset += PAGE_SIZE) {
        paddr_t page = alloc_pages(1);

        // Fill and map the page.
        auto chunk = image.subspan(offset, min(PAGE_SIZE, image.size() - offset));
        memcpy((void *) page, chunk.data(), chunk.size());
        map_page(page_table, USER_BASE + offset, page, PAGE_U | PAGE_R | PAGE_W | PAGE_X);
    }

    // Initialize fields.
    // Can't construct and swap because the uint8_t stack[];
    proc->pid = i;
    // MLFQ rule 3: Put process in the highest queue when created
    // I give it to the second highest because I'll give interactive program (shell) the highest.
    // Although that means only the shell takes the CPU forever.
    proc->prio = ProcPrio::IV; // default prio
    proc->sp = sp;
    proc->state = SUSPENDED;
    proc->page_table = page_table;
    return proc;
}

// No matter what's the type of process::sp, we want the pointer to the prev_sp, and the value of next_sp.
// I use template here because I wasn't sure what's the best type for process::sp: xlen_t, xlen_t *, vaddr_t, or vaddr_t
// *?
template <typename T>
[[gnu::naked]]
void switch_context(T *prev_sp, T next_sp) {
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

// TODO bad_pid

void ready(process *proc, bool instant) {
    csr::intr_guard g;
    proc->state = READY;
    ready_queue().insert_desc(proc->pid, static_cast<qkey_t>(proc->prio));
    if (instant)
        reschedule();
}

void chprio(process *proc, ProcPrio prio, bool instant) {
    proc->prio = prio;
    if (READY == proc->state) {
        csr::intr_guard g;
        ready_queue().remove(proc->pid);
        ready_queue().insert_desc(proc->pid, static_cast<qkey_t>(proc->prio));
    }
}

void suspend(process *proc, bool instant) {
    csr::intr_guard g;
    switch (proc->state) {
    case READY:
        ready_queue().remove(proc->pid);
        proc->state = SUSPENDED;
        break;
    case RUNNING:
        proc->state = SUSPENDED;
        if (instant)
            reschedule();
        break;
    default:
        return;
    }
}

void resume(process *proc, bool instant) {
    csr::intr_guard g;
    if (proc->state != SUSPENDED)
        return;
    ready(proc, instant);
}

void sleep_ms(int32_t delay) {
    if (delay < 0)
        return;
    if (!delay) {
        ready(current_proc);
        return;
    }
    if (MAXKEY <= delay)
        PANIC("Sleeping time exceeds MAXKEY.");
    csr::intr_guard g;
    sleep_queue().insert_delta(current_proc->pid, delay);
    current_proc->state = ProcState::SLEEPING;
    reschedule();
}

void wakeup() {
    auto sleepq = sleep_queue();
    while (!sleepq.empty() and sleepq.first_key() <= 0) {
        auto pid = sleepq.dequeue();
        // Given that the queue is nonempty, dequeue must have value
        // TODO unify the argument/return value: use either pid or procs
        ready(&procs[pid.value()], false);
    }
    reschedule();
}

void reschedule() {
    csr::intr_guard g;

    // printf("reschedule: current_proc->pid: %d, prio: %d, running: %d\n", current_proc->pid,
    //        static_cast<int>(current_proc->prio), RUNNING == current_proc->state);
    // print_procs();
    // printf("readyq:\n");
    // print(ready_queue);
    // printf("sleepq:\n");
    // print(sleep_queue);

    auto readyq = ready_queue();

    // The caller is supposed to change the state if needed
    // If it's still running, we only switch if a higher priority process is ready
    if (RUNNING == current_proc->state) { // MLFQ rule 1: Keep executing if current priority is the highest
        if (readyq.first_key() < static_cast<qkey_t>(current_proc->prio))
            return;

        current_proc->state = READY;
        readyq.insert_desc(current_proc->pid, static_cast<qkey_t>(current_proc->prio));
        // printf("readyq after insert_desc:\n");
        // print(ready_queue);
    }

    // Switch to highest priority ready process

    auto next_pid = readyq.dequeue();
    if (!next_pid)
        PANIC("There's no next ready process!");
    auto *next = &procs[next_pid.value()];

    csr::satp{ next->page_table }.write();

    auto kernel_stack = reinterpret_cast<xlen_t>(&next->stack[sizeof(next->stack)]);

    // Save the kernel stack for the currently executing process for exception handling
    asm volatile("csrw sscratch, %[sscratch]\n"
                 :
                 : [sscratch] "r"(kernel_stack));

    // Context switch
    process *prev = current_proc;
    current_proc = next;
    current_proc->state = RUNNING;
    // printf("reschedule: Switching from (pid=%d, sp=%p) to (pid=%d, sp=%p)\n", prev->pid,
    //        reinterpret_cast<void *>(prev->sp), next->pid, reinterpret_cast<void *>(next->sp));
    switch_context(&prev->sp, next->sp);
    // printf("reschedule: Returned from switching from (pid=%d, sp=%p) to (pid=%d, sp=%p)\n", prev->pid,
    //        reinterpret_cast<void *>(prev->sp), next->pid, reinterpret_cast<void *>(next->sp));
    // TODO print SIE and SPIE
}
