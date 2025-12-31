#pragma once

#include "kernel.h"
#include "span.h"

using pid_t = uint16_t;        // >= log2(PROCS_MAX)
constexpr pid_t PROCS_MAX = 8; // Maximum number of processes

enum class ProcState {
    UNUSED,
    SUSPENDED,
    READY,
    RUNNING,
    SLEEPING,
};

enum class ProcPrio : int32_t {
    V = 5,
    IV = 4,
    III = 3,
    II = 2,
    I = 1,
    N = 0, // Only for idle/nulla process
};

struct process {
    pid_t pid;       // Process ID
    ProcState state; // Process state
    ProcPrio prio;
    xlen_t *sp; // The stack pointer. We declare it as a pointer because it's being called pointer. Though it can also
                // be a vaddr_t *.
    uint8_t stack[1024 * 1024]; // Kernel stack
    paddr_t *page_table;        // Page table for this process
};

extern process procs[];
extern process *current_proc;
extern process *idle_proc;

inline bool bad_pid(pid_t id) { return id < 0 or PROCS_MAX <= id or ProcState::UNUSED == procs[id].state; }

constexpr xlen_t SATP_SV32 = 1u << 31;
constexpr xlen_t SATP_SV39 = 8ull << 60;

// The base virtual address of an application image. This needs to match the
// starting address defined in `user.ld`.
constexpr vaddr_t USER_BASE = 0x1000000;

process *create_process(span<const uint8_t> image);
void chprio(process *, ProcPrio, bool = true);
void suspend(process *, bool = true);
void resume(process *, bool = true);
void ready(process *, bool = true);
void sleep_ms(int32_t);
void wakeup();

void reschedule();

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
