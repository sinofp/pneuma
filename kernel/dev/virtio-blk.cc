#include "dev/mmio.h"
#include "dev/virtio.h"
#include "fdt/fdt.h"
#include "mem/mem.h"
#include "proc/proc.h"
#include "trap/plic.h"

namespace virtio::blk {
virtq *request_vq;
req_t *req;
paddr_t vq_paddr;
paddr_t req_paddr;
uint64_t capacity;
bool use_shifted_pfn = false;
paddr_t base = fdt::uninitialised;
} // namespace virtio::blk

namespace {

void virtio_reg_fetch_and_or32(size_t offset, uint32_t value) {
    auto addr = virtio::blk::base + offset;
    mmio::write<uint32_t>(addr, mmio::read<uint32_t>(addr) | value);
}

} // namespace

namespace virtio {

virtq *virtq_init(uint32_t index) {
    if (!blk::vq_paddr)
        PANIC("virtio: virtq buffers not allocated in mem::init");

    blk::request_vq = reinterpret_cast<virtq *>(blk::vq_paddr);
    memset(blk::request_vq, 0, sizeof(virtq));

    auto virtq_paddr = blk::vq_paddr;
    auto *vq = blk::request_vq;
    vq->queue_index = index;
    vq->used_index = reinterpret_cast<uint16_t *volatile>(&vq->used.index);

    // 1. Select the queue writing its index (first queue is 0) to QueueSel.
    mmio::write<uint32_t>(blk::base, REG_QUEUE_SEL, index);
    // 5. Notify the device about the queue size by writing the size to QueueNum.
    mmio::write<uint32_t>(blk::base, REG_QUEUE_NUM, VIRTQ_ENTRY_NUM);
    // 6. Notify the device about the used alignment by writing its value in bytes to QueueAlign.
    mmio::write<uint32_t>(blk::base, REG_QUEUE_ALIGN, PAGE_SIZE);
    // 7. Write the physical number of the first page of the queue to the QueuePFN register.
    // QEMU interprets this differently depending on whether U-Boot was used:
    // - Direct boot: expects raw physical address
    // - U-Boot boot: expects page frame number (paddr >> 12)
    // TODO find out why the behaviour is different and unify it
    auto queue_pfn = blk::use_shifted_pfn ? (virtq_paddr >> 12) : virtq_paddr;
    mmio::write<uint32_t>(blk::base, REG_QUEUE_PFN, queue_pfn);
    return vq;
}

namespace blk {
void init() {
    // Enumerate all virtio devices and find the smallest block device (likely disk.tar)
    paddr_t selected_base = 0;
    uint32_t selected_irq = 0;
    uint64_t biggest_capacity = 0;
    size_t blk_device_count = 0;

    printf("virtio-blk: Found %ld virtio devices\n", static_cast<long>(fdt::virtio_device_count));

    for (size_t i = 0; i < fdt::virtio_device_count; i++) {
        auto mmio_base = fdt::virtio_devices[i].mmio_base;

        // Check if this is a valid virtio-blk device
        if (mmio::read<uint32_t>(mmio_base, REG_MAGIC) != 0x74726976)
            continue;
        if (mmio::read<uint32_t>(mmio_base, REG_VERSION) != 1)
            continue;
        if (mmio::read<uint32_t>(mmio_base, REG_DEVICE_ID) != DEVICE_BLK)
            continue;

        ++blk_device_count;

        // Read capacity directly from config space without initializing the device
        // We avoid resetting devices here to prevent interfering with U-Boot's boot disk
        auto cap = mmio::read<uint64_t>(mmio_base, REG_DEVICE_CONFIG + 0) * SECTOR_SIZE;
        printf("  Device %ld: MMIO=0x%lx, capacity=%ld bytes\n", static_cast<long>(i),
               static_cast<unsigned long>(mmio_base), static_cast<long>(cap));

        // TODO better probe and deciding
        // Select the biggest capacity device (32MB FAT16 disk.img)
        if (biggest_capacity < cap) {
            biggest_capacity = cap;
            selected_base = mmio_base;
            selected_irq = fdt::virtio_devices[i].irq;
        }
    }

    if (0 == selected_base)
        PANIC("virtio-blk: No valid block device found");

    // Detect U-Boot boot: When booting via U-Boot, there are 2 blk devices
    // (boot disk + data disk). QEMU's VirtIO implementation behaves differently:
    // - Direct boot (1 device): QueuePFN = physical address
    // - U-Boot boot (2+ devices): QueuePFN = page frame number (paddr >> 12)
    // Note: this is heuristic and might cause problem in future
    use_shifted_pfn = (blk_device_count >= 2);

    base = selected_base;
    plic::VIRTIO_BLK_IRQ = selected_irq;

    printf("virtio-blk: Selected device at MMIO=0x%lx, IRQ=%d, capacity=%ld bytes (blk_devices=%ld)\n",
           static_cast<unsigned long>(base), static_cast<int>(plic::VIRTIO_BLK_IRQ),
           static_cast<long>(biggest_capacity), static_cast<long>(blk_device_count));

    // Now initialize the selected device
    if (mmio::read<uint32_t>(base, REG_MAGIC) != 0x74726976)
        PANIC("virtio: invalid magic value");
    if (mmio::read<uint32_t>(base, REG_VERSION) != 1)
        PANIC("virtio: invalid version");
    if (mmio::read<uint32_t>(base, REG_DEVICE_ID) != DEVICE_BLK)
        PANIC("virtio: invalid device id");

    // 1. Reset the device.
    mmio::write<uint32_t>(base, REG_DEVICE_STATUS, 0);
    // 2. Set the ACKNOWLEDGE status bit: the guest OS has noticed the device.
    virtio_reg_fetch_and_or32(REG_DEVICE_STATUS, STATUS_ACK);
    // 3. Set the DRIVER status bit.
    virtio_reg_fetch_and_or32(REG_DEVICE_STATUS, STATUS_DRIVER);
    // 5. Set the FEATURES_OK status bit.
    virtio_reg_fetch_and_or32(REG_DEVICE_STATUS, STATUS_FEAT_OK);
    // 7. Perform device-specific setup, including discovery of virtqueues for the device
    request_vq = virtq_init(0);
    // 8. Set the DRIVER_OK status bit.
    mmio::write<uint32_t>(base, REG_DEVICE_STATUS, STATUS_DRIVER_OK);

    // Get the disk capacity.
    capacity = mmio::read<uint64_t>(base, REG_DEVICE_CONFIG + 0) * SECTOR_SIZE;
    printf("virtio-blk: capacity is %ld bytes\n", static_cast<unsigned long>(capacity));

    // Allocate a region to store requests to the device.
    static_assert(sizeof(req_t) <= PAGE_SIZE, "blk_req bigger than a page frame");
    req_paddr = mem::alloc_frame();
    req = reinterpret_cast<req_t *>(req_paddr);

    // Disable interrupt suppression in the virtqueue
    // request_vq->avail.flags &= ~VIRTQ_AVAIL_F_NO_INTERRUPT;
}
} // namespace blk

// Notifies the device that there is a new request. `desc_index` is the index
// of the head descriptor of the new request.
void virtq_kick(virtq *vq, uint16_t desc_index) {
    // virtio spec: '2. The driver places the index of the head of the descriptor chain into the next ring entry of the
    // available ring.'
    vq->avail.ring[vq->avail.index % VIRTQ_ENTRY_NUM] = desc_index;

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
    mmio::write<uint32_t>(blk::base, REG_QUEUE_NOTIFY, vq->queue_index);

    ++vq->last_used_index;
}

// Returns whether there are requests being processed by the device.
bool virtq_is_busy(struct virtq *vq) { return vq->last_used_index != *vq->used_index; }

namespace blk {

// Reads/writes from/to virtio-blk device.
void read_write(void *buf, uint64_t sector, int is_write) {
    if (sector >= capacity / SECTOR_SIZE) {
        printf("virtio: tried to read/write sector=%ld, but capacity is %ld\n", static_cast<unsigned long>(sector),
               static_cast<unsigned long>(capacity / SECTOR_SIZE));
        return;
    }

    printf("  read_write_disk: %s sector %ld to buf@%p\n", is_write ? "write" : "read",
           static_cast<unsigned long>(sector), buf);

    // virtio spec: '1. The driver places the buffer into free descriptor(s) in the descriptor table, chaining as
    // necessary'
    req->sector = sector;
    req->type = is_write ? T_OUT : T_IN;
    if (is_write)
        memcpy(req->data, buf, SECTOR_SIZE);

    // Construct the virtqueue descriptors chain
    auto *vq = request_vq;
    vq->descs[0].addr = req_paddr;
    vq->descs[0].len = sizeof(uint32_t) * 2 + sizeof(uint64_t);
    vq->descs[0].flags = VIRTQ_DESC_F_NEXT;
    vq->descs[0].next = 1;

    vq->descs[1].addr = req_paddr + offsetof(req_t, data);
    vq->descs[1].len = SECTOR_SIZE;
    vq->descs[1].flags = VIRTQ_DESC_F_NEXT | (is_write ? 0 : VIRTQ_DESC_F_WRITE);
    vq->descs[1].next = 2;

    vq->descs[2].addr = req_paddr + offsetof(req_t, status);
    vq->descs[2].len = sizeof(uint8_t);
    vq->descs[2].flags = VIRTQ_DESC_F_WRITE;

    // Notify the device that there is a new request.
    virtq_kick(vq, 0);

    // Wait until the device finishes processing.
    while (virtq_is_busy(vq))
        ;

    // Check if the operation succeeded
    if (req->status != 0) {
        printf("virtio: warn: failed to read/write sector=%ld status=%ld\n", static_cast<unsigned long>(sector),
               static_cast<unsigned long>(req->status));
        return;
    }

    // For read operations, copy the data into the buffer.
    if (!is_write) {
        // printf("  read_write_disk: coping %d bytes from req->data@%p to buf%p\n", SECTOR_SIZE, req->data,
        // buf); printf("  First 4 bytes as hex: "); for (int i = 0; i < 4; i++) {
        //     printf("%lx ", static_cast<unsigned long>(req->data[i]));
        // }
        memcpy(buf, req->data, SECTOR_SIZE);
    }
    // printf("\n\n");
}

} // namespace blk

} // namespace virtio
