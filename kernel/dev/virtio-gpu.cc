#include "dev/virtio-gpu.h"
#include "dev/mmio.h"
#include "fdt/fdt.h"
#include "mem/mem.h"
#include "proc/proc.h"

namespace virtio::gpu {
paddr_t vq_paddr;
paddr_t base = fdt::uninitialised;
uint32_t irq = fdt::uninitialised;
} // namespace virtio::gpu

namespace {

using le32 = virtio::le32;
using le64 = virtio::le64;

enum ctrl_type : virtio::le32 {
    CMD_GET_DISPLAY_INFO = 0x0100,
    CMD_RESOURCE_CREATE_2D,
    CMD_RESOURCE_UNREF,
    CMD_SET_SCANOUT,
    CMD_RESOURCE_FLUSH,
    CMD_TRANSFER_TO_HOST_2D,
    CMD_RESOURCE_ATTACH_BACKING,
    CMD_RESOURCE_DETACH_BACKING,

    RESP_OK_NODATA = 0x1100,
    RESP_OK_DISPLAY_INFO,

    RESP_ERR_UNSPEC = 0x1200,
    RESP_ERR_OUT_OF_MEMORY,
    RESP_ERR_INVALID_SCANOUT_ID,
    RESP_ERR_INVALID_RESOURCE_ID,
};

struct [[gnu::packed]] ctrl_hdr {
    le32 type;
    le32 flags;
    le64 fence_id;
    le32 ctx_id;
    le32 padding;

    constexpr ctrl_hdr() = default;

    constexpr ctrl_hdr(le32 t)
        : type{ t }
        , flags{}
        , fence_id{}
        , ctx_id{}
        , padding{} {}
};

struct [[gnu::packed]] rect {
    le32 x;
    le32 y;
    le32 width;
    le32 height;
};

struct [[gnu::packed]] resource_create_2d {
    ctrl_hdr hdr;
    le32 resource_id;
    le32 format;
    le32 width;
    le32 height;
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

virtio::virtq *controlq = nullptr;
constexpr le32 DEVICE_ID = 16;
constexpr le32 FORMAT_B8G8R8A8_UNORM = 1;
constexpr le32 RESOURCE_ID = 1;
constexpr rect DISPLAY{ 0, 0, 320, 200 };

} // namespace

namespace virtio::gpu {

void init() {
    printf("virtio-gpu: Found %ld virtio devices\n", static_cast<long>(fdt::virtio_device_count));

    for (size_t i = 0; i < fdt::virtio_device_count; i++) {
        auto mmio_base = fdt::virtio_devices[i].mmio_base;

        // Check if this is a valid virtio-gpu device
        if (mmio::read<uint32_t>(mmio_base, REG_MAGIC) != MAGIC_VALUE)
            continue;
        if (mmio::read<uint32_t>(mmio_base, REG_VERSION) != 1)
            continue;
        if (mmio::read<uint32_t>(mmio_base, REG_DEVICE_ID) != DEVICE_ID)
            continue;

        // Assume we have at most one GPU
        base = mmio_base;
        irq = fdt::virtio_devices[i].irq;

        printf("virtio-gpu: Selected device at MMIO=0x%lx, IRQ=%d\n", static_cast<unsigned long>(base),
               static_cast<int>(irq));

        break;
    }

    if (fdt::uninitialised == base) {
        printf("virtio-gpu: No valid GPU device found\n");
        return;
    }

    controlq = device_init(base, vq_paddr, false, 0);

    resource_create_2d create_2d{ { CMD_RESOURCE_CREATE_2D }, RESOURCE_ID, FORMAT_B8G8R8A8_UNORM, DISPLAY.width, DISPLAY.height };
    // We use identity mapping and busy-waiting, so it's find to take the stack variable address as-is
    controlq->descs[0].addr = reinterpret_cast<paddr_t>(&create_2d);
    controlq->descs[0].len = sizeof create_2d;
    controlq->descs[0].flags = DESC_F_NEXT;
    controlq->descs[0].next = 1;

    ctrl_hdr resp;
    controlq->descs[1].addr = reinterpret_cast<paddr_t>(&resp);
    controlq->descs[1].len = sizeof resp;
    controlq->descs[1].flags = DESC_F_WRITE;
    controlq->descs[1].next = 0;

    virtq_kick(base, controlq, 0);
    while (virtq_is_busy(controlq)) {
    }
}

void attach(const vaddr_t fb) {
    // Assume contiguous now, TODO deal with scattered framebuffer

    resource_attach_backing attach_backing{ { CMD_RESOURCE_ATTACH_BACKING }, RESOURCE_ID, 1 };
    controlq->descs[0].addr = reinterpret_cast<paddr_t>(&attach_backing);
    controlq->descs[0].len = sizeof attach_backing;
    controlq->descs[0].flags = DESC_F_NEXT;
    controlq->descs[0].next = 1;

    mem_entry entry{ mem::to_paddr(proc::current->page_table, fb), DISPLAY.width * DISPLAY.height * sizeof(uint32_t) };
    controlq->descs[1].addr = reinterpret_cast<paddr_t>(&entry);
    controlq->descs[1].len = sizeof entry;
    controlq->descs[1].flags = DESC_F_NEXT;
    controlq->descs[1].next = 2;

    ctrl_hdr resp;
    controlq->descs[2].addr = reinterpret_cast<paddr_t>(&resp);
    controlq->descs[2].len = sizeof resp;
    controlq->descs[2].flags = DESC_F_WRITE;
    controlq->descs[2].next = 0;

    virtq_kick(base, controlq, 0);
    while (virtq_is_busy(controlq)) {
    }

    set_scanout scanout{ { CMD_SET_SCANOUT }, DISPLAY, 0, RESOURCE_ID };
    controlq->descs[0].addr = reinterpret_cast<paddr_t>(&scanout);
    controlq->descs[0].len = sizeof scanout;
    controlq->descs[0].flags = DESC_F_NEXT;
    controlq->descs[0].next = 2; // reuse resp

    virtq_kick(base, controlq, 0);
    while (virtq_is_busy(controlq)) {
    }
}

void flush() {
    ctrl_hdr resp;
    controlq->descs[1].addr = reinterpret_cast<paddr_t>(&resp);
    controlq->descs[1].len = sizeof resp;
    controlq->descs[1].flags = DESC_F_WRITE;
    controlq->descs[1].next = 0;

    transfer_to_host_2d transfer{ { CMD_TRANSFER_TO_HOST_2D }, DISPLAY, 0, RESOURCE_ID, 0 };
    controlq->descs[0].addr = reinterpret_cast<paddr_t>(&transfer);
    controlq->descs[0].len = sizeof transfer;
    controlq->descs[0].flags = DESC_F_NEXT;
    controlq->descs[0].next = 1;

    virtq_kick(base, controlq, 0);
    while (virtq_is_busy(controlq)) {
    }

    resource_flush _flush{ { CMD_RESOURCE_FLUSH }, DISPLAY, RESOURCE_ID, 0 };
    controlq->descs[0].addr = reinterpret_cast<paddr_t>(&_flush);
    controlq->descs[0].len = sizeof _flush;
    controlq->descs[0].flags = DESC_F_NEXT;
    controlq->descs[0].next = 1;

    virtq_kick(base, controlq, 0);
    while (virtq_is_busy(controlq)) {
    }
}

} // namespace virtio::gpu
