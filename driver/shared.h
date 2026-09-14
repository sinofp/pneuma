#pragma once

#include <stdint.h> // uint8_t, uint32_t, uintptr_t, etc.

// Can't use constexpr because C user programs can't use it
// Syscall numbers (4, 5, 7, 9, 10, 11 vacated by prior migrations; do not renumber survivors)
#define SYS_PUTCHAR    1
#define SYS_GETCHAR    2
#define SYS_EXIT       3
#define SYS_SLEEP      6
#define SYS_GET_TICKS  8
#define SYS_MP_SSEND   12
#define SYS_MP_RECV    13
#define SYS_FDT_VIRTIO 14
#define SYS_MMIO_MAP   15
#define SYS_DMA_ALLOC  16
#define SYS_IRQ_WAIT   17
#define SYS_PAGE_WALK  18
#define SYS_IRQ_CLAIM  19
#define SYS_REBOOT     20
#define SYS_FDT_QUERY  21

// shared data for SYS_FDT_QUERY
struct fdt_dev_t {
    unsigned long mmio_base;
    unsigned long mmio_size;
    uint32_t reg_shift;
    uint32_t reg_io_width;

    uint32_t irq;

    // virtio-only
    unsigned long vq_paddr; // single pre-reserved virtq region per device
    uint32_t device_id;     // device_id == 0 is filtered out in fdt.cc, so this can be used to determine virtio
};