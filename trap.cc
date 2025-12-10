#include "trap.h"
#include "csr.h"
#include "file.h"
#include "proc.h"

// proc.cc
extern process *idle_proc, *current_proc;

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

        if (i >= FILES_MAX or (i < FILES_MAX and files[i].name[0] == '\0')) {
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
    auto scause = csr::scause::read();
    auto stval = csr::stval::read();
    auto user_pc = csr::sepc::read();

    using SCAUSE = csr::scause_enc;
    switch (scause.encoding()) {
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

    user_pc.write();
}
