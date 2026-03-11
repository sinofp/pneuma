#include "dev/virtio-blk.h"
#include "dev/mmio.h"
#include "fdt/fdt.h"
#include "mem/mem.h"
#include "proc/proc.h"
#include "proc/qtab.h"
#include "trap/csr.h"
#include "trap/plic.h"

namespace virtio::blk {
paddr_t vq_paddr;
uint64_t capacity;
paddr_t base = fdt::uninitialised;
uint32_t irq = fdt::uninitialised;
} // namespace virtio::blk

namespace {

constexpr uint32_t DEVICE_ID = 2;
constexpr uint32_t T_IN = 0;
constexpr uint32_t T_OUT = 1;
constexpr size_t SECTOR_SIZE = 512;

struct [[gnu::packed]] req_t {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
    uint8_t data[512];
    uint8_t status;
};

virtio::virtq *requestq = nullptr;
req_t *req = nullptr;
paddr_t req_paddr = 0;
bool use_shifted_pfn = false;

// TODO make it a queue to serve multiple processes
proc::process *waiting_proc = nullptr;

} // namespace

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
    // TODO find out why the behaviour is different and unify it
    auto queue_pfn = shifted_pfn ? (vq_buf >> 12) : vq_buf;
    mmio::write<uint32_t>(mmio_base, REG_QUEUE_PFN, queue_pfn);
    return vq;
}

virtq *device_init(paddr_t mmio_base, paddr_t vq_buf, bool shifted_pfn, uint32_t queue_index) {
    // 1. Reset the device.
    mmio::write<uint32_t>(mmio_base, REG_DEVICE_STATUS, 0);
    // 2. Set the ACKNOWLEDGE status bit: the guest OS has noticed the device.
    mmio::fetch_and_or<uint32_t>(mmio_base, REG_DEVICE_STATUS, STATUS_ACK);
    // 3. Set the DRIVER status bit.
    mmio::fetch_and_or<uint32_t>(mmio_base, REG_DEVICE_STATUS, STATUS_DRIVER);
    // 5. Set the FEATURES_OK status bit.
    mmio::fetch_and_or<uint32_t>(mmio_base, REG_DEVICE_STATUS, STATUS_FEAT_OK);
    // 7. Perform device-specific setup, including discovery of virtqueues for the device.
    auto *vq = virtq_init(mmio_base, vq_buf, shifted_pfn, queue_index);
    // 8. Set the DRIVER_OK status bit.
    mmio::fetch_and_or<uint32_t>(mmio_base, REG_DEVICE_STATUS, STATUS_DRIVER_OK);
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
        if (mmio::read<uint32_t>(mmio_base, REG_DEVICE_ID) != DEVICE_ID)
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
    irq = selected_irq;
    plic::enable_irq(irq);

    printf("virtio-blk: Selected device at MMIO=0x%lx, IRQ=%d, capacity=%ld bytes (blk_devices=%ld)\n",
           static_cast<unsigned long>(base), static_cast<int>(irq), static_cast<long>(biggest_capacity),
           static_cast<long>(blk_device_count));

    requestq = device_init(base, vq_paddr, use_shifted_pfn, 0);

    // Get the disk capacity.
    capacity = mmio::read<uint64_t>(base, REG_DEVICE_CONFIG + 0) * SECTOR_SIZE;
    printf("virtio-blk: capacity is %ld bytes\n", static_cast<unsigned long>(capacity));

    // Allocate a region to store requests to the device.
    static_assert(sizeof(req_t) <= mem::PAGE_SIZE, "blk_req bigger than a page frame");
    req_paddr = mem::alloc_frame();
    req = reinterpret_cast<req_t *>(req_paddr);

    // Disable interrupt suppression in the virtqueue
    requestq->avail.flags &= ~AVAIL_F_NO_INTERRUPT;
}
} // namespace blk

// Notifies the device that there is a new request. `desc_index` is the index
// of the head descriptor of the new request.
void virtq_kick(paddr_t mmio_base, virtq *vq, uint16_t desc_index) {
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
    mmio::write<uint32_t>(mmio_base, REG_QUEUE_NOTIFY, vq->queue_index);

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

    // virtio spec: '1. The driver places the buffer into free descriptor(s) in the descriptor table, chaining as
    // necessary'
    req->sector = sector;
    req->type = is_write ? T_OUT : T_IN;
    if (is_write)
        memcpy(req->data, buf, SECTOR_SIZE);

    // Construct the virtqueue descriptors chain
    auto *vq = requestq;
    vq->descs[0].addr = req_paddr;
    vq->descs[0].len = sizeof(uint32_t) * 2 + sizeof(uint64_t);
    vq->descs[0].flags = DESC_F_NEXT;
    vq->descs[0].next = 1;

    vq->descs[1].addr = req_paddr + offsetof(req_t, data);
    vq->descs[1].len = SECTOR_SIZE;
    vq->descs[1].flags = DESC_F_NEXT | (is_write ? 0 : DESC_F_WRITE);
    vq->descs[1].next = 2;

    vq->descs[2].addr = req_paddr + offsetof(req_t, status);
    vq->descs[2].len = sizeof(uint8_t);
    vq->descs[2].flags = DESC_F_WRITE;

    waiting_proc = proc::current;
    waiting_proc->state = proc::state_t::SUSPENDED;
    // Notify the device that there is a new request.
    virtq_kick(base, vq, 0);
    // The interrupt might also fire here, triggering handle_io_completion before entering reschedule.
    // It's fine because we allow switching to itself in reschedule specifically for this case.
    proc::reschedule();

    // Resumed by interrupt and rescheduling, check if the operation succeeded
    if (req->status != 0) {
        printf("virtio: warn: failed to read/write sector=%ld status=%ld\n", static_cast<unsigned long>(sector),
               static_cast<unsigned long>(req->status));
        return;
    }

    // For read operations, copy the data into the buffer.
    if (!is_write)
        memcpy(buf, req->data, SECTOR_SIZE);
}

void handle_io_completion() {
    // Acknowledge device interrupt
    auto status = mmio::read<uint32_t>(base, REG_INTERRUPT_STATUS);
    mmio::write<uint32_t>(base, REG_INTERRUPT_ACK, status);

    if (waiting_proc) {
        ready(waiting_proc, false);
        waiting_proc = nullptr;
    }
}

} // namespace blk

} // namespace virtio
