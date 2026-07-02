#pragma once

// User-side inline wrappers for the driver-kit syscalls. Kernel does not include this
// (it dispatches these syscalls internally); the virtio_dev_t layout they operate on
// lives in <virtio.h> and is shared with the kernel.

#include <stddef.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <virtio.h>

// Copy kernel's virtio device table into caller; returns count actually written.
static inline size_t fdt_virtio(virtio::virtio_dev_t *buf, size_t max) {
    return syscall(SYS_FDT_VIRTIO, (unsigned long) buf, max, 0);
}

// Identity-map npages of physical memory into caller with U|R|W.
// Returns the (identity) vaddr, or 0 on failure.
static inline void *mmio_map(unsigned long paddr, size_t npages) {
    return (void *) syscall(SYS_MMIO_MAP, paddr, npages, 0);
}

// Allocate a single DMA-capable physical frame, identity-mapped U|R|W into caller.
// Returns the paddr (== vaddr), or 0 on failure.
static inline unsigned long dma_alloc_page() { return syscall(SYS_DMA_ALLOC, 1, 0, 0); }

// First call: registers ownership of `irq` for caller and unmasks at PLIC.
// Subsequent calls: plic::complete the prior IRQ, then suspend until next.
// Returns 0 on success, < 0 on error (e.g. already owned by another pid).
static inline long irq_wait(uint32_t irq) { return (long) syscall(SYS_IRQ_WAIT, irq, 0, 0); }
