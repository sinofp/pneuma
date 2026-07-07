// User-space virtio-input driver. Listens on PID INPUT_SERVER for input_req_t messages,
// drains the eventq into an internal ring, and replies one event per request (or "no event"
// if the ring is empty). Pure polling: the driver blocks in recv() and drains the used ring
// on each incoming request. The device's PLIC IRQ is left masked (never registered), which is
// safe because Doom polls every ~28 ms — the 16-slot used ring can't overflow at human
// keystroke rates.

#include <ipc/input.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <syscalls.h>
#include <virtio.h>
#include <virtio_helpers.h>

namespace {

using namespace virtio;

constexpr uint32_t INPUT_DEVICE_ID = 18;
constexpr le16 EV_SYN = 0;

struct [[gnu::packed]] event {
    le16 type;
    le16 code;
    le32 value;
};

// --- driver state ---
unsigned long mmio_base;
virtq *vq;
event *bufs; // 16 device-writable event buffers, one per prefilled descriptor
unsigned long bufs_paddr;

// Small ring for buffered events waiting to be delivered to clients.
constexpr size_t RING_SIZE = 64;
static_assert(0 == (RING_SIZE & (RING_SIZE - 1)), "RING_SIZE must be a power of two for the & mask below");
event ring[RING_SIZE];
size_t ring_head = 0, ring_count = 0;

// TODO ring queue class
bool ring_push(const event &ev) {
    if (RING_SIZE == ring_count)
        return false;
    ring[(ring_head + ring_count) & (RING_SIZE - 1)] = ev;
    ++ring_count;
    return true;
}

bool ring_pop(event &out) {
    if (0 == ring_count)
        return false;
    out = ring[ring_head];
    ring_head = (ring_head + 1) & (RING_SIZE - 1);
    --ring_count;
    return true;
}

bool probe_and_setup() {
    virtio_dev_t devs[8];
    auto n = fdt_virtio(devs, sizeof(devs) / sizeof(devs[0]));

    size_t best = static_cast<size_t>(-1);
    for (size_t i = 0; i < n; i++) {
        if (INPUT_DEVICE_ID != devs[i].device_id)
            continue;

        auto regs = reinterpret_cast<unsigned long>(mmio_map(devs[i].mmio_base, 1));
        if (!regs)
            continue;
        if (MAGIC_VALUE != mmio_read<uint32_t>(regs, REG_MAGIC))
            continue;
        if (1 != mmio_read<uint32_t>(regs, REG_VERSION))
            continue;

        best = i;
        // First keyboard/mouse wins; QEMU virt exposes one virtio-keyboard by default.
        break;
    }

    if (static_cast<size_t>(-1) == best) {
        printf("virtio-input(user): no input device found\n");
        return false;
    }

    mmio_base = reinterpret_cast<unsigned long>(mmio_map(devs[best].mmio_base, 1));

    vq = init(mmio_base, devs[best].vq_paddr);

    vq->avail.flags |= AVAIL_F_NO_INTERRUPT;

    bufs_paddr = dma_alloc_page();
    bufs = reinterpret_cast<event *>(bufs_paddr);

    // Prefill all 16 descriptors, each pointing at its own slot in `bufs`. The device fills
    // slots in used-ring order; we re-arm each on drain.
    static_assert(ENTRY_NUM * sizeof(event) <= PAGE_SIZE);
    for (uint16_t i = 0; i < ENTRY_NUM; i++) {
        vq->descs[i].addr = bufs_paddr + i * sizeof(event);
        vq->descs[i].len = sizeof(event);
        vq->descs[i].flags = DESC_F_WRITE;
        vq->descs[i].next = 0;
        vq->avail.ring[i] = i;
    }
    __sync_synchronize();
    vq->avail.index = ENTRY_NUM;
    __sync_synchronize();
    mmio_write<uint32_t>(mmio_base, REG_QUEUE_NOTIFY, 0);
    // `last_used_index` stays 0 — kick() bumps it on re-arm, matching the used ring.

    printf("virtio-input(user): selected MMIO=0x%lx (polling; IRQ left masked)\n", devs[best].mmio_base);
    return true;
}

// Move newly-arrived events from the used ring into the internal ring. Re-arm each descriptor.
void drain_used_ring() {
    while (vq->last_used_index != vq->used.index) {
        auto &used = vq->used.ring[vq->last_used_index % ENTRY_NUM];
        auto desc_idx = static_cast<uint16_t>(used.id);
        auto ev = bufs[desc_idx];
        // kick() re-arms this descriptor as available AND bumps our last_used_index.
        kick(mmio_base, vq, desc_idx, 0);
        if (EV_SYN != ev.type /* skip separater */)
            ring_push(ev);
    }
}

} // namespace

extern "C" void main() {
    if (!probe_and_setup()) {
        printf("virtio-input(user): init failed, exiting\n");
        return;
    }

    input_req_t req;
    input_resp_t resp;
    for (;;) {
        int_pid client = recv(&req, sizeof req, ANY_SOURCE);
        if (client < 0)
            continue;
        // Drain whatever the device has queued since our last visit, then serve the request.
        drain_used_ring();
        ack(mmio_base); // clear INTERRUPT_STATUS so the device keeps producing (idempotent otherwise)
        resp = {};
        event ev;
        if (ring_pop(ev)) {
            resp.has_event = 1;
            resp.type = ev.type;
            resp.code = ev.code;
            resp.value = static_cast<int32_t>(ev.value);
        }
        ssend(&resp, sizeof resp, client);
    }
}
