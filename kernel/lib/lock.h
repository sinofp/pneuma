#pragma once

#include "lib/hart.h"
#include "lib/types.h"

namespace lock {

constexpr size_t NO_HOLDER = static_cast<size_t>(-1);

// amoswap.aq/rl spinlock with holder tracking. Non-recursive; unlock asserts the releasing
// hart is the holder. Callers must disable interrupts (csr::intr_guard) BEFORE acquiring and
// keep them off while held, so a trap on the holding hart can never self-deadlock.
struct spinlock {
    int locked{ 0 };
    size_t holder{ NO_HOLDER };

    void lock();
    void unlock();
};

// Big kernel lock: procs[] state, qtab queues, currents[], driver IRQ tables, the frame
// allocator, and PLIC enables all mutate under this one lock.
extern spinlock bkl;

// Leaf lock serialising whole printf messages. Lock order: intr_guard -> bkl -> console.
// printf never takes bkl, so the order cannot invert.
extern spinlock console;

class guard {
    spinlock *lk;

public:
    explicit guard(spinlock &l)
        : lk{ &l } {
        lk->lock();
    }

    ~guard() {
        if (lk)
            lk->unlock();
    }

    // Suppress the destructor's unlock so we can release it in switch_context
    void disarm() { lk = nullptr; }

    guard(const guard &) = delete;
    guard &operator=(const guard &) = delete;
};

// For functions whose callers must already hold bkl TODO replace it by ownership based lock arguments
void assert_bkl_held();

} // namespace lock
