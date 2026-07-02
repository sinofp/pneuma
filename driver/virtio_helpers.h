#pragma once

// Inline helpers for virtio MMIO + virtqueue manipulation. Header-only so each driver
// links its own copy without needing a shared library.

#include "virtio.h"
#include <stddef.h>
#include <stdint.h>

namespace virtio {

template <typename T> inline T mmio_read(unsigned long base, size_t off) {
    return *reinterpret_cast<volatile T *>(base + off);
}

template <typename T> inline void mmio_write(unsigned long base, size_t off, T val) {
    *reinterpret_cast<volatile T *>(base + off) = val;
}

inline void mmio_or_u32(unsigned long base, size_t off, uint32_t bits) {
    mmio_write<uint32_t>(base, off, mmio_read<uint32_t>(base, off) | bits);
}

// Steps 1-5 of the virtio device-init dance. Caller follows up with setup_queue + finish_device.
inline void setup_device(unsigned long mmio_base) {
    // 1. Reset the device.
    mmio_write<uint32_t>(mmio_base, REG_DEVICE_STATUS, 0);
    // 2. Set the ACKNOWLEDGE status bit: the guest OS has noticed the device.
    mmio_or_u32(mmio_base, REG_DEVICE_STATUS, STATUS_ACK);
    // 3. Set the DRIVER status bit.
    mmio_or_u32(mmio_base, REG_DEVICE_STATUS, STATUS_DRIVER);
    // 5. Set the FEATURES_OK status bit.
    mmio_or_u32(mmio_base, REG_DEVICE_STATUS, STATUS_FEAT_OK);
}

// Wire one virtq to the device at `index`. `shifted_pfn` selects QEMU's two-mode behaviour:
// raw paddr on direct boot, paddr>>12 when booted via U-Boot.
inline void setup_queue(unsigned long mmio_base, unsigned long vq_paddr, bool shifted_pfn, uint32_t index) {
    // 1. Select the queue writing its index (first queue is 0) to QueueSel.
    mmio_write<uint32_t>(mmio_base, REG_QUEUE_SEL, index);
    // 5. Notify the device about the queue size by writing the size to QueueNum.
    mmio_write<uint32_t>(mmio_base, REG_QUEUE_NUM, ENTRY_NUM);
    // 6. Notify the device about the used alignment by writing its value in bytes to QueueAlign.
    mmio_write<uint32_t>(mmio_base, REG_QUEUE_ALIGN, PAGE_SIZE);
    // 7. Write the physical number of the first page of the queue to the QueuePFN register.
    // QEMU interprets this differently depending on whether U-Boot was used:
    // - Direct boot: expects raw physical address
    // - U-Boot boot: expects page frame number (paddr >> 12)
    // TODO find out why the behaviour is different and unify it
    mmio_write<uint32_t>(mmio_base, REG_QUEUE_PFN, shifted_pfn ? (vq_paddr >> 12) : vq_paddr);
}

inline void finish_device(unsigned long regs) {
    // 8. Set the DRIVER_OK status bit.
    mmio_or_u32(regs, REG_DEVICE_STATUS, STATUS_DRIVER_OK);
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

} // namespace virtio
