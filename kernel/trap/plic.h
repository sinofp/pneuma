#pragma once

#include "dev/mmio.h"
#include "lib/hart.h"
#include "lib/panic.h"
#include "lib/types.h"

namespace plic {

// Parsed in fdt.cc
extern paddr_t start_addr;
extern paddr_t end_addr;
constexpr size_t irq_ext_n = 2 * 2 * hart::MAX; // M, S for (phandle, irq)
extern uint32_t irq_ext[irq_ext_n];
extern uint32_t hart_to_phandle[hart::MAX];

constexpr size_t priority_base = 0x0000'0000;
constexpr size_t enable_base = 0x0000'2000;
constexpr size_t enable_stride = 0x80;
constexpr size_t context_base = 0x0020'0000;
constexpr size_t context_stride = 0x1000; // threshold at +0, claim/complete at +4

// The S-mode context of the hart that owns external interrupts (the boot-lottery winner);
// set by init(). Secondaries never enable SEIE, so one context suffices.
inline size_t s_context;

inline void init(size_t hartid) {
    const auto intc_phandle = hart_to_phandle[hartid];

    for (s_context = 0; s_context < irq_ext_n and
                        (intc_phandle != irq_ext[s_context * 2] or 9 /* SExternalIntr */ != irq_ext[s_context * 2 + 1]);
         ++s_context) {
    }
    if (s_context == irq_ext_n)
        PANIC("No valid hart to wire PLIC interrupts");

    // Accept all interrupts (threshold = 0)
    mmio::write(start_addr, context_base + s_context * context_stride, 0);
}

inline void enable_irq(uint32_t irq) {
    // Set priority for this IRQ
    mmio::write(start_addr, priority_base + irq * 4, 1);
    // Enable this IRQ in the (word-indexed: irq can exceed 31) enable register
    auto en = enable_base + s_context * enable_stride + (irq / 32) * 4;
    auto current = mmio::read<uint32_t>(start_addr, en);
    mmio::write(start_addr, en, current | (1u << (irq % 32)));
}

inline uint32_t claim() { return mmio::read<uint32_t>(start_addr, context_base + s_context * context_stride + 4); }

inline void complete(uint32_t irq) { mmio::write(start_addr, context_base + s_context * context_stride + 4, irq); }

} // namespace plic
