#include "proc/proc.h"
#include "dev/sbi.h"
#include "dev/virtio-blk.h"
#include "dev/virtio-gpu.h"
#include "dev/virtio-input.h"
#include "fdt/fdt.h"
#include "lib/debug.h"
#include "mem/mem.h"
#include "proc/qtab.h"
#include "trap/csr.h"
#include "trap/plic.h"

namespace proc {
process procs[MAX]; // All process control structures.
process *current;   // Currently running process
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

    current = create({ nullptr, 0 });
    printf("After creating idle proc\n");
    // Set the idle process running manually, so it can be saved to readyq in reschedule
    current->state = state_t::RUNNING;
    chprio(current, prio_t::N, false);
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

void backtrace() {
    // This line assumes everything before proc::init setting proc::current must not call backtrace
    printf("backtrace pid=%d:\n", current->pid);
    auto fp = reinterpret_cast<xlen_t>(__builtin_frame_address(0));
    printf("```zsh\n for addr (\n");
    while (fp) {
        // RISC-V Calling Contenvion: The previous frame pointer and return address pair will reside just prior to the
        // current stack address held in fp. This puts the return address at fp - PTRSZ/8, and the previous frame
        // pointer at fp - 2 * PTRSZ/8.
        auto ra = reinterpret_cast<xlen_t *>(fp)[-1];
        auto prev_fp = reinterpret_cast<xlen_t *>(fp)[-2];
        printf("    %p\n", ra - 4); // Theoretically I should also take care of compressed inst.
        fp = prev_fp;
    }
    // TODO save elf names for procs
    printf(")\n    llvm-symbolizer --obj=result-rv64/kernel.elf $addr\n```\n");
}

process *create(span<const uint8_t> image) {
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

    // All virtio-mmio have 0x00001000 size, so it's sufficient to map only one page to access their registers
    // Map virtio-block and virtio-input MMIO, skip for idle proc because it is created before blk::init()
    if (fdt::uninitialised != virtio::blk::base)
        mem::map_page(page_table, virtio::blk::base, virtio::blk::base, R | W);
    if (fdt::uninitialised != virtio::input::base)
        mem::map_page(page_table, virtio::input::base, virtio::input::base, R | W);
    // We might not even have GPU
    if (fdt::uninitialised != virtio::gpu::base)
        mem::map_page(page_table, virtio::gpu::base, virtio::gpu::base, R | W);

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

    // Map user heap pages (hard coded 8MB for now)
    vaddr_t heap_base = align_up(USER_BASE + image.size(), mem::PAGE_SIZE);
    for (auto offset = 0uz; offset < 8 * 1024 * 1024; offset += mem::PAGE_SIZE)
        mem::map_page(page_table, heap_base + offset, mem::alloc_frame(), U | R | W | X);

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

void destroy(process *proc) {
    mem::destroy_page_table(proc->page_table);
    proc->page_table = {};
}
} // namespace proc

namespace {

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

} // namespace

namespace proc {

// TODO bad_pid

void ready(process *proc, bool instant) {
    csr::intr_guard g;
    proc->state = READY;
    proc::ready_queue().enqueue(proc->pid, static_cast<qkey_t>(proc->prio));
    if (instant)
        reschedule();
}

void chprio(process *proc, proc::prio_t prio, bool instant) {
    proc->prio = prio;
    if (READY == proc->state) {
        csr::intr_guard g;
        proc::ready_queue().remove(proc->pid);
        proc::ready_queue().enqueue(proc->pid, static_cast<qkey_t>(proc->prio));
    }
}

void suspend(process *proc, bool instant) {
    csr::intr_guard g;
    switch (proc->state) {
    case READY:
        proc::ready_queue().remove(proc->pid);
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
        ready(current);
        return;
    }
    csr::intr_guard g;
    proc::sleep_queue().enqueue(current->pid, delay);
    current->state = proc::state_t::SLEEPING;
    reschedule();
}

void wakeup() {
    auto sleepq = proc::sleep_queue();
    while (!sleepq.empty() and sleepq.first_key() <= 0) {
        auto pid = sleepq.dequeue();
        // Given that the queue is nonempty, dequeue must have value
        // TODO unify the argument/return value: use either pid or procs
        ready(&procs[pid.value()], false);
    }
    reschedule();
}

id_t message_passing(span<uint8_t> buffer, id_t rank, bool send) {
    if ((-1 != rank and !send) and bad_id(rank))
        return -1;
    using enum state_t;
    // csr::intr_guard g;
    if (send) {
        auto &recipient = procs[rank];
        if (RECEIVING == recipient.state and (-1 == recipient.sender or current->pid == recipient.sender)) {
            auto length = min(buffer.size(), recipient.msg_buffer.size());
            mem::cross_proc_memcpy(recipient.page_table, reinterpret_cast<vaddr_t>(recipient.msg_buffer.data()),
                                   current->page_table, reinterpret_cast<vaddr_t>(buffer.data()), length);
            recipient.sender =
                current->pid; // so the recipient with sender=-1 knows who's the real sender after resuming
            ready(&recipient, false);
            return rank;
        } else {
            current->state = SENDING;
            current->msg_buffer = buffer;
            current->recipient = rank;

            send_queue().enqueue(current->pid, static_cast<qkey_t>(current->prio));
            reschedule();
            return rank;
        }
    } else {
        bool nobody_sending = true;
        auto sq = send_queue();
        for (auto it = sq.begin(); it != sq.end(); ++it) {
            id_t i = it.curr;
            if (auto &sender = procs[i]; current->pid == sender.recipient and (-1 == rank or rank == i)) {
                auto length = min(buffer.size(), sender.msg_buffer.size());
                mem::cross_proc_memcpy(current->page_table, reinterpret_cast<vaddr_t>(buffer.data()), sender.page_table,
                                       reinterpret_cast<vaddr_t>(sender.msg_buffer.data()), length);

                nobody_sending = false;
                sq.remove(i);
                ready(&sender, false);
                return i;
            }
        }

        if (nobody_sending) {
            current->state = RECEIVING;
            current->msg_buffer = buffer;
            current->sender = rank;

            reschedule();
            return current->sender;
        }
    }
}

void reschedule() {
    csr::intr_guard g;

    // printf("reschedule: current->pid: %d, prio: %d, running: %d\n", current->pid,
    //        static_cast<int>(current->prio), RUNNING == current->state);
    // print_procs();
    // printf("readyq:\n");
    // print(proc::ready_queue);
    // printf("sleepq:\n");
    // print(proc::sleep_queue);

    auto readyq = proc::ready_queue();

    // The caller is supposed to change the state if needed
    // If it's still running, we only switch if a higher priority process is ready
    if (RUNNING == current->state) { // MLFQ rule 1: Keep executing if current priority is the highest
        if (readyq.first_key() < static_cast<qkey_t>(current->prio))
            return;

        current->state = READY;
        readyq.enqueue(current->pid, static_cast<qkey_t>(current->prio));
        // printf("readyq after enqueue:\n");
        // print(proc::ready_queue);
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

    // Skip switch_context if switching to ourselves.
    // This can happen if interrupt woke us before reschedule.
    // i.e. we set the current to SUSPENDED, and then call reschedule,
    // but the interrupt handler calls ready(current, false), before entering reschedule
    if (next == current) {
        current->state = RUNNING;
        return;
    }

    process *prev = current;
    current = next;
    current->state = RUNNING;
    // printf("reschedule: Switching from (pid=%d, sp=%proc) to (pid=%d, sp=%proc)\n", prev->pid,
    //        reinterpret_cast<void *>(prev->sp), next->pid, reinterpret_cast<void *>(next->sp));
    switch_context(&prev->sp, next->sp);
    // printf("reschedule: Returned from switching from (pid=%d, sp=%proc) to (pid=%d, sp=%proc)\n", prev->pid,
    //        reinterpret_cast<void *>(prev->sp), next->pid, reinterpret_cast<void *>(next->sp));
}

} // namespace proc
