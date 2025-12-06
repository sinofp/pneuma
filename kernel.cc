#include "kernel.h"
#include "common.h"

// The base virtual address of an application image. This needs to match the
// starting address defined in `user.ld`.
constexpr vaddr_t USER_BASE = 0x1000000;

extern uint8_t __kernel_base[], __bss[], __bss_end[], __stack_top[], __free_ram[], __free_ram_end[];

// shell.bin.o
extern uint8_t _binary_shell_bin_start[], _binary_shell_bin_size[];

paddr_t alloc_pages(size_t n) {
    static auto next_paddr = reinterpret_cast<paddr_t>(__free_ram);
    auto paddr = next_paddr;
    next_paddr += n * PAGE_SIZE;

    if (next_paddr > reinterpret_cast<paddr_t>(__free_ram_end))
        PANIC("out of memory");

    memset(reinterpret_cast<void *>(paddr), 0, n * PAGE_SIZE);
    return paddr;
}

void map_page(paddr_t *table1, vaddr_t vaddr, paddr_t paddr, uint32_t flags) {
    if (!is_aligned(vaddr, PAGE_SIZE))
        PANIC("unaligned vaddr %p", reinterpret_cast<void *>(vaddr));

    if (!is_aligned(paddr, PAGE_SIZE))
        PANIC("unaligned paddr 0x%lx", paddr);

    // TODO recurisive?

    uint32_t vpn1 = vaddr >> 22;
    uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
    uint32_t pte = table1[vpn1];
    if (!(pte & PAGE_V)) {
        // Create the first level page if it doesn't exist
        paddr_t pt_paddr = alloc_pages(1);
        uint32_t ppn = pt_paddr / PAGE_SIZE;
        table1[vpn1] = pte = ppn << 10 | PAGE_V;
    }

    uint32_t ppn = pte >> 10;
    paddr_t *table0 = (uint32_t *) (ppn * PAGE_SIZE);
    ppn = paddr / PAGE_SIZE; // == >> 12, why waste the last two bits of PTE?
    pte = ppn << 10 | flags | PAGE_V;
    table0[vpn0] = pte;
}

__attribute__((naked)) __attribute__((aligned(4))) void kernel_entry() {
    __asm__ __volatile__(
        // clang-format off
        // We must not trust the stack pointer at the time of exception.
        // Retrieve the kernel stack of the running process from sscratch.
        "csrrw sp, sscratch, sp\n"

        // sp now points to the kernel (not user) stack of the currently running process.
        // sscratch now holds the original value of sp (user stack) at the time of the exception

        "addi sp, sp, -%[size]\n"
        STORE " ra,  %[ra_offset](sp)\n"
        STORE " gp,  %[gp_offset](sp)\n"
        STORE " tp,  %[tp_offset](sp)\n"
        STORE " t0,  %[t0_offset](sp)\n"
        STORE " t1,  %[t1_offset](sp)\n"
        STORE " t2,  %[t2_offset](sp)\n"
        STORE " t3,  %[t3_offset](sp)\n"
        STORE " t4,  %[t4_offset](sp)\n"
        STORE " t5,  %[t5_offset](sp)\n"
        STORE " t6,  %[t6_offset](sp)\n"
        STORE " a0,  %[a0_offset](sp)\n"
        STORE " a1,  %[a1_offset](sp)\n"
        STORE " a2,  %[a2_offset](sp)\n"
        STORE " a3,  %[a3_offset](sp)\n"
        STORE " a4,  %[a4_offset](sp)\n"
        STORE " a5,  %[a5_offset](sp)\n"
        STORE " a6,  %[a6_offset](sp)\n"
        STORE " a7,  %[a7_offset](sp)\n"
        STORE " s0,  %[s0_offset](sp)\n"
        STORE " s1,  %[s1_offset](sp)\n"
        STORE " s2,  %[s2_offset](sp)\n"
        STORE " s3,  %[s3_offset](sp)\n"
        STORE " s4,  %[s4_offset](sp)\n"
        STORE " s5,  %[s5_offset](sp)\n"
        STORE " s6,  %[s6_offset](sp)\n"
        STORE " s7,  %[s7_offset](sp)\n"
        STORE " s8,  %[s8_offset](sp)\n"
        STORE " s9,  %[s9_offset](sp)\n"
        STORE " s10, %[s10_offset](sp)\n"
        STORE " s11, %[s11_offset](sp)\n"

        // Retrieve and save the sp at the time of exception.
        "csrr a0, sscratch\n"
        STORE " a0, %[sp_offset](sp)\n"

        // Reset the kernel stack.
        "addi a0, sp, %[size]\n"
        "csrw sscratch, a0\n"

        "mv a0, sp\n"
        "call handle_trap\n"

        LOAD " ra,  %[ra_offset](sp)\n"
        LOAD " gp,  %[gp_offset](sp)\n"
        LOAD " tp,  %[tp_offset](sp)\n"
        LOAD " t0,  %[t0_offset](sp)\n"
        LOAD " t1,  %[t1_offset](sp)\n"
        LOAD " t2,  %[t2_offset](sp)\n"
        LOAD " t3,  %[t3_offset](sp)\n"
        LOAD " t4,  %[t4_offset](sp)\n"
        LOAD " t5,  %[t5_offset](sp)\n"
        LOAD " t6,  %[t6_offset](sp)\n"
        LOAD " a0,  %[a0_offset](sp)\n"
        LOAD " a1,  %[a1_offset](sp)\n"
        LOAD " a2,  %[a2_offset](sp)\n"
        LOAD " a3,  %[a3_offset](sp)\n"
        LOAD " a4,  %[a4_offset](sp)\n"
        LOAD " a5,  %[a5_offset](sp)\n"
        LOAD " a6,  %[a6_offset](sp)\n"
        LOAD " a7,  %[a7_offset](sp)\n"
        LOAD " s0,  %[s0_offset](sp)\n"
        LOAD " s1,  %[s1_offset](sp)\n"
        LOAD " s2,  %[s2_offset](sp)\n"
        LOAD " s3,  %[s3_offset](sp)\n"
        LOAD " s4,  %[s4_offset](sp)\n"
        LOAD " s5,  %[s5_offset](sp)\n"
        LOAD " s6,  %[s6_offset](sp)\n"
        LOAD " s7,  %[s7_offset](sp)\n"
        LOAD " s8,  %[s8_offset](sp)\n"
        LOAD " s9,  %[s9_offset](sp)\n"
        LOAD " s10, %[s10_offset](sp)\n"
        LOAD " s11, %[s11_offset](sp)\n"
        LOAD " sp,  %[sp_offset](sp)\n"
        "sret\n"
        // clang-format on
        :
        : [size] "i"(static_cast<xlen_t>(sizeof(trap_frame))), [ra_offset] "i"(offsetof(trap_frame, ra)),
          [gp_offset] "i"(offsetof(trap_frame, gp)), [tp_offset] "i"(offsetof(trap_frame, tp)),
          [t0_offset] "i"(offsetof(trap_frame, t0)), [t1_offset] "i"(offsetof(trap_frame, t1)),
          [t2_offset] "i"(offsetof(trap_frame, t2)), [t3_offset] "i"(offsetof(trap_frame, t3)),
          [t4_offset] "i"(offsetof(trap_frame, t4)), [t5_offset] "i"(offsetof(trap_frame, t5)),
          [t6_offset] "i"(offsetof(trap_frame, t6)), [a0_offset] "i"(offsetof(trap_frame, a0)),
          [a1_offset] "i"(offsetof(trap_frame, a1)), [a2_offset] "i"(offsetof(trap_frame, a2)),
          [a3_offset] "i"(offsetof(trap_frame, a3)), [a4_offset] "i"(offsetof(trap_frame, a4)),
          [a5_offset] "i"(offsetof(trap_frame, a5)), [a6_offset] "i"(offsetof(trap_frame, a6)),
          [a7_offset] "i"(offsetof(trap_frame, a7)), [s0_offset] "i"(offsetof(trap_frame, s0)),
          [s1_offset] "i"(offsetof(trap_frame, s1)), [s2_offset] "i"(offsetof(trap_frame, s2)),
          [s3_offset] "i"(offsetof(trap_frame, s3)), [s4_offset] "i"(offsetof(trap_frame, s4)),
          [s5_offset] "i"(offsetof(trap_frame, s5)), [s6_offset] "i"(offsetof(trap_frame, s6)),
          [s7_offset] "i"(offsetof(trap_frame, s7)), [s8_offset] "i"(offsetof(trap_frame, s8)),
          [s9_offset] "i"(offsetof(trap_frame, s9)), [s10_offset] "i"(offsetof(trap_frame, s10)),
          [s11_offset] "i"(offsetof(trap_frame, s11)), [sp_offset] "i"(offsetof(trap_frame, sp)));
}

struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long fid, long eid) {
    register long a0 __asm__("a0") = arg0;
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    register long a3 __asm__("a3") = arg3;
    register long a4 __asm__("a4") = arg4;
    register long a5 __asm__("a5") = arg5;
    register long a6 __asm__("a6") = fid;
    register long a7 __asm__("a7") = eid;

    __asm__ __volatile__("ecall"
                         : "=r"(a0), "=r"(a1)
                         : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                         : "memory");
    return (struct sbiret) { .error = a0, .value = a1 };
}

void putchar(char ch) {
    // DBCN: Console Write Byte (FID #2), struct sbiret sbi_debug_console_write_byte(uint8_t byte)
    sbi_call((uint8_t) ch, 0, 0, 0, 0, 0, 2, 0x4442434E);
}

void puts(const char *s) {
    // DBCN: Console Write (FID #0) struct sbiret sbi_debug_console_write(unsigned long num_bytes, unsigned long
    // base_addr_lo, unsigned long base_addr_hi)
    unsigned long num_bytes;
    for (num_bytes = 0; s[num_bytes] != '\0'; ++num_bytes) {
    }
    long base_addr = (long) s;
    sbi_call(num_bytes, base_addr, 0, 0, 0, 0, 0, 0x4442434E);
}

long getchar() {
    struct sbiret ret = sbi_call(0, 0, 0, 0, 0, 0, 0, 2);
    return ret.error;
}

// No matter what's the type of process::sp, we want the pointer to the prev_sp, and the value of next_sp.
// I use template here because I wasn't sure what's the best type for process::sp: xlen_t, xlen_t *, vaddr_t, or vaddr_t
// *?
template <typename T> __attribute__((naked)) void switch_context(T *prev_sp, T next_sp) {
    __asm__ __volatile__(
        // clang-format off
        // Save callee-saved registers onto the current process's stack.
        "addi sp, sp, -%[size]\n"
        STORE " ra,  %[ra_offset](sp)\n"
        STORE " s0,  %[s0_offset](sp)\n"
        STORE " s1,  %[s1_offset](sp)\n"
        STORE " s2,  %[s2_offset](sp)\n"
        STORE " s3,  %[s3_offset](sp)\n"
        STORE " s4,  %[s4_offset](sp)\n"
        STORE " s5,  %[s5_offset](sp)\n"
        STORE " s6,  %[s6_offset](sp)\n"
        STORE " s7,  %[s7_offset](sp)\n"
        STORE " s8,  %[s8_offset](sp)\n"
        STORE " s9,  %[s9_offset](sp)\n"
        STORE " s10, %[s10_offset](sp)\n"
        STORE " s11, %[s11_offset](sp)\n"

        // Switch the stack pointer.
        STORE " sp, (a0)\n" // *prev_sp = sp;
        "mv sp, a1\n"       // Switch stack pointer (sp) here

        // Restore callee-saved registers from the next process's stack.
        LOAD " ra,  %[ra_offset](sp)\n"
        LOAD " s0,  %[s0_offset](sp)\n"
        LOAD " s1,  %[s1_offset](sp)\n"
        LOAD " s2,  %[s2_offset](sp)\n"
        LOAD " s3,  %[s3_offset](sp)\n"
        LOAD " s4,  %[s4_offset](sp)\n"
        LOAD " s5,  %[s5_offset](sp)\n"
        LOAD " s6,  %[s6_offset](sp)\n"
        LOAD " s7,  %[s7_offset](sp)\n"
        LOAD " s8,  %[s8_offset](sp)\n"
        LOAD " s9,  %[s9_offset](sp)\n"
        LOAD " s10, %[s10_offset](sp)\n"
        LOAD " s11, %[s11_offset](sp)\n"
        "addi sp, sp, %[size]\n" // Pop restored registers from stack
        "ret\n"
        // clang-format on
        :
        : [size] "i"(static_cast<xlen_t>(sizeof(callee_saved_regs))), [ra_offset] "i"(offsetof(callee_saved_regs, ra)),
          [s0_offset] "i"(offsetof(callee_saved_regs, s0)), [s1_offset] "i"(offsetof(callee_saved_regs, s1)),
          [s2_offset] "i"(offsetof(callee_saved_regs, s2)), [s3_offset] "i"(offsetof(callee_saved_regs, s3)),
          [s4_offset] "i"(offsetof(callee_saved_regs, s4)), [s5_offset] "i"(offsetof(callee_saved_regs, s5)),
          [s6_offset] "i"(offsetof(callee_saved_regs, s6)), [s7_offset] "i"(offsetof(callee_saved_regs, s7)),
          [s8_offset] "i"(offsetof(callee_saved_regs, s8)), [s9_offset] "i"(offsetof(callee_saved_regs, s9)),
          [s10_offset] "i"(offsetof(callee_saved_regs, s10)), [s11_offset] "i"(offsetof(callee_saved_regs, s11)));
}

// ↓ __attribute__((naked)) is very important!
__attribute__((naked)) void user_entry(void) {
    __asm__ __volatile__("csrw sepc, %[sepc]        \n"
                         "csrw sstatus, %[sstatus]  \n"
                         "sret                      \n"
                         :
                         : [sepc] "r"(USER_BASE), [sstatus] "r"(SSTATUS_SPIE));
}

struct process procs[PROCS_MAX]; // All process control structures.

struct process *create_process(const void *image, size_t image_size) {
    // Find an unused process control structure.
    struct process *proc = nullptr;
    size_t i;
    for (i = 0; i < PROCS_MAX; i++)
        if (procs[i].state == PROC_UNUSED) {
            proc = &procs[i];
            break;
        }

    if (!proc)
        PANIC("no free process slots");

    // Stack callee-saved registers. These register values will be restored in
    // the first context switch in switch_context.
    auto *sp = reinterpret_cast<decltype(proc->sp)>(&proc->stack[sizeof(proc->stack)]); // the stack grows downward
    // Make space for callee_saved_regs. The number of registers of RV32 and RV64 are the same
    constexpr size_t stack_length = sizeof(callee_saved_regs) / sizeof(xlen_t);
    sp -= stack_length;                         // The same as sp -= 13;
    *sp = reinterpret_cast<xlen_t>(user_entry); // std::remove_pointer?

    // Identical mapping for kernel pages, prepared for user's syscalls.
    // Map everything from __kernel_base to __free_ram_end.
    // This approach ensures that the kernel can always access both statically allocated areas (like .text), and
    // dynamically allocated areas managed by alloc_pages
    auto *page_table = reinterpret_cast<paddr_t *>(alloc_pages(1));
    for (vaddr_t kaddr = reinterpret_cast<vaddr_t>(__kernel_base); kaddr < reinterpret_cast<vaddr_t>(__free_ram_end);
         kaddr += PAGE_SIZE)
        map_page(page_table, kaddr, kaddr, PAGE_R | PAGE_W | PAGE_X);

    // Map user pages.
    for (auto offset = 0uz; offset < image_size; offset += PAGE_SIZE) {
        paddr_t page = alloc_pages(1);

        // Fill and map the page.
        memcpy((void *) page, static_cast<const uint8_t *>(image) + offset, min(PAGE_SIZE, image_size - offset));
        map_page(page_table, USER_BASE + offset, page, PAGE_U | PAGE_R | PAGE_W | PAGE_X);
    }

    // Initialize fields.
    // TODO C++ construct and swap?
    proc->pid = i + 1;
    proc->sp = sp;
    proc->state = PROC_RUNNABLE;
    proc->page_table = page_table;
    return proc;
}

struct process *current_proc; // Currently running process
struct process *idle_proc;    // Idle process

void yield() {
    // Search for a runnable process
    struct process *next = idle_proc;
    for (auto i = 0uz; i < PROCS_MAX; i++) {
        struct process *proc = &procs[(current_proc->pid + i) % PROCS_MAX];
        if (proc->state == PROC_RUNNABLE && proc->pid > 0) {
            next = proc;
            break;
        }
    }

    // If there's no runnable process other than the current one, return and continue processing
    if (next == current_proc)
        return;

    __asm__ __volatile__(
        // Switch page table
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"

        // Save the kernel stack for the currently executing process for exception handling
        "csrw sscratch, %[sscratch]\n"
        :
        : [satp] "r"(SATP_SV32 | ((uint32_t) next->page_table / PAGE_SIZE)), [sscratch] "r"(
                                                                                 (uint32_t) &next->stack[sizeof(
                                                                                     next->stack)]));

    // Context switch
    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, next->sp);
}

void handle_syscall(struct trap_frame *f) {
    switch (f->a3) {
    case SYS_PUTCHAR:
        putchar(f->a0);
        break;
    case SYS_GETCHAR:
        for (;;) {
            long ch = getchar();
            if (ch >= 0) {
                f->a0 = ch;
                break;
            }
            yield();
        }
        break;
    case SYS_EXIT:
        printf("process %d exited\n", current_proc->pid);
        current_proc->state = PROC_EXITED;
        // TODO free resources of exited proc
        yield();
        PANIC("unreachable");
    default:
        PANIC("unexpected syscall a3=0x%x\n", f->a3);
    }
}

extern "C" void handle_trap(struct trap_frame *f) {
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);

    if (scause == SCAUSE_ECALL) {
        handle_syscall(f);
        // Advance an instruction to skip the ecall
        user_pc += 4;
    } else {
        PANIC("unexpected trap scause=0x%x, stval=0x%x, sepc=0x%x\n", scause, stval, user_pc);
    }

    WRITE_CSR(sepc, user_pc);
}

extern "C" void kernel_main() {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    const char *s = "\n\nhello world!\n";
    for (int i = 0; s[i] != '\0'; i++) {
        putchar(s[i]);
    }
    puts(s);

    printf("\n\nhello %s\n", "world!");
    // %lx should be able to output 0x12345678'ffffffff on RV64 (LP64)
    printf("1 + 2 = %d, 0x%x, 0x%lx\n", 1 + 2, 0x1234abcd, 0x12345678);

    paddr_t paddr0 = alloc_pages(2);
    paddr_t paddr1 = alloc_pages(1);
    printf("alloc_pages test: paddr0=0x%x\n", paddr0);
    printf("alloc_pages test: paddr1=0x%x\n", paddr1);

    idle_proc = create_process(nullptr, 0);
    idle_proc->pid = 0; // idle
    current_proc = idle_proc;

    create_process(_binary_shell_bin_start, (size_t) _binary_shell_bin_size);

    yield();
    PANIC("switched to idle process");
}

__attribute__((section(".text.boot"))) __attribute__((naked)) void boot() {
    __asm__ __volatile__("mv sp, %[stack_top]\n" // Set the stack pointer
                         "j kernel_main\n"       // Jump to the kernel main function
                         :
                         : [stack_top] "r"(__stack_top) // Pass the stack top address as %[stack_top]
    );
}
