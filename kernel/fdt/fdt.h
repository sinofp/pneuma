#pragma once

#include "lib/types.h"

namespace fdt {

constexpr uint32_t uninitialised = 0x990529;
constexpr size_t MAX_VIRTIO_DEVICES = 8;

struct virtio_device {
    paddr_t mmio_base;
    paddr_t vq_paddr; // pre-reserved virtq region; populated by mem::init
    uint32_t irq;
    uint32_t device_id; // cached from MMIO at init time
};

struct header;

void init(const header *fdt_ptr);

extern virtio_device virtio_devices[MAX_VIRTIO_DEVICES];
extern size_t virtio_device_count;

// True if the device tree found any virtio-mmio node with the given device_id (e.g. 16 for gpu, 18 for input).
inline bool has_device_id(uint32_t device_id) {
    for (size_t i = 0; i < virtio_device_count; i++)
        if (device_id == virtio_devices[i].device_id)
            return true;
    return false;
}

// Clock frequencies extracted from device tree
extern uint32_t clk_freq_ms;
extern uint32_t clk_freq_us;

} // namespace fdt
