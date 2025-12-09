#pragma once

#include "kernel.h"

void kernel_entry();

using sxlen_t = unsigned long;

#define READ_CSR(reg)                         \
    ({                                        \
        sxlen_t __tmp;                        \
        __asm__ __volatile__("csrr %0, " #reg \
                             : "=r"(__tmp));  \
        __tmp;                                \
    })

#define WRITE_CSR(reg, value)                                   \
    do {                                                        \
        sxlen_t __tmp = (value);                                \
        __asm__ __volatile__("csrw " #reg ", %0" ::"r"(__tmp)); \
    } while (0)

struct [[gnu::packed]] trap_frame {
    xlen_t ra;
    xlen_t gp;
    xlen_t tp;
    xlen_t t0;
    xlen_t t1;
    xlen_t t2;
    xlen_t t3;
    xlen_t t4;
    xlen_t t5;
    xlen_t t6;
    xlen_t a0;
    xlen_t a1;
    xlen_t a2;
    xlen_t a3;
    xlen_t a4;
    xlen_t a5;
    xlen_t a6;
    xlen_t a7;
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
    xlen_t sp;
};

constexpr sxlen_t intr_bit = 1ull << (__riscv_xlen - 1);

enum class SCAUSE : sxlen_t {
    InstAddrMisaligned,
    InstAccessFault,
    IllegalInst,
    Breakpoint,
    LoadAddrMisaligned,
    LoadAccessFault,
    StoreAmoAddrMisaligned,
    StoreAmoAccessFault,
    UEcall,
    SEcall,
    InstPageFault = 12,
    LoadPageFault,
    StoreAmoPageFault = 15,
    SoftwareCheck = 18,
    HardwareError = 19,
    SSoftwareIntr = intr_bit | 1,
    STimerIntr = intr_bit | 5,
    SExternalIntr = intr_bit | 9,
    CntOverflowIntr = intr_bit | 13,
};
