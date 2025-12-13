#pragma once

#include "kernel.h"

namespace plic {

constexpr uint32_t VIRTIO_BLK_IRQ = 1;

constexpr size_t quantity = 1;
static_assert(quantity <= 132, "U74 only has 1 to 132 PLIC sources");

constexpr size_t spacing = 4;

constexpr paddr_t start_addr = 0x0C00'0000;
// PLIC actually ends at 0x1000'0000.
// However, we don't use anything after hart0_s_claim_complete, so we won't map them.
constexpr paddr_t end_addr = 0x0C20'1008;

constexpr paddr_t priority_base = 0x0C00'0004;
constexpr paddr_t hart0_s_intr_en = 0x0C00'2080;

constexpr paddr_t hart0_s_prio_threshold = 0x0C20'1000;
constexpr paddr_t hart0_s_claim_complete = 0x0C20'1004;

inline void init() {
    // Set priority for VirtIO block device interrupt and enable
    mmio::write(priority_base + VIRTIO_BLK_IRQ * 4, 1);
    mmio::write(hart0_s_intr_en, 1u << VIRTIO_BLK_IRQ);

    // Accept all interrupts
    mmio::write(hart0_s_prio_threshold, 0);
}

inline uint32_t claim(size_t i) {
    if (i > quantity) {
        // TODO
    }
    return mmio::read<uint32_t>(hart0_s_claim_complete + i * spacing);
}

inline void complete(size_t i, uint32_t irq) {
    if (i > quantity) {
        // TODO
    }
    mmio::write(hart0_s_claim_complete + i * spacing, irq);
}
} // namespace plic
