#pragma once

#include "kernel.h"

void virtio_blk_init();
void read_write_disk(void *, uint64_t, int);
// void virtio_blk_intr();

constexpr size_t SECTOR_SIZE = 512;
constexpr size_t VIRTQ_ENTRY_NUM = 16;
constexpr uint32_t VIRTIO_DEVICE_BLK = 2;

constexpr paddr_t VIRTIO_BLK_PADDR = 0x10001000;
constexpr paddr_t VIRTIO_REG_MAGIC = VIRTIO_BLK_PADDR + 0x00;
constexpr paddr_t VIRTIO_REG_VERSION = VIRTIO_BLK_PADDR + 0x04;
constexpr paddr_t VIRTIO_REG_DEVICE_ID = VIRTIO_BLK_PADDR + 0x08;
constexpr paddr_t VIRTIO_REG_QUEUE_SEL = VIRTIO_BLK_PADDR + 0x30;
constexpr paddr_t VIRTIO_REG_QUEUE_NUM_MAX = VIRTIO_BLK_PADDR + 0x34;
constexpr paddr_t VIRTIO_REG_QUEUE_NUM = VIRTIO_BLK_PADDR + 0x38;
constexpr paddr_t VIRTIO_REG_QUEUE_ALIGN = VIRTIO_BLK_PADDR + 0x3c;
constexpr paddr_t VIRTIO_REG_QUEUE_PFN = VIRTIO_BLK_PADDR + 0x40;
constexpr paddr_t VIRTIO_REG_QUEUE_READY = VIRTIO_BLK_PADDR + 0x44;
constexpr paddr_t VIRTIO_REG_QUEUE_NOTIFY = VIRTIO_BLK_PADDR + 0x50;
constexpr paddr_t VIRTIO_REG_DEVICE_STATUS = VIRTIO_BLK_PADDR + 0x70;
constexpr paddr_t VIRTIO_REG_DEVICE_CONFIG = VIRTIO_BLK_PADDR + 0x100;

constexpr uint32_t VIRTIO_STATUS_ACK = 1;
constexpr uint32_t VIRTIO_STATUS_DRIVER = 2;
constexpr uint32_t VIRTIO_STATUS_DRIVER_OK = 4;
constexpr uint32_t VIRTIO_STATUS_FEAT_OK = 8;

constexpr uint16_t VIRTQ_DESC_F_NEXT = 1;
constexpr uint16_t VIRTQ_DESC_F_WRITE = 2;
constexpr uint16_t VIRTQ_AVAIL_F_NO_INTERRUPT = 1;

constexpr uint32_t VIRTIO_BLK_T_IN = 0;
constexpr uint32_t VIRTIO_BLK_T_OUT = 1;

// Virtqueue Descriptor area entry.
struct [[gnu::packed]] virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

// Virtqueue Available Ring.
struct [[gnu::packed]] virtq_avail {
    uint16_t flags;
    uint16_t index;
    uint16_t ring[VIRTQ_ENTRY_NUM];
};

// Virtqueue Used Ring entry.
struct [[gnu::packed]] virtq_used_elem {
    uint32_t id;
    uint32_t len;
};

// Virtqueue Used Ring.
struct [[gnu::packed]] virtq_used {
    uint16_t flags;
    uint16_t index;
    struct virtq_used_elem ring[VIRTQ_ENTRY_NUM];
};

// Virtqueue.
struct [[gnu::packed]] virtio_virtq {
    struct virtq_desc descs[VIRTQ_ENTRY_NUM];
    struct virtq_avail avail;
    struct virtq_used used __attribute__((aligned(PAGE_SIZE)));
    int queue_index;
    volatile uint16_t *used_index;
    uint16_t last_used_index;
};

// Virtio-blk request.
struct [[gnu::packed]] virtio_blk_req {
    // First descriptor: read-only from the device
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
    // Second descriptor: writable by the device if it's a read operation (VIRTQ_DESC_F_WRITE)
    uint8_t data[512];
    // Third descriptor: writable by the device (VIRTQ_DESC_F_WRITE)
    uint8_t status;
};
