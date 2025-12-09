#pragma once
#include "common.h"

struct sbiret {
    long error;

    union {
        unsigned long uvalue;
        long value;
    };
};

constexpr decltype(sbiret::error) SBI_SUCCESS = 0;

#define PANIC(fmt, ...)                                                       \
    do {                                                                      \
        printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        while (1) {                                                           \
        }                                                                     \
    } while (0)

#if __riscv_xlen == 32
#define STORE "sw"
#define LOAD "lw"
#else
#define STORE "sd"
#define LOAD "ld"
#endif

// Or uintptr_t.
// I choose unsigned long because of its usage in OpenSBI interfaces
using xlen_t = unsigned long;
using sxlen_t = unsigned long;

constexpr xlen_t SBI_EID_DBCN = 0x4442434E;

struct [[gnu::packed]] callee_saved_regs {
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

constexpr size_t PROCS_MAX = 8; // Maximum number of processes

enum class ProcState {
    UNUSED,
    RUNNABLE,
    EXITED,
};

struct process {
    size_t pid;      // Process ID
    ProcState state; // Process state
    xlen_t *sp; // The stack pointer. We declare it as a pointer because it's being called pointer. Though it can also
                // be a vaddr_t *.
    uint8_t stack[8192]; // Kernel stack
    paddr_t *page_table; // Page table for this process
};

constexpr xlen_t SATP_SV32 = 1u << 31;
constexpr xlen_t SATP_SV39 = 8ull << 60;
constexpr unsigned PAGE_V = 1 << 0; // "Valid" bit (entry is enabled)
constexpr unsigned PAGE_R = 1 << 1; // Readable
constexpr unsigned PAGE_W = 1 << 2; // Writable
constexpr unsigned PAGE_X = 1 << 3; // Executable
constexpr unsigned PAGE_U = 1 << 4; // User (accessible in user mode)

constexpr unsigned SSTATUS_SPIE = 1 << 5;
constexpr unsigned SSTATUS_SUM = 1 << 18; // Permit Supervisor User Memory access

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
