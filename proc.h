#pragma once

#include "kernel.h"
#include "span.h"

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

constexpr unsigned SSTATUS_SPIE = 1 << 5;
constexpr unsigned SSTATUS_SUM = 1 << 18; // Permit Supervisor User Memory access

// The base virtual address of an application image. This needs to match the
// starting address defined in `user.ld`.
constexpr vaddr_t USER_BASE = 0x1000000;

process *create_process(span<const uint8_t> image);
void yield();

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
