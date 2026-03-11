#pragma once

#include "lib/span.h"
#include "mem/pagetable.h"

namespace proc {

using id_t = uint16_t;  // >= log2(PROCS_MAX)
constexpr id_t MAX = 8; // Maximum number of processes

enum class state_t {
    UNUSED,
    SUSPENDED,
    READY,
    RUNNING,
    SLEEPING,
};

enum class prio_t : int32_t {
    V = 5,
    IV = 4,
    III = 3,
    II = 2,
    I = 1,
    N = 0, // Only for idle/nulla process
};

struct process {
    id_t pid;      // Process ID
    state_t state; // Process state
    prio_t prio;
    xlen_t *sp; // The stack pointer. We declare it as a pointer because it's being called pointer. Though it can also
                // be a vaddr_t *.
    uint8_t stack[1024 * 1024]; // Kernel stack
    mem::pt_t page_table;       // Page table for this process
};

// The base virtual address of an application image. This needs to match the
// starting address defined in `user.ld`.
constexpr vaddr_t USER_BASE = 0x1000000;

extern process procs[MAX];
extern process *current;
extern process *idle;

process *create(span<const uint8_t> image);
void destroy(process *);
void chprio(process *, prio_t, bool = true);
void suspend(process *, bool = true);
void resume(process *, bool = true);
void ready(process *, bool = true);
void sleep_ms(int32_t);
void wakeup();
void reschedule();

inline bool bad_id(id_t id) { return id < 0 or MAX <= id or state_t::UNUSED == proc::procs[id].state; }
} // namespace proc
