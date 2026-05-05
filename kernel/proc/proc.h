#pragma once

#include "lib/span.h"
#include "mem/pagetable.h"

namespace proc {

void init();
[[noreturn]]
void idle_loop();
void backtrace();

using id_t =
    intptr_t; // Signed, so it can convey error code. PTRSZ, so passing by register for syscall preserves signess
constexpr id_t MAX = 8; // Maximum number of processes, for now

constexpr id_t FILE_SERVER = 1;

enum class state_t {
    UNUSED,
    SUSPENDED,
    READY,
    RUNNING,
    SLEEPING,
    SENDING,
    RECEIVING,
};

enum class prio_t : int_fast8_t {
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
    span<uint8_t> msg_buffer{ nullptr, 0 };

    union {
        id_t sender;
        id_t recipient;
    };
};

// The base virtual address of an application image. This needs to match the
// starting address defined in `user.ld`.
constexpr vaddr_t USER_BASE = 0x1000000;

extern process procs[MAX];
extern process *current;
extern process *idle;

process *create(span<const uint8_t>);
void destroy(process *);
void chprio(process *, prio_t, bool = true);
void suspend(process *, bool = true);
void resume(process *, bool = true);
void ready(process *, bool = true);
void sleep_ms(int32_t);
void wakeup();
id_t message_passing(span<uint8_t>, id_t, bool);
void reschedule();

inline bool bad_id(id_t id) { return id < 0 or MAX <= id or state_t::UNUSED == proc::procs[id].state; }
} // namespace proc
