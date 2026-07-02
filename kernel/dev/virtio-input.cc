#include "dev/virtio-input.h"
#include "dev/mmio.h"
#include "fdt/fdt.h"
#include "mem/mem.h"
#include "trap/plic.h"

namespace virtio::input {
paddr_t base = fdt::uninitialised;
uint32_t irq = fdt::uninitialised;
} // namespace virtio::input

namespace {

constexpr uint32_t DEVICE_ID = 18;
constexpr virtio::le16 EV_SYN = 0;

virtio::virtq *eventq = nullptr;
paddr_t vq_bufs_paddr = 0;

} // namespace

namespace virtio::input {

void init() {
    // TODO extract these enumerations
    paddr_t vq_paddr = 0;
    for (size_t i = 0; i < fdt::virtio_device_count; i++) {
        auto mmio_base = fdt::virtio_devices[i].mmio_base;

        // Check if this is a valid virtio-input device
        if (mmio::read<uint32_t>(mmio_base, REG_MAGIC) != MAGIC_VALUE)
            continue;
        if (mmio::read<uint32_t>(mmio_base, REG_VERSION) != 1)
            continue;
        if (mmio::read<uint32_t>(mmio_base, REG_DEVICE_ID) != DEVICE_ID)
            continue;

        // Assume we have at most one keyboard & mouse
        base = mmio_base;
        irq = fdt::virtio_devices[i].irq;
        vq_paddr = fdt::virtio_devices[i].vq_paddr;
        plic::enable_irq(irq);

        printf("virtio-input: Selected device at MMIO=0x%lx, IRQ=%d\n", static_cast<unsigned long>(base),
               static_cast<int>(irq));
        break;
    }

    if (fdt::uninitialised == base) {
        printf("virtio-input: No valid input device found\n");
        return;
    }

    eventq = device_init(base, vq_paddr, false, 0);

    static_assert(ENTRY_NUM * sizeof(event) <= mem::PAGE_SIZE);
    vq_bufs_paddr = mem::alloc_frame();

    // Prefill the eventq to write to their corresponding buffers, so we only need to kick the device in dequeue
    for (auto i = 0uz; i < ENTRY_NUM; ++i) {
        eventq->descs[i].addr = vq_bufs_paddr + i * sizeof(event);
        eventq->descs[i].len = sizeof(event);
        eventq->descs[i].flags = DESC_F_WRITE;
        eventq->descs[i].next = 0;
        eventq->avail.ring[i] = i;
    }
    __sync_synchronize();
    eventq->avail.index = ENTRY_NUM;
    __sync_synchronize();
    mmio::write<uint32_t>(base, REG_QUEUE_NOTIFY, 0);
}

void handle_interrupt() {
    auto status = mmio::read<uint32_t>(base, REG_INTERRUPT_STATUS);
    mmio::write<uint32_t>(base, REG_INTERRUPT_ACK, status);
}

bool dequeue(event &out) {
    while (eventq->last_used_index != *eventq->used_index) {
        auto &used = eventq->used.ring[eventq->last_used_index % ENTRY_NUM];
        auto desc_idx = static_cast<uint16_t>(used.id);
        auto ev = reinterpret_cast<event *>(vq_bufs_paddr)[desc_idx];
        virtq_kick(base, eventq, desc_idx);
        if (EV_SYN != ev.type /* skip separater */) {
            out = ev;
            return true;
        }
    }
    return false;
}

} // namespace virtio::input
