#pragma once

#include "dev/mmio.h"
#include "lib/types.h"

namespace plic {

extern uint32_t VIRTIO_BLK_IRQ;

constexpr size_t quantity = 1;
static_assert(quantity <= 132, "U74 only has 1 to 132 PLIC sources");

constexpr size_t spacing = 4;

// Parsed in fdt.cc
extern paddr_t start_addr;
extern paddr_t end_addr;

constexpr size_t priority_base = 0x0000'0004;
constexpr size_t hart0_s_intr_en = 0x0000'2080;

constexpr size_t hart0_s_prio_threshold = 0x0020'1000;
constexpr size_t hart0_s_claim_complete = 0x0020'1004;

inline void init() {
    // Set priority for VirtIO block device interrupt and enable
    mmio::write(start_addr, priority_base + VIRTIO_BLK_IRQ * 4, 1);
    mmio::write(start_addr, hart0_s_intr_en, 1u << VIRTIO_BLK_IRQ);

    // Accept all interrupts
    mmio::write(start_addr, hart0_s_prio_threshold, 0);
}

inline uint32_t claim(size_t i) {
    if (i > quantity) {
        // TODO
    }
    return mmio::read<uint32_t>(start_addr, hart0_s_claim_complete + i * spacing);
}

inline void complete(size_t i, uint32_t irq) {
    if (i > quantity) {
        // TODO
    }
    mmio::write(start_addr, hart0_s_claim_complete + i * spacing, irq);
}
} // namespace plic
