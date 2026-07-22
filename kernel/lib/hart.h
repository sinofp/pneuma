#pragma once

#include "lib/types.h"

namespace hart {

// Paired with the boot-stack array in kernel.ld and the kernel_entry instantiations in trap.cc.
// On VF2 Lite, 0 is the disabled S7.
constexpr size_t MAX = 5;

// The hart that won OpenSBI's boot lottery: ticks the sleep queue and owns the PLIC S-context.
extern size_t primary;

extern bool startable[MAX];
extern bool started[MAX];

[[gnu::always_inline]]
inline size_t id() {
    xlen_t v;
    asm volatile("mv %0, tp"
                 : "=r"(v));
    return v;
}

[[gnu::always_inline]]
inline void wfi() {
    asm volatile("wfi");
}

} // namespace hart
