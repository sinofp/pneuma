#pragma once

#include "kernel.h"

namespace fdt {

constexpr uint32_t uninitialised = 0x990529;
constexpr size_t MAX_VIRTIO_DEVICES = 8;

struct virtio_device {
    paddr_t mmio_base;
    uint32_t irq;
};

struct header;

void init(const header *fdt_ptr);

extern virtio_device virtio_devices[MAX_VIRTIO_DEVICES];
extern size_t virtio_device_count;

} // namespace fdt
