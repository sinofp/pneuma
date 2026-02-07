#include "fdt/fdt.h"
#include "dev/mmio.h"
#include "mem/mem.h"
#include "trap/plic.h"
#include "proc/proc.h"
#include "dev/virtio.h"

virtio_virtq *blk_request_vq;
virtio_blk_req *blk_req;
paddr_t blk_vq_paddr;
paddr_t blk_req_paddr;
uint64_t blk_capacity;
bool use_shifted_pfn = false;

void virtio_reg_fetch_and_or32(size_t offset, uint32_t value) {
    auto addr = virtio_blk_base + offset;
    mmio::write<uint32_t>(addr, mmio::read<uint32_t>(addr) | value);
}

virtio_virtq *virtq_init(uint32_t index) {
    if (!blk_vq_paddr)
        PANIC("virtio: virtq buffers not allocated in mem::init");

    blk_request_vq = reinterpret_cast<virtio_virtq *>(blk_vq_paddr);
    memset(blk_request_vq, 0, sizeof(virtio_virtq));

    auto virtq_paddr = blk_vq_paddr;
    auto *vq = blk_request_vq;
    vq->queue_index = index;
    vq->used_index = reinterpret_cast<uint16_t *volatile>(&vq->used.index);

    // 1. Select the queue writing its index (first queue is 0) to QueueSel.
    mmio::write<uint32_t>(virtio_blk_base, VIRTIO_REG_QUEUE_SEL, index);
    // 5. Notify the device about the queue size by writing the size to QueueNum.
    mmio::write<uint32_t>(virtio_blk_base, VIRTIO_REG_QUEUE_NUM, VIRTQ_ENTRY_NUM);
    // 6. Notify the device about the used alignment by writing its value in bytes to QueueAlign.
    mmio::write<uint32_t>(virtio_blk_base, VIRTIO_REG_QUEUE_ALIGN, PAGE_SIZE);
    // 7. Write the physical number of the first page of the queue to the QueuePFN register.
    // QEMU interprets this differently depending on whether U-Boot was used:
    // - Direct boot: expects raw physical address
    // - U-Boot boot: expects page frame number (paddr >> 12)
    // TODO find out why the behaviour is different and unify it
    auto queue_pfn = use_shifted_pfn ? (virtq_paddr >> 12) : virtq_paddr;
    mmio::write<uint32_t>(virtio_blk_base, VIRTIO_REG_QUEUE_PFN, queue_pfn);
    return vq;
}

void virtio_blk_init() {
    // Enumerate all virtio devices and find the smallest block device (likely disk.tar)
    paddr_t selected_base = 0;
    uint32_t selected_irq = 0;
    uint64_t smallest_capacity = UINT64_MAX;
    size_t blk_device_count = 0;

    printf("virtio-blk: Found %ld virtio devices\n", static_cast<long>(fdt::virtio_device_count));

    for (size_t i = 0; i < fdt::virtio_device_count; i++) {
        auto base = fdt::virtio_devices[i].mmio_base;

        // Check if this is a valid virtio-blk device
        if (mmio::read<uint32_t>(base, VIRTIO_REG_MAGIC) != 0x74726976)
            continue;
        if (mmio::read<uint32_t>(base, VIRTIO_REG_VERSION) != 1)
            continue;
        if (mmio::read<uint32_t>(base, VIRTIO_REG_DEVICE_ID) != VIRTIO_DEVICE_BLK)
            continue;

        ++blk_device_count;

        // Read capacity directly from config space without initializing the device
        // We avoid resetting devices here to prevent interfering with U-Boot's boot disk
        auto capacity = mmio::read<uint64_t>(base, VIRTIO_REG_DEVICE_CONFIG + 0) * SECTOR_SIZE;
        printf("  Device %ld: MMIO=0x%lx, capacity=%ld bytes\n", static_cast<long>(i), static_cast<unsigned long>(base),
               static_cast<long>(capacity));

        // Select the smallest capacity device (disk.tar)
        if (capacity < smallest_capacity) {
            smallest_capacity = capacity;
            selected_base = base;
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

    virtio_blk_base = selected_base;
    plic::VIRTIO_BLK_IRQ = selected_irq;

    printf("virtio-blk: Selected device at MMIO=0x%lx, IRQ=%d, capacity=%ld bytes (blk_devices=%ld)\n",
           static_cast<unsigned long>(virtio_blk_base), static_cast<int>(plic::VIRTIO_BLK_IRQ),
           static_cast<long>(smallest_capacity), static_cast<long>(blk_device_count));

    // Now initialize the selected device
    if (mmio::read<uint32_t>(virtio_blk_base, VIRTIO_REG_MAGIC) != 0x74726976)
        PANIC("virtio: invalid magic value");
    if (mmio::read<uint32_t>(virtio_blk_base, VIRTIO_REG_VERSION) != 1)
        PANIC("virtio: invalid version");
    if (mmio::read<uint32_t>(virtio_blk_base, VIRTIO_REG_DEVICE_ID) != VIRTIO_DEVICE_BLK)
        PANIC("virtio: invalid device id");

    // 1. Reset the device.
    mmio::write<uint32_t>(virtio_blk_base, VIRTIO_REG_DEVICE_STATUS, 0);
    // 2. Set the ACKNOWLEDGE status bit: the guest OS has noticed the device.
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACK);
    // 3. Set the DRIVER status bit.
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER);
    // 5. Set the FEATURES_OK status bit.
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_FEAT_OK);
    // 7. Perform device-specific setup, including discovery of virtqueues for the device
    blk_request_vq = virtq_init(0);
    // 8. Set the DRIVER_OK status bit.
    mmio::write<uint32_t>(virtio_blk_base, VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER_OK);

    // Get the disk capacity.
    blk_capacity = mmio::read<uint64_t>(virtio_blk_base, VIRTIO_REG_DEVICE_CONFIG + 0) * SECTOR_SIZE;
    printf("virtio-blk: capacity is %ld bytes\n", static_cast<unsigned long>(blk_capacity));

    // Allocate a region to store requests to the device.
    static_assert(sizeof(virtio_blk_req) <= PAGE_SIZE, "blk_req bigger than a page frame");
    blk_req_paddr = alloc_frame();
    blk_req = reinterpret_cast<virtio_blk_req *>(blk_req_paddr);

    // Disable interrupt suppression in the virtqueue
    // blk_request_vq->avail.flags &= ~VIRTQ_AVAIL_F_NO_INTERRUPT;
}

// Notifies the device that there is a new request. `desc_index` is the index
// of the head descriptor of the new request.
void virtq_kick(virtio_virtq *vq, uint16_t desc_index) {
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
    mmio::write<uint32_t>(virtio_blk_base, VIRTIO_REG_QUEUE_NOTIFY, vq->queue_index);

    ++vq->last_used_index;
}

// Returns whether there are requests being processed by the device.
bool virtq_is_busy(struct virtio_virtq *vq) { return vq->last_used_index != *vq->used_index; }

// Reads/writes from/to virtio-blk device.
void read_write_disk(void *buf, uint64_t sector, int is_write) {
    if (sector >= blk_capacity / SECTOR_SIZE) {
        printf("virtio: tried to read/write sector=%ld, but capacity is %ld\n", static_cast<unsigned long>(sector),
               static_cast<unsigned long>(blk_capacity / SECTOR_SIZE));
        return;
    }

    printf("  read_write_disk: %s sector %ld to buf@%p\n", is_write ? "write" : "read",
           static_cast<unsigned long>(sector), buf);

    // virtio spec: '1. The driver places the buffer into free descriptor(s) in the descriptor table, chaining as
    // necessary'
    blk_req->sector = sector;
    blk_req->type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    if (is_write)
        memcpy(blk_req->data, buf, SECTOR_SIZE);

    // Construct the virtqueue descriptors chain
    auto *vq = blk_request_vq;
    vq->descs[0].addr = blk_req_paddr;
    vq->descs[0].len = sizeof(uint32_t) * 2 + sizeof(uint64_t);
    vq->descs[0].flags = VIRTQ_DESC_F_NEXT;
    vq->descs[0].next = 1;

    vq->descs[1].addr = blk_req_paddr + offsetof(virtio_blk_req, data);
    vq->descs[1].len = SECTOR_SIZE;
    vq->descs[1].flags = VIRTQ_DESC_F_NEXT | (is_write ? 0 : VIRTQ_DESC_F_WRITE);
    vq->descs[1].next = 2;

    vq->descs[2].addr = blk_req_paddr + offsetof(virtio_blk_req, status);
    vq->descs[2].len = sizeof(uint8_t);
    vq->descs[2].flags = VIRTQ_DESC_F_WRITE;

    // Notify the device that there is a new request.
    virtq_kick(vq, 0);

    // Wait until the device finishes processing.
    while (virtq_is_busy(vq))
        ;

    // Check if the operation succeeded
    if (blk_req->status != 0) {
        printf("virtio: warn: failed to read/write sector=%ld status=%ld\n", static_cast<unsigned long>(sector),
               static_cast<unsigned long>(blk_req->status));
        return;
    }

    // For read operations, copy the data into the buffer.
    if (!is_write) {
        // printf("  read_write_disk: coping %d bytes from blk_req->data@%p to buf%p\n", SECTOR_SIZE, blk_req->data,
        // buf); printf("  First 4 bytes as hex: "); for (int i = 0; i < 4; i++) {
        //     printf("%lx ", static_cast<unsigned long>(blk_req->data[i]));
        // }
        memcpy(buf, blk_req->data, SECTOR_SIZE);
    }
    // printf("\n\n");
}
