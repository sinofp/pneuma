#include "kernel.h"
#include "common.h"
#include "file.h"
#include "span.h"
#include "virtio.h"

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

    if (next_paddr > reinterpret_cast<paddr_t>(__free_ram_end)) [[unlikely]]
        PANIC("out of memory");

    memset(reinterpret_cast<void *>(paddr), 0, n * PAGE_SIZE);
    return paddr;
}

void map_page(paddr_t *page_table, vaddr_t vaddr, paddr_t paddr, uint32_t flags) {
    if (!is_aligned(vaddr, PAGE_SIZE)) [[unlikely]]
        PANIC("unaligned vaddr %p", reinterpret_cast<void *>(vaddr));

    if (!is_aligned(paddr, PAGE_SIZE)) [[unlikely]]
        PANIC("unaligned paddr 0x%lx", paddr);

    // TODO recurisive?

    if constexpr (__riscv_xlen == 32) {
        // Sv32: 2-level page table, 10-bit VPNs
        auto *table1 = page_table;
        const xlen_t vpn1 = vaddr >> 22;           // [31:22]
        const xlen_t vpn0 = (vaddr >> 12) & 0x3ff; // [21:12]

        xlen_t pte = table1[vpn1];
        if (!(pte & PAGE_V)) {
            // Create the first level page if it doesn't exist
            auto pt_paddr = alloc_pages(1);
            xlen_t ppn = pt_paddr / PAGE_SIZE;
            table1[vpn1] = pte = ppn << 10 | PAGE_V;
        }

        xlen_t ppn = pte >> 10;
        auto *const table0 = reinterpret_cast<paddr_t *>(ppn * PAGE_SIZE);
        ppn = paddr / PAGE_SIZE; // == >> 12, why waste the last two bits of PTE?
        pte = ppn << 10 | flags | PAGE_V;
        table0[vpn0] = pte;
    } else {
        // Sv39: 3-level page table, 9-bit VPNs
        auto *table2 = page_table;
        const xlen_t vpn2 = (vaddr >> 30) & 0x1ff; // Ignore sign extension [63:39], keep [38:30]
        const xlen_t vpn1 = (vaddr >> 21) & 0x1ff; // [29:21]
        const xlen_t vpn0 = (vaddr >> 12) & 0x1ff; // [20:12]

        xlen_t pte = table2[vpn2];
        if (!(pte & PAGE_V)) {
            // Create the second level page if it doesn't exist
            auto pt_paddr = alloc_pages(1);
            xlen_t ppn = pt_paddr / PAGE_SIZE;
            table2[vpn2] = pte = ppn << 10 | PAGE_V; // No need to sign ext for pte
        }

        xlen_t ppn = (pte >> 10) & 0xfff'ffff'ffff; // Ignore reserved [63:54], keep [53:10]
        auto *const table1 = reinterpret_cast<paddr_t *>(ppn * PAGE_SIZE);
        pte = table1[vpn1];
        if (!(pte & PAGE_V)) {
            // Create the first level page if it doesn't exist
            auto pt_paddr = alloc_pages(1);
            xlen_t ppn = pt_paddr / PAGE_SIZE;
            table1[vpn1] = pte = ppn << 10 | PAGE_V; // No need to sign ext for pte
        }

        ppn = (pte >> 10) & 0xfff'ffff'ffff; // Ignore reserved [63:54], keep [53:10]
        auto *const table0 = reinterpret_cast<paddr_t *>(ppn * PAGE_SIZE);
        ppn = paddr / PAGE_SIZE;
        pte = ppn << 10 | flags | PAGE_V;
        table0[vpn0] = pte;
    }
}

[[using gnu: naked, aligned(4)]]
void kernel_entry() {
    asm volatile(
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

// Many function arguments in SBI spec are defined as unsigned long.
// I prefer xlen_t for clarity, although they're the same.
sbiret sbi_call(xlen_t arg0, xlen_t arg1, xlen_t arg2, xlen_t arg3, xlen_t arg4, xlen_t arg5, xlen_t fid, xlen_t eid) {
    register xlen_t a0 asm("a0") = arg0;
    register xlen_t a1 asm("a1") = arg1;
    register xlen_t a2 asm("a2") = arg2;
    register xlen_t a3 asm("a3") = arg3;
    register xlen_t a4 asm("a4") = arg4;
    register xlen_t a5 asm("a5") = arg5;
    register xlen_t a6 asm("a6") = fid;
    register xlen_t a7 asm("a7") = eid;

    asm volatile("ecall"
                 : "=r"(a0), "=r"(a1)
                 : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                 : "memory");
    return { static_cast<long>(a0), { a1 } };
}

int putchar(int ch) {
    // DBCN: Console Write Byte (FID #2), struct sbiret sbi_debug_console_write_byte(uint8_t byte)
    const auto ret = sbi_call(static_cast<uint8_t>(ch), 0, 0, 0, 0, 0, 2, SBI_EID_DBCN);
    if (SBI_SUCCESS == ret.error)
        return ch;
    else
        return EOF;
}

int getchar() {
    // DBCN: Console Read (FID #1), struct sbiret sbi_debug_console_read(unsigned long num_bytes, unsigned long
    // base_addr_lo, unsigned long base_addr_hi)
    char ch = '\0'; // C Manual: 'Character values are returned as an unsigned char converted to an int'
    const unsigned long num_bytes = sizeof(char);
    const auto base_addr = reinterpret_cast<xlen_t>(&ch);
    const auto ret = sbi_call(num_bytes, base_addr, 0, 0, 0, 0, 1, SBI_EID_DBCN);
    if (SBI_SUCCESS == ret.error and num_bytes == ret.uvalue)
        return ch;
    else
        // This is a non-blocking call, so can return SBI_SUCCESS with no value. It's an error for us
        return EOF;
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

[[gnu::naked]]
void user_entry(void) {
    asm volatile("csrw sepc, %[sepc]        \n"
                 "csrw sstatus, %[sstatus]  \n"
                 "sret                      \n"
                 :
                 : [sepc] "r"(USER_BASE), [sstatus] "r"(SSTATUS_SPIE | SSTATUS_SUM));
}

process procs[PROCS_MAX]; // All process control structures.

process *create_process(span<const uint8_t> image) {
    // Find an unused process control structure.
    process *proc = nullptr;
    size_t i;
    for (i = 0; i < PROCS_MAX; i++)
        if (procs[i].state == ProcState::UNUSED) {
            proc = &procs[i];
            break;
        }

    if (!proc) [[unlikely]]
        PANIC("no free process slots");

    // Stack callee-saved registers. These register values will be restored in
    // the first context switch in switch_context.
    auto *sp = reinterpret_cast<decltype(proc->sp)>(&proc->stack[sizeof(proc->stack)]); // the stack grows downward
    // Make space for callee_saved_regs. The number of registers of RV32 and RV64 are the same
    constexpr size_t stack_length = sizeof(callee_saved_regs) / sizeof(xlen_t);
    static_assert(13 == stack_length, "callee_saved_regs size donesn't equal to the number of regs");
    sp -= stack_length;
    *sp = reinterpret_cast<xlen_t>(user_entry); // std::remove_pointer?

    // Identical mapping for kernel pages, prepared for user's syscalls.
    // Map everything from __kernel_base to __free_ram_end.
    // This approach ensures that the kernel can always access both statically allocated areas (like .text), and
    // dynamically allocated areas managed by alloc_pages
    auto *page_table = reinterpret_cast<paddr_t *>(alloc_pages(1));
    for (vaddr_t kaddr = reinterpret_cast<vaddr_t>(__kernel_base); kaddr < reinterpret_cast<vaddr_t>(__free_ram_end);
         kaddr += PAGE_SIZE)
        map_page(page_table, kaddr, kaddr, PAGE_R | PAGE_W | PAGE_X);

    // Map virtio-block MMIO
    map_page(page_table, VIRTIO_BLK_PADDR, VIRTIO_BLK_PADDR, PAGE_R | PAGE_W);

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
    proc->pid = i + 1;
    proc->sp = sp;
    proc->state = ProcState::RUNNABLE;
    proc->page_table = page_table;
    return proc;
}

process *current_proc; // Currently running process
process *idle_proc;    // Idle process

void yield() {
    // Search for a runnable process
    process *next = idle_proc;
    for (auto i = 0uz; i < PROCS_MAX; i++) {
        process *proc = &procs[(current_proc->pid + i) % PROCS_MAX];
        if (proc->state == ProcState::RUNNABLE && proc->pid > 0) {
            next = proc;
            break;
        }
    }

    // If there's no runnable process other than the current one, return and continue processing
    if (next == current_proc)
        return;

    xlen_t satp = reinterpret_cast<xlen_t>(next->page_table) / PAGE_SIZE;
    if constexpr (__riscv_xlen == 32)
        satp |= SATP_SV32;
    else
        satp |= SATP_SV39;

    auto kernel_stack = reinterpret_cast<xlen_t>(&next->stack[sizeof(next->stack)]);

    asm volatile(
        // Switch page table
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"

        // Save the kernel stack for the currently executing process for exception handling
        "csrw sscratch, %[sscratch]\n"
        :
        : [satp] "r"(satp), [sscratch] "r"(kernel_stack));

    // Context switch
    process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, next->sp);
}

file files[FILES_MAX];
void fs_flush();

void handle_syscall(trap_frame *f) {
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
            yield();
        }
        break;
    case SYS_EXIT:
        printf("process %d exited\n", current_proc->pid);
        current_proc->state = ProcState::EXITED;
        // TODO free resources of exited proc
        yield();
        PANIC("unreachable");
    case SYS_READFILE:
    case SYS_WRITEFILE: {
        size_t i = f->a0;
        auto *buf = reinterpret_cast<char *>(f->a1);
        size_t len = f->a2;

        if (i >= FILES_MAX or i < FILES_MAX and files[i].name[0] == '\0') {
            printf("file descriptor not found: %d\n", i);
            f->a0 = -1;
            break;
        }

        auto *fl = &files[i];
        printf("%s %s\n", (f->a3 == SYS_WRITEFILE) ? "Writing" : "Reading", fl->name);

        if (len > sizeof(fl->data))
            len = fl->size;

        if (f->a3 == SYS_WRITEFILE) {
            memcpy(fl->data, buf, len);
            fl->size = len;
            fs_flush();
        } else
            memcpy(buf, fl->data, len);

        f->a0 = len;
        break;
    } break;
    default:
        PANIC("unexpected syscall a3=0x%lx\n", f->a3);
    }
}

extern "C" void handle_trap(trap_frame *f) {
    // sxlen_t?
    auto scause = static_cast<SCAUSE>(READ_CSR(scause));
    auto stval = READ_CSR(stval);
    auto user_pc = READ_CSR(sepc);

    // InstAddrMisaligned,
    // InstAccessFault,
    // IllegalInst,
    // Breakpoint,
    // LoadAddrMisaligned,
    // LoadAccessFault,
    // StoreAmoAddrMisaligned,
    // StoreAmoAccessFault,
    // UEcall,
    // SEcall,
    // InstPageFault = 12,
    // LoadPageFault,
    // StoreAmoPageFault = 15,
    // SoftwareCheck = 18,
    // HardwareError = 19,
    // SSoftwareIntr = intr_bit | 1,
    // STimerIntr = intr_bit | 5,
    // SExternalIntr = intr_bit | 9,
    // CntOverflowIntr = intr_bit | 13,
    switch (scause) {
    case SCAUSE::UEcall:
        handle_syscall(f);
        // Advance an instruction to skip the ecall
        user_pc += 4; // Instructions are always 4 bytes, even on RV64
        break;
    case SCAUSE::IllegalInst:
        PANIC("Illegal Instruction: stval=0x%lx, sepc=0x%lx\n", stval, user_pc);
        break;
    case SCAUSE::LoadAddrMisaligned:
    case SCAUSE::StoreAmoAddrMisaligned:
        PANIC("Address Misaligned: scause=0x%lx, stval=0x%lx, sepc=0x%lx\n", scause, stval, user_pc);
        break;
    case SCAUSE::LoadAccessFault:
    case SCAUSE::StoreAmoAccessFault:
        PANIC("Access Fault: scause=0x%lx, stval=0x%lx, sepc=0x%lx\n", scause, stval, user_pc);
        break;
    case SCAUSE::LoadPageFault:
    case SCAUSE::StoreAmoPageFault:
    case SCAUSE::InstPageFault:
        PANIC("Page Fault (check memory mapping!): scause=0x%lx, stval=0x%lx, sepc=0x%lx\n", scause, stval, user_pc);
        break;
    default:
        PANIC("Unexpected trap: scause=0x%lx, stval=0x%lx, sepc=0x%lx\n", scause, stval, user_pc);
    }

    WRITE_CSR(sepc, user_pc);
}

uint32_t virtio_reg_read32(size_t offset) { return *reinterpret_cast<volatile uint32_t *>(VIRTIO_BLK_PADDR + offset); }

uint64_t virtio_reg_read64(size_t offset) { return *reinterpret_cast<volatile uint64_t *>(VIRTIO_BLK_PADDR + offset); }

void virtio_reg_write32(size_t offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t *>(VIRTIO_BLK_PADDR + offset) = value;
}

void virtio_reg_fetch_and_or32(unsigned offset, uint32_t value) {
    virtio_reg_write32(offset, virtio_reg_read32(offset) | value);
}

virtio_virtq *virtq_init(uint32_t index) {
    // Allocate a region for the virtqueue.
    auto virtq_paddr = alloc_pages(align_up(sizeof(virtio_virtq), PAGE_SIZE) / PAGE_SIZE);
    auto *vq = reinterpret_cast<virtio_virtq *>(virtq_paddr);
    vq->queue_index = index;
    vq->used_index = reinterpret_cast<uint16_t *volatile>(&vq->used.index);

    // 1. Select the queue writing its index (first queue is 0) to QueueSel.
    virtio_reg_write32(VIRTIO_REG_QUEUE_SEL, index);
    // 5. Notify the device about the queue size by writing the size to QueueNum.
    virtio_reg_write32(VIRTIO_REG_QUEUE_NUM, VIRTQ_ENTRY_NUM);
    // 6. Notify the device about the used alignment by writing its value in bytes to QueueAlign.
    virtio_reg_write32(VIRTIO_REG_QUEUE_ALIGN, 0);
    // 7. Write the physical number of the first page of the queue to the QueuePFN register.
    virtio_reg_write32(VIRTIO_REG_QUEUE_PFN, virtq_paddr);
    return vq;
}

virtio_virtq *blk_request_vq;
virtio_blk_req *blk_req;
paddr_t blk_req_paddr;
uint64_t blk_capacity;

void virtio_blk_init() {
    if (virtio_reg_read32(VIRTIO_REG_MAGIC) != 0x74726976)
        PANIC("virtio: invalid magic value");
    if (virtio_reg_read32(VIRTIO_REG_VERSION) != 1)
        PANIC("virtio: invalid version");
    if (virtio_reg_read32(VIRTIO_REG_DEVICE_ID) != VIRTIO_DEVICE_BLK)
        PANIC("virtio: invalid device id");

    // 1. Reset the device.
    virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, 0);
    // 2. Set the ACKNOWLEDGE status bit: the guest OS has noticed the device.
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACK);
    // 3. Set the DRIVER status bit.
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER);
    // 5. Set the FEATURES_OK status bit.
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_FEAT_OK);
    // 7. Perform device-specific setup, including discovery of virtqueues for the device
    blk_request_vq = virtq_init(0);
    // 8. Set the DRIVER_OK status bit.
    virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER_OK);

    // Get the disk capacity.
    blk_capacity = virtio_reg_read64(VIRTIO_REG_DEVICE_CONFIG + 0) * SECTOR_SIZE;
    printf("virtio-blk: capacity is %ld bytes\n", static_cast<unsigned long>(blk_capacity));

    // Allocate a region to store requests to the device.
    blk_req_paddr = alloc_pages(align_up(sizeof(*blk_req), PAGE_SIZE) / PAGE_SIZE);
    blk_req = reinterpret_cast<virtio_blk_req *>(blk_req_paddr);
}

// Notifies the device that there is a new request. `desc_index` is the index
// of the head descriptor of the new request.
void virtq_kick(virtio_virtq *vq, uint16_t desc_index) {
    vq->avail.ring[vq->avail.index % VIRTQ_ENTRY_NUM] = desc_index;
    ++vq->avail.index;

    __sync_synchronize();

    virtio_reg_write32(VIRTIO_REG_QUEUE_NOTIFY, vq->queue_index);
    ++vq->last_used_index;
}

// Returns whether there are requests being processed by the device.
bool virtq_is_busy(struct virtio_virtq *vq) { return vq->last_used_index != *vq->used_index; }

// Reads/writes from/to virtio-blk device.
void read_write_disk(void *buf, uint64_t sector, int is_write) {
    if (sector >= blk_capacity / SECTOR_SIZE) {
        printf("virtio: tried to read/write sector=%ld, but capacity is %ld\n", static_cast<unsigned long>(sector),
               static_cast<unsigned long>(blk_capacity / SECTOR_SIZE));
        return;
    }

    // Construct the request according to the virtio-blk specification.
    blk_req->sector = sector;
    blk_req->type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    if (is_write)
        memcpy(blk_req->data, buf, SECTOR_SIZE);

    // Construct the virtqueue descriptors (using 3 descriptors).
    // We always use the first three descriptors because we're busy-waiting
    struct virtio_virtq *vq = blk_request_vq;
    vq->descs[0].addr = blk_req_paddr;
    vq->descs[0].len = sizeof(uint32_t) * 2 + sizeof(uint64_t);
    vq->descs[0].flags = VIRTQ_DESC_F_NEXT;
    vq->descs[0].next = 1;

    vq->descs[1].addr = blk_req_paddr + offsetof(struct virtio_blk_req, data);
    vq->descs[1].len = SECTOR_SIZE;
    vq->descs[1].flags = VIRTQ_DESC_F_NEXT | (is_write ? 0 : VIRTQ_DESC_F_WRITE);
    vq->descs[1].next = 2;

    vq->descs[2].addr = blk_req_paddr + offsetof(struct virtio_blk_req, status);
    vq->descs[2].len = sizeof(uint8_t);
    vq->descs[2].flags = VIRTQ_DESC_F_WRITE;

    // Notify the device that there is a new request.
    virtq_kick(vq, 0);

    // Wait until the device finishes processing.
    while (virtq_is_busy(vq))
        ;

    // virtio-blk: If a non-zero value is returned, it's an error.
    if (blk_req->status != 0) {
        printf("virtio: warn: failed to read/write sector=%ld status=%ld\n", static_cast<unsigned long>(sector),
               static_cast<unsigned long>(blk_req->status));
        return;
    }

    // For read operations, copy the data into the buffer.
    if (!is_write)
        memcpy(buf, blk_req->data, SECTOR_SIZE);
}

uint8_t disk[DISK_MAX_SIZE];

// TODO span
int oct2int(char *oct, int len) {
    int dec = 0;
    for (int i = 0; i < len; i++) {
        if (oct[i] < '0' || oct[i] > '7')
            break;

        dec = dec * 8 + (oct[i] - '0');
    }
    return dec;
}

void fs_init() {
    for (uint64_t sector = 0; sector < sizeof(disk) / SECTOR_SIZE; sector++)
        read_write_disk(&disk[sector * SECTOR_SIZE], sector, false);

    auto offset = 0uz;
    for (auto i = 0uz; i < FILES_MAX; ++i) {
        auto *header = reinterpret_cast<tar_header *>(&disk[offset]);
        if (header->name[0] == '\0')
            break;

        if (!strcmp(header->magic, USTAR_MAGIC))
            PANIC("invalid tar header: magic=\"%s\"", header->magic);

        auto filesz = oct2int(header->size, sizeof(header->size));
        auto *f = &files[i];
        f->in_use = true;
        strcpy(f->name, header->name);
        memcpy(f->data, header->data, filesz);
        f->size = filesz;
        printf("file (%d): %s, size=%d\n", i, f->name, f->size);

        offset += align_up(sizeof(tar_header) + filesz, SECTOR_SIZE);
    }
}

void fs_flush() {
    // Copy all file contents into `disk` buffer.
    memset(disk, 0, sizeof(disk));
    auto offset = 0uz;
    for (auto file_i = 0uz; file_i < FILES_MAX; ++file_i) {
        file *f = &files[file_i];
        if (!f->in_use)
            continue;

        auto *header = reinterpret_cast<tar_header *>(&disk[offset]);
        memset(header, 0, sizeof(*header));
        strcpy(header->name, f->name);
        strcpy(header->mode, "000644");
        strcpy(header->magic, USTAR_MAGIC);
        strcpy(header->version, "00");
        header->type = '0';

        // Turn the f size into an octal string.
        // TODO int2oct
        int filesz = f->size;
        for (int i = sizeof(header->size); i > 0; --i) {
            header->size[i - 1] = (filesz % 8) + '0';
            filesz /= 8;
        }

        // Calculate the checksum.
        int checksum = ' ' * sizeof(header->checksum);
        for (auto i = 0uz; i < sizeof(tar_header); ++i)
            checksum += disk[offset + i];

        for (int i = 5; i >= 0; --i) {
            header->checksum[i] = (checksum % 8) + '0';
            checksum /= 8;
        }

        // Copy f data.
        memcpy(header->data, f->data, f->size);
        offset += align_up(sizeof(tar_header) + f->size, SECTOR_SIZE);
    }

    // Write `disk` buffer into the virtio-blk.
    for (uint64_t sector = 0; sector < sizeof(disk) / SECTOR_SIZE; ++sector)
        read_write_disk(&disk[sector * SECTOR_SIZE], sector, true);

    printf("wrote %d bytes to disk\n", sizeof(disk));
}

extern "C" void kernel_main() {
    memset(__bss, 0, __bss_end - __bss);

    WRITE_CSR(stvec, reinterpret_cast<xlen_t>(kernel_entry));

    printf("\n\nhello %s\n", "world!");
    // %lx should be able to output 0x12345678'ffffffff on RV64 (LP64)
    printf("1 + 2 = %d, 0x%lx, 0x%lx\n", 1 + 2, 0x1234abcd, 0x12345678);

    auto paddr0 = alloc_pages(2);
    auto paddr1 = alloc_pages(1);
    printf("alloc_pages test: paddr0=0x%lx\n", paddr0);
    printf("alloc_pages test: paddr1=0x%lx\n", paddr1);

    virtio_blk_init();
    fs_init();

    idle_proc = create_process({ nullptr, 0 });
    idle_proc->pid = 0; // idle
    current_proc = idle_proc;

    printf("USER_BASE is %p\n", reinterpret_cast<void *>(USER_BASE));
    printf("_binary_shell_bin_start is 0x%lx\n", _binary_shell_bin_start);
    printf("_binary_shell_bin_size is 0x%lx\n", _binary_shell_bin_size);
    printf("About to create shell\n");
    create_process({ _binary_shell_bin_start, reinterpret_cast<size_t>(_binary_shell_bin_size) });

    yield();
    PANIC("switched to idle process");
}

[[using gnu: section(".text.boot"), naked]]
void boot() {
    asm volatile("mv sp, %[stack_top]\n" // Set the stack pointer
                 "j kernel_main\n"       // Jump to the kernel main function
                 :
                 : [stack_top] "r"(__stack_top) // Pass the stack top address as %[stack_top]
    );
}
