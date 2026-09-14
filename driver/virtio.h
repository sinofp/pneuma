#pragma once

// virtio MMIO + virtqueue definitions (legacy v1) — shared by all user-space virtio drivers.
// Mirrors kernel/dev/virtio.h; the two will collapse once virtio-input/gpu also migrate.

#include <stddef.h>
#include <stdint.h>

namespace virtio {

using le16 = uint16_t;
using le32 = uint32_t;
using le64 = uint64_t;

constexpr size_t PAGE_SIZE = 4096;

// MMIO register offsets
constexpr size_t REG_MAGIC = 0x00;
constexpr size_t REG_VERSION = 0x04;
constexpr size_t REG_DEVICE_ID = 0x08;
constexpr size_t REG_GUEST_PAGE_SIZE = 0x28;
constexpr size_t REG_QUEUE_SEL = 0x30;
constexpr size_t REG_QUEUE_NUM_MAX = 0x34;
constexpr size_t REG_QUEUE_NUM = 0x38;
constexpr size_t REG_QUEUE_ALIGN = 0x3c;
constexpr size_t REG_QUEUE_PFN = 0x40;
constexpr size_t REG_QUEUE_READY = 0x44;
constexpr size_t REG_QUEUE_NOTIFY = 0x50;
constexpr size_t REG_INTERRUPT_STATUS = 0x60;
constexpr size_t REG_INTERRUPT_ACK = 0x64;
constexpr size_t REG_DEVICE_STATUS = 0x70;
constexpr size_t REG_DEVICE_CONFIG = 0x100;

constexpr uint32_t MAGIC_VALUE = 0x74726976;
constexpr uint32_t STATUS_ACK = 1;
constexpr uint32_t STATUS_DRIVER = 2;
constexpr uint32_t STATUS_DRIVER_OK = 4;
constexpr uint32_t STATUS_FEAT_OK = 8;

constexpr size_t ENTRY_NUM = 16;
constexpr uint16_t DESC_F_NEXT = 1;
constexpr uint16_t DESC_F_WRITE = 2;
constexpr uint16_t AVAIL_F_NO_INTERRUPT = 1;

struct [[gnu::packed]] virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct [[gnu::packed]] virtq_avail {
    uint16_t flags;
    uint16_t index;
    uint16_t ring[ENTRY_NUM];
};

struct [[gnu::packed]] virtq_used_elem {
    uint32_t id;
    uint32_t len;
};

struct [[gnu::packed]] virtq_used {
    uint16_t flags;
    uint16_t index;
    virtq_used_elem ring[ENTRY_NUM];
};

struct [[gnu::packed]] virtq {
    virtq_desc descs[ENTRY_NUM];
    virtq_avail avail;
    virtq_used used __attribute__((aligned(PAGE_SIZE)));
    uint16_t last_used_index;
};

constexpr auto virtq_pages = __builtin_align_up(sizeof(virtio::virtq), PAGE_SIZE) / PAGE_SIZE;

} // namespace virtio
