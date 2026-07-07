// User-space virtio-blk driver server. Listens on PID BLK_SERVER for blk_req_t messages,
// translates them into virtio descriptor chains, kicks the device, blocks on irq_wait,
// and ssends a blk_resp_t reply. All disk I/O for the system goes through here.

#include <ipc/blk.h>
#include <ipc/fs.h>
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

constexpr uint32_t BLK_DEVICE_ID = 2;
constexpr uint32_t T_IN = 0;
constexpr uint32_t T_OUT = 1;
constexpr uint32_t T_FLUSH = 4;

// Device-side request layout (virtio-blk-specific; kept private to this driver).
struct [[gnu::packed]] req_t {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
    uint8_t data[BLK_SECTOR_BYTES];
    uint8_t status;
};

// --- driver state ---
unsigned long mmio_base;
virtq *vq;
req_t *req;
unsigned long req_paddr;
uint32_t irq;
uint64_t capacity_sectors;

// --- probe: find the largest virtio-blk device and set up MMIO ---
bool probe_and_setup() {
    virtio_dev_t devs[8];
    auto n = fdt_virtio(devs, sizeof(devs) / sizeof(devs[0]));

    size_t best = static_cast<size_t>(-1);
    uint64_t best_cap = 0;

    // Find the biggest device by capacity (likely the data disk; the U-Boot boot disk is smaller).
    for (size_t i = 0; i < n; i++) {
        if (BLK_DEVICE_ID != devs[i].device_id)
            continue;

        auto regs = reinterpret_cast<unsigned long>(mmio_map(devs[i].mmio_base, 1));
        if (!regs)
            continue;
        if (MAGIC_VALUE != mmio_read<uint32_t>(regs, REG_MAGIC))
            continue;
        if (1 != mmio_read<uint32_t>(regs, REG_VERSION))
            continue;

        // Capacity in sectors lives in config space.
        auto cap_sectors = mmio_read<uint64_t>(regs, REG_DEVICE_CONFIG + 0);
        printf("virtio-blk(user): candidate %d: MMIO=%p irq=%d capacity=%ld sectors\n", i, devs[i].mmio_base,
               devs[i].irq, cap_sectors);

        if (best_cap < cap_sectors) {
            best_cap = cap_sectors;
            best = i;
        }
    }

    if (static_cast<size_t>(-1) == best) {
        printf("virtio-blk(user): No valid block device found\n");
        return false;
    }

    mmio_base = reinterpret_cast<unsigned long>(mmio_map(devs[best].mmio_base, 1));
    irq = devs[best].irq;
    capacity_sectors = best_cap;

    vq = init(mmio_base, devs[best].vq_paddr);

    // Single-page DMA scratch for the device-facing req_t.
    static_assert(sizeof(req_t) <= PAGE_SIZE, "req_t exceeds one page");
    req_paddr = dma_alloc_page();
    req = reinterpret_cast<req_t *>(req_paddr);

    // Disable interrupt suppression in the virtqueue
    vq->avail.flags &= ~AVAIL_F_NO_INTERRUPT;

    if (irq_claim(irq) < 0) {
        printf("virtio-blk(user): irq %d already claimed\n", irq);
        return false;
    }

    printf("virtio-blk(user): selected MMIO=%p irq=%d capacity=%ld bytes\n", devs[best].mmio_base, irq,
           best_cap * BLK_SECTOR_BYTES);
    return true;
}

int read_write(void *buf, uint64_t sector, bool is_write) {
    if (sector >= capacity_sectors) {
        printf("virtio-blk(user): tried to read/write sector=%ld, but capacity is %ld sectors\n",
               static_cast<unsigned long>(sector), static_cast<unsigned long>(capacity_sectors));
        return -1;
    }

    // virtio spec: '1. The driver places the buffer into free descriptor(s) in the descriptor table, chaining as
    // necessary'
    req->sector = sector;
    req->type = is_write ? T_OUT : T_IN;
    if (is_write)
        memcpy(req->data, buf, BLK_SECTOR_BYTES);

    // Construct the virtqueue descriptors chain
    vq->descs[0].addr = req_paddr;
    vq->descs[0].len = sizeof(uint32_t) * 2 + sizeof(uint64_t);
    vq->descs[0].flags = DESC_F_NEXT;
    vq->descs[0].next = 1;

    vq->descs[1].addr = req_paddr + offsetof(req_t, data);
    vq->descs[1].len = BLK_SECTOR_BYTES;
    vq->descs[1].flags = DESC_F_NEXT | (is_write ? 0 : DESC_F_WRITE);
    vq->descs[1].next = 2;

    vq->descs[2].addr = req_paddr + offsetof(req_t, status);
    vq->descs[2].len = sizeof(uint8_t);
    vq->descs[2].flags = DESC_F_WRITE;

    // Notify the device that there is a new request.
    kick(mmio_base, vq, 0);
    // Wait on the used ring, not on IRQ delivery: a stale pending flag (IRQ claimed while we
    // were still running) makes irq_wait return before this request's completion.
    while (is_busy(vq))
        irq_wait(irq);
    ack(mmio_base);

    // Resumed by interrupt and rescheduling, check if the operation succeeded
    if (0 != req->status) {
        printf("virtio-blk(user): sector=%lu status=%u\n", static_cast<unsigned long>(sector),
               static_cast<unsigned>(req->status));
        return -1;
    }

    // For read operations, copy the data into the buffer.
    if (!is_write)
        memcpy(buf, req->data, BLK_SECTOR_BYTES);
    return 0;
}

// Assumes the device advertises VIRTIO_BLK_F_FLUSH (QEMU virtio-blk does by default).
// A proper implementation would check REG_HOST_FEATURES bit 9 during probe.
int flush() {
    req->type = T_FLUSH;
    req->sector = 0; // unused per virtio spec for FLUSH

    vq->descs[0].addr = req_paddr;
    vq->descs[0].len = sizeof(uint32_t) * 2 + sizeof(uint64_t); // header only, no data
    vq->descs[0].flags = DESC_F_NEXT;
    vq->descs[0].next = 1;

    vq->descs[1].addr = req_paddr + offsetof(req_t, status);
    vq->descs[1].len = sizeof(uint8_t);
    vq->descs[1].flags = DESC_F_WRITE; // end of chain (no DESC_F_NEXT)

    kick(mmio_base, vq, 0);
    while (is_busy(vq))
        irq_wait(irq);
    ack(mmio_base);

    if (0 != req->status) {
        printf("virtio-blk(user): flush status=%u\n", static_cast<unsigned>(req->status));
        return -1;
    }
    return 0;
}

} // namespace

extern "C" void main() {
    if (!probe_and_setup()) {
        printf("virtio-blk(user): init failed, exiting\n");
        return;
    }

    blk_req_t req;
    blk_resp_t resp;
    for (;;) {
        int_pid client = recv(&req, sizeof req, FILE_SERVER);
        if (client < 0)
            continue;

        // Per-op reply size; default to header-only (just `result`).
        size_t resp_size = sizeof(int);
        switch (req.op) {
        case BLK_OP_READ: {
            auto n = req.count;
            if (n < 1 or BLK_MAX_SECTORS < n) {
                resp.result = -1;
                break;
            }
            resp.result = 0;
            for (uint32_t i = 0; i < n; i++)
                if (read_write(resp.data + i * BLK_SECTOR_BYTES, req.sector + i, false)) {
                    resp.result = -1;
                    break;
                }
            if (0 == resp.result)
                resp_size = offsetof(blk_resp_t, data) + n * BLK_SECTOR_BYTES;
        } break;
        case BLK_OP_WRITE: {
            auto n = req.count;
            if (n < 1 or BLK_MAX_SECTORS < n) {
                resp.result = -1;
                break;
            }
            resp.result = 0;
            for (uint32_t i = 0; i < n; i++)
                if (read_write(req.data + i * BLK_SECTOR_BYTES, req.sector + i, true)) {
                    resp.result = -1;
                    break;
                }
        } break;
        case BLK_OP_IOCTL_SECTOR_COUNT:
            resp.result = static_cast<int>(capacity_sectors);
            break;
        case BLK_OP_IOCTL_SECTOR_SIZE:
            resp.result = BLK_SECTOR_BYTES;
            break;
        case BLK_OP_IOCTL_BLOCK_SIZE:
            resp.result = 1;
            break;
        case BLK_OP_FLUSH:
            resp.result = flush();
            break;
        default:
            resp.result = -1;
        }
        ssend(&resp, resp_size, client);
    }
}
