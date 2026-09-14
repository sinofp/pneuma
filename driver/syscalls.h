#pragma once

// User-side inline wrappers for the driver-kit syscalls. Kernel does not include this
// (it dispatches these syscalls internally). C-compatible except for `fdt_virtio` which
// takes a namespaced `virtio::fdt_dev_t` (guarded by __cplusplus).

#include <stddef.h>
#include <stdint.h>
#include <sys/syscall.h>

// Identity-map npages of physical memory into caller with U|R|W.
// Returns the (identity) vaddr, or 0 on failure.
static inline void *mmio_map(unsigned long paddr, size_t npages) {
    return (void *) syscall(SYS_MMIO_MAP, paddr, npages, 0);
}

// Allocate a single DMA-capable physical frame, identity-mapped U|R|W into caller.
// Returns the paddr (== vaddr), or 0 on failure.
static inline unsigned long dma_alloc_page() { return syscall(SYS_DMA_ALLOC, 1, 0, 0); }

// Enumerate the pages backing the caller's own vaddr range. Emits one page-aligned paddr per
// page covered by [vaddr, vaddr+len); the caller reconstructs the intra-page head offset
// (`vaddr & (PAGE_SIZE-1)`) itself. paddrs_out must hold at least ceil((len+head_off)/PAGE_SIZE) entries.
static inline size_t page_walk(unsigned long vaddr, size_t len, uint64_t *paddrs_out) {
    return syscall(SYS_PAGE_WALK, vaddr, len, (unsigned long) paddrs_out);
}

// Register ownership of `irq` for the caller and unmask it at the PLIC.
// Returns 0 on success, < 0 if the irq is invalid or already owned.
static inline long irq_claim(uint32_t irq) { return (long) syscall(SYS_IRQ_CLAIM, irq, 0, 0); }

// Complete the prior IRQ cycle at the PLIC, then suspend until the next interrupt for `irq` (returns at once if one was
// already delivered). Delivery is a latched flag, not a count so callers must re-check their completion condition and
// loop Returns 0 on success, < 0 if the caller does not own `irq`.
static inline long irq_wait(uint32_t irq) { return (long) syscall(SYS_IRQ_WAIT, irq, 0, 0); }

#ifdef __cplusplus
#include <shared.h>
#include <virtio.h>

// Copy kernel's device table into caller; returns count actually written.
static inline size_t fdt_query(const char *compatible, fdt_dev_t *buf, size_t max) {
    return syscall(SYS_FDT_QUERY, reinterpret_cast<unsigned long>(compatible), reinterpret_cast<unsigned long>(buf),
                   max);
}
#endif
