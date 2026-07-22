#include "lib/lock.h"
#include "lib/common.h"
#include "lib/panic.h"

namespace lock {

spinlock bkl;
spinlock console;

// Adapted from https://docs.riscv.org/reference/isa/unpriv/a-st-ext.html#critical
void spinlock::lock() {
    int tmp;
    asm volatile("1:\n"
                 "  lw   %0, (%1)\n"
                 "  bnez %0, 1b\n"
                 "  li   %0, 1\n"
                 "  amoswap.w.aq %0, %0, (%1)\n"
                 "  bnez %0, 1b\n"
                 : "=&r"(tmp)
                 : "r"(&locked)
                 : "memory");
    holder = hart::id();
}

void spinlock::unlock() {
    if (hart::id() != holder) {
        // printf_unlocked: this may BE the console lock, and PANIC's printf would deadlock
        // against ourselves if we held it.
        printf_unlocked("lock holder is hart %d, but hart %d is releasing\n", static_cast<int>(holder),
                        static_cast<int>(hart::id()));
        PANIC("releasing another hart's lock");
    }
    holder = NO_HOLDER;
    asm volatile("amoswap.w.rl x0, x0, (%0)"
                 :
                 : "r"(&locked)
                 : "memory");
}

void assert_bkl_held() {
    if (hart::id() != bkl.holder)
        PANIC("bkl not held");
}

} // namespace lock
