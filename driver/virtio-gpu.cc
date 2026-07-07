// User-space virtio-gpu driver. Listens on PID GPU_SERVER for gpu_req_t messages, drives the
// virtio-gpu control queue (create_2d, attach_backing, set_scanout, transfer_to_host_2d,
// resource_flush) and replies. Client (Doom) supplies the framebuffer paddrs in ATTACH.
//
// Polling-only: each kick is followed by wait_done() on the virtq. No IRQ ownership.

#include <ipc/gpu.h>
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

constexpr uint32_t GPU_DEVICE_ID = 16;
constexpr le32 FORMAT_B8G8R8A8_UNORM = 1;
constexpr le32 RESOURCE_ID = 1;

enum ctrl_type : le32 {
    CMD_RESOURCE_CREATE_2D = 0x0101,
    CMD_SET_SCANOUT = 0x0103,
    CMD_RESOURCE_FLUSH = 0x0104,
    CMD_TRANSFER_TO_HOST_2D = 0x0105,
    CMD_RESOURCE_ATTACH_BACKING = 0x0106,
};

struct [[gnu::packed]] ctrl_hdr {
    ctrl_type type;
    le32 flags;
    le64 fence_id;
    le32 ctx_id;
    le32 padding;

    constexpr ctrl_hdr(ctrl_type t)
        : type{ t }
        , flags{}
        , fence_id{}
        , ctx_id{}
        , padding{} {}
};

struct [[gnu::packed]] rect {
    le32 x, y, width, height;
};

struct [[gnu::packed]] resource_create_2d {
    ctrl_hdr hdr;
    le32 resource_id;
    le32 format;
    le32 width, height;
};

struct [[gnu::packed]] set_scanout {
    ctrl_hdr hdr;
    rect r;
    le32 scanout_id;
    le32 resource_id;
};

struct [[gnu::packed]] resource_flush {
    ctrl_hdr hdr;
    rect r;
    le32 resource_id;
    le32 padding;
};

struct [[gnu::packed]] transfer_to_host_2d {
    ctrl_hdr hdr;
    rect r;
    le64 offset;
    le32 resource_id;
    le32 padding;
};

struct [[gnu::packed]] resource_attach_backing {
    ctrl_hdr hdr;
    le32 resource_id;
    le32 nr_entries;
};

struct [[gnu::packed]] mem_entry {
    le64 addr;
    le32 length;
    le32 padding;
};

// One DMA page holds all command scratch. Each command struct lives at a fixed offset.
struct [[gnu::packed]] scratch_t {
    resource_create_2d create_2d;
    resource_attach_backing attach_hdr;
    mem_entry entries[GPU_MAX_PAGES];
    set_scanout scanout;
    transfer_to_host_2d transfer;
    resource_flush flush_cmd;
    ctrl_hdr resp;
};

static_assert(sizeof(scratch_t) <= PAGE_SIZE, "scratch layout exceeds one page");

// --- driver state ---
unsigned long mmio_base;
virtq *controlq;
scratch_t *scratch;
unsigned long scratch_paddr;
rect display;

// paddr helper for command structs
#define SCRATCH_PADDR(field) (scratch_paddr + offsetof(scratch_t, field))

bool probe_and_setup() {
    virtio_dev_t devs[8];
    auto n = fdt_virtio(devs, sizeof(devs) / sizeof(devs[0]));

    size_t best = static_cast<size_t>(-1);
    for (size_t i = 0; i < n; i++) {
        if (GPU_DEVICE_ID != devs[i].device_id)
            continue;

        auto regs = reinterpret_cast<unsigned long>(mmio_map(devs[i].mmio_base, 1));
        if (!regs)
            continue;
        if (MAGIC_VALUE != mmio_read<uint32_t>(regs, REG_MAGIC))
            continue;
        if (1 != mmio_read<uint32_t>(regs, REG_VERSION))
            continue;

        best = i;
        break;
    }

    if (static_cast<size_t>(-1) == best) {
        printf("virtio-gpu(user): no gpu device found\n");
        return false;
    }

    mmio_base = reinterpret_cast<unsigned long>(mmio_map(devs[best].mmio_base, 1));

    controlq = init(mmio_base, devs[best].vq_paddr, false);

    // We use polling
    controlq->avail.flags |= AVAIL_F_NO_INTERRUPT;

    scratch_paddr = dma_alloc_page();
    scratch = reinterpret_cast<scratch_t *>(scratch_paddr);
    memset(scratch, 0, sizeof(scratch_t));

    printf("virtio-gpu(user): selected MMIO=0x%lx\n", devs[best].mmio_base);
    return true;
}

// 2-descriptor: [cmd | resp], and wait for done
void submit(unsigned long cmd_paddr, size_t cmd_len) {
    controlq->descs[0].addr = cmd_paddr;
    controlq->descs[0].len = cmd_len;
    controlq->descs[0].flags = DESC_F_NEXT;
    controlq->descs[0].next = 1;

    controlq->descs[1].addr = SCRATCH_PADDR(resp);
    controlq->descs[1].len = sizeof(ctrl_hdr);
    controlq->descs[1].flags = DESC_F_WRITE;
    controlq->descs[1].next = 0;

    kick(mmio_base, controlq, 0, 0);
    wait_done(controlq);
}

int attach(const gpu_req_t &req) {
    if (0 == req.nr_pages or GPU_MAX_PAGES < req.nr_pages)
        return -1;

    display = { 0, 0, req.width, req.height };
    scratch->create_2d = {
        { CMD_RESOURCE_CREATE_2D }, RESOURCE_ID, FORMAT_B8G8R8A8_UNORM, display.width, display.height
    };
    submit(SCRATCH_PADDR(create_2d), sizeof(resource_create_2d));

    // Reconstruct virtio-gpu mem_entry[] from (paddrs, head_off, total_len). The first entry
    // absorbs any intra-page head offset; subsequent entries start on a page boundary. The
    // last entry gets whatever bytes remain (typically a partial page).
    uint32_t remaining = req.width * req.height * 4; // B8G8R8A8_UNORM = 4 bytes/px
    uint32_t off = req.head_off;
    for (uint32_t i = 0; i < req.nr_pages; i++) {
        uint32_t chunk = remaining < (PAGE_SIZE - off) ? remaining : (PAGE_SIZE - off); // TODO min
        scratch->entries[i] = { req.paddrs[i] + off, chunk, 0 };
        remaining -= chunk;
        off = 0;
    }

    scratch->attach_hdr = { { CMD_RESOURCE_ATTACH_BACKING }, RESOURCE_ID, req.nr_pages };
    // The attach_backing command is header + entries[] as a single contiguous struct sequence.
    submit(SCRATCH_PADDR(attach_hdr), sizeof(resource_attach_backing) + req.nr_pages * sizeof(mem_entry));

    scratch->scanout = { { CMD_SET_SCANOUT }, display, 0, RESOURCE_ID };
    submit(SCRATCH_PADDR(scanout), sizeof(set_scanout));

    return 0;
}

int flush() {
    scratch->transfer = { { CMD_TRANSFER_TO_HOST_2D }, display, 0, RESOURCE_ID, 0 };
    submit(SCRATCH_PADDR(transfer), sizeof(transfer_to_host_2d));

    scratch->flush_cmd = { { CMD_RESOURCE_FLUSH }, display, RESOURCE_ID, 0 };
    submit(SCRATCH_PADDR(flush_cmd), sizeof(resource_flush));

    return 0;
}

} // namespace

extern "C" void main() {
    if (!probe_and_setup()) {
        printf("virtio-gpu(user): init failed, exiting\n");
        return;
    }

    gpu_req_t req;
    gpu_resp_t resp;
    for (;;) {
        int_pid client = recv(&req, sizeof req, ANY_SOURCE);
        if (client < 0)
            continue;

        switch (req.op) {
        case GPU_OP_ATTACH:
            resp.result = attach(req);
            break;
        case GPU_OP_FLUSH:
            resp.result = flush();
            break;
        default:
            resp.result = -1;
        }
        ssend(&resp, sizeof resp, client);
    }
}
