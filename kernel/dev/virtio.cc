// Shared virtio MMIO + virtqueue helpers used by the in-kernel virtio drivers
// (virtio-input, virtio-gpu). virtio-blk has moved to user space; once gpu and input
// follow, these helpers will move alongside them into a shared user library.

#include "dev/virtio.h"
#include "dev/mmio.h"
#include "lib/common.h"
#include "mem/mem.h"

namespace virtio {

virtq *virtq_init(paddr_t mmio_base, paddr_t vq_buf, bool shifted_pfn, uint32_t index) {
    if (!vq_buf)
        PANIC("virtio: virtq buffer not allocated");

    auto *vq = reinterpret_cast<virtq *>(vq_buf);
    memset(vq, 0, sizeof(virtq));
    vq->queue_index = index;
    vq->used_index = reinterpret_cast<uint16_t *volatile>(&vq->used.index);

    // 1. Select the queue writing its index (first queue is 0) to QueueSel.
    mmio::write<uint32_t>(mmio_base, REG_QUEUE_SEL, index);
    // 5. Notify the device about the queue size by writing the size to QueueNum.
    mmio::write<uint32_t>(mmio_base, REG_QUEUE_NUM, ENTRY_NUM);
    // 6. Notify the device about the used alignment by writing its value in bytes to QueueAlign.
    mmio::write<uint32_t>(mmio_base, REG_QUEUE_ALIGN, mem::PAGE_SIZE);
    // 7. Write the physical number of the first page of the queue to the QueuePFN register.
    // QEMU interprets this differently depending on whether U-Boot was used:
    // - Direct boot: expects raw physical address
    // - U-Boot boot: expects page frame number (paddr >> 12)
    auto queue_pfn = shifted_pfn ? (vq_buf >> 12) : vq_buf;
    mmio::write<uint32_t>(mmio_base, REG_QUEUE_PFN, queue_pfn);
    return vq;
}

virtq *device_init(paddr_t mmio_base, paddr_t vq_buf, bool shifted_pfn, uint32_t queue_index) {
    // 1. Reset the device.
    mmio::write<uint32_t>(mmio_base, REG_DEVICE_STATUS, 0);
    // 2. ACKNOWLEDGE: guest OS has noticed the device.
    mmio::fetch_and_or<uint32_t>(mmio_base, REG_DEVICE_STATUS, STATUS_ACK);
    // 3. DRIVER status bit.
    mmio::fetch_and_or<uint32_t>(mmio_base, REG_DEVICE_STATUS, STATUS_DRIVER);
    // 5. FEATURES_OK status bit.
    mmio::fetch_and_or<uint32_t>(mmio_base, REG_DEVICE_STATUS, STATUS_FEAT_OK);
    // 7. Device-specific virtqueue discovery.
    auto *vq = virtq_init(mmio_base, vq_buf, shifted_pfn, queue_index);
    // 8. DRIVER_OK status bit.
    mmio::fetch_and_or<uint32_t>(mmio_base, REG_DEVICE_STATUS, STATUS_DRIVER_OK);
    return vq;
}

void virtq_kick(paddr_t mmio_base, virtq *vq, uint16_t desc_index) {
    vq->avail.ring[vq->avail.index % ENTRY_NUM] = desc_index;
    __sync_synchronize();
    ++vq->avail.index;
    __sync_synchronize();
    mmio::write<uint32_t>(mmio_base, REG_QUEUE_NOTIFY, vq->queue_index);
    ++vq->last_used_index;
}

bool virtq_is_busy(virtq *vq) { return vq->last_used_index != *vq->used_index; }

} // namespace virtio
