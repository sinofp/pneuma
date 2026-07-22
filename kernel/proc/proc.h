#pragma once

#include "lib/hart.h"
#include "lib/lock.h"
#include "lib/span.h"
#include "mem/pagetable.h"

namespace proc {

void init();
void init_idle();
[[noreturn]]
void idle_loop();
void backtrace(xlen_t);

using id_t =
    intptr_t; // Signed, so it can convey error code. PTRSZ, so passing by register for syscall preserves signess
constexpr id_t MAX = 16; // Maximum number of processes: one idle per hart + user procs

constexpr id_t FILE_SERVER = 1;

enum class state_t {
    UNUSED,
    SUSPENDED,
    READY,
    RUNNING,
    SLEEPING,
    SENDING,
    RECEIVING,
    EXITED,
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
    id_t pid = -1; // Process ID
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
        uint32_t awaited_irq; // when SUSPENDED via sys_irq_wait
    };
};

// The base virtual address of an application image. This needs to match the
// starting address defined in `user.ld`.
constexpr vaddr_t USER_BASE = 0x1000000;

extern process procs[MAX];
extern process *currents[hart::MAX]; // per-hart running process
extern process *idles[hart::MAX];    // per-hart idle; never enters the shared ready queue

inline process &current() { return *currents[hart::id()]; }

process *create(span<const uint8_t>, bool = false);
void chprio(process *, prio_t);
void resume(process *);
void ready(process *);
// Requires bkl held; the guard& parameter only proves it. Will also pick an IPI target (SMP).
void ready(process *, lock::guard &);
void sleep_ms(int32_t);
// Timer-trap body (SIE already 0): tick the sleep queue, wake expired sleepers, preempt.
void timer_tick(int64_t elapsed_ms, bool preempt_due);
id_t message_passing(span<uint8_t>, id_t, bool);
void reschedule();
// On return the bkl is no longer reliably held: if a switch happened, switch_context released
// it long before this resumes; touching shared state after requires re-acquiring (see sys_irq_wait)
void reschedule(lock::guard &g);

inline bool bad_id(id_t id) { return id < 0 or MAX <= id or state_t::UNUSED == proc::procs[id].state; }
} // namespace proc
