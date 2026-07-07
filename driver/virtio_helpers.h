#pragma once

// Inline helpers for virtio MMIO + virtqueue manipulation. Header-only so each driver
// links its own copy without needing a shared library.

#include "virtio.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syscalls.h>

namespace virtio {

template <typename T> inline T mmio_read(unsigned long base, size_t off) {
    return *reinterpret_cast<volatile T *>(base + off);
}

template <typename T> inline void mmio_write(unsigned long base, size_t off, T val) {
    *reinterpret_cast<volatile T *>(base + off) = val;
}

template <typename T> inline void mmio_fetch_and_or(unsigned long base, size_t off, T bits) {
    mmio_write<T>(base, off, mmio_read<T>(base, off) | bits);
}

inline virtq *init(unsigned long mmio_base, unsigned long vq_paddr) {
    auto *vq = static_cast<virtq *>(mmio_map(vq_paddr, 2));
    memset(vq, 0, sizeof(virtq));

    mmio_write<uint32_t>(mmio_base, REG_DEVICE_STATUS, 0);
    mmio_fetch_and_or<uint32_t>(mmio_base, REG_DEVICE_STATUS, STATUS_ACK);
    mmio_fetch_and_or<uint32_t>(mmio_base, REG_DEVICE_STATUS, STATUS_DRIVER);
    mmio_fetch_and_or<uint32_t>(mmio_base, REG_DEVICE_STATUS, STATUS_FEAT_OK);

    // Must happen after FEAT_OK and before DRIVER_OK.
    // The device_status=0 reset above zeroes queue state, so PFN/NUM/ALIGN must be written now.
    mmio_write<uint32_t>(mmio_base, REG_QUEUE_SEL, 0);
    // GuestPageSize must be written explicitly: QEMU's guest_page_shift survives the reset
    // above, so whatever a previous driver (e.g. U-Boot) set would otherwise still apply.
    mmio_write<uint32_t>(mmio_base, REG_GUEST_PAGE_SIZE, PAGE_SIZE);
    mmio_write<uint32_t>(mmio_base, REG_QUEUE_NUM, ENTRY_NUM);
    mmio_write<uint32_t>(mmio_base, REG_QUEUE_ALIGN, PAGE_SIZE);
    mmio_write<uint32_t>(mmio_base, REG_QUEUE_PFN, vq_paddr / PAGE_SIZE);

    mmio_fetch_and_or<uint32_t>(mmio_base, REG_DEVICE_STATUS, STATUS_DRIVER_OK);
    return vq;
}

// Publish a chain head and notify the device.
inline void kick(unsigned long mmio_base, virtq *vq, uint16_t desc_index, uint32_t queue_index = 0) {
    // virtio spec: '2. The driver places the index of the head of the descriptor chain into the next ring entry of the
    // available ring.'
    vq->avail.ring[vq->avail.index % ENTRY_NUM] = desc_index;
    // virtio spec: '4. The driver performs a suitable memory barrier to ensure the device sees the updated descriptor
    // table and available ring before the next step.'
    __sync_synchronize();
    // virtio spec: '5. The available idx is increased by the number of descriptor chain heads added to the available
    // ring.' We use three descriptors, but it's one chain.
    ++vq->avail.index;
    // virtio spec: '6. The driver performs a suitable memory barrier to ensure that it updates the idx field before
    // checking for notification suppression.'
    __sync_synchronize();
    // virtio spec: '7. The driver sends an available buffer notification to the device if such notifications are not
    // suppressed.'
    mmio_write<uint32_t>(mmio_base, REG_QUEUE_NOTIFY, queue_index);

    ++vq->last_used_index;
}

// Ack the device's IRQ assertion. Call after sys_irq_wait returns.
inline void ack(unsigned long regs) {
    auto status = mmio_read<uint32_t>(regs, REG_INTERRUPT_STATUS);
    mmio_write<uint32_t>(regs, REG_INTERRUPT_ACK, status);
}

// True while the driver's last kick hasn't been reflected in used.index yet.
inline bool is_busy(virtq *vq) {
    __sync_synchronize();
    return vq->last_used_index != vq->used.index;
}

// Spin for polling drivers until the outstanding request completes.
// Returns false if no answer, so the high prio driver won't starve other apps.
inline bool wait_done(virtq *vq) {
    constexpr uint32_t max_spins = 100'000'000;
    for (uint32_t spins = 0; spins < max_spins; spins++)
        if (!is_busy(vq))
            return true;
    return false;
}

} // namespace virtio
